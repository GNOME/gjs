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

#include "gjs-module.h"
#include "coverage.h"
#include "coverage-internal.h"

#include "util/error.h"

struct _GjsCoveragePrivate {
    gchar **prefixes;
    GjsContext *context;
    JSObject *coverage_statistics;

    char *cache_path;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsCoverage,
                           gjs_coverage,
                           G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_PREFIXES,
    PROP_CONTEXT,
    PROP_CACHE,
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
    gboolean     hit;
} GjsCoverageBranch;

typedef struct _GjsCoverageFunction {
    char         *key;
    unsigned int line_number;
    unsigned int hit_count;
} GjsCoverageFunction;

static void
write_source_file_header(GOutputStream *stream,
                         const gchar   *source_file_path)
{
    g_output_stream_printf(stream, NULL, NULL, NULL, "SF:%s\n", source_file_path);
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
    gboolean      branch_point_was_hit;
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
copy_source_file_to_coverage_output(const char *source,
                                    const char *destination)
{
    /* Either source_file or destination_file could be a resource,
     * so we must use g_file_new_for_commandline_arg to disambiguate
     * between URI paths and filesystem paths. */
    GFile *source_file = g_file_new_for_commandline_arg(source);
    GFile *destination_file = g_file_new_for_commandline_arg(destination);
    GError *error = NULL;

    /* We also need to recursively make the directory we
     * want to copy to, as g_file_copy doesn't do that */
    gchar *destination_dirname = g_path_get_dirname(destination);
    g_mkdir_with_parents(destination_dirname, S_IRWXU);

    if (!g_file_copy(source_file,
                     destination_file,
                     G_FILE_COPY_OVERWRITE,
                     NULL,
                     NULL,
                     NULL,
                     &error)) {
        g_critical("Failed to copy source file %s to destination %s: %s\n",
                   source,
                   destination,
                   error->message);
    }

    g_clear_error(&error);

    g_free(destination_dirname);
    g_object_unref(destination_file);
    g_object_unref(source_file);
}

/* This function will strip a URI scheme and return
 * the string with the URI scheme stripped or NULL
 * if the path was not a valid URI
 */
static const char *
strip_uri_scheme(const char *potential_uri)
{
    char *uri_header = g_uri_parse_scheme(potential_uri);

    if (uri_header) {
        gsize offset = strlen(uri_header);
        g_free(uri_header);

        /* g_uri_parse_scheme only parses the name
         * of the scheme, we also need to strip the
         * characters '://' */
        return potential_uri + offset + 3;
    }

    return NULL;
}

/* This function will return a string of pathname
 * components from the first directory indicating
 * where two directories diverge. For instance:
 *
 * child_path: /a/b/c/d/e
 * parent_path: /a/b/d/
 *
 * Will return: c/d/e
 *
 * If the directories are not at all similar then
 * the full dirname of the child_path effectively
 * be returned.
 *
 * As a special case, child paths that are a URI
 * automatically return the full URI path with
 * the URI scheme stripped out.
 */
static char *
find_diverging_child_components(const char *child_path,
                                const char *parent_path)
{
    const char *stripped_uri = strip_uri_scheme(child_path);

    if (stripped_uri)
        return g_strdup(stripped_uri);

    char **child_path_components = g_strsplit(child_path, "/", -1);
    char **parent_path_components = g_strsplit(parent_path, "/", -1);
    char **child_path_component_iterator = child_path_components;
    char **parent_path_component_iterator = parent_path_components;

    for (; *child_path_component_iterator != NULL &&
           *parent_path_component_iterator != NULL;
           ++child_path_component_iterator,
           ++parent_path_component_iterator) {
        if (g_strcmp0(*child_path_component_iterator,
                      *parent_path_component_iterator))
            break;
    }

    /* Paste the child path components back together */
    char *diverged = g_strjoinv("/", child_path_component_iterator);

    g_strfreev(child_path_components);
    g_strfreev(parent_path_components);

    return diverged;
}

/* The coverage output directory could be a relative path
 * so we need to get an absolute path */
static char *
get_absolute_path(const char *path)
{
    char *absolute_path = NULL;

    if (!g_path_is_absolute(path)) {
        char *current_dir = g_get_current_dir();
        absolute_path = g_build_filename(current_dir, path, NULL);
        g_free(current_dir);
    } else {
        absolute_path = g_strdup(path);
    }

    return absolute_path;
}

typedef gboolean (*ConvertAndInsertJSVal) (GArray    *array,
                                           JSContext *context,
                                           jsval     *element);

static gboolean
get_array_from_js_value(JSContext             *context,
                        jsval                 *value,
                        size_t                 array_element_size,
                        GDestroyNotify         element_clear_func,
                        ConvertAndInsertJSVal  inserter,
                        GArray                **out_array)
{
    g_return_val_if_fail(out_array != NULL, FALSE);
    g_return_val_if_fail(*out_array == NULL, FALSE);

    JSObject *js_array = JSVAL_TO_OBJECT(*value);

    if (!JS_IsArrayObject(context, js_array)) {
        g_critical("Returned object from is not an array");
        return FALSE;
    }

    /* We're not preallocating any space here at the moment until
     * we have some profiling data that suggests a good size to
     * preallocate to. */
    GArray *c_side_array = g_array_new(TRUE, TRUE, array_element_size);
    u_int32_t js_array_len;

    if (element_clear_func)
        g_array_set_clear_func(c_side_array, element_clear_func);

    if (JS_GetArrayLength(context, js_array, &js_array_len)) {
        u_int32_t i = 0;
        for (; i < js_array_len; ++i) {
            jsval element;
            if (!JS_GetElement(context, js_array, i, &element)) {
                g_array_unref(c_side_array);
                gjs_throw(context, "Failed to get function names array element %d", i);
                return FALSE;
            }

            if (!(inserter(c_side_array, context, &element))) {
                g_array_unref(c_side_array);
                gjs_throw(context, "Failed to convert array element %d", i);
                return FALSE;
            }
        }
    }

    *out_array = c_side_array;

    return TRUE;
}

static gboolean
convert_and_insert_unsigned_int(GArray    *array,
                                JSContext *context,
                                jsval     *element)
{
    if (!JSVAL_IS_INT(*element) &&
        !JSVAL_IS_VOID(*element) &&
        !JSVAL_IS_NULL(*element)) {
        g_critical("Array element is not an integer or undefined or null");
        return FALSE;
    }

    if (JSVAL_IS_INT(*element)) {
        unsigned int element_integer = JSVAL_TO_INT(*element);
        g_array_append_val(array, element_integer);
    } else {
        int not_executable = -1;
        g_array_append_val(array, not_executable);
    }

    return TRUE;
}

static GArray *
get_executed_lines_for(JSContext        *context,
                       JS::HandleObject  coverage_statistics,
                       jsval            *filename_value)
{
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, coverage_statistics, "getExecutedLinesFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof(unsigned int), NULL, convert_and_insert_unsigned_int, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
init_covered_function(GjsCoverageFunction *function,
                      char                *key,
                      unsigned int        line_number,
                      unsigned int        hit_count)
{
    function->key = key;
    function->line_number = line_number;
    function->hit_count = hit_count;
}

static void
clear_coverage_function(gpointer info_location)
{
    GjsCoverageFunction *info = (GjsCoverageFunction *) info_location;
    g_free(info->key);
}

static gboolean
convert_and_insert_function_decl(GArray    *array,
                                 JSContext *context,
                                 jsval     *element)
{
    JSObject *object = JSVAL_TO_OBJECT(*element);

    if (!object) {
        gjs_throw(context, "Converting element to object failed");
        return FALSE;
    }

    jsval    function_name_property_value;

    if (!JS_GetProperty(context, object, "name", &function_name_property_value)) {
        gjs_throw(context, "Failed to get name property for function object");
        return FALSE;
    }

    char *utf8_string;

    if (JSVAL_IS_STRING(function_name_property_value)) {
        if (!gjs_string_to_utf8(context,
                                function_name_property_value,
                                &utf8_string)) {
            gjs_throw(context, "Failed to convert function_name to string");
            return FALSE;
        }
    } else if (JSVAL_IS_NULL(function_name_property_value)) {
        utf8_string = NULL;
    } else {
        gjs_throw(context, "Unexpected type for function_name");
        return FALSE;
    }

    jsval hit_count_property_value;
    if (!JS_GetProperty(context, object, "hitCount", &hit_count_property_value) ||
        !JSVAL_IS_INT(hit_count_property_value)) {
        gjs_throw(context, "Failed to get hitCount property for function object");
        return FALSE;
    }

    jsval line_number_property_value;
    if (!JS_GetProperty(context, object, "line", &line_number_property_value) ||
        !JSVAL_IS_INT(line_number_property_value)) {
        gjs_throw(context, "Failed to get line property for function object");
        return FALSE;
    }

    unsigned int line_number = JSVAL_TO_INT(line_number_property_value);
    unsigned int hit_count = JSVAL_TO_INT(hit_count_property_value);

    GjsCoverageFunction info;
    init_covered_function(&info,
                          utf8_string,
                          line_number,
                          hit_count);

    g_array_append_val(array, info);

    return TRUE;
}

static GArray *
get_functions_for(JSContext        *context,
                  JS::HandleObject  coverage_statistics,
                  jsval            *filename_value)
{
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, coverage_statistics, "getFunctionsFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof(GjsCoverageFunction), clear_coverage_function, convert_and_insert_function_decl, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
init_covered_branch(GjsCoverageBranch *branch,
                    unsigned int       point,
                    JSBool             was_hit,
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

static gboolean
convert_and_insert_branch_exit(GArray    *array,
                               JSContext *context,
                               jsval     *element)
{
    if (!JSVAL_IS_OBJECT(*element)) {
        gjs_throw(context, "Branch exit array element is not an object");
        return FALSE;
    }

    JSObject *object = JSVAL_TO_OBJECT(*element);

    if (!object) {
        gjs_throw(context, "Converting element to object failed");
        return FALSE;
    }

    jsval   line_value;
    int32_t line;

    if (!JS_GetProperty(context, object, "line", &line_value) ||
        !JSVAL_IS_INT(line_value)) {
        gjs_throw(context, "Failed to get line property from element");
        return FALSE;
    }

    line = JSVAL_TO_INT(line_value);

    jsval   hit_count_value;
    int32_t hit_count;

    if (!JS_GetProperty(context, object, "hitCount", &hit_count_value) ||
        !JSVAL_IS_INT(hit_count_value)) {
        gjs_throw(context, "Failed to get hitCount property from element");
        return FALSE;
    }

    hit_count = JSVAL_TO_INT(hit_count_value);

    GjsCoverageBranchExit exit = {
        (unsigned int) line,
        (unsigned int) hit_count
    };

    g_array_append_val(array, exit);

    return TRUE;
}

static gboolean
convert_and_insert_branch_info(GArray    *array,
                               JSContext *context,
                               jsval     *element)
{
    if (!JSVAL_IS_OBJECT(*element) &&
        !JSVAL_IS_VOID(*element)) {
        gjs_throw(context, "Branch array element is not an object or undefined");
        return FALSE;
    }

    if (JSVAL_IS_OBJECT(*element)) {
        JSObject *object = JSVAL_TO_OBJECT(*element);

        if (!object) {
            gjs_throw(context, "Converting element to object failed");
            return FALSE;
        }

        jsval   branch_point_value;
        int32_t branch_point;

        if (!JS_GetProperty(context, object, "point", &branch_point_value) ||
            !JSVAL_IS_INT(branch_point_value)) {
            gjs_throw(context, "Failed to get point property from element");
            return FALSE;
        }

        branch_point = JSVAL_TO_INT(branch_point_value);

        jsval  was_hit_value;
        JSBool was_hit;

        if (!JS_GetProperty(context, object, "hit", &was_hit_value) ||
            !JSVAL_IS_BOOLEAN(was_hit_value)) {
            gjs_throw(context, "Failed to get point property from element");
            return FALSE;
        }

        was_hit = JSVAL_TO_BOOLEAN(was_hit_value);

        jsval  branch_exits_value;
        GArray *branch_exits_array = NULL;

        if (!JS_GetProperty(context, object, "exits", &branch_exits_value) ||
            !JSVAL_IS_OBJECT(branch_exits_value)) {
            gjs_throw(context, "Failed to get exits property from element");
            return FALSE;
        }

        if (!get_array_from_js_value(context,
                                     &branch_exits_value,
                                     sizeof(GjsCoverageBranchExit),
                                     NULL,
                                     convert_and_insert_branch_exit,
                                     &branch_exits_array)) {
            /* Already logged the exception, no need to do anything here */
            return FALSE;
        }

        GjsCoverageBranch branch;
        init_covered_branch(&branch,
                            branch_point,
                            was_hit,
                            branch_exits_array);

        g_array_append_val(array, branch);
    }

    return TRUE;
}

static GArray *
get_branches_for(JSContext        *context,
                 JS::HandleObject  coverage_statistics,
                 jsval            *filename_value)
{
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, coverage_statistics, "getBranchesFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof(GjsCoverageBranch), clear_coverage_branch, convert_and_insert_branch_info, &array)) {
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

static gboolean
fetch_coverage_file_statistics_from_js(JSContext                 *context,
                                       JS::HandleObject           coverage_statistics,
                                       const char                *filename,
                                       GjsCoverageFileStatistics *statistics)
{
    JSAutoCompartment compartment(context, coverage_statistics);
    JSAutoRequest ar(context);

    JSString *filename_jsstr = JS_NewStringCopyZ(context, filename);
    jsval    filename_jsval = STRING_TO_JSVAL(filename_jsstr);

    GArray *lines = get_executed_lines_for(context, coverage_statistics, &filename_jsval);
    GArray *functions = get_functions_for(context, coverage_statistics, &filename_jsval);
    GArray *branches = get_branches_for(context, coverage_statistics, &filename_jsval);

    if (!lines || !functions || !branches)
    {
        g_clear_pointer(&lines, g_array_unref);
        g_clear_pointer(&functions, g_array_unref);
        g_clear_pointer(&branches, g_array_unref);
        return FALSE;
    }

    statistics->filename = g_strdup(filename);
    statistics->lines = lines;
    statistics->functions = functions;
    statistics->branches = branches;

    return TRUE;
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
                          const char                *output_directory,
                          GOutputStream             *ostream)
{
    char *absolute_output_directory = get_absolute_path(output_directory);
    char *diverged_paths =
        find_diverging_child_components(file_statistics->filename,
                                        absolute_output_directory);
    char *destination_filename = g_build_filename(absolute_output_directory,
                                                  diverged_paths,
                                                  NULL);

    copy_source_file_to_coverage_output(file_statistics->filename, destination_filename);

    write_source_file_header(ostream, (const char *) destination_filename);
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
    g_free(destination_filename);
    g_free(absolute_output_directory);
}

static char **
get_covered_files(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, priv->coverage_statistics);
    jsval rval;
    JSObject *files_obj;

    char **files = NULL;
    uint32_t n_files;

    if (!JS_CallFunctionName(context, priv->coverage_statistics, "getCoveredFiles", 0, NULL, &rval)) {
        gjs_log_exception(context);
        goto error;
    }

    if (!rval.isObject())
        goto error;

    files_obj = &rval.toObject();
    if (!JS_GetArrayLength(context, files_obj, &n_files))
        goto error;

    files = g_new0(char *, n_files + 1);
    for (uint32_t i = 0; i < n_files; i++) {
        jsval element;
        char *file;
        if (!JS_GetElement(context, files_obj, i, &element))
            goto error;

        if (!gjs_string_to_utf8(context, element, &file))
            goto error;

        files[i] = file;
    }

    files[n_files] = NULL;
    return files;

 error:
    g_strfreev(files);
    return NULL;
}

gboolean
gjs_get_path_mtime(const char *path, GTimeVal *mtime)
{
    /* path could be a resource path, as the callers don't check
     * if the path is a resource path, but rather if the mtime fetch
     * operation succeeded. Use g_file_new_for_commandline_arg to handle
     * that case. */
    GError *error = NULL;
    GFile *file = g_file_new_for_commandline_arg(path);
    GFileInfo *info = g_file_query_info(file,
                                        "time::modified,time::modified-usec",
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL,
                                        &error);

    g_clear_object(&file);

    if (!info) {
        g_warning("Failed to get modification time of %s, "
                  "falling back to checksum method for caching. Reason was: %s",
                  path, error->message);
        g_clear_object(&info);
        return FALSE;
    }

    g_file_info_get_modification_time(info, mtime);
    g_clear_object(&info);

    /* For some URI types, eg, resources, the operation getting
     * the mtime might succeed, but by default zero is returned.
     *
     * Check if that is the case for boht tv_sec and tv_usec and if
     * so return FALSE. */
    return !(mtime->tv_sec == 0 && mtime->tv_usec == 0);
}

static GBytes *
read_all_bytes_from_path(const char *path)
{
    /* path could be a resource, so use g_file_new_for_commandline_arg. */
    GFile *file = g_file_new_for_commandline_arg(path);

    /* We have to use g_file_query_exists here since
     * g_file_test(path, G_FILE_TEST_EXISTS) is implemented in terms
     * of access(), which doesn't work with resource paths. */
    if (!g_file_query_exists(file, NULL)) {
        g_object_unref(file);
        return NULL;
    }

    gsize len = 0;
    gchar *data = NULL;

    GError *error = NULL;

    if (!g_file_load_contents(file,
                              NULL,
                              &data,
                              &len,
                              NULL,
                              &error)) {
        g_printerr("Unable to read bytes from: %s, reason was: %s\n",
                   path, error->message);
        g_clear_error(&error);
        g_object_unref(file);
        return NULL;
    }

    return g_bytes_new_take(data, len);
}

gchar *
gjs_get_path_checksum(const char *path)
{
    GBytes *data = read_all_bytes_from_path(path);

    if (!data)
        return NULL;

    gchar *checksum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA512, data);

    g_bytes_unref(data);
    return checksum;
}

static unsigned int COVERAGE_STATISTICS_CACHE_MAGIC = 0xC0432463;

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
    JSRuntime *js_runtime = JS_GetRuntime(js_context);

    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalForObject(js_context,
                                               priv->coverage_statistics));

    JS::RootedValue string_value_return(js_runtime);

    if (!JS_CallFunctionName(js_context,
                             priv->coverage_statistics,
                             "stringify",
                             0,
                             NULL,
                             &(string_value_return.get()))) {
        gjs_log_exception(js_context);
        return NULL;
    }

    if (!string_value_return.isString())
        return NULL;

    /* Free'd by g_bytes_new_take */
    char *statistics_as_json_string = NULL;

    if (!gjs_string_to_utf8(js_context,
                            string_value_return.get(),
                            &statistics_as_json_string)) {
        gjs_log_exception(js_context);
        return NULL;
    }

    return g_bytes_new_take((guint8 *) statistics_as_json_string,
                            strlen(statistics_as_json_string));
}

static JSObject *
gjs_get_generic_object_constructor(JSContext        *context,
                                   JSRuntime        *runtime,
                                   JS::HandleObject  global_object)
{
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, global_object);

    jsval object_constructor_value;
    if (!JS_GetProperty(context, global_object, "Object", &object_constructor_value) ||
        !JSVAL_IS_OBJECT(object_constructor_value))
        g_assert_not_reached();

    return JSVAL_TO_OBJECT(object_constructor_value);
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
    gpointer string = g_bytes_unref_to_data(g_bytes_ref(cache_data),
                                            &len);

    return JS_NewStringCopyN(context, (const char *) string, len);
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
    JS::RootedObject global_object(JS_GetRuntime(context),
                                   JS_GetGlobalForObject(context, priv->coverage_statistics));
    return gjs_deserialize_cache_to_object_for_compartment(context, global_object, cache_data);
}

GArray *
gjs_fetch_statistics_from_js(GjsCoverage *coverage,
                             gchar       **coverage_files)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);

    GArray *file_statistics_array = g_array_new(FALSE,
                                                FALSE,
                                                sizeof(GjsCoverageFileStatistics));
    g_array_set_clear_func(file_statistics_array,
                           gjs_coverage_statistics_file_statistics_clear);

    JS::RootedObject rooted_coverage_statistics(JS_GetRuntime(js_context),
                                                priv->coverage_statistics);

    char                      **file_iter = coverage_files;
    GjsCoverageFileStatistics *statistics_iter = (GjsCoverageFileStatistics *) file_statistics_array->data;
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

gboolean
gjs_write_cache_to_path(const char *path,
                        GBytes     *cache)
{
    GFile *file = g_file_new_for_commandline_arg(path);
    gsize cache_len = 0;
    char *cache_data = (char *) g_bytes_get_data(cache, &cache_len);
    GError *error = NULL;

    if (!g_file_replace_contents(file,
                                 cache_data,
                                 cache_len,
                                 NULL,
                                 FALSE,
                                 G_FILE_CREATE_NONE,
                                 NULL,
                                 NULL,
                                 &error)) {
        g_object_unref(file);
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

        return FALSE;
    }

    g_object_unref(file);

    return TRUE;
}

static JSBool
coverage_statistics_has_stale_cache(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);

    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);

    jsval stale_cache_value;
    if (!JS_CallFunctionName(js_context,
                             priv->coverage_statistics,
                             "staleCache",
                             0,
                             NULL,
                             &stale_cache_value)) {
        gjs_log_exception(js_context);
        g_error("Failed to call into javascript to get stale cache value. This is a bug");
    }

    return JSVAL_TO_BOOLEAN(stale_cache_value);
}

static unsigned int _suppressed_coverage_messages_count = 0;

void
gjs_coverage_write_statistics(GjsCoverage *coverage,
                              const char  *output_directory)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GError *error = NULL;

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoCompartment compartment(context, priv->coverage_statistics);
    JSAutoRequest ar(context);

    /* Create output_directory if it doesn't exist */
    g_mkdir_with_parents(output_directory, 0755);

    char  *output_file_path = g_build_filename(output_directory,
                                               "coverage.lcov",
                                               NULL);
    GFile *output_file = g_file_new_for_commandline_arg(output_file_path);

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
                print_statistics_for_file(statistics, output_directory, ostream);

                /* Inner loop */
                break;
            }
        }
    }

    g_strfreev(executed_coverage_files);

    const gboolean has_cache_path = priv->cache_path != NULL;
    const gboolean cache_is_stale = coverage_statistics_has_stale_cache(coverage);

    if (has_cache_path && cache_is_stale) {
        GBytes *cache_data = gjs_serialize_statistics(coverage);
        gjs_write_cache_to_path(priv->cache_path, cache_data);
        g_bytes_unref(cache_data);
    }

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

static JSClass coverage_global_class = {
    "GjsCoverageGlobal", JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(GJS_GLOBAL_SLOT_LAST),
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, NULL,
    NULL /* checkAccess */, NULL /* call */, NULL /* hasInstance */, NULL /* construct */, NULL,
    { NULL }
};

static gboolean
gjs_context_eval_file_in_compartment(GjsContext *context,
                                     const char *filename,
                                     JSObject   *compartment_object,
                                     GError     **error)
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
        return FALSE;
    }

    g_object_unref(file);

    jsval return_value;

    JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);

    JSAutoCompartment compartment(js_context, compartment_object);

    if (!gjs_eval_with_scope(js_context,
                             compartment_object,
                             script, script_len, filename,
                             &return_value)) {
        g_free(script);
        gjs_log_exception(js_context);
        g_free(script);
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED, "Failed to evaluate %s", filename);
        return FALSE;
    }

    g_free(script);

    return TRUE;
}

static JSBool
coverage_log(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    char *s;
    JSExceptionState *exc_state;
    JSString *jstr;

    if (argc != 1) {
        gjs_throw(context, "Must pass a single argument to log()");
        return JS_FALSE;
    }

    JSAutoRequest ar(context);

    if (!g_getenv("GJS_SHOW_COVERAGE_MESSAGES")) {
        _suppressed_coverage_messages_count++;
        argv.rval().set(JSVAL_VOID);
        return JS_TRUE;
    }

    /* JS_ValueToString might throw, in which we will only
     *log that the value could be converted to string */
    exc_state = JS_SaveExceptionState(context);
    jstr = JS_ValueToString(context, argv[0]);
    if (jstr != NULL)
        argv[0] = STRING_TO_JSVAL(jstr);    // GC root
    JS_RestoreExceptionState(context, exc_state);

    if (jstr == NULL) {
        g_message("JS LOG: <cannot convert value to string>");
        return JS_TRUE;
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
        return JS_FALSE;
    }

    g_message("JS COVERAGE MESSAGE: %s", s);
    g_free(s);

    argv.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

static char *
get_filename_from_filename_as_js_string(JSContext    *context,
                                        JS::CallArgs &args) {
    char *filename = NULL;

    if (!gjs_parse_call_args(context, "getFileContents", "s", args,
                             "filename", &filename))
        return NULL;

    return filename;
}

static GFile *
get_file_from_filename_as_js_string(JSContext    *context,
                                    JS::CallArgs &args) {
    char *filename = get_filename_from_filename_as_js_string(context, args);

    if (!filename) {
        gjs_throw(context, "Failed to parse arguments for filename");
        return NULL;
    }

    GFile *file = g_file_new_for_commandline_arg(filename);

    g_free(filename);
    return file;
}

static JSBool
coverage_get_file_modification_time(JSContext *context,
                                    unsigned  argc,
                                    jsval     *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JSRuntime    *runtime = JS_GetRuntime(context);
    GTimeVal mtime;
    JSBool ret = JS_FALSE;
    char *filename = get_filename_from_filename_as_js_string(context, args);

    if (!filename)
        goto out;

    if (gjs_get_path_mtime(filename, &mtime)) {
        JS::RootedObject mtime_values_array(runtime,
                                            JS_NewArrayObject(context, 0, NULL));
        if (!JS_DefineElement(context, mtime_values_array, 0, JS::Int32Value(mtime.tv_sec), NULL, NULL, 0))
            goto out;
        if (!JS_DefineElement(context, mtime_values_array, 1, JS::Int32Value(mtime.tv_usec), NULL, NULL, 0))
            goto out;
        args.rval().setObject(*(mtime_values_array.get()));
    } else {
        args.rval().setNull();
    }

    ret = JS_TRUE;

out:
    g_free(filename);
    return ret;
}

static JSBool
coverage_get_file_checksum(JSContext *context,
                           unsigned  argc,
                           jsval     *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JSRuntime    *runtime = JS_GetRuntime(context);
    GTimeVal mtime;
    JSBool ret = JS_FALSE;
    char *filename = get_filename_from_filename_as_js_string(context, args);

    if (!filename)
        return JS_FALSE;

    char *checksum = gjs_get_path_checksum(filename);

    if (!checksum) {
        gjs_throw(context, "Failed to read %s and get its checksum", filename);
        return JS_FALSE;
    }

    JS::RootedString rooted_checksum(runtime, JS_NewStringCopyZ(context,
                                                                checksum));
    args.rval().setString(rooted_checksum);

    g_free(filename);
    g_free(checksum);
    return JS_TRUE;
}

static JSBool
coverage_get_file_contents(JSContext *context,
                           unsigned   argc,
                           jsval     *vp)
{
    JSBool ret = JS_FALSE;
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    char *filename = NULL;
    GFile *file = NULL;
    char *script = NULL;
    gsize script_len;
    JSString *script_jsstr;
    GError *error = NULL;

    if (!gjs_parse_call_args(context, "getFileContents", "s", args,
                             "filename", &filename))
        goto out;

    file = g_file_new_for_commandline_arg(filename);

    if (!g_file_load_contents(file,
                              NULL,
                              &script,
                              &script_len,
                              NULL,
                              &error)) {
        gjs_throw(context, "Failed to load contents for filename %s: %s", filename, error->message);
        goto out;
    }

    args.rval().setString(JS_NewStringCopyN(context, script, script_len));
    ret = JS_TRUE;

 out:
    g_clear_error(&error);
    if (file)
        g_object_unref(file);
    g_free(filename);
    g_free(script);
    return ret;
}

static JSFunctionSpec coverage_funcs[] = {
    { "log", JSOP_WRAPPER(coverage_log), 1, GJS_MODULE_PROP_FLAGS },
    { "getFileContents", JSOP_WRAPPER(coverage_get_file_contents), 1, GJS_MODULE_PROP_FLAGS },
    { "getFileModificationTime", JSOP_WRAPPER(coverage_get_file_modification_time), 1, GJS_MODULE_PROP_FLAGS },
    { "getFileChecksum", JSOP_WRAPPER(coverage_get_file_checksum), 1, GJS_MODULE_PROP_FLAGS },
    { NULL }
};

static void
coverage_statistics_tracer(JSTracer *trc, void *data)
{
    GjsCoverage *coverage = (GjsCoverage *) data;
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JS_CallObjectTracer(trc, &priv->coverage_statistics, "coverage_statistics");
}

/* This function is mainly used in the tests in order to fiddle with
 * the internals of the coverage statisics collector on the coverage
 * compartment side */
gboolean
gjs_run_script_in_coverage_compartment(GjsCoverage *coverage,
                                       const char  *script)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext          *js_context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);
    jsval rval;
    if (!gjs_eval_with_scope(js_context,
                             priv->coverage_statistics,
                             script,
                             strlen(script),
                             "<coverage_modifier>",
                             &rval)) {
        gjs_log_exception(js_context);
        g_warning("Failed to evaluate <coverage_modifier>");
        return FALSE;
    }

    return TRUE;
}

gboolean
gjs_inject_value_into_coverage_compartment(GjsCoverage     *coverage,
                                           JS::HandleValue handle_value,
                                           const char      *property)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JSContext     *js_context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(js_context);
    JSAutoCompartment ac(js_context, priv->coverage_statistics);

    JS::RootedObject coverage_global_scope(JS_GetRuntime(js_context),
                                           JS_GetGlobalForObject(js_context, priv->coverage_statistics));

    jsval value = handle_value;
    if (!JS_SetProperty(js_context, coverage_global_scope, property, &value)) {
        g_warning("Failed to set property %s to requested value", property);
        return FALSE;
    }

    return TRUE;
}

/* Gets the root import and wraps it into a cross-compartment
 * object so that it can be used in the debugger compartment */
static JSObject *
gjs_wrap_root_importer_in_compartment(JSContext *context,
                                      JS::HandleObject compartment)
{
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, compartment);
    JS::RootedValue importer (JS_GetRuntime(context),
                              gjs_get_global_slot(context,
                                                  GJS_GLOBAL_SLOT_IMPORTS));

    g_assert (!JSVAL_IS_VOID(importer));

    JS::RootedObject wrapped_importer(JS_GetRuntime(context),
                                      JSVAL_TO_OBJECT(importer));
    if (!JS_WrapObject(context, wrapped_importer.address())) {
        return NULL;
    }

    return wrapped_importer;
}

static gboolean
bootstrap_coverage(GjsCoverage *coverage)
{
    static const char  *coverage_script = "resource:///org/gnome/gjs/modules/coverage.js";
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GBytes             *cache_bytes = NULL;
    GError             *error = NULL;

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);

    JSObject *debuggee = JS_GetGlobalObject(context);
    JS::CompartmentOptions options;
    options.setVersion(JSVERSION_LATEST);
    JS::RootedObject debugger_compartment(JS_GetRuntime(context),
                                          JS_NewGlobalObject(context, &coverage_global_class, NULL, options));
    {
        JSAutoCompartment compartment(context, debugger_compartment);
        JS::RootedObject debuggeeWrapper(context, debuggee);
        if (!JS_WrapObject(context, debuggeeWrapper.address())) {
            gjs_throw(context, "Failed to wrap debugeee");
            return FALSE;
        }

        JS::RootedValue debuggeeWrapperValue(context, JS::ObjectValue(*debuggeeWrapper));
        if (!JS_SetProperty(context, debugger_compartment, "debuggee", debuggeeWrapperValue.address())) {
            gjs_throw(context, "Failed to set debuggee property");
            return FALSE;
        }

        if (!JS_InitStandardClasses(context, debugger_compartment)) {
            gjs_throw(context, "Failed to init standard classes");
            return FALSE;
        }

        if (!JS_InitReflect(context, debugger_compartment)) {
            gjs_throw(context, "Failed to init Reflect");
            return FALSE;
        }

        if (!JS_DefineDebuggerObject(context, debugger_compartment)) {
            gjs_throw(context, "Failed to init Debugger");
            return FALSE;
        }

        JS::RootedObject wrapped_importer(JS_GetRuntime(context),
                                          gjs_wrap_root_importer_in_compartment(context,
                                                                                debugger_compartment));;

        if (!wrapped_importer) {
            gjs_throw(context, "Failed to wrap root importer in debugger compartment");
            return FALSE;
        }

        /* Now copy the global root importer (which we just created,
         * if it didn't exist) to our global object
         */
        if (!gjs_define_root_importer_object(context, debugger_compartment, wrapped_importer)) {
            gjs_throw(context, "Failed to set 'imports' on debugger compartment");
            return FALSE;
        }

        if (!JS_DefineFunctions(context, debugger_compartment, &coverage_funcs[0]))
            g_error("Failed to init coverage");

        if (!gjs_context_eval_file_in_compartment(priv->context,
                                                  coverage_script,
                                                  debugger_compartment,
                                                  &error))
            g_error("Failed to eval coverage script: %s\n", error->message);

        jsval coverage_statistics_prototype_value;
        if (!JS_GetProperty(context, debugger_compartment, "CoverageStatistics", &coverage_statistics_prototype_value) ||
            !JSVAL_IS_OBJECT(coverage_statistics_prototype_value)) {
            gjs_throw(context, "Failed to get CoverageStatistics prototype");
            return FALSE;
        }

        /* Create value for holding the cache. This will be undefined if
         * the cache does not exist, otherwise it will be an object set
         * to the value of the cache */
        JS::RootedValue cache_value(JS_GetRuntime(context));

        if (priv->cache_path)
            cache_bytes = read_all_bytes_from_path(priv->cache_path);

        if (cache_bytes) {
            JSString *cache_object = gjs_deserialize_cache_to_object_for_compartment(context,
                                                                                     debugger_compartment,
                                                                                     cache_bytes);
            cache_value.set(JS::StringValue(cache_object));
            g_bytes_unref(cache_bytes);
        } else {
            cache_value.set(JS::UndefinedValue());
        }

        JSObject *coverage_statistics_constructor = JSVAL_TO_OBJECT(coverage_statistics_prototype_value);

        /* Now create the array to pass the desired prefixes over */
        JSObject *prefixes = gjs_build_string_array(context, -1, priv->prefixes);

        jsval coverage_statistics_constructor_arguments[] = {
            OBJECT_TO_JSVAL(prefixes),
            cache_value.get()
        };

        JSObject *coverage_statistics = JS_New(context,
                                               coverage_statistics_constructor,
                                               2,
                                               coverage_statistics_constructor_arguments);

        if (!coverage_statistics) {
            gjs_throw(context, "Failed to create coverage_statitiscs object");
            return FALSE;
        }

        /* Add a tracer, as suggested by jdm on #jsapi */
        JS_AddExtraGCRootsTracer(JS_GetRuntime(context),
                                 coverage_statistics_tracer,
                                 coverage);

        priv->coverage_statistics = coverage_statistics;
    }

    return TRUE;
}

static void
gjs_coverage_constructed(GObject *object)
{
    G_OBJECT_CLASS(gjs_coverage_parent_class)->constructed(object);

    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);

    /* Before bootstrapping, turn off the JIT on the context */
    guint32 options_flags = JS_GetOptions(context) & ~(JSOPTION_ION | JSOPTION_BASELINE | JSOPTION_ASMJS);
    JS_SetOptions(context, options_flags);

    if (!bootstrap_coverage(coverage)) {
        JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
        JSAutoCompartment compartment(context, JS_GetGlobalObject(context));
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
        priv->cache_path = g_value_dup_string(value);
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
        JS::RootedValue rval(JS_GetRuntime(js_context));
        if (!JS_CallFunctionName(js_context,
                                 priv->coverage_statistics,
                                 "deactivate",
                                 0,
                                 NULL,
                                 rval.address())) {
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
    g_clear_pointer(&priv->cache_path, (GDestroyNotify) g_free);

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
    properties[PROP_CACHE] = g_param_spec_string("cache",
                                                 "Cache",
                                                 "Path to a file containing a cache to preload ASTs from",
                                                 NULL,
                                                 (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

    g_object_class_install_properties(object_class,
                                      PROP_N,
                                      properties);
}

/**
 * gjs_coverage_new:
 * @coverage_prefixes: (transfer none): A null-terminated strv of prefixes of files to perform coverage on
 * coverage_data for
 *
 * Returns: A #GjsCoverage object
 */
GjsCoverage *
gjs_coverage_new (const char **prefixes,
                  GjsContext  *context)
{
    GjsCoverage *coverage =
        GJS_COVERAGE(g_object_new(GJS_TYPE_COVERAGE,
                                  "prefixes", prefixes,
                                  "context", context,
                                  NULL));

    return coverage;
}

/**
 * gjs_coverage_new_from_cache:
 * Creates a new GjsCoverage object, but uses @cache_path to pre-fill the AST information for
 * the specified scripts in coverage_paths, so long as the data in the cache has the same
 * mtime as those scripts.
 *
 * @coverage_prefixes: (transfer none): A null-terminated strv of prefixes of files to perform coverage on
 * @context: (transfer full): A #GjsContext object.
 * @cache_path: A path to a file containing a serialized cache.
 *
 * Returns: A #GjsCoverage object
 */
GjsCoverage *
gjs_coverage_new_from_cache(const char **coverage_prefixes,
                            GjsContext *context,
                            const char *cache_path)
{
    GjsCoverage *coverage =
        GJS_COVERAGE(g_object_new(GJS_TYPE_COVERAGE,
                                  "prefixes", coverage_prefixes,
                                  "context", context,
                                  "cache", cache_path,
                                  NULL));

    return coverage;
}
