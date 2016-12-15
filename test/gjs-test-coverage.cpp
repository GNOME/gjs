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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <fcntl.h>
#include <ftw.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixoutputstream.h>
#include <gjs/gjs.h>

#include "gjs/coverage.h"
#include "gjs/coverage-internal.h"

#include "gjs-test-utils.h"

typedef struct _GjsCoverageFixture {
    GjsContext    *context;
    GjsCoverage   *coverage;

    GFile *tmp_output_dir;
    GFile *tmp_js_script;
    GFile *lcov_output_dir;
    GFile *lcov_output;
} GjsCoverageFixture;

static void
replace_file(GFile      *file,
              const char *contents)
{
    GError *error = NULL;
    g_file_replace_contents(file, contents, strlen(contents), NULL /* etag */,
                            FALSE /* make backup */, G_FILE_CREATE_NONE,
                            NULL /* etag out */, NULL /* cancellable */, &error);
    g_assert_no_error(error);
}

static void
recursive_delete_dir(GFile *dir)
{
    GFileEnumerator *files =
        g_file_enumerate_children(dir, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                  G_FILE_QUERY_INFO_NONE, NULL, NULL);
    while (TRUE) {
        GFile *file;
        GFileInfo *info;
        if (!g_file_enumerator_iterate(files, &info, &file, NULL, NULL) ||
            !file || !info)
            break;
        if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
            recursive_delete_dir(file);
            continue;
        }
        g_file_delete(file, NULL, NULL);
    }
    g_file_delete(dir, NULL, NULL);
    g_object_unref(files);
}

static void
gjs_coverage_fixture_set_up(gpointer      fixture_data,
                            gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char         *js_script = "function f() { return 1; }\n";

    char *tmp_output_dir_name = g_strdup("/tmp/gjs_coverage_tmp.XXXXXX");
    tmp_output_dir_name = mkdtemp(tmp_output_dir_name);

    if (!tmp_output_dir_name)
        g_error("Failed to create temporary directory for test files: %s\n", strerror(errno));

    fixture->tmp_output_dir = g_file_new_for_path(tmp_output_dir_name);
    fixture->tmp_js_script = g_file_get_child(fixture->tmp_output_dir,
                                              "gjs_coverage_script.js");
    fixture->lcov_output_dir = g_file_get_child(fixture->tmp_output_dir,
                                                "gjs_coverage_test_coverage");
    fixture->lcov_output = g_file_get_child(fixture->lcov_output_dir,
                                            "coverage.lcov");

    g_file_make_directory_with_parents(fixture->lcov_output_dir, NULL, NULL);

    char *tmp_js_script_filename = g_file_get_path(fixture->tmp_js_script);

    /* Allocate a strv that we can pass over to gjs_coverage_new */
    char *coverage_paths[] = {
        tmp_js_script_filename,
        NULL
    };

    char *search_paths[] = {
        tmp_output_dir_name,
        NULL
    };

    fixture->context = gjs_context_new_with_search_path((char **) search_paths);
    fixture->coverage = gjs_coverage_new((const char **)coverage_paths, fixture->context,
                                         fixture->lcov_output_dir);

    replace_file(fixture->tmp_js_script, js_script);
    g_free(tmp_output_dir_name);
    g_free(tmp_js_script_filename);
}

static void
gjs_coverage_fixture_tear_down(gpointer      fixture_data,
                               gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    recursive_delete_dir(fixture->tmp_output_dir);

    g_object_unref(fixture->tmp_js_script);
    g_object_unref(fixture->tmp_output_dir);
    g_object_unref(fixture->lcov_output_dir);
    g_object_unref(fixture->lcov_output);
    g_object_unref(fixture->coverage);
    g_object_unref(fixture->context);
}

static const char *
line_starting_with(const char *data,
                   const char *needle)
{
    const gsize needle_length = strlen(needle);
    const char  *iter = data;

    while (iter) {
        if (strncmp(iter, needle, needle_length) == 0)
          return iter;

        iter = strstr(iter, "\n");

        if (iter)
          iter += 1;
    }

    return NULL;
}

static char *
write_statistics_and_get_coverage_data(GjsCoverage *coverage,
                                       GFile       *lcov_output,
                                       gsize       *coverage_data_length_return)
{
    gjs_coverage_write_statistics(coverage);

    char  *coverage_data_contents;

    g_file_load_contents(lcov_output, NULL /* cancellable */,
                         &coverage_data_contents, coverage_data_length_return,
                         NULL /* etag */,  NULL /* error */);

    return coverage_data_contents;
}

static char *
get_script_identifier(GFile *script)
{
    char *filename = g_file_get_path(script);
    if (!filename)
        filename = g_file_get_uri(script);
    return filename;
}

static bool
eval_script(GjsContext *cx,
            GFile      *script)
{
    char *filename = get_script_identifier(script);
    bool retval = gjs_context_eval_file(cx, filename, NULL, NULL);
    g_free(filename);
    return retval;
}

static char *
eval_script_and_get_coverage_data(GjsContext  *context,
                                  GjsCoverage *coverage,
                                  GFile       *script,
                                  GFile       *lcov_output,
                                  gsize       *coverage_data_length_return)
{
    eval_script(context, script);
    return write_statistics_and_get_coverage_data(coverage, lcov_output,
                                                  coverage_data_length_return);
}

static bool
coverage_data_contains_value_for_key(const char *data,
                                     const char *key,
                                     const char *value)
{
    const char *sf_line = line_starting_with(data, key);

    if (!sf_line)
        return false;

    return strncmp(&sf_line[strlen(key)],
                   value,
                   strlen(value)) == 0;
}

typedef bool (*CoverageDataMatchFunc) (const char *value,
                                       gpointer    user_data);

static bool
coverage_data_matches_value_for_key_internal(const char            *line,
                                             const char            *key,
                                             CoverageDataMatchFunc  match,
                                             gpointer               user_data)
{
    return (*match)(line, user_data);
}

static bool
coverage_data_matches_value_for_key(const char            *data,
                                    const char            *key,
                                    CoverageDataMatchFunc  match,
                                    gpointer               user_data)
{
    const char *line = line_starting_with(data, key);

    if (!line)
        return false;

    return coverage_data_matches_value_for_key_internal(line, key, match, user_data);
}

static bool
coverage_data_matches_any_value_for_key(const char            *data,
                                        const char            *key,
                                        CoverageDataMatchFunc  match,
                                        gpointer               user_data)
{
    data = line_starting_with(data, key);

    while (data) {
        if (coverage_data_matches_value_for_key_internal(data, key, match, user_data))
            return true;

        data = line_starting_with(data + 1, key);
    }

    return false;
}

static bool
coverage_data_matches_values_for_key(const char            *data,
                                     const char            *key,
                                     gsize                  n,
                                     CoverageDataMatchFunc  match,
                                     gpointer               user_data,
                                     gsize                  data_size)
{
    const char *line = line_starting_with (data, key);
    /* Keep matching. If we fail to match one of them then
     * bail out */
    char *data_iterator = (char *) user_data;

    while (line && n > 0) {
        if (!coverage_data_matches_value_for_key_internal(line, key, match, (gpointer) data_iterator))
            return false;

        line = line_starting_with(line + 1, key);
        --n;
        data_iterator += data_size;
    }

    /* If n is zero then we've found all available matches */
    if (n == 0)
        return true;

    return false;
}

/* A simple wrapper around gjs_coverage_new */
static GjsCoverage *
create_coverage_for_script(GjsContext *context,
                           GFile      *script,
                           GFile      *output_dir)
{
    char *script_path = get_script_identifier(script);
    char *coverage_scripts[] = {
        script_path,
        NULL
    };

    GjsCoverage *retval = gjs_coverage_new((const char **) coverage_scripts,
                                           context, output_dir);
    g_free(script_path);
    return retval;
}

static GjsCoverage *
create_coverage_for_script_and_cache(GjsContext *context,
                                     GFile      *cache,
                                     GFile      *script,
                                     GFile      *output_dir)
{
    char *script_path = get_script_identifier(script);
    char *coverage_scripts[] = {
        script_path,
        NULL
    };

    GjsCoverage *retval = gjs_coverage_new_from_cache((const char **) coverage_scripts,
                                                      context, output_dir, cache);
    g_free(script_path);
    return retval;
}

static void
test_covered_file_is_duplicated_into_output_if_resource(gpointer      fixture_data,
                                                        gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *mock_resource_filename = "resource:///org/gnome/gjs/mock/test/gjs-test-coverage/loadedJSFromResource.js";
    const char *coverage_scripts[] = {
        mock_resource_filename,
        NULL
    };

    g_object_unref(fixture->context);
    g_object_unref(fixture->coverage);
    char *js_script_dirname = g_file_get_path(fixture->tmp_output_dir);
    char *search_paths[] = {
        js_script_dirname,
        NULL
    };

    fixture->context = gjs_context_new_with_search_path(search_paths);
    fixture->coverage =
        gjs_coverage_new(coverage_scripts, fixture->context,
                         fixture->lcov_output_dir);

    gjs_context_eval_file(fixture->context,
                          mock_resource_filename,
                          NULL,
                          NULL);

    gjs_coverage_write_statistics(fixture->coverage);

    GFile *expected_temporary_js_script =
        g_file_resolve_relative_path(fixture->lcov_output_dir,
                                     "org/gnome/gjs/mock/test/gjs-test-coverage/loadedJSFromResource.js");

    g_assert_true(g_file_query_exists(expected_temporary_js_script, NULL));
    g_object_unref(expected_temporary_js_script);
    g_free(js_script_dirname);
}

static GFile *
get_output_file_for_script_on_disk(GFile *script,
                                   GFile *output_dir)

{
    char *base = g_file_get_basename(script);
    GFile *output = g_file_get_child(output_dir, base);

    g_free(base);
    return output;
}

static char *
get_output_path_for_script_on_disk(GFile *script,
                                   GFile *output_dir)
{
    GFile *output = get_output_file_for_script_on_disk(script, output_dir);
    char *output_path = g_file_get_path(output);
    g_object_unref(output);
    return output_path;
}

static void
test_covered_file_is_duplicated_into_output_if_path(gpointer      fixture_data,
                                                    gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    eval_script(fixture->context, fixture->tmp_js_script);

    gjs_coverage_write_statistics(fixture->coverage);

    GFile *expected_temporary_js_script =
        get_output_file_for_script_on_disk(fixture->tmp_js_script,
                                           fixture->lcov_output_dir);

    g_assert_true(g_file_query_exists(expected_temporary_js_script, NULL));

    g_object_unref(expected_temporary_js_script);
}

static void
test_previous_contents_preserved(gpointer      fixture_data,
                                 gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    g_assert(strstr(coverage_data_contents, existing_contents) != NULL);
    g_free(coverage_data_contents);
}


static void
test_new_contents_written(gpointer      fixture_data,
                          gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* We have new content in the coverage data */
    g_assert(strlen(existing_contents) != strlen(coverage_data_contents));
    g_free(coverage_data_contents);
}

static void
test_expected_source_file_name_written_to_coverage_data(gpointer      fixture_data,
                                                        gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    char *expected_source_filename =
        get_output_path_for_script_on_disk(fixture->tmp_js_script, fixture->lcov_output_dir);

    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "SF:",
                                                  expected_source_filename));

    g_free(expected_source_filename);
    g_free(coverage_data_contents);
}

static void
silence_log_func(const gchar    *domain,
                 GLogLevelFlags  log_level,
                 const gchar    *message,
                 gpointer        user_data)
{
}

static void
test_expected_entry_not_written_for_nonexistent_file(gpointer      fixture_data,
                                                        gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *coverage_paths[] = {
        "doesnotexist",
        NULL
    };

    g_object_unref(fixture->coverage);
    fixture->coverage = gjs_coverage_new(coverage_paths,
                                         fixture->context,
                                         fixture->lcov_output_dir);

    /* Temporarily disable fatal mask and silence warnings */
    GLogLevelFlags old_flags = g_log_set_always_fatal((GLogLevelFlags) G_LOG_LEVEL_ERROR);
    GLogFunc old_log_func = g_log_set_default_handler(silence_log_func, NULL);

    GFile *doesnotexist = g_file_new_for_path("doesnotexist");
    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context, fixture->coverage,
                                          doesnotexist, fixture->lcov_output,
                                          NULL);

    g_log_set_always_fatal(old_flags);
    g_log_set_default_handler(old_log_func, NULL);

    g_assert(!(coverage_data_contains_value_for_key(coverage_data_contents,
                                                    "SF:",
                                                    "doesnotexist")));

    g_free(coverage_data_contents);
    g_object_unref(doesnotexist);
}

typedef enum _BranchTaken {
    NOT_EXECUTED,
    NOT_TAKEN,
    TAKEN
} BranchTaken;

typedef struct _BranchLineData {
    int         expected_branch_line;
    int         expected_id;
    BranchTaken taken;
} BranchLineData;

static bool
branch_at_line_should_be_taken(const char *line,
                               gpointer user_data)
{
    BranchLineData *branch_data = (BranchLineData *) user_data;
    int line_no, branch_id, block_no, hit_count_num, nmatches;
    char hit_count[20];  /* can hold maxint64 (19 digits) + nul terminator */

    /* Advance past "BRDA:" */
    line += 5;

    nmatches = sscanf(line, "%i,%i,%i,%19s", &line_no, &block_no, &branch_id, hit_count);
    if (nmatches != 4) {
        if (errno != 0)
            g_error("sscanf: %s", strerror(errno));
        else
            g_error("sscanf: only matched %i", nmatches);
    }

    /* Determine the branch hit count. It will be either:
     * > -1 if the line containing the branch was never executed, or
     * > N times the branch was taken.
     *
     * The value of -1 is represented by a single "-" character, so
     * we should detect this case and set the value based on that */
    if (strlen(hit_count) == 1 && *hit_count == '-')
        hit_count_num = -1;
    else
        hit_count_num = atoi(hit_count);

    const bool hit_correct_branch_line =
        branch_data->expected_branch_line == line_no;
    const bool hit_correct_branch_id =
        branch_data->expected_id == branch_id;
    bool branch_correctly_taken_or_not_taken;

    switch (branch_data->taken) {
    case NOT_EXECUTED:
        branch_correctly_taken_or_not_taken = hit_count_num == -1;
        break;
    case NOT_TAKEN:
        branch_correctly_taken_or_not_taken = hit_count_num == 0;
        break;
    case TAKEN:
        branch_correctly_taken_or_not_taken = hit_count_num > 0;
        break;
    default:
        g_assert_not_reached();
    };

    return hit_correct_branch_line &&
           hit_correct_branch_id &&
           branch_correctly_taken_or_not_taken;

}

static void
test_single_branch_coverage_written_to_coverage_data(gpointer      fixture_data,
                                                     gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_basic_branch =
            "let x = 0;\n"
            "if (x > 0)\n"
            "    x++;\n"
            "else\n"
            "    x++;\n";

    replace_file(fixture->tmp_js_script, script_with_basic_branch);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    const BranchLineData expected_branches[] = {
        { 2, 0, NOT_TAKEN },
        { 2, 1, TAKEN }
    };
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "BRDA:",
                                                  expected_branches_len,
                                                  branch_at_line_should_be_taken,
                                                  (gpointer) expected_branches,
                                                  sizeof(BranchLineData)));

    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "BRF:",
                                                  "2"));
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "BRH:",
                                                  "1"));
    g_free(coverage_data_contents);
}

static void
test_multiple_branch_coverage_written_to_coverage_data(gpointer      fixture_data,
                                                       gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_case_statements_branch =
            "let y;\n"
            "for (let x = 0; x < 3; x++) {\n"
            "    switch (x) {\n"
            "    case 0:\n"
            "        y = x + 1;\n"
            "        break;\n"
            "    case 1:\n"
            "        y = x + 1;\n"
            "        break;\n"
            "    case 2:\n"
            "        y = x + 1;\n"
            "        break;\n"
            "    }\n"
            "}\n";

    replace_file(fixture->tmp_js_script, script_with_case_statements_branch);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    const BranchLineData expected_branches[] = {
        { 3, 0, TAKEN },
        { 3, 1, TAKEN },
        { 3, 2, TAKEN }
    };
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "BRDA:",
                                                  expected_branches_len,
                                                  branch_at_line_should_be_taken,
                                                  (gpointer) expected_branches,
                                                  sizeof(BranchLineData)));
    g_free(coverage_data_contents);
}

static void
test_branches_for_multiple_case_statements_fallthrough(gpointer      fixture_data,
                                                       gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_case_statements_branch =
            "let y;\n"
            "for (let x = 0; x < 3; x++) {\n"
            "    switch (x) {\n"
            "    case 0:\n"
            "    case 1:\n"
            "        y = x + 1;\n"
            "        break;\n"
            "    case 2:\n"
            "        y = x + 1;\n"
            "        break;\n"
            "    case 3:\n"
            "        y = x +1;\n"
            "        break;\n"
            "    }\n"
            "}\n";

    replace_file(fixture->tmp_js_script, script_with_case_statements_branch);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    const BranchLineData expected_branches[] = {
        { 3, 0, TAKEN },
        { 3, 1, TAKEN },
        { 3, 2, NOT_TAKEN }
    };
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "BRDA:",
                                                  expected_branches_len,
                                                  branch_at_line_should_be_taken,
                                                  (gpointer) expected_branches,
                                                  sizeof(BranchLineData)));
    g_free(coverage_data_contents);
}

static void
test_branch_not_hit_written_to_coverage_data(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_never_executed_branch =
            "let x = 0;\n"
            "if (x > 0) {\n"
            "    if (x > 0)\n"
            "        x++;\n"
            "} else {\n"
            "    x++;\n"
            "}\n";

    replace_file(fixture->tmp_js_script, script_with_never_executed_branch);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    const BranchLineData expected_branch = {
        3, 0, NOT_EXECUTED
    };

    g_assert(coverage_data_matches_any_value_for_key(coverage_data_contents,
                                                     "BRDA:",
                                                     branch_at_line_should_be_taken,
                                                     (gpointer) &expected_branch));
    g_free(coverage_data_contents);
}

static bool
has_function_name(const char *line,
                  gpointer    user_data)
{
    /* User data is const char ** */
    const char *expected_function_name = *((const char **) user_data);

    /* Advance past "FN:" */
    line += 3;

    /* Advance past the first comma */
    while (*(line - 1) != ',')
        ++line;

    return strncmp(line,
                   expected_function_name,
                   strlen(expected_function_name)) == 0;
}

static void
test_function_names_written_to_coverage_data(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_named_and_unnamed_functions =
            "function f(){}\n"
            "let b = function(){}\n";

    replace_file(fixture->tmp_js_script, script_with_named_and_unnamed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* The internal hash table is sorted in alphabetical order
     * so the function names need to be in this order too */
    const char * expected_function_names[] = {
        "(anonymous):2:0",
        "f:1:0"
    };
    const gsize expected_function_names_len = G_N_ELEMENTS(expected_function_names);

    /* Just expect that we've got an FN matching out expected function names */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FN:",
                                                  expected_function_names_len,
                                                  has_function_name,
                                                  (gpointer) expected_function_names,
                                                  sizeof(const char *)));
    g_free(coverage_data_contents);
}

static bool
has_function_line(const char *line,
                  gpointer    user_data)
{
    /* User data is const char ** */
    const char *expected_function_line = *((const char **) user_data);

    /* Advance past "FN:" */
    line += 3;

    return strncmp(line,
                   expected_function_line,
                   strlen(expected_function_line)) == 0;
}

static void
test_function_lines_written_to_coverage_data(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_functions =
        "function f(){}\n"
        "\n"
        "function g(){}\n";

    replace_file(fixture->tmp_js_script, script_with_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);
    const char * expected_function_lines[] = {
        "1",
        "3"
    };
    const gsize expected_function_lines_len = G_N_ELEMENTS(expected_function_lines);

    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FN:",
                                                  expected_function_lines_len,
                                                  has_function_line,
                                                  (gpointer) expected_function_lines,
                                                  sizeof(const char *)));
    g_free(coverage_data_contents);
}

typedef struct _FunctionHitCountData {
    const char   *function;
    unsigned int hit_count_minimum;
} FunctionHitCountData;

static bool
hit_count_is_more_than_for_function(const char *line,
                                    gpointer   user_data)
{
    FunctionHitCountData *data = (FunctionHitCountData *) user_data;
    char                 *detected_function = NULL;
    unsigned int         hit_count;
    size_t max_buf_size;
    int nmatches;

    /* Advance past "FNDA:" */
    line += 5;

    max_buf_size = strcspn(line, "\n");
    detected_function = g_new(char, max_buf_size + 1);
    nmatches = sscanf(line, "%i,%s", &hit_count, detected_function);
    if (nmatches != 2) {
        if (errno != 0)
            g_error("sscanf: %s", strerror(errno));
        else
            g_error("sscanf: only matched %d", nmatches);
    }

    const bool function_name_match = g_strcmp0(data->function, detected_function) == 0;
    const bool hit_count_more_than = hit_count >= data->hit_count_minimum;

    g_free(detected_function);

    return function_name_match &&
           hit_count_more_than;
}

/* For functions with whitespace between their definition and
 * first executable line, its possible that the JS engine might
 * enter their frame a little later in the script than where their
 * definition starts. We need to handle that case */
static void
test_function_hit_counts_for_big_functions_written_to_coverage_data(gpointer      fixture_data,
                                                                    gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){\n"
            "\n"
            "\n"
            "var x = 1;\n"
            "}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* The internal hash table is sorted in alphabetical order
     * so the function names need to be in this order too */
    FunctionHitCountData expected_hit_counts[] = {
        { "(anonymous):6:0", 1 },
        { "f:1:0", 1 }
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FNDA:",
                                                  expected_hit_count_len,
                                                  hit_count_is_more_than_for_function,
                                                  (gpointer) expected_hit_counts,
                                                  sizeof(FunctionHitCountData)));

    g_free(coverage_data_contents);
}

/* For functions which start executing at a function declaration
 * we also need to make sure that we roll back to the real function, */
static void
test_function_hit_counts_for_little_functions_written_to_coverage_data(gpointer      fixture_data,
                                                                       gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){\n"
            "var x = function(){};\n"
            "}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* The internal hash table is sorted in alphabetical order
     * so the function names need to be in this order too */
    FunctionHitCountData expected_hit_counts[] = {
        { "(anonymous):2:0", 0 },
        { "(anonymous):4:0", 1 },
        { "f:1:0", 1 }
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FNDA:",
                                                  expected_hit_count_len,
                                                  hit_count_is_more_than_for_function,
                                                  (gpointer) expected_hit_counts,
                                                  sizeof(FunctionHitCountData)));

    g_free(coverage_data_contents);
}

static void
test_function_hit_counts_written_to_coverage_data(gpointer      fixture_data,
                                                  gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* The internal hash table is sorted in alphabetical order
     * so the function names need to be in this order too */
    FunctionHitCountData expected_hit_counts[] = {
        { "(anonymous):2:0", 1 },
        { "f:1:0", 1 }
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FNDA:",
                                                  expected_hit_count_len,
                                                  hit_count_is_more_than_for_function,
                                                  (gpointer) expected_hit_counts,
                                                  sizeof(FunctionHitCountData)));

    g_free(coverage_data_contents);
}

static void
test_total_function_coverage_written_to_coverage_data(gpointer      fixture_data,
                                                      gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_some_executed_functions =
            "function f(){}\n"
            "let b = function(){}\n"
            "f();\n";

    replace_file(fixture->tmp_js_script, script_with_some_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* More than one assert per test is bad, but we are testing interlinked concepts */
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "FNF:",
                                                  "2"));
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "FNH:",
                                                  "1"));
    g_free(coverage_data_contents);
}

typedef struct _LineCountIsMoreThanData {
    unsigned int expected_lineno;
    unsigned int expected_to_be_more_than;
} LineCountIsMoreThanData;

static bool
line_hit_count_is_more_than(const char *line,
                            gpointer    user_data)
{
    LineCountIsMoreThanData *data = (LineCountIsMoreThanData *) user_data;

    const char *coverage_line = &line[3];
    char *comma_ptr = NULL;

    unsigned int lineno = strtol(coverage_line, &comma_ptr, 10);

    g_assert(comma_ptr[0] == ',');

    char *end_ptr = NULL;

    unsigned int value = strtol(&comma_ptr[1], &end_ptr, 10);

    g_assert(end_ptr[0] == '\0' ||
             end_ptr[0] == '\n');

    return data->expected_lineno == lineno &&
           value > data->expected_to_be_more_than;
}

static void
test_single_line_hit_written_to_coverage_data(gpointer      fixture_data,
                                              gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    LineCountIsMoreThanData data = {
        1,
        0
    };

    g_assert(coverage_data_matches_value_for_key(coverage_data_contents,
                                                 "DA:",
                                                 line_hit_count_is_more_than,
                                                 &data));
    g_free(coverage_data_contents);
}

static void
test_hits_on_multiline_if_cond(gpointer      fixture_data,
                                gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_multine_if_cond =
            "let a = 1;\n"
            "let b = 1;\n"
            "if (a &&\n"
            "    b) {\n"
            "}\n";

    replace_file(fixture->tmp_js_script, script_with_multine_if_cond);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* Hits on all lines, including both lines with a condition (3 and 4) */
    LineCountIsMoreThanData data[] = {
        { 1, 0 },
        { 2, 0 },
        { 3, 0 },
        { 4, 0 }
    };

    g_assert(coverage_data_matches_value_for_key(coverage_data_contents,
                                                 "DA:",
                                                 line_hit_count_is_more_than,
                                                 data));
    g_free(coverage_data_contents);
}

static void
test_full_line_tally_written_to_coverage_data(gpointer      fixture_data,
                                              gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    /* More than one assert per test is bad, but we are testing interlinked concepts */
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "LF:",
                                                  "1"));
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "LH:",
                                                  "1"));
    g_free(coverage_data_contents);
}

static void
test_no_hits_to_coverage_data_for_unexecuted(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        write_statistics_and_get_coverage_data(fixture->coverage,
                                               fixture->lcov_output,
                                               NULL);

    /* No files were executed, so the coverage data is empty. */
    g_assert_cmpstr(coverage_data_contents, ==, "");
    g_free(coverage_data_contents);
}

static void
test_end_of_record_section_written_to_coverage_data(gpointer      fixture_data,
                                                    gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    g_assert(strstr(coverage_data_contents, "end_of_record") != NULL);
    g_free(coverage_data_contents);
}

typedef struct _GjsCoverageMultipleSourcesFixture {
    GjsCoverageFixture base_fixture;
    GFile *second_js_source_file;
} GjsCoverageMultpleSourcesFixutre;

static void
gjs_coverage_multiple_source_files_to_single_output_fixture_set_up(gpointer fixture_data,
                                                                         gconstpointer user_data)
{
    gjs_coverage_fixture_set_up(fixture_data, user_data);

    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;
    fixture->second_js_source_file =
        g_file_get_child(fixture->base_fixture.tmp_output_dir,
                         "gjs_coverage_second_source_file.js");

    /* Because GjsCoverage searches the coverage paths at object-creation time,
     * we need to destroy the previously constructed one and construct it again */
    char *first_js_script_path = g_file_get_path(fixture->base_fixture.tmp_js_script);
    char *second_js_script_path = g_file_get_path(fixture->second_js_source_file);
    char *coverage_paths[] = {
        first_js_script_path,
        second_js_script_path,
        NULL
    };

    g_object_unref(fixture->base_fixture.context);
    g_object_unref(fixture->base_fixture.coverage);
    char *output_path = g_file_get_path(fixture->base_fixture.tmp_output_dir);
    char *search_paths[] = {
        output_path,
        NULL
    };

    fixture->base_fixture.context = gjs_context_new_with_search_path(search_paths);
    fixture->base_fixture.coverage = gjs_coverage_new((const char **) coverage_paths,
                                                      fixture->base_fixture.context,
                                                      fixture->base_fixture.lcov_output_dir);

    g_free(output_path);
    g_free(first_js_script_path);
    g_free(second_js_script_path);

    char *base_name = g_file_get_basename(fixture->base_fixture.tmp_js_script);
    char *base_name_without_extension = g_strndup(base_name,
                                                  strlen(base_name) - 3);
    char *mock_script = g_strconcat("const FirstScript = imports.",
                                    base_name_without_extension,
                                    ";\n",
                                    "let a = FirstScript.f;\n"
                                    "\n",
                                    NULL);

    replace_file(fixture->second_js_source_file, mock_script);

    g_free(mock_script);
    g_free(base_name_without_extension);
    g_free(base_name);
}

static void
gjs_coverage_multiple_source_files_to_single_output_fixture_tear_down(gpointer      fixture_data,
                                                                      gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;
    g_object_unref(fixture->second_js_source_file);

    gjs_coverage_fixture_tear_down(fixture_data, user_data);
}

static void
test_multiple_source_file_records_written_to_coverage_data(gpointer      fixture_data,
                                                           gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->second_js_source_file,
                                          fixture->base_fixture.lcov_output,
                                          NULL);

    const char *first_sf_record = line_starting_with(coverage_data_contents, "SF:");
    g_assert(first_sf_record != NULL);

    const char *second_sf_record = line_starting_with(first_sf_record + 1, "SF:");
    g_assert(second_sf_record != NULL);

    g_free(coverage_data_contents);
}

typedef struct _ExpectedSourceFileCoverageData {
    const char              *source_file_path;
    LineCountIsMoreThanData *more_than;
    unsigned int            n_more_than_matchers;
    const char              expected_lines_hit_character;
    const char              expected_lines_found_character;
} ExpectedSourceFileCoverageData;

static bool
check_coverage_data_for_source_file(ExpectedSourceFileCoverageData *expected,
                                    const gsize                     expected_size,
                                    const char                     *section_start)
{
    gsize i;
    for (i = 0; i < expected_size; ++i) {
        if (strncmp(&section_start[3],
                    expected[i].source_file_path,
                    strlen (expected[i].source_file_path)) == 0) {
            const bool line_hits_match = coverage_data_matches_values_for_key(section_start,
                                                                              "DA:",
                                                                              expected[i].n_more_than_matchers,
                                                                              line_hit_count_is_more_than,
                                                                              expected[i].more_than,
                                                                              sizeof (LineCountIsMoreThanData));
            const char *total_hits_record = line_starting_with(section_start, "LH:");
            const bool total_hits_match = total_hits_record[3] == expected[i].expected_lines_hit_character;
            const char *total_found_record = line_starting_with(section_start, "LF:");
            const bool total_found_match = total_found_record[3] == expected[i].expected_lines_found_character;

            return line_hits_match &&
                   total_hits_match &&
                   total_found_match;
        }
    }

    return false;
}

static void
test_correct_line_coverage_data_written_for_both_source_file_sectons(gpointer      fixture_data,
                                                                     gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->second_js_source_file,
                                          fixture->base_fixture.lcov_output,
                                          NULL);

    LineCountIsMoreThanData first_script_matcher = {
        1,
        0
    };

    LineCountIsMoreThanData second_script_matchers[] = {
        {
            1,
            0
        },
        {
            2,
            0
        }
    };

    char *first_script_output_path =
        get_output_path_for_script_on_disk(fixture->base_fixture.tmp_js_script,
                                           fixture->base_fixture.lcov_output_dir);
    char *second_script_output_path =
        get_output_path_for_script_on_disk(fixture->second_js_source_file,
                                           fixture->base_fixture.lcov_output_dir);

    ExpectedSourceFileCoverageData expected[] = {
        {
            first_script_output_path,
            &first_script_matcher,
            1,
            '1',
            '1'
        },
        {
            second_script_output_path,
            second_script_matchers,
            2,
            '2',
            '2'
        }
    };

    const gsize expected_len = G_N_ELEMENTS(expected);

    const char *first_sf_record = line_starting_with(coverage_data_contents, "SF:");
    g_assert(check_coverage_data_for_source_file(expected, expected_len, first_sf_record));

    const char *second_sf_record = line_starting_with(first_sf_record + 3, "SF:");
    g_assert(check_coverage_data_for_source_file(expected, expected_len, second_sf_record));

    g_free(first_script_output_path);
    g_free(second_script_output_path);
    g_free(coverage_data_contents);
}

static GString *
append_tuples_to_array_in_object_notation(GString    *string,
                                          const char  *tuple_contents_strv)
{
    char *original_ptr = (char *) tuple_contents_strv;
    char *expected_tuple_contents = NULL;
    while ((expected_tuple_contents = strsep((char **) &tuple_contents_strv, ";")) != NULL) {
       if (!strlen(expected_tuple_contents))
           continue;

       if (expected_tuple_contents != original_ptr)
           g_string_append_printf(string, ",");
        g_string_append_printf(string, "{%s}", expected_tuple_contents);
    }

    return string;
}

static GString *
format_expected_cache_object_notation(const char *mtimes,
                                      const char *hash,
                                      GFile      *script,
                                      const char *expected_executable_lines_array,
                                      const char *expected_branches,
                                      const char *expected_functions)
{
    char *script_name = get_script_identifier(script);
    GString *string = g_string_new("");
    g_string_append_printf(string,
                           "{\"%s\":{\"mtime\":%s,\"checksum\":%s,\"lines\":[%s],\"branches\":[",
                           script_name,
                           mtimes,
                           hash,
                           expected_executable_lines_array);
    g_free(script_name);
    append_tuples_to_array_in_object_notation(string, expected_branches);
    g_string_append_printf(string, "],\"functions\":[");
    append_tuples_to_array_in_object_notation(string, expected_functions);
    g_string_append_printf(string, "]}}");
    return string;
}

typedef struct _GjsCoverageCacheObjectNotationTestTableData {
    const char *test_name;
    const char *script;
    const char *uri;
    const char *expected_executable_lines;
    const char *expected_branches;
    const char *expected_functions;
} GjsCoverageCacheObjectNotationTableTestData;

static GBytes *
serialize_ast_to_bytes(GjsCoverage *coverage,
                       const char **coverage_paths)
{
    return gjs_serialize_statistics(coverage);
}

static char *
serialize_ast_to_object_notation(GjsCoverage *coverage,
                                 const char **coverage_paths)
{
    /* Unfortunately, we need to pass in this paramater here since
     * the len parameter is not allow-none.
     *
     * The caller doesn't need to know about the length of the
     * data since it is only used for strcmp and the data is
     * NUL-terminated anyway. */
    gsize len = 0;
    return (char *)g_bytes_unref_to_data(serialize_ast_to_bytes(coverage, coverage_paths),
                                         &len);
}

static char *
eval_file_for_ast_in_object_notation(GjsContext  *context,
                                     GjsCoverage *coverage,
                                     GFile       *script)
{
    bool success = eval_script(context, script);
    g_assert_true(success);

    char *filename = g_file_get_path(script);
    const gchar *coverage_paths[] = {
        filename,
        NULL
    };

    char *retval = serialize_ast_to_object_notation(coverage, coverage_paths);
    g_free(filename);
    return retval;
}

static void
test_coverage_cache_data_in_expected_format(gpointer      fixture_data,
                                            gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    GjsCoverageCacheObjectNotationTableTestData *table_data = (GjsCoverageCacheObjectNotationTableTestData *) user_data;

    replace_file(fixture->tmp_js_script, table_data->script);
    char *cache_in_object_notation = eval_file_for_ast_in_object_notation(fixture->context,
                                                                          fixture->coverage,
                                                                          fixture->tmp_js_script);
    g_assert(cache_in_object_notation != NULL);

    /* Sleep for a little while to make sure that the new file has a
     * different mtime */
    sleep(1);

    GTimeVal mtime;
    bool successfully_got_mtime = gjs_get_file_mtime(fixture->tmp_js_script, &mtime);
    g_assert_true(successfully_got_mtime);

    char *mtime_string = g_strdup_printf("[%li,%li]", mtime.tv_sec, mtime.tv_usec);
    GString *expected_cache_object_notation = format_expected_cache_object_notation(mtime_string,
                                                                                    "null",
                                                                                    fixture->tmp_js_script,
                                                                                    table_data->expected_executable_lines,
                                                                                    table_data->expected_branches,
                                                                                    table_data->expected_functions);

    g_assert_cmpstr(cache_in_object_notation, ==, expected_cache_object_notation->str);

    g_string_free(expected_cache_object_notation, true);
    g_free(cache_in_object_notation);
    g_free(mtime_string);
}

static void
test_coverage_cache_data_in_expected_format_resource(gpointer      fixture_data,
                                                     gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    GjsCoverageCacheObjectNotationTableTestData *table_data = (GjsCoverageCacheObjectNotationTableTestData *) user_data;

    GFile *resource = g_file_new_for_uri(table_data->uri);

    char *hash_string_no_quotes = gjs_get_file_checksum(resource);

    char *hash_string = g_strdup_printf("\"%s\"", hash_string_no_quotes);
    g_free(hash_string_no_quotes);

    GString *expected_cache_object_notation = format_expected_cache_object_notation("null",
                                                                                    hash_string,
                                                                                    resource,
                                                                                    table_data->expected_executable_lines,
                                                                                    table_data->expected_branches,
                                                                                    table_data->expected_functions);

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script(fixture->context, resource,
                                                   fixture->tmp_output_dir);
    char *cache_in_object_notation = eval_file_for_ast_in_object_notation(fixture->context,
                                                                          fixture->coverage,
                                                                          resource);
    g_object_unref(resource);

    g_assert_cmpstr(cache_in_object_notation, ==, expected_cache_object_notation->str);

    g_string_free(expected_cache_object_notation, true);
    g_free(cache_in_object_notation);
    g_free(hash_string);
}

static char *
generate_coverage_compartment_verify_script(GFile      *coverage_script,
                                            const char *user_script)
{
    char *coverage_script_filename = g_file_get_path(coverage_script);
    char *retval =
        g_strdup_printf("const JSUnit = imports.jsUnit;\n"
                        "const covered_script_filename = '%s';\n"
                        "function assertArrayEquals(lhs, rhs) {\n"
                        "    JSUnit.assertEquals(lhs.length, rhs.length);\n"
                        "    for (let i = 0; i < lhs.length; i++)\n"
                        "        JSUnit.assertEquals(lhs[i], rhs[i]);\n"
                        "}\n"
                        "\n"
                        "%s", coverage_script_filename, user_script);
    g_free(coverage_script_filename);
    return retval;
}

typedef struct _GjsCoverageCacheJSObjectTableTestData {
    const char *test_name;
    const char *script;
    const char *verify_js_script;
} GjsCoverageCacheJSObjectTableTestData;

static void
test_coverage_cache_as_js_object_has_expected_properties(gpointer      fixture_data,
                                                         gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    GjsCoverageCacheJSObjectTableTestData *table_data = (GjsCoverageCacheJSObjectTableTestData *) user_data;

    replace_file(fixture->tmp_js_script, table_data->script);
    eval_script(fixture->context, fixture->tmp_js_script);

    char *script_filename = g_file_get_path(fixture->tmp_js_script);
    const gchar *coverage_paths[] = {
        script_filename,
        NULL
    };

    GBytes *cache = serialize_ast_to_bytes(fixture->coverage, coverage_paths);
    JS::RootedString cache_results(JS_GetRuntime((JSContext *) gjs_context_get_native_context(fixture->context)),
                                   gjs_deserialize_cache_to_object(fixture->coverage, cache));
    JS::RootedValue cache_result_value(JS_GetRuntime((JSContext *) gjs_context_get_native_context(fixture->context)),
                                       JS::StringValue(cache_results));
    gjs_inject_value_into_coverage_compartment(fixture->coverage,
                                               cache_result_value,
                                               "coverage_cache");

    char *verify_script_complete = generate_coverage_compartment_verify_script(fixture->tmp_js_script,
                                                                               table_data->verify_js_script);
    gjs_run_script_in_coverage_compartment(fixture->coverage,
                                           verify_script_complete);
    g_free(verify_script_complete);
    g_free(script_filename);
    g_bytes_unref(cache);
}

typedef struct _GjsCoverageCacheEqualResultsTableTestData {
    const char *test_name;
    const char *script;
} GjsCoverageCacheEqualResultsTableTestData;


static GFile *
get_coverage_tmp_cache(void)
{
    GFileIOStream *stream;
    GError *error = NULL;
    GFile *cache_file = g_file_new_tmp("gjs-coverage-cache-XXXXXX", &stream, &error);
    g_assert_no_error(error);
    g_assert_nonnull(cache_file);
    g_object_unref(stream);

    return cache_file;
}

static GFile *
write_cache_to_temporary_file(GBytes *cache)
{
    GFile *temporary_file = get_coverage_tmp_cache();

    if (!gjs_write_cache_file(temporary_file, cache)) {
        g_object_unref(temporary_file);
        return NULL;
    }

    return temporary_file;
}

static GFile *
serialize_ast_to_cache_in_temporary_file(GjsCoverage *coverage,
                                         const char  **coverage_paths)
{
    GBytes   *cache = serialize_ast_to_bytes(coverage, coverage_paths);
    GFile *cache_file = write_cache_to_temporary_file(cache);

    g_bytes_unref(cache);

    return cache_file;
}

static void
test_coverage_cache_equal_results_to_reflect_parse(gpointer      fixture_data,
                                                   gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    GjsCoverageCacheEqualResultsTableTestData *equal_results_data = (GjsCoverageCacheEqualResultsTableTestData *) user_data;

    replace_file(fixture->tmp_js_script, equal_results_data->script);

    char *tmp_js_script_filename = g_file_get_path(fixture->tmp_js_script);
    const gchar *coverage_paths[] = {
        tmp_js_script_filename,
        NULL
    };

    char *coverage_data_contents_no_cache =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);
    GFile *cache_file = serialize_ast_to_cache_in_temporary_file(fixture->coverage,
                                                                 coverage_paths);
    g_assert_nonnull(cache_file);

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);
    g_object_unref(cache_file);

    /* Overwrite tracefile with nothing and start over */
    replace_file(fixture->lcov_output, "");

    char *coverage_data_contents_cached =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          NULL);

    g_assert_cmpstr(coverage_data_contents_cached, ==, coverage_data_contents_no_cache);

    g_free(coverage_data_contents_cached);
    g_free(coverage_data_contents_no_cache);
    g_free(tmp_js_script_filename);
}

static GFile *
eval_file_for_tmp_ast_cache(GjsContext  *context,
                            GjsCoverage *coverage,
                            GFile       *script)
{
    bool success = eval_script(context, script);
    g_assert_true(success);

    char *filename = g_file_get_path(script);
    const gchar *coverage_paths[] = {
        filename,
        NULL
    };

    GFile *retval = serialize_ast_to_cache_in_temporary_file(coverage,
                                                             coverage_paths);
    g_free(filename);
    return retval;
}

/* Effectively, the results should be what we expect even though
 * we overwrote the original script after getting coverage and
 * fetching the cache */
static void
test_coverage_cache_invalidation(gpointer      fixture_data,
                                 gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    GFile *cache_file = eval_file_for_tmp_ast_cache(fixture->context,
                                                    fixture->coverage,
                                                    fixture->tmp_js_script);

    /* Sleep for a little while to make sure that the new file has a
     * different mtime */
    sleep(1);

    /* Overwrite tracefile with nothing */
    replace_file(fixture->lcov_output, "");

    /* Write a new script into the temporary js file, which will be
     * completely different to the original script that was there */
    replace_file(fixture->tmp_js_script,
                 "let i = 0;\n"
                 "let j = 0;\n");

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);
    g_object_unref(cache_file);

    gsize coverage_data_len = 0;
    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output,
                                          &coverage_data_len);

    LineCountIsMoreThanData matchers[] =
    {
        {
            1,
            0
        },
        {
            2,
            0
        }
    };

    char *script_output_path = get_output_path_for_script_on_disk(fixture->tmp_js_script,
                                                                  fixture->lcov_output_dir);

    ExpectedSourceFileCoverageData expected[] = {
        {
            script_output_path,
            matchers,
            2,
            '2',
            '2'
        }
    };

    const gsize expected_len = G_N_ELEMENTS(expected);
    const char *record = line_starting_with(coverage_data_contents, "SF:");
    g_assert(check_coverage_data_for_source_file(expected, expected_len, record));

    g_free(script_output_path);
    g_free(coverage_data_contents);
}

static void
unload_resource(GResource *resource)
{
    g_resources_unregister(resource);
    g_resource_unref(resource);
}

static GResource *
load_resource_from_builddir(const char *name)
{
    char *resource_path = g_build_filename(GJS_TOP_BUILDDIR,
                                           name,
                                           NULL);

    GError    *error = NULL;
    GResource *resource = g_resource_load(resource_path,
                                          &error);

    g_assert_no_error(error);
    g_resources_register(resource);

    g_free(resource_path);

    return resource;
}

/* Load first resource, then unload and load second resource. Both have
 * the same path, but different contents */
static void
test_coverage_cache_invalidation_resource(gpointer      fixture_data,
                                          gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    GFile *mock_resource = g_file_new_for_uri("resource:///org/gnome/gjs/mock/cache/resource.js");

    /* Load the resource archive and register it */
    GResource *first_resource = load_resource_from_builddir("mock-cache-invalidation-before.gresource");

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script(fixture->context,
                                                   mock_resource,
                                                   fixture->lcov_output_dir);

    GFile *cache_file = eval_file_for_tmp_ast_cache(fixture->context,
                                                    fixture->coverage,
                                                    mock_resource);

    /* Load the "after" resource, but have the exact same coverage paths */
    unload_resource(first_resource);
    GResource *second_resource = load_resource_from_builddir("mock-cache-invalidation-after.gresource");

    /* Overwrite tracefile with nothing */
    replace_file(fixture->lcov_output, "");

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             mock_resource,
                                                             fixture->lcov_output_dir);
    g_object_unref(cache_file);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context, fixture->coverage,
                                          mock_resource, fixture->lcov_output,
                                          NULL);

    /* Don't need this anymore */
    g_object_unref(mock_resource);
    unload_resource(second_resource);

    /* Now assert that the coverage file has executable lines in
     * the places that we expect them to be */
    LineCountIsMoreThanData matchers[] = {
        {
            1,
            0
        },
        {
            2,
            0
        }
    };

    GFile *output_script =
        g_file_resolve_relative_path(fixture->lcov_output_dir,
                                     "org/gnome/gjs/mock/cache/resource.js");
    char *script_output_path = g_file_get_path(output_script);
    g_object_unref(output_script);

    ExpectedSourceFileCoverageData expected[] = {
        {
            script_output_path,
            matchers,
            2,
            '2',
            '2'
        }
    };

    const gsize expected_len = G_N_ELEMENTS(expected);
    const char *record = line_starting_with(coverage_data_contents, "SF:");
    g_assert(check_coverage_data_for_source_file(expected, expected_len, record));

    g_free(script_output_path);
    g_free(coverage_data_contents);
}

static void
test_coverage_cache_file_written_when_no_cache_exists(gpointer      fixture_data,
                                                      gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    GFile *cache_file = get_coverage_tmp_cache();

    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);

    /* We need to execute the script now in order for a cache entry
     * to be created, since unexecuted scripts are not counted as
     * part of the coverage report. */
    bool success = eval_script(fixture->context, fixture->tmp_js_script);
    g_assert_true(success);

    gjs_coverage_write_statistics(fixture->coverage);

    g_assert_true(g_file_query_exists(cache_file, NULL));
    g_object_unref(cache_file);
}

static GTimeVal
eval_script_for_cache_mtime(GjsContext  *context,
                            GjsCoverage *coverage,
                            GFile       *cache_file,
                            GFile       *script)
{
    bool success = eval_script(context, script);
    g_assert_true(success);

    gjs_coverage_write_statistics(coverage);

    GTimeVal mtime;
    bool successfully_got_mtime = gjs_get_file_mtime(cache_file, &mtime);
    g_assert_true(successfully_got_mtime);

    return mtime;
}

static void
test_coverage_cache_updated_when_cache_stale(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    GFile *cache_file = get_coverage_tmp_cache();
    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);

    GTimeVal first_cache_mtime = eval_script_for_cache_mtime(fixture->context,
                                                             fixture->coverage,
                                                             cache_file,
                                                             fixture->tmp_js_script);

    /* Sleep for a little while to make sure that the new file has a
     * different mtime */
    sleep(1);

    /* Write a new script into the temporary js file, which will be
     * completely different to the original script that was there */
    replace_file(fixture->tmp_js_script,
                 "let i = 0;\n"
                 "let j = 0;\n");

    /* Re-create coverage object, covering new script */
    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);


    /* Run the script again, which will cause an attempt
     * to look up the AST data. Upon writing the statistics
     * again, the cache should have been missed some of the time
     * so the second mtime will be greater than the first */
    GTimeVal second_cache_mtime = eval_script_for_cache_mtime(fixture->context,
                                                              fixture->coverage,
                                                              cache_file,
                                                              fixture->tmp_js_script);


    const bool seconds_different = (first_cache_mtime.tv_sec != second_cache_mtime.tv_sec);
    const bool microseconds_different = (first_cache_mtime.tv_usec != second_cache_mtime.tv_usec);

    g_assert_true(seconds_different || microseconds_different);

    g_object_unref(cache_file);
}

static void
test_coverage_cache_not_updated_on_full_hits(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    GFile *cache_file = get_coverage_tmp_cache();
    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);

    GTimeVal first_cache_mtime = eval_script_for_cache_mtime(fixture->context,
                                                             fixture->coverage,
                                                             cache_file,
                                                             fixture->tmp_js_script);

    /* Re-create coverage object, covering same script */
    g_clear_object(&fixture->coverage);
    fixture->coverage = create_coverage_for_script_and_cache(fixture->context,
                                                             cache_file,
                                                             fixture->tmp_js_script,
                                                             fixture->lcov_output_dir);


    /* Run the script again, which will cause an attempt
     * to look up the AST data. Upon writing the statistics
     * again, the cache should have been hit of the time
     * so the second mtime will be the same as the first */
    GTimeVal second_cache_mtime = eval_script_for_cache_mtime(fixture->context,
                                                              fixture->coverage,
                                                              cache_file,
                                                              fixture->tmp_js_script);

    g_assert_cmpint(first_cache_mtime.tv_sec, ==, second_cache_mtime.tv_sec);
    g_assert_cmpint(first_cache_mtime.tv_usec, ==, second_cache_mtime.tv_usec);

    g_object_unref(cache_file);
}

typedef struct _FixturedTest {
    gsize            fixture_size;
    GTestFixtureFunc set_up;
    GTestFixtureFunc tear_down;
} FixturedTest;

static void
add_test_for_fixture(const char      *name,
                     FixturedTest    *fixture,
                     GTestFixtureFunc test_func,
                     gconstpointer    user_data)
{
    g_test_add_vtable(name,
                      fixture->fixture_size,
                      user_data,
                      fixture->set_up,
                      test_func,
                      fixture->tear_down);
}

/* All table driven tests must be binary compatible with at
 * least this header */
typedef struct _TestTableDataHeader {
    const char *test_name;
} TestTableDataHeader;

static void
add_table_driven_test_for_fixture(const char                *name,
                                  FixturedTest              *fixture,
                                  GTestFixtureFunc          test_func,
                                  gsize                     table_entry_size,
                                  gsize                     n_table_entries,
                                  const TestTableDataHeader *test_table)
{
    const char  *test_table_ptr = (const char *)test_table;
    gsize test_table_index;

    for (test_table_index = 0;
         test_table_index < n_table_entries;
         ++test_table_index, test_table_ptr += table_entry_size) {
        const TestTableDataHeader *header =
            reinterpret_cast<const TestTableDataHeader *>(test_table_ptr);
        gchar *test_name_for_table_index = g_strdup_printf("%s/%s",
                                                           name,
                                                           header->test_name);
        g_test_add_vtable(test_name_for_table_index,
                          fixture->fixture_size,
                          test_table_ptr,
                          fixture->set_up,
                          test_func,
                          fixture->tear_down);
        g_free(test_name_for_table_index);
    }
}

void gjs_test_add_tests_for_coverage()
{
    FixturedTest coverage_fixture = {
        sizeof(GjsCoverageFixture),
        gjs_coverage_fixture_set_up,
        gjs_coverage_fixture_tear_down
    };

    add_test_for_fixture("/gjs/coverage/file_duplicated_into_output_path",
                         &coverage_fixture,
                         test_covered_file_is_duplicated_into_output_if_path,
                         NULL);
    add_test_for_fixture("/gjs/coverage/file_duplicated_full_resource_path",
                         &coverage_fixture,
                         test_covered_file_is_duplicated_into_output_if_resource,
                         NULL);
    add_test_for_fixture("/gjs/coverage/contents_preserved_accumulate_mode",
                         &coverage_fixture,
                         test_previous_contents_preserved,
                         NULL);
    add_test_for_fixture("/gjs/coverage/new_contents_appended_accumulate_mode",
                         &coverage_fixture,
                         test_new_contents_written,
                         NULL);
    add_test_for_fixture("/gjs/coverage/expected_source_file_name_written_to_coverage_data",
                         &coverage_fixture,
                         test_expected_source_file_name_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/entry_not_written_for_nonexistent_file",
                         &coverage_fixture,
                         test_expected_entry_not_written_for_nonexistent_file,
                         NULL);
    add_test_for_fixture("/gjs/coverage/single_branch_coverage_written_to_coverage_data",
                         &coverage_fixture,
                         test_single_branch_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/multiple_branch_coverage_written_to_coverage_data",
                         &coverage_fixture,
                         test_multiple_branch_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/branches_for_multiple_case_statements_fallthrough",
                         &coverage_fixture,
                         test_branches_for_multiple_case_statements_fallthrough,
                         NULL);
    add_test_for_fixture("/gjs/coverage/not_hit_branch_point_written_to_coverage_data",
                         &coverage_fixture,
                         test_branch_not_hit_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/function_names_written_to_coverage_data",
                         &coverage_fixture,
                         test_function_names_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/function_lines_written_to_coverage_data",
                         &coverage_fixture,
                         test_function_lines_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/function_hit_counts_written_to_coverage_data",
                         &coverage_fixture,
                         test_function_hit_counts_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/big_function_hit_counts_written_to_coverage_data",
                         &coverage_fixture,
                         test_function_hit_counts_for_big_functions_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/little_function_hit_counts_written_to_coverage_data",
                         &coverage_fixture,
                         test_function_hit_counts_for_little_functions_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/total_function_coverage_written_to_coverage_data",
                         &coverage_fixture,
                         test_total_function_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/single_line_hit_written_to_coverage_data",
                         &coverage_fixture,
                         test_single_line_hit_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/hits_on_multiline_if_cond",
                         &coverage_fixture,
                         test_hits_on_multiline_if_cond,
                         NULL);
    add_test_for_fixture("/gjs/coverage/full_line_tally_written_to_coverage_data",
                         &coverage_fixture,
                         test_full_line_tally_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/no_hits_for_unexecuted_file",
                         &coverage_fixture,
                         test_no_hits_to_coverage_data_for_unexecuted,
                         NULL);
    add_test_for_fixture("/gjs/coverage/end_of_record_section_written_to_coverage_data",
                         &coverage_fixture,
                         test_end_of_record_section_written_to_coverage_data,
                         NULL);

    FixturedTest coverage_for_multiple_files_to_single_output_fixture = {
        sizeof(GjsCoverageMultpleSourcesFixutre),
        gjs_coverage_multiple_source_files_to_single_output_fixture_set_up,
        gjs_coverage_multiple_source_files_to_single_output_fixture_tear_down
    };

    add_test_for_fixture("/gjs/coverage/multiple_source_file_records_written_to_coverage_data",
                         &coverage_for_multiple_files_to_single_output_fixture,
                         test_multiple_source_file_records_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/correct_line_coverage_data_written_for_both_sections",
                         &coverage_for_multiple_files_to_single_output_fixture,
                         test_correct_line_coverage_data_written_for_both_source_file_sectons,
                         NULL);

    /* This must be static, because g_test_add_vtable does not copy it */
    static GjsCoverageCacheObjectNotationTableTestData data_in_expected_format_table[] = {
        {
            "simple_executable_lines",
            "let i = 0;\n",
            "resource://org/gnome/gjs/mock/test/gjs-test-coverage/cache_notation/simple_executable_lines.js",
            "1",
            "",
            ""
        },
        {
            "simple_branch",
            "let i = 0;\n"
            "if (i) {\n"
            "    i = 1;\n"
            "} else {\n"
            "    i = 2;\n"
            "}\n",
            "resource://org/gnome/gjs/mock/test/gjs-test-coverage/cache_notation/simple_branch.js",
            "1,2,3,5",
            "\"point\":2,\"exits\":[3,5]",
            ""
        },
        {
            "simple_function",
            "function f() {\n"
            "}\n",
            "resource://org/gnome/gjs/mock/test/gjs-test-coverage/cache_notation/simple_function.js",
            "1,2",
            "",
            "\"key\":\"f:1:0\",\"line\":1"
        }
    };

    add_table_driven_test_for_fixture("/gjs/coverage/cache/data_format",
                                      &coverage_fixture,
                                      test_coverage_cache_data_in_expected_format,
                                      sizeof(GjsCoverageCacheObjectNotationTableTestData),
                                      G_N_ELEMENTS(data_in_expected_format_table),
                                      (const TestTableDataHeader *) data_in_expected_format_table);

    add_table_driven_test_for_fixture("/gjs/coverage/cache/data_format_resource",
                                      &coverage_fixture,
                                      test_coverage_cache_data_in_expected_format_resource,
                                      sizeof(GjsCoverageCacheObjectNotationTableTestData),
                                      G_N_ELEMENTS(data_in_expected_format_table),
                                      (const TestTableDataHeader *) data_in_expected_format_table);

    static GjsCoverageCacheJSObjectTableTestData object_has_expected_properties_table[] = {
        {
            "simple_executable_lines",
            "let i = 0;\n",
            "assertArrayEquals(JSON.parse(coverage_cache)[covered_script_filename].lines, [1]);\n"
        },
        {
            "simple_branch",
            "let i = 0;\n"
            "if (i) {\n"
            "    i = 1;\n"
            "} else {\n"
            "    i = 2;\n"
            "}\n",
            "JSUnit.assertEquals(2, JSON.parse(coverage_cache)[covered_script_filename].branches[0].point);\n"
            "assertArrayEquals([3, 5], JSON.parse(coverage_cache)[covered_script_filename].branches[0].exits);\n"
        },
        {
            "simple_function",
            "function f() {\n"
            "}\n",
            "JSUnit.assertEquals('f:1:0', JSON.parse(coverage_cache)[covered_script_filename].functions[0].key);\n"
        }
    };

    add_table_driven_test_for_fixture("/gjs/coverage/cache/object_props",
                                      &coverage_fixture,
                                      test_coverage_cache_as_js_object_has_expected_properties,
                                      sizeof(GjsCoverageCacheJSObjectTableTestData),
                                      G_N_ELEMENTS(object_has_expected_properties_table),
                                      (const TestTableDataHeader *) object_has_expected_properties_table);

    static GjsCoverageCacheEqualResultsTableTestData equal_results_table[] = {
        {
            "simple_executable_lines",
            "let i = 0;\n"
            "let j = 1;\n"
        },
        {
            "simple_branch",
            "let i = 0;\n"
            "if (i) {\n"
            "    i = 1;\n"
            "} else {\n"
            "    i = 2;\n"
            "}\n"
        },
        {
            "simple_function",
            "function f() {\n"
            "}\n"
        }
    };

    add_table_driven_test_for_fixture("/gjs/coverage/cache/equal/executable_lines",
                                      &coverage_fixture,
                                      test_coverage_cache_equal_results_to_reflect_parse,
                                      sizeof(GjsCoverageCacheEqualResultsTableTestData),
                                      G_N_ELEMENTS(equal_results_table),
                                      (const TestTableDataHeader *) equal_results_table);

    add_test_for_fixture("/gjs/coverage/cache/invalidation",
                         &coverage_fixture,
                         test_coverage_cache_invalidation,
                         NULL);

    add_test_for_fixture("/gjs/coverage/cache/invalidation_resource",
                         &coverage_fixture,
                         test_coverage_cache_invalidation_resource,
                         NULL);

    add_test_for_fixture("/gjs/coverage/cache/file_written",
                         &coverage_fixture,
                         test_coverage_cache_file_written_when_no_cache_exists,
                         NULL);

    add_test_for_fixture("/gjs/coverage/cache/no_update_on_full_hits",
                         &coverage_fixture,
                         test_coverage_cache_not_updated_on_full_hits,
                         NULL);

    add_test_for_fixture("/gjs/coverage/cache/update_on_misses",
                         &coverage_fixture,
                         test_coverage_cache_updated_when_cache_stale,
                         NULL);
}
