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

#include "util/error.h"

typedef struct _GjsDebugHooks GjsDebugHooks;
typedef struct _GjsCoverageBranchData GjsCoverageBranchData;

struct _GjsCoveragePrivate {
    gchar         **covered_paths;

    GjsContext    *context;
    JSObject      *coverage_statistics;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsCoverage,
                           gjs_coverage,
                           G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_COVERAGE_PATHS,
    PROP_CONTEXT,
    PROP_N
};

static GParamSpec *properties[PROP_N] = { NULL, };

struct _GjsCoverageBranchData {
    GArray       *branch_alternatives;
    GArray       *branch_alternatives_taken;
    unsigned int branch_point;
    unsigned int last_branch_exit;
    gboolean     branch_hit;
};

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
    unsigned int hit_count;
} GjsCoverageFunction;

static void
write_string_into_stream(GOutputStream *stream,
                         const gchar   *string)
{
    g_output_stream_write(stream, (gconstpointer) string, strlen(string) * sizeof(gchar), NULL, NULL);
}

static void
write_source_file_header(GOutputStream *stream,
                         const gchar   *source_file_path)
{
    write_string_into_stream(stream, "SF:");
    write_string_into_stream(stream, source_file_path);
    write_string_into_stream(stream, "\n");
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
    char *line = g_strdup_printf("FNDA:%i,%s\n",
                                 hit_count,
                                 function_name);

    (*n_functions_found)++;

    if (hit_count > 0)
        (*n_functions_hit)++;

    write_string_into_stream(stream, line);
    g_free(line);
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

    write_string_into_stream(stream, "FN:");
    write_string_into_stream(stream, function->key);
    write_string_into_stream(stream, "\n");
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
write_uint32_into_stream(GOutputStream *stream,
                         unsigned int   integer)
{
    char buf[32];
    g_snprintf(buf, 32, "%u", integer);
    g_output_stream_write(stream, (gconstpointer) buf, strlen(buf) * sizeof(char), NULL, NULL);
}

static void
write_int32_into_stream(GOutputStream *stream,
                        int            integer)
{
    char buf[32];
    g_snprintf(buf, 32, "%i", integer);
    g_output_stream_write(stream, (gconstpointer) buf, strlen(buf) * sizeof(char), NULL, NULL);
}

static void
write_function_coverage(GOutputStream *data_stream,
                        unsigned int  n_found_functions,
                        unsigned int  n_hit_functions)
{
    write_string_into_stream(data_stream, "FNF:");
    write_uint32_into_stream(data_stream, n_found_functions);
    write_string_into_stream(data_stream, "\n");

    write_string_into_stream(data_stream, "FNH:");
    write_uint32_into_stream(data_stream, n_hit_functions);
    write_string_into_stream(data_stream, "\n");
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
            hit_count_string = g_strdup_printf("%i", alternative_counter);

        char *branch_alternative_line = g_strdup_printf("BRDA:%i,0,%i,%s\n",
                                                        branch_point,
                                                        i,
                                                        hit_count_string);

        write_string_into_stream(data->output_stream, branch_alternative_line);
        g_free(hit_count_string);
        g_free(branch_alternative_line);

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
    write_string_into_stream(stream, "BRF:");
    write_uint32_into_stream(stream, n_branch_exits_found);
    write_string_into_stream(stream, "\n");

    write_string_into_stream(stream, "BRH:");
    write_uint32_into_stream(stream, n_branch_exits_hit);
    write_string_into_stream(stream, "\n");
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

        write_string_into_stream(stream, "DA:");
        write_uint32_into_stream(stream, i);
        write_string_into_stream(stream, ",");
        write_int32_into_stream(stream, hit_count_for_line);
        write_string_into_stream(stream, "\n");

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
    write_string_into_stream(stream, "LH:");
    write_uint32_into_stream(stream, lines_hit_count);
    write_string_into_stream(stream, "\n");

    write_string_into_stream(stream, "LF:");
    write_uint32_into_stream(stream, executable_lines_count);
    write_string_into_stream(stream, "\n");
}

static void
write_end_of_record(GOutputStream *stream)
{
    write_string_into_stream(stream, "end_of_record\n");
}

static void
copy_source_file_to_coverage_output(const char *source,
                                    const char *destination)
{
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

typedef struct _StatisticsPrintUserData {
    GjsContext        *reflection_context;
    GFileOutputStream *ostream;
    const gchar       *output_directory;
    JSContext         *context;
    JSObject          *object;
} StatisticsPrintUserData;

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
                gjs_throw(context, "Failed to get function names array element %i", i);
                return FALSE;
            }

            if (!(inserter(c_side_array, context, &element))) {
                g_array_unref(c_side_array);
                gjs_throw(context, "Failed to convert array element %i", i);
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
get_executed_lines_for(GjsCoverage *coverage,
                       jsval       *filename_value)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, priv->coverage_statistics, "getExecutedLinesFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof (unsigned int), NULL, convert_and_insert_unsigned_int, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
init_covered_function(GjsCoverageFunction *function,
                      char                *key,
                      unsigned int        hit_count)
{
    function->key = key;
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

    unsigned int line_number = JSVAL_TO_INT(hit_count_property_value);

    GjsCoverageFunction info;
    init_covered_function(&info,
                          utf8_string,
                          line_number);

    g_array_append_val(array, info);

    return TRUE;
}

static GArray *
get_functions_for(GjsCoverage *coverage,
                  jsval       *filename_value)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, priv->coverage_statistics, "getFunctionsFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof (GjsCoverageFunction), clear_coverage_function, convert_and_insert_function_decl, &array)) {
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
get_branches_for(GjsCoverage *coverage,
                 jsval       *filename_value)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    GArray *array = NULL;
    jsval rval;

    if (!JS_CallFunctionName(context, priv->coverage_statistics, "getBranchesFor", 1, filename_value, &rval)) {
        gjs_log_exception(context);
        return NULL;
    }

    if (!get_array_from_js_value(context, &rval, sizeof (GjsCoverageBranch), clear_coverage_branch, convert_and_insert_branch_info, &array)) {
        gjs_log_exception(context);
        return NULL;
    }

    return array;
}

static void
print_statistics_for_file(GjsCoverage   *coverage,
                          char          *filename,
                          const char    *output_directory,
                          GOutputStream *ostream)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    char *absolute_output_directory = get_absolute_path(output_directory);
    char *diverged_paths =
        find_diverging_child_components(filename,
                                        absolute_output_directory);
    char *destination_filename = g_build_filename(absolute_output_directory,
                                                  diverged_paths,
                                                  NULL);

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoCompartment compartment(context, priv->coverage_statistics);
    JSAutoRequest ar(context);

    JSString *filename_jsstr = JS_NewStringCopyZ(context, filename);
    jsval    filename_jsval = STRING_TO_JSVAL(filename_jsstr);

    GArray *lines = get_executed_lines_for(coverage, &filename_jsval);
    GArray *functions = get_functions_for(coverage, &filename_jsval);
    GArray *branches = get_branches_for(coverage, &filename_jsval);

    if (!lines || !functions || !branches)
        return;

    copy_source_file_to_coverage_output(filename, destination_filename);

    write_source_file_header(ostream, (const char *) destination_filename);
    write_functions(ostream, functions);

    unsigned int functions_hit_count = 0;
    unsigned int functions_found_count = 0;

    write_functions_hit_counts(ostream,
                               functions,
                               &functions_found_count,
                               &functions_hit_count);
    write_function_coverage(ostream,
                            functions_found_count,
                            functions_hit_count);

    unsigned int branches_hit_count = 0;
    unsigned int branches_found_count = 0;

    write_branch_coverage(ostream,
                          branches,
                          &branches_found_count,
                          &branches_hit_count);
    write_branch_totals(ostream,
                        branches_found_count,
                        branches_hit_count);

    unsigned int lines_hit_count = 0;
    unsigned int executable_lines_count = 0;

    write_line_coverage(ostream,
                        lines,
                        &lines_hit_count,
                        &executable_lines_count);
    write_line_totals(ostream,
                      lines_hit_count,
                      executable_lines_count);
    write_end_of_record(ostream);

    g_array_unref(lines);
    g_array_unref(functions);
    g_array_unref(branches);

    g_free(diverged_paths);
    g_free(destination_filename);
    g_free(absolute_output_directory);
}

void
gjs_coverage_write_statistics(GjsCoverage *coverage,
                              const char  *output_directory)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GError *error = NULL;

    /* Create output_directory if it doesn't exist */
    g_mkdir_with_parents(output_directory, 0755);

    char  *output_file_path = g_build_filename(output_directory,
                                               "coverage.lcov",
                                               NULL);
    GFile *output_file = g_file_new_for_commandline_arg(output_file_path);
    g_free (output_file_path);

    GOutputStream *ostream =
        G_OUTPUT_STREAM(g_file_append_to(output_file,
                                         G_FILE_CREATE_NONE,
                                         NULL,
                                         &error));

    char **file_iter = priv->covered_paths;
    while (*file_iter) {
        print_statistics_for_file(coverage, *file_iter, output_directory, ostream);
        ++file_iter;
    }

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
                              error))
        return FALSE;

    jsval return_value;

    JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);

    JSAutoCompartment compartment(js_context, compartment_object);

    if (!gjs_eval_with_scope(js_context,
                             compartment_object,
                             script, script_len, filename,
                             &return_value)) {
        gjs_log_exception(js_context);
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED, "Failed to evaluate %s", filename);
        return FALSE;
    }

    g_free(script);

    return TRUE;
}

static JSBool
coverage_warning(JSContext *context,
                 unsigned   argc,
                 jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *s;
    JSExceptionState *exc_state;
    JSString *jstr;

    if (argc != 1) {
        gjs_throw(context, "Must pass a single argument to warning()");
        return JS_FALSE;
    }

    JS_BeginRequest(context);

    /* JS_ValueToString might throw, in which we will only
     *log that the value could be converted to string */
    exc_state = JS_SaveExceptionState(context);
    jstr = JS_ValueToString(context, argv[0]);
    if (jstr != NULL)
        argv[0] = STRING_TO_JSVAL(jstr);    // GC root
    JS_RestoreExceptionState(context, exc_state);

    if (jstr == NULL) {
        g_message("JS LOG: <cannot convert value to string>");
        JS_EndRequest(context);
        return JS_TRUE;
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    g_message("JS COVERAGE WARNING: %s", s);
    g_free(s);

    JS_EndRequest(context);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
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
    { "warning", JSOP_WRAPPER (coverage_warning), 1, GJS_MODULE_PROP_FLAGS },
    { "getFileContents", JSOP_WRAPPER (coverage_get_file_contents), 1, GJS_MODULE_PROP_FLAGS },
    { NULL },
};

static gboolean
bootstrap_coverage(GjsCoverage *coverage)
{
    static const char  *coverage_script = "resource:///org/gnome/gjs/modules/coverage.js";
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    GError             *error = NULL;

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);

    JSObject *debuggee = JS_GetGlobalObject(context);
    JS::CompartmentOptions options;
    options.setVersion(JSVERSION_LATEST);
    JSObject *debugger_compartment = JS_NewGlobalObject(context, &coverage_global_class, NULL, options);

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

        JSObject *coverage_statistics_constructor = JSVAL_TO_OBJECT(coverage_statistics_prototype_value);

        /* Now create the array to pass the desired script names over */
        JSObject *filenames_js_array = gjs_build_string_array(context, -1, priv->covered_paths);

        jsval coverage_statistics_constructor_arguments[] = {
            OBJECT_TO_JSVAL(filenames_js_array)
        };

        JSObject *coverage_statistics = JS_New(context,
                                               coverage_statistics_constructor,
                                               1,
                                               coverage_statistics_constructor_arguments);

        if (!coverage_statistics) {
            gjs_throw(context, "Failed to create coverage_statitiscs object");
            return FALSE;
        }

        priv->coverage_statistics = coverage_statistics;
    }

    return TRUE;
}

static void
gjs_coverage_constructed(GObject *object)
{
    G_OBJECT_CLASS(gjs_coverage_parent_class)->constructed(object);

    GjsCoverage *coverage = GJS_DEBUG_COVERAGE(object);
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
    GjsCoverage *coverage = GJS_DEBUG_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    switch (prop_id) {
    case PROP_COVERAGE_PATHS:
        g_assert(priv->covered_paths == NULL);
        priv->covered_paths = (char **) g_value_dup_boxed (value);
        break;
    case PROP_CONTEXT:
        priv->context = GJS_CONTEXT(g_value_dup_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gjs_coverage_dispose(GObject *object)
{
    GjsCoverage *coverage = GJS_DEBUG_COVERAGE (object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    g_clear_object(&priv->context);

    G_OBJECT_CLASS(gjs_coverage_parent_class)->dispose(object);
}

static void
gjs_coverage_finalize (GObject *object)
{
    GjsCoverage *coverage = GJS_DEBUG_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    g_strfreev(priv->covered_paths);

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

    properties[PROP_COVERAGE_PATHS] = g_param_spec_boxed("coverage-paths",
                                                         "Coverage Paths",
                                                         "Paths (and included subdirectories) of which to perform coverage analysis",
                                                         G_TYPE_STRV,
                                                         (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_CONTEXT] = g_param_spec_object("context",
                                                   "Context",
                                                   "A context to gather coverage stats for",
                                                   GJS_TYPE_CONTEXT,
                                                   (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

    g_object_class_install_properties(object_class,
                                      PROP_N,
                                      properties);
}

/**
 * gjs_coverage_new:
 * @coverage_paths: (transfer none): A null-terminated strv of directories to generate
 * coverage_data for
 *
 * Returns: A #GjsDebugCoverage
 */
GjsCoverage *
gjs_coverage_new (const char    **coverage_paths,
                  GjsContext    *context)
{
    GjsCoverage *coverage =
        GJS_DEBUG_COVERAGE(g_object_new(GJS_TYPE_DEBUG_COVERAGE,
                                        "coverage-paths", coverage_paths,
                                        "context", context,
                                        NULL));

    return coverage;
}
