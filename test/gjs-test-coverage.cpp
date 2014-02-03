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
#include <gjs/gjs.h>
#include <gjs/coverage.h>

typedef struct _GjsCoverageFixture {
    GjsContext    *context;
    GjsCoverage   *coverage;
    char          *temporary_js_script_directory_name;
    char          *temporary_js_script_filename;
    int           temporary_js_script_open_handle;
} GjsCoverageFixture;

static void
write_to_file(int        handle,
              const char *contents)
{
    if (write(handle,
              (gconstpointer) contents,
              sizeof(char) * strlen(contents)) == -1)
        g_error("Failed to write %s to file", contents);
}

static void
write_to_file_at_beginning(int        handle,
                           const char *content)
{
    if (ftruncate(handle, 0) == -1)
        g_print("Error deleting contents of test temporary file: %s\n", strerror(errno));
    lseek(handle, 0, SEEK_SET);
    write_to_file(handle, content);
}

static int
unlink_if_node_is_a_file(const char *path, const struct stat *sb, int typeflag)
{
    if (typeflag == FTW_F)
        unlink(path);
    return 0;
}

static int
rmdir_if_node_is_a_dir(const char *path, const struct stat *sb, int typeflag)
{
    if (typeflag == FTW_D)
        rmdir(path);
    return 0;
}

static void
recursive_delete_dir_at_path(const char *path)
{
    /* We have to recurse twice - once to delete files, and once
     * to delete directories (because ftw uses preorder traversal) */
    ftw(path, unlink_if_node_is_a_file, 100);
    ftw(path, rmdir_if_node_is_a_dir, 100);
}

static void
gjs_coverage_fixture_set_up(gpointer      fixture_data,
                            gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char         *js_script = "function f () { return 1; }\n";

    fixture->temporary_js_script_directory_name = g_strdup("/tmp/gjs_coverage_tmp.XXXXXX");
    fixture->temporary_js_script_directory_name =
        mkdtemp (fixture->temporary_js_script_directory_name);

    if (!fixture->temporary_js_script_directory_name)
        g_error ("Failed to create temporary directory for test files: %s\n", strerror (errno));

    fixture->temporary_js_script_filename = g_strconcat(fixture->temporary_js_script_directory_name,
                                                        "/",
                                                        "gjs_coverage_script_XXXXXX.js",
                                                        NULL);
    fixture->temporary_js_script_open_handle =
        mkstemps(fixture->temporary_js_script_filename, 3);

    /* Allocate a strv that we can pass over to gjs_coverage_new */
    const char *coverage_paths[] = {
        fixture->temporary_js_script_filename,
        NULL
    };

    const char *search_paths[] = {
        fixture->temporary_js_script_directory_name,
        NULL
    };

    fixture->context = gjs_context_new_with_search_path((char **) search_paths);
    fixture->coverage = gjs_coverage_new(coverage_paths,
                                         fixture->context);

    write_to_file(fixture->temporary_js_script_open_handle, js_script);
}

static void
gjs_coverage_fixture_tear_down(gpointer      fixture_data,
                               gconstpointer user_data)
{
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    unlink(fixture->temporary_js_script_filename);
    g_free(fixture->temporary_js_script_filename);
    close(fixture->temporary_js_script_open_handle);
    recursive_delete_dir_at_path(fixture->temporary_js_script_directory_name);
    g_free(fixture->temporary_js_script_directory_name);

    g_object_unref(fixture->coverage);
    g_object_unref(fixture->context);
}

typedef struct _GjsCoverageToSingleOutputFileFixture {
    GjsCoverageFixture base_fixture;
    char         *output_file_directory;
    char         *output_file_name;
    unsigned int output_file_handle;
} GjsCoverageToSingleOutputFileFixture;

static void
gjs_coverage_to_single_output_file_fixture_set_up (gpointer      fixture_data,
                                                   gconstpointer user_data)
{
    gjs_coverage_fixture_set_up (fixture_data, user_data);

    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;
    fixture->output_file_directory = g_build_filename(fixture->base_fixture.temporary_js_script_directory_name,
                                                      "gjs_coverage_test_coverage.XXXXXX",
                                                      NULL);
    fixture->output_file_directory = mkdtemp(fixture->output_file_directory);
    fixture->output_file_name = g_build_filename(fixture->output_file_directory,
                                                 "coverage.lcov",
                                                 NULL);
    fixture->output_file_handle = open(fixture->output_file_name,
                                       O_CREAT | O_CLOEXEC | O_RDWR,
                                       S_IRWXU);
}

static void
gjs_coverage_to_single_output_file_fixture_tear_down (gpointer      fixture_data,
                                                      gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;
    unlink(fixture->output_file_name);
    close(fixture->output_file_handle);
    g_free(fixture->output_file_name);
    recursive_delete_dir_at_path(fixture->output_file_directory);
    g_free(fixture->output_file_directory);

    gjs_coverage_fixture_tear_down(fixture_data, user_data);
}

static const char *
line_starting_with(const char *data,
                   const char *needle)
{
    const gsize needle_length = strlen (needle);
    const char  *iter = data;

    while (iter) {
        if (strncmp (iter, needle, needle_length) == 0)
          return iter;

        iter = strstr (iter, "\n");

        if (iter)
          iter += 1;
    }

    return NULL;
}

static char *
write_statistics_and_get_coverage_data(GjsCoverage *coverage,
                                       const char  *filename,
                                       const char  *output_directory,
                                       gsize       *coverage_data_length_return)
{
    gjs_coverage_write_statistics(coverage, output_directory);

    gsize coverage_data_length;
    char  *coverage_data_contents;

    char  *output_filename = g_build_filename(output_directory,
                                              "coverage.lcov",
                                              NULL);

    g_file_get_contents(output_filename,
                        &coverage_data_contents,
                        &coverage_data_length,
                        NULL);

    g_free(output_filename);

    if (coverage_data_length_return)
      *coverage_data_length_return = coverage_data_length;

    return coverage_data_contents;
}

static char *
eval_script_and_get_coverage_data(GjsContext  *context,
                                  GjsCoverage *coverage,
                                  const char  *filename,
                                  const char  *output_directory,
                                  gsize       *coverage_data_length_return)
{
    gjs_context_eval_file(context,
                          filename,
                          NULL,
                          NULL);

    return write_statistics_and_get_coverage_data(coverage,
                                                  filename,
                                                  output_directory,
                                                  coverage_data_length_return);
}

static gboolean
coverage_data_contains_value_for_key(const char *data,
                                     const char *key,
                                     const char *value)
{
    const char *sf_line = line_starting_with(data, key);

    if (!sf_line)
        return FALSE;

    return strncmp(&sf_line[strlen (key)],
                   value,
                   strlen (value)) == 0;
}

typedef gboolean (*CoverageDataMatchFunc) (const char *value,
                                           gpointer    user_data);

static gboolean
coverage_data_matches_value_for_key_internal(const char            *line,
                                             const char            *key,
                                             CoverageDataMatchFunc  match,
                                             gpointer               user_data)
{
    return (*match) (line, user_data);
}

static gboolean
coverage_data_matches_value_for_key(const char            *data,
                                    const char            *key,
                                    CoverageDataMatchFunc  match,
                                    gpointer               user_data)
{
    const char *line = line_starting_with(data, key);

    if (!line)
        return FALSE;

    return coverage_data_matches_value_for_key_internal(line, key, match, user_data);
}

static gboolean
coverage_data_matches_any_value_for_key(const char            *data,
                                        const char            *key,
                                        CoverageDataMatchFunc  match,
                                        gpointer               user_data)
{
    data = line_starting_with(data, key);

    while (data) {
        if (coverage_data_matches_value_for_key_internal(data, key, match, user_data))
            return TRUE;

        data = line_starting_with(data + 1, key);
    }

    return FALSE;
}

static gboolean
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
        if (!coverage_data_matches_value_for_key_internal (line, key, match, (gpointer) data_iterator))
            return FALSE;

        line = line_starting_with (line + 1, key);
        --n;
        data_iterator += data_size;
    }

    /* If n is zero then we've found all available matches */
    if (n == 0)
        return TRUE;

    return FALSE;
}

static void
test_covered_file_is_duplicated_into_output_if_resource(gpointer      fixture_data,
                                                        gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *mock_resource_filename = "resource:///org/gnome/gjs/mock/test/gjs-test-coverage/loadedJSFromResource.js";
    const char *coverage_scripts[] = {
        mock_resource_filename,
        NULL
    };

    g_object_unref(fixture->base_fixture.context);
    g_object_unref(fixture->base_fixture.coverage);
    const char *search_paths[] = {
        fixture->base_fixture.temporary_js_script_directory_name,
        NULL
    };

    fixture->base_fixture.context = gjs_context_new_with_search_path((char **) search_paths);
    fixture->base_fixture.coverage =
        gjs_coverage_new(coverage_scripts,
                         fixture->base_fixture.context);

    gjs_context_eval_file(fixture->base_fixture.context,
                          mock_resource_filename,
                          NULL,
                          NULL);

    gjs_coverage_write_statistics(fixture->base_fixture.coverage,
                                  fixture->output_file_directory);

    char *expected_temporary_js_script_file_path =
        g_build_filename(fixture->output_file_directory,
                         "org/gnome/gjs/mock/test/gjs-test-coverage/loadedJSFromResource.js",
                         NULL);

    GFile *file_for_expected_path = g_file_new_for_path(expected_temporary_js_script_file_path);

    g_assert(g_file_query_exists(file_for_expected_path, NULL) == TRUE);

    g_object_unref(file_for_expected_path);
    g_free(expected_temporary_js_script_file_path);
}


static void
test_covered_file_is_duplicated_into_output_if_path(gpointer      fixture_data,
                                                    gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    gjs_context_eval_file(fixture->base_fixture.context,
                          fixture->base_fixture.temporary_js_script_filename,
                          NULL,
                          NULL);

    gjs_coverage_write_statistics(fixture->base_fixture.coverage,
                                  fixture->output_file_directory);

    char *temporary_js_script_basename =
        g_filename_display_basename(fixture->base_fixture.temporary_js_script_filename);
    char *expected_temporary_js_script_file_path =
        g_build_filename(fixture->output_file_directory,
                         temporary_js_script_basename,
                         NULL);

    GFile *file_for_expected_path = g_file_new_for_path(expected_temporary_js_script_file_path);

    g_assert(g_file_query_exists(file_for_expected_path, NULL) == TRUE);

    g_object_unref(file_for_expected_path);
    g_free(expected_temporary_js_script_file_path);
    g_free(temporary_js_script_basename);
}

static void
test_previous_contents_preserved(gpointer      fixture_data,
                                 gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    write_to_file(fixture->output_file_handle, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
                                          NULL);

    g_assert(strstr(coverage_data_contents, existing_contents) != NULL);
    g_free(coverage_data_contents);
}


static void
test_new_contents_written(gpointer      fixture_data,
                          gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    write_to_file(fixture->output_file_handle, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
                                          NULL);

    /* We have new content in the coverage data */
    g_assert(strlen(existing_contents) != strlen(coverage_data_contents));
    g_free(coverage_data_contents);
}

static void
test_expected_source_file_name_written_to_coverage_data(gpointer      fixture_data,
                                                        gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
                                          NULL);

    char *temporary_js_script_basename =
        g_filename_display_basename(fixture->base_fixture.temporary_js_script_filename);
    char *expected_source_filename =
        g_build_filename(fixture->output_file_directory,
                         temporary_js_script_basename,
                         NULL);

    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "SF:",
                                                  expected_source_filename));

    g_free(expected_source_filename);
    g_free(temporary_js_script_basename);
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *coverage_paths[] = {
        "doesnotexist",
        NULL
    };

    g_object_unref(fixture->base_fixture.coverage);
    fixture->base_fixture.coverage = gjs_coverage_new(coverage_paths,
                                                      fixture->base_fixture.context);

    /* Temporarily disable fatal mask and silence warnings */
    GLogLevelFlags old_flags = g_log_set_always_fatal((GLogLevelFlags) G_LOG_LEVEL_ERROR);
    GLogFunc old_log_func = g_log_set_default_handler(silence_log_func, NULL);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          "doesnotexist",
                                          fixture->output_file_directory,
                                          NULL);

    g_log_set_always_fatal(old_flags);
    g_log_set_default_handler(old_log_func, NULL);

    char *temporary_js_script_basename =
        g_filename_display_basename("doesnotexist");

    g_assert(!(coverage_data_contains_value_for_key(coverage_data_contents,
                                                    "SF:",
                                                    temporary_js_script_basename)));

    g_free(temporary_js_script_basename);
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

static gboolean
branch_at_line_should_be_taken(const char *line,
                               gpointer user_data)
{
    BranchLineData *branch_data = (BranchLineData *) user_data;
    int line_no, branch_id, block_no, hit_count_num;
    char *hit_count = NULL;

    /* Advance past "BRDA:" */
    line += 5;

    if (sscanf(line, "%i,%i,%i,%as", &line_no, &block_no, &branch_id, &hit_count) != 4)
        g_error("sscanf: %s", strerror(errno));

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

    /* The glibc extension to sscanf dynamically allocates hit_count, so
     * we need to free it here */
    free(hit_count);

    const gboolean hit_correct_branch_line =
        branch_data->expected_branch_line == line_no;
    const gboolean hit_correct_branch_id =
        branch_data->expected_id == branch_id;
    gboolean branch_correctly_taken_or_not_taken;

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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_basic_branch =
            "let x = 0;\n"
            "if (x > 0)\n"
            "    x++;\n"
            "else\n"
            "    x++;\n";

    /* We have to seek backwards and overwrite */
    lseek(fixture->base_fixture.temporary_js_script_open_handle, 0, SEEK_SET);

    if (write(fixture->base_fixture.temporary_js_script_open_handle,
              (const char *) script_with_basic_branch,
              sizeof(char) * strlen(script_with_basic_branch)) == 0)
        g_error("Failed to basic branch script");

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

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

    /* We have to seek backwards and overwrite */
    lseek(fixture->base_fixture.temporary_js_script_open_handle, 0, SEEK_SET);

    if (write(fixture->base_fixture.temporary_js_script_open_handle,
              (const char *) script_with_case_statements_branch,
              sizeof(char) * strlen(script_with_case_statements_branch)) == 0)
        g_error("Failed to write script");

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

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

    /* We have to seek backwards and overwrite */
    lseek(fixture->base_fixture.temporary_js_script_open_handle, 0, SEEK_SET);

    if (write(fixture->base_fixture.temporary_js_script_open_handle,
              (const char *) script_with_case_statements_branch,
              sizeof(char) * strlen(script_with_case_statements_branch)) == 0)
        g_error("Failed to write script");

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_never_executed_branch =
            "let x = 0;\n"
            "if (x > 0) {\n"
            "    if (x > 0)\n"
            "        x++;\n"
            "} else {\n"
            "    x++;\n"
            "}\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_never_executed_branch);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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

static gboolean
has_function_name(const char *line,
                  gpointer    user_data)
{
    /* User data is const char ** */
    const char *expected_function_name = *((const char **) user_data);

    /* Advance past "FN:" */
    line += 3;

    return strncmp(line,
                   expected_function_name,
                   strlen(expected_function_name)) == 0;
}

static void
test_function_names_written_to_coverage_data(gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_named_and_unnamed_functions =
            "function f(){}\n"
            "let b = function(){}\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_named_and_unnamed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
                                          NULL);

    /* The internal hash table is sorted in alphabetical order
     * so the function names need to be in this order too */
    const char * expected_function_names[] = {
        "(anonymous):2:0",
        "f:1:0"
    };
    const gsize expected_function_names_len = G_N_ELEMENTS(expected_function_names);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    g_assert(coverage_data_matches_values_for_key(coverage_data_contents,
                                                  "FN:",
                                                  expected_function_names_len,
                                                  has_function_name,
                                                  (gpointer) expected_function_names,
                                                  sizeof(const char *)));
    g_free(coverage_data_contents);
}

typedef struct _FunctionHitCountData {
    const char   *function;
    unsigned int hit_count_minimum;
} FunctionHitCountData;

static gboolean
hit_count_is_more_than_for_function(const char *line,
                                    gpointer   user_data)
{
    FunctionHitCountData *data = (FunctionHitCountData *) user_data;
    char                 *detected_function = NULL;
    unsigned int         hit_count;


    /* Advance past "FNDA:" */
    line += 5;

    if (sscanf(line, "%i,%as", &hit_count, &detected_function) != 2)
        g_error("sscanf: %s", strerror(errno));

    const gboolean function_name_match = g_strcmp0(data->function, detected_function) == 0;
    const gboolean hit_count_more_than = hit_count >= data->hit_count_minimum;

    /* See above, we must free detected_functon */
    free(detected_function);

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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){\n"
            "\n"
            "\n"
            "var x = 1;\n"
            "}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){\n"
            "var x = function(){};\n"
            "}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_executed_functions =
            "function f(){}\n"
            "let b = function(){}\n"
            "f();\n"
            "b();\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    const char *script_with_some_executed_functions =
            "function f(){}\n"
            "let b = function(){}\n"
            "f();\n";

    write_to_file_at_beginning(fixture->base_fixture.temporary_js_script_open_handle,
                               script_with_some_executed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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

static gboolean
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
test_full_line_tally_written_to_coverage_data(gpointer      fixture_data,
                                              gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
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
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    char *coverage_data_contents =
        write_statistics_and_get_coverage_data(fixture->base_fixture.coverage,
                                               fixture->base_fixture.temporary_js_script_filename,
                                               fixture->output_file_directory,
                                               NULL);

    /* More than one assert per test is bad, but we are testing interlinked concepts */
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "LF:",
                                                  "1"));
    g_assert(coverage_data_contains_value_for_key(coverage_data_contents,
                                                  "LH:",
                                                  "0"));
    g_free(coverage_data_contents);
}

static void
test_end_of_record_section_written_to_coverage_data(gpointer      fixture_data,
                                                    gconstpointer user_data)
{
    GjsCoverageToSingleOutputFileFixture *fixture = (GjsCoverageToSingleOutputFileFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->base_fixture.temporary_js_script_filename,
                                          fixture->output_file_directory,
                                          NULL);

    g_assert(strstr(coverage_data_contents, "end_of_record") != NULL);
    g_free(coverage_data_contents);
}

typedef struct _GjsCoverageMultipleSourcesFixture {
    GjsCoverageToSingleOutputFileFixture base_fixture;
    char         *second_js_source_file_name;
    unsigned int second_gjs_source_file_handle;
} GjsCoverageMultpleSourcesFixutre;

static void
gjs_coverage_multiple_source_files_to_single_output_fixture_set_up(gpointer fixture_data,
                                                                         gconstpointer user_data)
{
    gjs_coverage_to_single_output_file_fixture_set_up (fixture_data, user_data);

    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;
    fixture->second_js_source_file_name = g_strconcat(fixture->base_fixture.base_fixture.temporary_js_script_directory_name,
                                                      "/",
                                                      "gjs_coverage_second_source_file_XXXXXX.js",
                                                      NULL);
    fixture->second_gjs_source_file_handle = mkstemps(fixture->second_js_source_file_name, 3);

    /* Because GjsCoverage searches the coverage paths at object-creation time,
     * we need to destroy the previously constructed one and construct it again */
    const char *coverage_paths[] = {
        fixture->base_fixture.base_fixture.temporary_js_script_filename,
        fixture->second_js_source_file_name,
        NULL
    };

    g_object_unref(fixture->base_fixture.base_fixture.context);
    g_object_unref(fixture->base_fixture.base_fixture.coverage);
    const char *search_paths[] = {
        fixture->base_fixture.base_fixture.temporary_js_script_directory_name,
        NULL
    };

    fixture->base_fixture.base_fixture.context = gjs_context_new_with_search_path((char **) search_paths);
    fixture->base_fixture.base_fixture.coverage = gjs_coverage_new(coverage_paths,
                                                                   fixture->base_fixture.base_fixture.context);

    char *base_name = g_path_get_basename(fixture->base_fixture.base_fixture.temporary_js_script_filename);
    char *base_name_without_extension = g_strndup(base_name,
                                                  strlen(base_name) - 3);
    char *mock_script = g_strconcat("const FirstScript = imports.",
                                    base_name_without_extension,
                                    ";\n",
                                    "let a = FirstScript.f;\n"
                                    "\n",
                                    NULL);

    write_to_file_at_beginning(fixture->second_gjs_source_file_handle, mock_script);

    g_free(mock_script);
    g_free(base_name_without_extension);
    g_free(base_name);
}

static void
gjs_coverage_multiple_source_files_to_single_output_fixture_tear_down(gpointer      fixture_data,
                                                                      gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;
    unlink(fixture->second_js_source_file_name);
    g_free(fixture->second_js_source_file_name);
    close(fixture->second_gjs_source_file_handle);

    gjs_coverage_to_single_output_file_fixture_tear_down(fixture_data, user_data);
}

static void
test_multiple_source_file_records_written_to_coverage_data (gpointer      fixture_data,
                                                            gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.base_fixture.context,
                                          fixture->base_fixture.base_fixture.coverage,
                                          fixture->second_js_source_file_name,
                                          fixture->base_fixture.output_file_directory,
                                          NULL);

    const char *first_sf_record = line_starting_with(coverage_data_contents, "SF:");
    const char *second_sf_record = line_starting_with(first_sf_record + 1, "SF:");

    g_assert(first_sf_record != NULL);
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

static gboolean
check_coverage_data_for_source_file(ExpectedSourceFileCoverageData *expected,
                                    const gsize                     expected_size,
                                    const char                     *section_start)
{
    gsize i;
    for (i = 0; i < expected_size; ++i) {
        if (strncmp (&section_start[3],
                     expected[i].source_file_path,
                     strlen (expected[i].source_file_path)) == 0) {
            const gboolean line_hits_match = coverage_data_matches_values_for_key (section_start,
                                                                                   "DA:",
                                                                                   expected[i].n_more_than_matchers,
                                                                                   line_hit_count_is_more_than,
                                                                                   expected[i].more_than,
                                                                                   sizeof (LineCountIsMoreThanData));
            const char *total_hits_record = line_starting_with (section_start, "LH:");
            const gboolean total_hits_match = total_hits_record[3] == expected[i].expected_lines_hit_character;
            const char *total_found_record = line_starting_with (section_start, "LF:");
            const gboolean total_found_match = total_found_record[3] == expected[i].expected_lines_found_character;

            return line_hits_match &&
                   total_hits_match &&
                   total_found_match;
        }
    }

    return FALSE;
}

static void
test_correct_line_coverage_data_written_for_both_source_file_sectons(gpointer      fixture_data,
                                                                     gconstpointer user_data)
{
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.base_fixture.context,
                                          fixture->base_fixture.base_fixture.coverage,
                                          fixture->second_js_source_file_name,
                                          fixture->base_fixture.output_file_directory,
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

    char *first_script_basename =
        g_filename_display_basename(fixture->base_fixture.base_fixture.temporary_js_script_filename);
    char *second_script_basename =
        g_filename_display_basename(fixture->second_js_source_file_name);

    char *first_script_output_path =
        g_build_filename(fixture->base_fixture.output_file_directory,
                         first_script_basename,
                         NULL);
    char *second_script_output_path =
        g_build_filename(fixture->base_fixture.output_file_directory,
                         second_script_basename,
                         NULL);

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

    g_free(first_script_basename);
    g_free(first_script_output_path);
    g_free(second_script_basename);
    g_free(second_script_output_path);
    g_free(coverage_data_contents);
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

void gjs_test_add_tests_for_coverage()
{
    FixturedTest coverage_to_single_output_fixture = {
        sizeof(GjsCoverageToSingleOutputFileFixture),
        gjs_coverage_to_single_output_file_fixture_set_up,
        gjs_coverage_to_single_output_file_fixture_tear_down
    };

    add_test_for_fixture("/gjs/coverage/file_duplicated_into_output_path",
                         &coverage_to_single_output_fixture,
                         test_covered_file_is_duplicated_into_output_if_path,
                         NULL);
    add_test_for_fixture("/gjs/coverage/file_duplicated_full_resource_path",
                         &coverage_to_single_output_fixture,
                         test_covered_file_is_duplicated_into_output_if_resource,
                         NULL);
    add_test_for_fixture("/gjs/coverage/contents_preserved_accumulate_mode",
                         &coverage_to_single_output_fixture,
                         test_previous_contents_preserved,
                         NULL);
    add_test_for_fixture("/gjs/coverage/new_contents_appended_accumulate_mode",
                         &coverage_to_single_output_fixture,
                         test_new_contents_written,
                         NULL);
    add_test_for_fixture("/gjs/coverage/expected_source_file_name_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_expected_source_file_name_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/entry_not_written_for_nonexistent_file",
                         &coverage_to_single_output_fixture,
                         test_expected_entry_not_written_for_nonexistent_file,
                         NULL);
    add_test_for_fixture("/gjs/coverage/single_branch_coverage_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_single_branch_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/multiple_branch_coverage_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_multiple_branch_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/branches_for_multiple_case_statements_fallthrough",
                         &coverage_to_single_output_fixture,
                         test_branches_for_multiple_case_statements_fallthrough,
                         NULL);
    add_test_for_fixture("/gjs/coverage/not_hit_branch_point_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_branch_not_hit_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/function_names_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_function_names_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/function_hit_counts_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_function_hit_counts_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/big_function_hit_counts_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_function_hit_counts_for_big_functions_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/little_function_hit_counts_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_function_hit_counts_for_little_functions_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/total_function_coverage_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_total_function_coverage_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/single_line_hit_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_single_line_hit_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/full_line_tally_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
                         test_full_line_tally_written_to_coverage_data,
                         NULL);
    add_test_for_fixture("/gjs/coverage/no_hits_for_unexecuted_file",
                         &coverage_to_single_output_fixture,
                         test_no_hits_to_coverage_data_for_unexecuted,
                         NULL);
    add_test_for_fixture("/gjs/coverage/end_of_record_section_written_to_coverage_data",
                         &coverage_to_single_output_fixture,
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
}
