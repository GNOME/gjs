/*
 * Copyright © 2014 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */

#include <sys/stat.h>
#include <gio/gio.h>

#include <gjs/context.h>

#include "coverage.h"
#include "coverage-internal.h"
#include "global.h"
#include "importer.h"
#include "jsapi-util-args.h"
#include "util/error.h"

struct _GjsCoverage {
    GObject parent;
};

typedef struct {
    gchar **prefixes;
    GjsContext *context;
    JS::Heap<JSObject *> coverage_statistics;

    GFile *output_dir;
    GFile *cache;
    /* tells whether priv->cache == NULL means no cache, or not specified */
    bool cache_specified;
} GjsCoveragePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GjsCoverage,
                           gjs_coverage,
                           G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_PREFIXES,
    PROP_CONTEXT,
    PROP_CACHE,
    PROP_OUTPUT_DIRECTORY,
    PROP_N
};

static GParamSpec *properties[PROP_N] = { NULL, };

typedef struct _GjsCoverageBranchExit {
    unsigned int line;
    unsigned int hit_count;
} GjsCoverageBranchExit;

typedef struct _GjsCoverageBranch {
    GArray       *exits;
    unsigned int point;
    bool         hit;
} GjsCoverageBranch;

typedef struct _GjsCoverageFunction {
    char         *key;
    unsigned int line_number;
    unsigned int hit_count;
} GjsCoverageFunction;

static char *
get_file_identifier(GFile *source_file) {
    char *path = g_file_get_path(source_file);
    if (!path)
        path = g_file_get_uri(source_file);
    return path;
}

static void
write_source_file_header(GOutputStream *stream,
                         GFile         *source_file)
{
    char *path = get_file_identifier(source_file);
    g_output_stream_printf(stream, NULL, NULL, NULL, "SF:%s\n", path);
    g_free(path);
}

typedef struct _FunctionHitCountData {
    GOutputStream *stream;
    unsigned int  *n_functions_found;
    unsigned int  *n_functions_hit;
} FunctionHitCountData;

static void
write_function_hit_count(GOutputStream *stream,
                         const char    *function_name,
                         unsigned int   hit_count,
                         unsigned int  *n_functions_found,
                         unsigned int  *n_functions_hit)
{
    (*n_functions_found)++;

    if (hit_count > 0)
        (*n_functions_hit)++;

    g_output_stream_printf(stream, NULL, NULL, NULL, "FNDA:%d,%s\n", hit_count, function_name);
}

static void
write_functions_hit_counts(GOutputStream *stream,
                           GArray        *functions,
                           unsigned int  *n_functions_found,
                           unsigned int  *n_functions_hit)
{
    unsigned int i = 0;

    for (; i < functions->len; ++i) {
        GjsCoverageFunction *function = &(g_array_index(functions, GjsCoverageFunction, i));
        write_function_hit_count(stream,
                                 function->key,
                                 function->hit_count,
                                 n_functions_found,
                                 n_functions_hit);
    }
}

static void
write_function_foreach_func(gpointer value,
                            gpointer user_data)
{
    GOutputStream       *stream = (GOutputStream *) user_data;
    GjsCoverageFunction *function = (GjsCoverageFunction *) value;

    g_output_stream_printf(stream, NULL, NULL, NULL, "FN:%d,%s\n", function->line_number, function->key);
}

static void
for_each_element_in_array(GArray   *array,
                          GFunc     func,
                          gpointer  user_data)
{
    const gsize element_size = g_array_get_element_size(array);
    unsigned int i;
    char         *current_array_pointer = (char *) array->data;

    for (i = 0; i < array->len; ++i, current_array_pointer += element_size)
        (*func)(current_array_pointer, user_data);
}

static void
write_functions(GOutputStream *data_stream,
                GArray        *functions)
{
    for_each_element_in_array(functions, write_function_foreach_func, data_stream);
}

static void
write_function_coverage(GOutputStream *data_stream,
                        unsigned int  n_found_functions,
                        unsigned int  n_hit_functions)
{
    g_output_stream_printf(data_stream, NULL, NULL, NULL, "FNF:%d\n", n_found_functions);
    g_output_stream_printf(data_stream, NULL, NULL, NULL, "FNH:%d\n", n_hit_functions);
}

typedef struct _WriteAlternativeData {
    unsigned int  *n_branch_alternatives_found;
    unsigned int  *n_branch_alternatives_hit;
    GOutputStream *output_stream;
    gpointer      *all_alternatives;
    bool           branch_point_was_hit;
} WriteAlternativeData;

typedef struct _WriteBranchInfoData {
    unsigned int *n_branch_exits_found;
    unsigned int *n_branch_exits_hit;
    GOutputStream *output_stream;
} WriteBranchInfoData;

static void
write_individual_branch(gpointer branch_ptr,
                        gpointer user_data)
{
    GjsCoverageBranch   *branch = (GjsCoverageBranch *) branch_ptr;
    WriteBranchInfoData *data = (WriteBranchInfoData *) user_data;

    /* This line is not a branch, don't write anything */
    if (!branch->point)
        return;

    unsigned int i = 0;
    for (; i < branch->exits->len; ++i) {
        GjsCoverageBranchExit *exit = &(g_array_index(branch->exits, GjsCoverageBranchExit, i));
        unsigned int alternative_counter = exit->hit_count;
        unsigned int branch_point = branch->point;
        char         *hit_count_string = NULL;

        if (!branch->hit)
            hit_count_string = g_strdup_printf("-");
        else
            hit_count_string = g_strdup_printf("%d", alternative_counter);

        g_output_stream_printf(data->output_stream, NULL, NULL, NULL, "BRDA:%d,0,%d,%s\n",
                               branch_point, i, hit_count_string);
        g_free(hit_count_string);

        ++(*data->n_branch_exits_found);

        if (alternative_counter > 0)
            ++(*data->n_branch_exits_hit);
    }
}

static void
write_branch_coverage(GOutputStream *stream,
                      GArray        *branches,
                      unsigned int  *n_branch_exits_found,
                      unsigned int  *n_branch_exits_hit)

{
    /* Write individual branches and pass-out the totals */
    WriteBranchInfoData data = {
        n_branch_exits_found,
        n_branch_exits_hit,
        stream
    };

    for_each_element_in_array(branches,
                              write_individual_branch,
                              &data);
}

static void
write_branch_totals(GOutputStream *stream,
                    unsigned int   n_branch_exits_found,
                    unsigned int   n_branch_exits_hit)
{
    g_output_stream_printf(stream, NULL, NULL, NULL, "BRF:%d\n", n_branch_exits_found);
    g_output_stream_printf(stream, NULL, NULL, NULL, "BRH:%d\n", n_branch_exits_hit);
}

static void
write_line_coverage(GOutputStream *stream,
                    GArray        *stats,
                    unsigned int  *lines_hit_count,
                    unsigned int  *executable_lines_count)
{
    unsigned int i = 0;
    for (i = 0; i < stats->len; ++i) {
        int hit_count_for_line = g_array_index(stats, int, i);

        if (hit_count_for_line == -1)
            continue;

        g_output_stream_printf(stream, NULL, NULL, NULL, "DA:%d,%d\n", i, hit_count_for_line);

        if (hit_count_for_line > 0)
            ++(*lines_hit_count);

        ++(*executable_lines_count);
    }
}

static void
write_line_totals(GOutputStream *stream,
                  unsigned int   lines_hit_count,
                  unsigned int   executable_lines_count)
{
    g_output_stream_printf(stream, NULL, NULL, NULL, "LH:%d\n", lines_hit_count);
    g_output_stream_printf(stream, NULL, NULL, NULL, "LF:%d\n", executable_lines_count);
}

static void
write_end_of_record(GOutputStream *stream)
{
    g_output_stream_printf(stream, NULL, NULL, NULL, "end_of_record\n");
}

static void
copy_source_file_to_coverage_output(GFile *source_file,
                                    GFile *destination_file)
{
    GError *error = NULL;

    /* We need to recursively make the directory we
     * want to copy to, as g_file_copy doesn't do that */
    GjsAutoUnref<GFile> destination_dir = g_file_get_parent(destination_file);
    if (!g_file_make_directory_with_parents(destination_dir, NULL, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            goto fail;
        g_clear_error(&error);
    }

    if (!g_file_copy(source_file,
                     destination_file,
                     G_FILE_COPY_OVERWRITE,
                     NULL,
                     NULL,
                     NULL,
                     &error)) {
        goto fail;
    }

    return;

fail:
    char *source_uri = get_file_identifier(source_file);
    char *dest_uri = get_file_identifier(destination_file);
    g_critical("Failed to copy source file %s to destination %s: %s\n",
               source_uri, dest_uri, error->message);
    g_free(source_uri);
    g_free(dest_uri);
    g_clear_error(&error);
}

/* This function will strip a URI scheme and return
 * the string with the URI scheme stripped or NULL
 * if the path was not a valid URI
 */
static char *
strip_uri_scheme(const char *potential_uri)
{
    char *uri_header = g_uri_parse_scheme(potential_uri);

    if (uri_header) {
        gsize offset = strlen(uri_header);
        g_free(uri_header);

        /* g_uri_parse_scheme only parses the name
         * of the scheme, we also need to strip the
         * characters ':///' */
        return g_strdup(potential_uri + offset + 4);
    }

    return NULL;
}

/* This function will return a string of pathname
 * components from the first directory indicating
 * where two directories diverge. For instance:
 *
 * child: /a/b/c/d/e
 * parent: /a/b/d/
 *
 * Will return: c/d/e
 *
 * If the directories are not at all similar then
 * the full dirname of the child_path effectively
 * be returned.
 *
 * As a special case, child paths that are a URI
 * automatically return the full URI path with
 * the URI scheme and leading slash stripped out.
 */
static char *
find_diverging_child_components(GFile *child,
                                GFile *parent)
{
    g_object_ref(parent);
    GFile *ancestor = parent;
    while (ancestor != NULL) {
        char *relpath = g_file_get_relative_path(ancestor, child);
        if (relpath) {
            g_object_unref(ancestor);
            return relpath;
        }
        GFile *new_ancestor = g_file_get_parent(ancestor);
        g_object_unref(ancestor);
        ancestor = new_ancestor;
    }

    /* This is a special case of getting the URI below. The difference is that
     * this gives you a regular path name; getting it through the URI would
     * give a URI-encoded path (%20 for spaces, etc.) */
    GFile *root = g_file_new_for_path("/");
    char *child_path = g_file_get_relative_path(root, child);
    g_object_unref(root);
    if (child_path)
        return child_path;

    char *child_uri = g_file_get_uri(child);
    char *stripped_uri = strip_uri_scheme(child_uri);
    g_free(child_uri);
    return stripped_uri;
}

typedef bool (*ConvertAndInsertJSVal) (GArray         *array,
                                       JSContext      *context,
                                       JS::HandleValue element);

static bool
get_array_from_js_value(JSContext             *context,
                        JS::HandleValue        value,
                        size_t                 array_element_size,
                        GDestroyNotify         element_clear_func,
                        ConvertAndInsertJSVal  inserter,
                        GArray                **out_array)
{
    g_return_val_if_fail(out_array != NULL, false);
    g_return_val_if_fail(*out_array == NULL, false);

    bool is_array;
    if (!JS_IsArrayObject(context, value, &is_array))
        return false;
    if (!is_array) {
        g_critical("Returned object from is not an array");
        return false;
    }

    /* We're not preallocating any space here at the moment until
     * we have some profiling data that suggests a good size to
     * preallocate to. */
    GArray *c_side_array = g_array_new(true, true, array_element_size);
    uint32_t js_array_len;
    JS::RootedObject js_array(context, &value.toObject());

    if (element_clear_func)
        g_array_set_clear_func(c_side_array, element_clear_func);

    if (JS_GetArrayLength(context, js_array, &js_array_len)) {
        uint32_t i = 0;
        JS::RootedValue element(context);
        for (; i < js_array_len; ++i) {
            if (!JS_GetElement(context, js_array, i, &element)) {
                g_array_unref(c_side_array);
                gjs_throw(context, "Failed to get function names array element %d", i);
                return false;
            }

            if (!(inserter(c_side_array, context, element))) {
                g_array_unref(c_side_array);
                gjs_throw(context, "Failed to convert array element %d", i);
                return false;
            }
        }
    }

    *out_array = c_side_array;

    return true;
}

static bool
convert_and_insert_unsigned_int(GArray         *array,
                                JSContext      *context,
                                JS::HandleValue element)
{
    if (!element.isInt32() && !element.isUndefined() && !element.isNull()) {
        g_critical("Array element is not an integer or undefined or null");
        return false;
    }

    if (element.isInt32()) {
        unsigned int element_integer = element.toInt32();
        g_array_append_val(array, element_integer);
    } else {
        int not_executable = -1;
        g_array_append_val(array, not_executable);
    }

    return true;
}

static GArray *
get_executed_lines_for(JSContext        *context,
                       JS::HandleObject  coverage_statistics,
                       JS::HandleValue   filename_value)
{
    GArray *array = NULL;
    JS::RootedValue rval(context);
    JS::AutoValueArray<1> args(context);
    args[0].set(filename_value);

    if (!JS_CallFunctionName(context, coverage_statistics, "getExecutedLinesFor",
                             args, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, rval, sizeof(unsigned int), NULL,
        convert_and_insert_unsigned_int, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
init_covered_function(GjsCoverageFunction *function,
                      const char          *key,
                      unsigned int        line_number,
                      unsigned int        hit_count)
{
    function->key = g_strdup(key);
    function->line_number = line_number;
    function->hit_count = hit_count;
}

static void
clear_coverage_function(gpointer info_location)
{
    GjsCoverageFunction *info = (GjsCoverageFunction *) info_location;
    g_free(info->key);
}

static bool
get_hit_count_and_line_data(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            int32_t         *hit_count,
                            int32_t         *line)
{
    JS::RootedId hit_count_name(cx, gjs_intern_string_to_id(cx, "hitCount"));
    if (!gjs_object_require_property(cx, obj, "function element",
                                     hit_count_name, hit_count))
        return false;

    JS::RootedId line_number_name(cx, gjs_intern_string_to_id(cx, "line"));
    return gjs_object_require_property(cx, obj, "function_element",
                                       line_number_name, line);
}

static bool
convert_and_insert_function_decl(GArray         *array,
                                 JSContext      *context,
                                 JS::HandleValue element)
{
    if (!element.isObject()) {
        gjs_throw(context, "Function element is not an object");
        return false;
    }

    JS::RootedObject object(context, &element.toObject());
    JS::RootedValue function_name_property_value(context);

    if (!gjs_object_require_property(context, object, NULL, GJS_STRING_NAME,
                                     &function_name_property_value))
        return false;

    GjsAutoJSChar utf8_string(context);

    if (function_name_property_value.isString()) {
        if (!gjs_string_to_utf8(context,
                                function_name_property_value,
                                &utf8_string)) {
            gjs_throw(context, "Failed to convert function_name to string");
            return false;
        }
    } else if (!function_name_property_value.isNull()) {
        gjs_throw(context, "Unexpected type for function_name");
        return false;
    }

    int32_t hit_count;
    int32_t line_number;
    if (!get_hit_count_and_line_data(context, object, "function element",
                                     &hit_count, &line_number))
        return false;

    GjsCoverageFunction info;
    init_covered_function(&info,
                          utf8_string,
                          line_number,
                          hit_count);

    g_array_append_val(array, info);

    return true;
}

static GArray *
get_functions_for(JSContext        *context,
                  JS::HandleObject  coverage_statistics,
                  JS::HandleValue   filename_value)
{
    GArray *array = NULL;
    JS::RootedValue rval(context);
    JS::AutoValueArray<1> args(context);
    args[0].set(filename_value);

    if (!JS_CallFunctionName(context, coverage_statistics, "getFunctionsFor",
                             args, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, rval, sizeof(GjsCoverageFunction),
        clear_coverage_function, convert_and_insert_function_decl, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
init_covered_branch(GjsCoverageBranch *branch,
                    unsigned int       point,
                    bool               was_hit,
                    GArray            *exits)
{
    branch->point = point;
    branch->hit = !!was_hit;
    branch->exits = exits;
}

static void
clear_coverage_branch(gpointer branch_location)
{
    GjsCoverageBranch *branch = (GjsCoverageBranch *) branch_location;
    g_array_unref(branch->exits);
}

static bool
convert_and_insert_branch_exit(GArray         *array,
                               JSContext      *context,
                               JS::HandleValue element)
{
    if (!element.isObject()) {
        gjs_throw(context, "Branch exit array element is not an object");
        return false;
    }

    JS::RootedObject object(context, &element.toObject());

    int32_t hit_count;
    int32_t line;
    if (!get_hit_count_and_line_data(context, object, "branch exit array element",
                                     &hit_count, &line))
        return false;

    GjsCoverageBranchExit exit = {
        (unsigned int) line,
        (unsigned int) hit_count
    };

    g_array_append_val(array, exit);

    return true;
}

static bool
convert_and_insert_branch_info(GArray         *array,
                               JSContext      *context,
                               JS::HandleValue element)
{
    if (!element.isObject() && !element.isUndefined()) {
        gjs_throw(context, "Branch array element is not an object or undefined");
        return false;
    }

    if (element.isObject()) {
        JS::RootedObject object(context, &element.toObject());

        int32_t branch_point;
        JS::RootedId point_name(context, gjs_intern_string_to_id(context, "point"));

        if (!gjs_object_require_property(context, object,
                                         "branch array element",
                                         point_name, &branch_point))
            return false;

        bool was_hit;
        JS::RootedId hit_name(context, gjs_intern_string_to_id(context, "hit"));

        if (!gjs_object_require_property(context, object,
                                         "branch array element",
                                         hit_name, &was_hit))
            return false;

        JS::RootedValue branch_exits_value(context);
        GArray *branch_exits_array = NULL;

        if (!JS_GetProperty(context, object, "exits", &branch_exits_value) ||
            !branch_exits_value.isObject()) {
            gjs_throw(context, "Failed to get exits property from element");
            return false;
        }

        if (!get_array_from_js_value(context,
                                     branch_exits_value,
                                     sizeof(GjsCoverageBranchExit),
                                     NULL,
                                     convert_and_insert_branch_exit,
                                     &branch_exits_array)) {
            /* Already logged the exception, no need to do anything here */
            return false;
        }

        GjsCoverageBranch branch;
        init_covered_branch(&branch,
                            branch_point,
                            was_hit,
                            branch_exits_array);

        g_array_append_val(array, branch);
    }

    return true;
}

static GArray *
get_branches_for(JSContext        *context,
                 JS::HandleObject  coverage_statistics,
                 JS::HandleValue   filename_value)
{
    GArray *array = NULL;
    JS::AutoValueArray<1> args(context);
    args[0].set(filename_value);
    JS::RootedValue rval(context);

    if (!JS_CallFunctionName(context, coverage_statistics, "getBranchesFor",
                             args, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, rval, sizeof(GjsCoverageBranch),
                                 clear_coverage_branch,
                                 convert_and_insert_branch_info, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

typedef struct _GjsCoverageFileStatistics {
    char       *filename;
    GArray     *lines;
    GArray     *functions;
    GArray     *branches;
} GjsCoverageFileStatistics;

static bool
fetch_coverage_file_statistics_from_js(JSContext                 *context,
                                       JS::HandleObject           coverage_statistics,
                                       const char                *filename,
                                       GjsCoverageFileStatistics *statistics)
{
    JSAutoCompartment compartment(context, coverage_statistics);
    JSAutoRequest ar(context);

    JSString *filename_jsstr = JS_NewStringCopyZ(context, filename);
    JS::RootedValue filename_jsval(context, JS::StringValue(filename_jsstr));

    GArray *lines = get_executed_lines_for(context, coverage_statistics, filename_jsval);
    GArray *functions = get_functions_for(context, coverage_statistics, filename_jsval);
    GArray *branches = get_branches_for(context, coverage_statistics, filename_jsval);

    if (!lines || !functions || !branches)
    {
        g_clear_pointer(&lines, g_array_unref);
        g_clear_pointer(&functions, g_array_unref);
        g_clear_pointer(&branches, g_array_unref);
        return false;
    }

    statistics->filename = g_strdup(filename);
    statistics->lines = lines;
    statistics->functions = functions;
    statistics->branches = branches;

    return true;
}

static void
gjs_coverage_statistics_file_statistics_clear(gpointer data)
{
    GjsCoverageFileStatistics *statistics = (GjsCoverageFileStatistics *) data;
    g_free(statistics->filename);
    g_array_unref(statistics->lines);
    g_array_unref(statistics->functions);
    g_array_unref(statistics->branches);
}

static void
print_statistics_for_file(GjsCoverageFileStatistics *file_statistics,
                          GFile                     *output_dir,
                          GOutputStream             *ostream)
{
    /* The source file could be a resource, so we must use
     * g_file_new_for_commandline_arg() to disambiguate between URIs and
     * filesystem paths. */
    GFile *source = g_file_new_for_commandline_arg(file_statistics->filename);

    char *diverged_paths = find_diverging_child_components(source, output_dir);
    GFile *dest = g_file_resolve_relative_path(output_dir, diverged_paths);

    copy_source_file_to_coverage_output(source, dest);
    g_object_unref(source);

    write_source_file_header(ostream, dest);
    g_object_unref(dest);

    write_functions(ostream, file_statistics->functions);

    unsigned int functions_hit_count = 0;
    unsigned int functions_found_count = 0;

    write_functions_hit_counts(ostream,
                               file_statistics->functions,
                               &functions_found_count,
                               &functions_hit_count);
    write_function_coverage(ostream,
                            functions_found_count,
                            functions_hit_count);

    unsigned int branches_hit_count = 0;
    unsigned int branches_found_count = 0;

    write_branch_coverage(ostream,
                          file_statistics->branches,
                          &branches_found_count,
                          &branches_hit_count);
    write_branch_totals(ostream,
                        branches_found_count,
                        branches_hit_count);

    unsigned int lines_hit_count = 0;
    unsigned int executable_lines_count = 0;

    write_line_coverage(ostream,
                        file_statistics->lines,
                        &lines_hit_count,
                        &executable_lines_count);
    write_line_totals(ostream,
                      lines_hit_count,
                      executable_lines_count);
    write_end_of_record(ostream);

    g_free(diverged_paths);
}

static char **
get_covered_files(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, priv->coverage_statistics);
    JS::RootedObject rooted_priv(context, priv->coverage_statistics);
    JS::RootedValue rval(context);

    char **files = NULL;
    uint32_t n_files;

    if (!JS_CallFunctionName(context, rooted_priv, "getCoveredFiles",
                             JS::HandleValueArray::empty(), &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!rval.isObject())
        return NULL;

    JS::RootedObject files_obj(context, &rval.toObject());
    if (!JS_GetArrayLength(context, files_obj, &n_files))
        return NULL;

    files = g_new0(char *, n_files + 1);
    JS::RootedValue element(context);
    for (uint32_t i = 0; i < n_files; i++) {
        GjsAutoJSChar file(context);
        if (!JS_GetElement(context, files_obj, i, &element))
            goto error;

        if (!gjs_string_to_utf8(context, element, &file))
            goto error;

        files[i] = file.copy();
    }

    files[n_files] = NULL;
    return files;

 error:
    g_strfreev(files);
    return NULL;
}

bool
gjs_get_file_mtime(GFile    *file,
                   GTimeVal *mtime)
{
    GError *error = NULL;
    GFileInfo *info = g_file_query_info(file,
                                        "time::modified,time::modified-usec",
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL,
                                        &error);

    if (!info) {
        char *path = get_file_identifier(file);
        g_warning("Failed to get modification time of %s, "
                  "falling back to checksum method for caching. Reason was: %s",
                  path, error->message);
        g_clear_object(&info);
        return false;
    }

    g_file_info_get_modification_time(info, mtime);
    g_clear_object(&info);

    /* For some URI types, eg, resources, the operation getting
     * the mtime might succeed, but by default zero is returned.
     *
     * Check if that is the case for both tv_sec and tv_usec and if
     * so return false. */
    return !(mtime->tv_sec == 0 && mtime->tv_usec == 0);
}

static GBytes *
read_all_bytes_from_file(GFile *file)
{
    /* We have to use g_file_query_exists here since
     * g_file_test(path, G_FILE_TEST_EXISTS) is implemented in terms
     * of access(), which doesn't work with resource paths. */
    if (!g_file_query_exists(file, NULL))
        return NULL;

    gsize len = 0;
    gchar *data = NULL;

    GError *error = NULL;

    if (!g_file_load_contents(file,
                              NULL,
                              &data,
                              &len,
                              NULL,
                              &error)) {
        char *path = get_file_identifier(file);
        g_critical("Unable to read bytes from: %s, reason was: %s\n",
                   path, error->message);
        g_clear_error(&error);
        g_free(path);
        return NULL;
    }

    return g_bytes_new_take(data, len);
}

gchar *
gjs_get_file_checksum(GFile *file)
{
    GBytes *data = read_all_bytes_from_file(file);

    if (!data)
        return NULL;

    gchar *checksum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA512, data);

    g_bytes_unref(data);
    return checksum;
}

/* The binary data for the cache has the following structure:
 *
 * {
 *     array [ tuple {
 *         string filename;
 *         string? checksum;
 *         tuple? {
 *             mtime_sec;
 *             mtime_usec;
 *         }
 *         array [
 *             int line;
 *         ] executable lines;
 *         array [ tuple {
 *             int branch_point;
 *             array [
 *                 int line;
 *             ] exits;
 *         } branch_info ] branches;
 *         array [ tuple {
 *             int line;
 *             string key;
 *         } function ] functions;
 *     } file ] files;
 */
const char *COVERAGE_STATISTICS_CACHE_BINARY_DATA_TYPE = "a(sm(xx)msaia(iai)a(is))";

GBytes *
gjs_serialize_statistics(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(priv->context);

    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);
    JS::RootedObject rooted_priv(js_context, priv->coverage_statistics);
    JS::RootedValue string_value_return(js_context);

    if (!JS_CallFunctionName(js_context, rooted_priv, "stringify",
                             JS::HandleValueArray::empty(),
                             &string_value_return)) {
        gjs_log_exception(js_context);
        return NULL;
    }

    if (!string_value_return.isString())
        return NULL;

    /* Free'd by g_bytes_new_take */
    GjsAutoJSChar statistics_as_json_string(js_context);

    if (!gjs_string_to_utf8(js_context,
                            string_value_return.get(),
                            &statistics_as_json_string)) {
        gjs_log_exception(js_context);
        return NULL;
    }

    int json_string_len = strlen(statistics_as_json_string);
    auto json_bytes =
        reinterpret_cast<uint8_t*>(statistics_as_json_string.copy());

    return g_bytes_new_take(json_bytes,
                            json_string_len);
}

static JSString *
gjs_deserialize_cache_to_object_for_compartment(JSContext        *context,
                                                JS::HandleObject global_object,
                                                GBytes           *cache_data)
{
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context,
                         JS_GetGlobalForObject(context,
                                               global_object));

    gsize len = 0;
    auto string = static_cast<const char *>(g_bytes_get_data(cache_data, &len));

    return JS_NewStringCopyN(context, string, len);
}

JSString *
gjs_deserialize_cache_to_object(GjsCoverage *coverage,
                                GBytes      *cache_data)
{
    /* Deserialize into an object with the following structure:
     *
     * object = {
     *     'filename': {
     *         contents: (file contents),
     *         nLines: (number of lines in file),
     *         lines: Number[nLines + 1],
     *         branches: Array for n_branches of {
     *             point: branch_point,
     *             exits: Number[nLines + 1]
     *         },
     *         functions: Array for n_functions of {
     *             key: function_name,r
     *             line: line
     *         }
     * }
     */

    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, priv->coverage_statistics);
    JS::RootedObject global_object(context,
                                   JS_GetGlobalForObject(context, priv->coverage_statistics));
    return gjs_deserialize_cache_to_object_for_compartment(context, global_object, cache_data);
}

static GArray *
gjs_fetch_statistics_from_js(GjsCoverage *coverage,
                             gchar       **coverage_files)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);

    GArray *file_statistics_array = g_array_new(false,
                                                false,
                                                sizeof(GjsCoverageFileStatistics));
    g_array_set_clear_func(file_statistics_array,
                           gjs_coverage_statistics_file_statistics_clear);

    JS::RootedObject rooted_coverage_statistics(js_context,
                                                priv->coverage_statistics);

    char **file_iter = coverage_files;
    while (*file_iter) {
        GjsCoverageFileStatistics statistics;
        if (fetch_coverage_file_statistics_from_js(js_context,
                                                   rooted_coverage_statistics,
                                                   *file_iter,
                                                   &statistics))
            g_array_append_val(file_statistics_array, statistics);
        else
            g_warning("Couldn't fetch statistics for %s", *file_iter);

        ++file_iter;
    }

    return file_statistics_array;
}

bool
gjs_write_cache_file(GFile  *file,
                     GBytes *cache)
{
    gsize cache_len = 0;
    char *cache_data = (char *) g_bytes_get_data(cache, &cache_len);
    GError *error = NULL;

    if (!g_file_replace_contents(file,
                                 cache_data,
                                 cache_len,
                                 NULL,
                                 false,
                                 G_FILE_CREATE_NONE,
                                 NULL,
                                 NULL,
                                 &error)) {
        char *path = get_file_identifier(file);
        g_warning("Failed to write all bytes to %s, reason was: %s\n",
                  path, error->message);
        g_warning("Will remove this file to prevent inconsistent cache "
                  "reads next time.");
        g_clear_error(&error);
        if (!g_file_delete(file, NULL, &error)) {
            g_assert(error != NULL);
            g_critical("Deleting %s failed because %s! You will need to "
                       "delete it manually before running the coverage "
                       "mode again.", path, error->message);
            g_clear_error(&error);
        }
        g_free(path);

        return false;
    }

    return true;
}

static bool
coverage_statistics_has_stale_cache(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);

    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);
    JS::RootedObject rooted_priv(js_context, priv->coverage_statistics);
    JS::RootedValue stale_cache_value(js_context);
    if (!JS_CallFunctionName(js_context, rooted_priv, "staleCache",
                             JS::HandleValueArray::empty(),
                             &stale_cache_value)) {
        gjs_log_exception(js_context);
        g_error("Failed to call into javascript to get stale cache value. This is a bug");
    }

    return stale_cache_value.toBoolean();
}

static unsigned int _suppressed_coverage_messages_count = 0;

/**
 * gjs_coverage_write_statistics:
 * @coverage: A #GjsCoverage
 * @output_directory: A directory to write coverage information to. Scripts
 * which were provided as part of the coverage-paths construction property will be written
 * out to output_directory, in the same directory structure relative to the source dir where
 * the tests were run.
 *
 * This function takes all available statistics and writes them out to either the file provided
 * or to files of the pattern (filename).info in the same directory as the scanned files. It will
 * provide coverage data for all files ending with ".js" in the coverage directories, even if they
 * were never actually executed.
 */
void
gjs_coverage_write_statistics(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GError *error = NULL;

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoCompartment compartment(context, priv->coverage_statistics);
    JSAutoRequest ar(context);

    /* Create output directory if it doesn't exist */
    if (!g_file_make_directory_with_parents(priv->output_dir, NULL, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
            g_critical("Could not create coverage output: %s", error->message);
            g_clear_error(&error);
            return;
        }
        g_clear_error(&error);
    }

    GFile *output_file = g_file_get_child(priv->output_dir, "coverage.lcov");

    GOutputStream *ostream =
        G_OUTPUT_STREAM(g_file_append_to(output_file,
                                         G_FILE_CREATE_NONE,
                                         NULL,
                                         &error));

    char **executed_coverage_files = get_covered_files(coverage);
    GArray *file_statistics_array = gjs_fetch_statistics_from_js(coverage,
                                                                 executed_coverage_files);

    for (size_t i = 0; i < file_statistics_array->len; ++i)
    {
        GjsCoverageFileStatistics *statistics = &(g_array_index(file_statistics_array, GjsCoverageFileStatistics, i));

        /* Only print statistics if the file was actually executed */
        for (char **iter = executed_coverage_files; *iter; ++iter) {
            if (g_strcmp0(*iter, statistics->filename) == 0) {
                print_statistics_for_file(statistics, priv->output_dir, ostream);

                /* Inner loop */
                break;
            }
        }
    }

    g_strfreev(executed_coverage_files);

    const bool has_cache_path = priv->cache != NULL;
    const bool cache_is_stale = coverage_statistics_has_stale_cache(coverage);

    if (has_cache_path && cache_is_stale) {
        GBytes *cache_data = gjs_serialize_statistics(coverage);
        gjs_write_cache_file(priv->cache, cache_data);
        g_bytes_unref(cache_data);
    }

    char *output_file_path = g_file_get_path(priv->output_dir);
    g_message("Wrote coverage statistics to %s", output_file_path);
    if (_suppressed_coverage_messages_count) {
        g_message("There were %i suppressed message(s) when collecting "
                  "coverage, set GJS_SHOW_COVERAGE_MESSAGES to see them.",
                  _suppressed_coverage_messages_count);
        _suppressed_coverage_messages_count = 0;
    }

    g_free(output_file_path);
    g_array_unref(file_statistics_array);
    g_object_unref(ostream);
    g_object_unref(output_file);
}

static void
gjs_coverage_init(GjsCoverage *self)
{
}

static bool
gjs_context_eval_file_in_compartment(GjsContext      *context,
                                     const char      *filename,
                                     JS::HandleObject compartment_object,
                                     GError         **error)
{
    char  *script = NULL;
    gsize script_len = 0;

    GFile *file = g_file_new_for_commandline_arg(filename);

    if (!g_file_load_contents(file,
                              NULL,
                              &script,
                              &script_len,
                              NULL,
                              error)) {
        g_object_unref(file);
        return false;
    }

    g_object_unref(file);

    int start_line_number = 1;
    const char *stripped_script = gjs_strip_unix_shebang(script, &script_len,
                                                         &start_line_number);

    JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);

    JSAutoCompartment compartment(js_context, compartment_object);

    JS::CompileOptions options(js_context);
    options.setUTF8(true)
           .setFileAndLine(filename, start_line_number)
           .setSourceIsLazy(true);
    JS::RootedScript compiled_script(js_context);
    if (!JS::Compile(js_context, options, stripped_script, script_len,
                     &compiled_script))
        return false;

    if (!JS::CloneAndExecuteScript(js_context, compiled_script)) {
        g_free(script);
        gjs_log_exception(js_context);
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED, "Failed to evaluate %s", filename);
        return false;
    }

    g_free(script);

    return true;
}

static bool
coverage_log(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoJSChar s(context);
    JSExceptionState *exc_state;

    if (argc != 1) {
        gjs_throw(context, "Must pass a single argument to log()");
        return false;
    }

    JSAutoRequest ar(context);

    if (!g_getenv("GJS_SHOW_COVERAGE_MESSAGES")) {
        _suppressed_coverage_messages_count++;
        argv.rval().setUndefined();
        return true;
    }

    /* JS::ToString might throw, in which case we will only log that the value
     * could not be converted to string */
    exc_state = JS_SaveExceptionState(context);
    JS::RootedString jstr(context, JS::ToString(context, argv[0]));
    if (jstr != NULL)
        argv[0].setString(jstr);  // GC root
    JS_RestoreExceptionState(context, exc_state);

    if (jstr == NULL) {
        g_message("JS LOG: <cannot convert value to string>");
        return true;
    }

    if (!gjs_string_to_utf8(context, JS::StringValue(jstr), &s)) {
        return false;
    }

    g_message("JS COVERAGE MESSAGE: %s", s.get());

    argv.rval().setUndefined();
    return true;
}

static GFile *
get_file_from_call_args_filename(JSContext    *context,
                                 JS::CallArgs &args) {
    char *filename = NULL;

    if (!gjs_parse_call_args(context, "getFileContents", args, "s",
                             "filename", &filename))
        return NULL;

    /* path could be a resource, so use g_file_new_for_commandline_arg. */
    GFile *file = g_file_new_for_commandline_arg(filename);

    g_free(filename);
    return file;
}

static bool
coverage_get_file_modification_time(JSContext *context,
                                    unsigned  argc,
                                    JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GTimeVal mtime;
    bool ret = false;
    GFile *file = get_file_from_call_args_filename(context, args);

    if (!file)
        return false;

    if (gjs_get_file_mtime(file, &mtime)) {
        JS::AutoValueArray<2> mtime_values_array(context);
        mtime_values_array[0].setInt32(mtime.tv_sec);
        mtime_values_array[1].setInt32(mtime.tv_usec);
        JS::RootedObject array_obj(context,
            JS_NewArrayObject(context, mtime_values_array));
        if (array_obj == NULL)
            goto out;
        args.rval().setObject(*array_obj);
    } else {
        args.rval().setNull();
    }

    ret = true;

out:
    g_object_unref(file);
    return ret;
}

static bool
coverage_get_file_checksum(JSContext *context,
                           unsigned  argc,
                           JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GFile *file = get_file_from_call_args_filename(context, args);

    if (!file)
        return false;

    char *checksum = gjs_get_file_checksum(file);

    if (!checksum) {
        char *filename = get_file_identifier(file);
        gjs_throw(context, "Failed to read %s and get its checksum", filename);
        g_free(filename);
        g_object_unref(file);
        return false;
    }

    args.rval().setString(JS_NewStringCopyZ(context, checksum));

    g_object_unref(file);
    g_free(checksum);
    return true;
}

static bool
coverage_get_file_contents(JSContext *context,
                           unsigned   argc,
                           JS::Value *vp)
{
    bool ret = false;
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GFile *file = NULL;
    char *script = NULL;
    gsize script_len;
    GError *error = NULL;

    file = get_file_from_call_args_filename(context, args);
    if (!file)
        return false;

    if (!g_file_load_contents(file,
                              NULL,
                              &script,
                              &script_len,
                              NULL,
                              &error)) {
        char *filename = get_file_identifier(file);
        gjs_throw(context, "Failed to load contents for filename %s: %s", filename, error->message);
        g_free(filename);
        goto out;
    }

    args.rval().setString(JS_NewStringCopyN(context, script, script_len));
    ret = true;

 out:
    g_clear_error(&error);
    g_object_unref(file);
    g_free(script);
    return ret;
}

static JSFunctionSpec coverage_funcs[] = {
    JS_FS("log", coverage_log, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS("getFileContents", coverage_get_file_contents, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS("getFileModificationTime", coverage_get_file_modification_time, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS("getFileChecksum", coverage_get_file_checksum, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};

static void
coverage_statistics_tracer(JSTracer *trc, void *data)
{
    GjsCoverage *coverage = (GjsCoverage *) data;
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JS::TraceEdge<JSObject *>(trc, &priv->coverage_statistics,
                              "coverage_statistics");
}

/* This function is mainly used in the tests in order to fiddle with
 * the internals of the coverage statisics collector on the coverage
 * compartment side */
bool
gjs_run_script_in_coverage_compartment(GjsCoverage *coverage,
                                       const char  *script)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);
    JSAutoRequest ar(js_context);

    JS::CompileOptions options(js_context);
    options.setUTF8(true);

    JS::RootedValue rval(js_context);
    if (!JS::Evaluate(js_context, options, script, strlen(script), &rval)) {
        gjs_log_exception(js_context);
        g_warning("Failed to evaluate <coverage_modifier>");
        return false;
    }

    return true;
}

bool
gjs_inject_value_into_coverage_compartment(GjsCoverage     *coverage,
                                           JS::HandleValue  value,
                                           const char      *property)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JSContext     *js_context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);

    JS::RootedObject coverage_global_scope(js_context,
                                           JS_GetGlobalForObject(js_context, priv->coverage_statistics));

    if (!JS_SetProperty(js_context, coverage_global_scope, property,
                        value)) {
        g_warning("Failed to set property %s to requested value", property);
        return false;
    }

    return true;
}

static bool
bootstrap_coverage(GjsCoverage *coverage)
{
    static const char  *coverage_script = "resource:///org/gnome/gjs/modules/coverage.js";
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GBytes             *cache_bytes = NULL;
    GError             *error = NULL;

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);

    JSObject *debuggee = gjs_get_import_global(context);
    JS::CompartmentOptions options;
    options.behaviors().setVersion(JSVERSION_LATEST);
    JS::RootedObject debugger_compartment(context,
                                          gjs_create_global_object(context));
    {
        JSAutoCompartment compartment(context, debugger_compartment);
        JS::RootedObject debuggeeWrapper(context, debuggee);
        if (!JS_WrapObject(context, &debuggeeWrapper)) {
            gjs_throw(context, "Failed to wrap debugeee");
            return false;
        }

        JS::RootedValue debuggeeWrapperValue(context, JS::ObjectValue(*debuggeeWrapper));
        if (!JS_SetProperty(context, debugger_compartment, "debuggee",
                            debuggeeWrapperValue)) {
            gjs_throw(context, "Failed to set debuggee property");
            return false;
        }

        if (!gjs_define_global_properties(context, debugger_compartment)) {
            gjs_throw(context, "Failed to define global properties on debugger "
                      "compartment");
            return false;
        }

        if (!JS_DefineFunctions(context, debugger_compartment, &coverage_funcs[0]))
            g_error("Failed to init coverage");

        if (!gjs_context_eval_file_in_compartment(priv->context,
                                                  coverage_script,
                                                  debugger_compartment,
                                                  &error))
            g_error("Failed to eval coverage script: %s\n", error->message);

        JS::RootedObject coverage_statistics_constructor(context);
        JS::RootedId coverage_statistics_name(context,
            gjs_intern_string_to_id(context, "CoverageStatistics"));
        if (!gjs_object_require_property(context, debugger_compartment,
                                         "debugger compartment",
                                         coverage_statistics_name,
                                         &coverage_statistics_constructor))
            return false;

        /* Create value for holding the cache. This will be undefined if
         * the cache does not exist, otherwise it will be an object set
         * to the value of the cache */
        JS::RootedValue cache_value(context);

        if (priv->cache)
            cache_bytes = read_all_bytes_from_file(priv->cache);

        if (cache_bytes) {
            JSString *cache_object = gjs_deserialize_cache_to_object_for_compartment(context,
                                                                                     debugger_compartment,
                                                                                     cache_bytes);
            cache_value.setString(cache_object);
            g_bytes_unref(cache_bytes);
        }

        /* Now create the array to pass the desired prefixes over */
        JSObject *prefixes = gjs_build_string_array(context, -1, priv->prefixes);

        JS::AutoValueArray<3> coverage_statistics_constructor_args(context);
        coverage_statistics_constructor_args[0].setObject(*prefixes);
        coverage_statistics_constructor_args[1].set(cache_value);
        coverage_statistics_constructor_args[2]
            .setBoolean(g_getenv("GJS_DEBUG_COVERAGE_EXECUTED_LINES"));

        JSObject *coverage_statistics = JS_New(context,
                                               coverage_statistics_constructor,
                                               coverage_statistics_constructor_args);

        if (!coverage_statistics) {
            gjs_throw(context, "Failed to create coverage_statitiscs object");
            return false;
        }

        /* Add a tracer, as suggested by jdm on #jsapi */
        JS_AddExtraGCRootsTracer(JS_GetRuntime(context),
                                 coverage_statistics_tracer,
                                 coverage);

        priv->coverage_statistics = coverage_statistics;
    }

    return true;
}

static void
gjs_coverage_constructed(GObject *object)
{
    G_OBJECT_CLASS(gjs_coverage_parent_class)->constructed(object);

    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    new (&priv->coverage_statistics) JS::Heap<JSObject *>();

    if (!priv->cache_specified) {
        g_message("Cache path was not given, picking default one");
        priv->cache = g_file_new_for_path(".internal-gjs-coverage-cache");
    }

    if (!bootstrap_coverage(coverage)) {
        JSContext *context = static_cast<JSContext *>(gjs_context_get_native_context(priv->context));
        JSAutoCompartment compartment(context, gjs_get_import_global(context));
        gjs_log_exception(context);
    }
}

static void
gjs_coverage_set_property(GObject      *object,
                          unsigned int  prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    switch (prop_id) {
    case PROP_PREFIXES:
        g_assert(priv->prefixes == NULL);
        priv->prefixes = (char **) g_value_dup_boxed (value);
        break;
    case PROP_CONTEXT:
        priv->context = GJS_CONTEXT(g_value_dup_object(value));
        break;
    case PROP_CACHE:
        priv->cache_specified = true;
        /* g_value_dup_object() adds a reference if not NULL */
        priv->cache = static_cast<GFile *>(g_value_dup_object(value));
        break;
    case PROP_OUTPUT_DIRECTORY:
        priv->output_dir = G_FILE(g_value_dup_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gjs_clear_js_side_statistics_from_coverage_object(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    if (priv->coverage_statistics) {
        /* Remove tracer before disposing the context */
        JSContext *js_context = (JSContext *) gjs_context_get_native_context(priv->context);
        JSAutoRequest ar(js_context);
        JSAutoCompartment ac(js_context, priv->coverage_statistics);
        JS::RootedObject rooted_priv(js_context, priv->coverage_statistics);
        JS::RootedValue rval(js_context);
        if (!JS_CallFunctionName(js_context, rooted_priv, "deactivate",
                                 JS::HandleValueArray::empty(), &rval)) {
            gjs_log_exception(js_context);
            g_error("Failed to deactivate debugger - this is a fatal error");
        }

        /* Remove GC roots trace after we've decomissioned the object
         * and no longer need it to be traced here. */
        JS_RemoveExtraGCRootsTracer(JS_GetRuntime(js_context),
                                    coverage_statistics_tracer,
                                    coverage);

        priv->coverage_statistics = NULL;
    }
}

static void
gjs_coverage_dispose(GObject *object)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    /* Decomission objects inside of the JSContext before
     * disposing of the context */
    gjs_clear_js_side_statistics_from_coverage_object(coverage);
    g_clear_object(&priv->context);

    G_OBJECT_CLASS(gjs_coverage_parent_class)->dispose(object);
}

static void
gjs_coverage_finalize (GObject *object)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    g_strfreev(priv->prefixes);
    g_clear_object(&priv->output_dir);
    g_clear_object(&priv->cache);
    priv->coverage_statistics.~Heap();

    G_OBJECT_CLASS(gjs_coverage_parent_class)->finalize(object);
}

static void
gjs_coverage_class_init (GjsCoverageClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    object_class->constructed = gjs_coverage_constructed;
    object_class->dispose = gjs_coverage_dispose;
    object_class->finalize = gjs_coverage_finalize;
    object_class->set_property = gjs_coverage_set_property;

    properties[PROP_PREFIXES] = g_param_spec_boxed("prefixes",
                                                   "Prefixes",
                                                   "Prefixes of files on which to perform coverage analysis",
                                                   G_TYPE_STRV,
                                                   (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_CONTEXT] = g_param_spec_object("context",
                                                   "Context",
                                                   "A context to gather coverage stats for",
                                                   GJS_TYPE_CONTEXT,
                                                   (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_CACHE] = g_param_spec_object("cache",
                                                 "Cache",
                                                 "File containing a cache to preload ASTs from",
                                                 G_TYPE_FILE,
                                                 (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_OUTPUT_DIRECTORY] =
        g_param_spec_object("output-directory", "Output directory",
                            "Directory handle at which to output coverage statistics",
                            G_TYPE_FILE,
                            (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class,
                                      PROP_N,
                                      properties);
}

/**
 * gjs_coverage_new:
 * @prefixes: A null-terminated strv of prefixes of files on which to record
 * code coverage
 * @context: A #GjsContext object
 * @output_dir: A #GFile handle to a directory in which to write coverage
 * information
 *
 * Creates a new #GjsCoverage object, using a cache in a temporary file to
 * pre-fill the AST information for the specified scripts in @prefixes, so long
 * as the data in the cache has the same mtime as those scripts.
 *
 * Scripts which were provided as part of @prefixes will be written out to
 * @output_dir, in the same directory structure relative to the source dir where
 * the tests were run.
 *
 * Returns: A #GjsCoverage object
 */
GjsCoverage *
gjs_coverage_new (const char * const *prefixes,
                  GjsContext         *context,
                  GFile              *output_dir)
{
    GjsCoverage *coverage =
        GJS_COVERAGE(g_object_new(GJS_TYPE_COVERAGE,
                                  "prefixes", prefixes,
                                  "context", context,
                                  "output-directory", output_dir,
                                  NULL));

    return coverage;
}

GjsCoverage *
gjs_coverage_new_internal_with_cache(const char * const *coverage_prefixes,
                                     GjsContext         *context,
                                     GFile              *output_dir,
                                     GFile              *cache)
{
    GjsCoverage *coverage =
        GJS_COVERAGE(g_object_new(GJS_TYPE_COVERAGE,
                                  "prefixes", coverage_prefixes,
                                  "context", context,
                                  "cache", cache,
                                  "output-directory", output_dir,
                                  NULL));

    return coverage;
}

GjsCoverage *
gjs_coverage_new_internal_without_cache(const char * const *prefixes,
                                        GjsContext         *cx,
                                        GFile              *output_dir)
{
    return gjs_coverage_new_internal_with_cache(prefixes, cx, output_dir, NULL);
}
