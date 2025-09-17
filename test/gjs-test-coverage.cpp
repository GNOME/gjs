/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Endless Mobile, Inc.
// SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>

#include <config.h>

#include <errno.h>   // for errno
#include <stdio.h>   // for sscanf, size_t
#include <stdlib.h>  // for strtol, atoi, mkdtemp
#include <string.h>  // for strlen, strstr, strcmp, strncmp, strcspn

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/auto.h"
#include "gjs/context.h"
#include "gjs/coverage.h"
#include "gjs/gerror-result.h"

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
    Gjs::AutoError error;
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

static void gjs_coverage_fixture_set_up(void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char* js_script = "var f = function () { return 1; }\n";

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

    gjs_coverage_enable();
    fixture->context = gjs_context_new_with_search_path((char **) search_paths);
    fixture->coverage = gjs_coverage_new(coverage_paths, fixture->context,
                                         fixture->lcov_output_dir);

    replace_file(fixture->tmp_js_script, js_script);
    g_free(tmp_output_dir_name);
    g_free(tmp_js_script_filename);
}

static void gjs_coverage_fixture_tear_down(void* fixture_data, const void*) {
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
                                       GFile       *lcov_output)
{
    gjs_coverage_write_statistics(coverage);

    char  *coverage_data_contents;

    g_file_load_contents(lcov_output, NULL /* cancellable */,
                         &coverage_data_contents, nullptr, /* length out */
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
                                  GFile       *lcov_output)
{
    eval_script(context, script);
    return write_statistics_and_get_coverage_data(coverage, lcov_output);
}

static void
assert_coverage_data_contains_value_for_key(const char *data,
                                            const char *key,
                                            const char *value)
{
    const char *sf_line = line_starting_with(data, key);

    g_assert_nonnull(sf_line);

    Gjs::AutoChar actual{g_strndup(&sf_line[strlen(key)], strlen(value))};
    g_assert_cmpstr(value, ==, actual);
}

using CoverageDataMatchFunc = void (*)(const char *value,
                                       const void *user_data);

static void
assert_coverage_data_matches_value_for_key(const char            *data,
                                           const char            *key,
                                           CoverageDataMatchFunc  match,
                                           const void            *user_data)
{
    const char *line = line_starting_with(data, key);

    g_assert_nonnull(line);
    (*match)(line, user_data);
}

static void
assert_coverage_data_matches_values_for_key(const char            *data,
                                            const char            *key,
                                            size_t                 n,
                                            CoverageDataMatchFunc  match,
                                            const void            *user_data,
                                            size_t                 data_size)
{
    const char *line = line_starting_with (data, key);
    /* Keep matching. If we fail to match one of them then
     * bail out */
    char *data_iterator = (char *) user_data;

    while (line && n > 0) {
        (*match)(line, data_iterator);

        line = line_starting_with(line + 1, key);
        --n;
        data_iterator += data_size;
    }

    /* If n is zero then we've found all available matches */
    g_assert_cmpuint(n, ==, 0);
}

static void test_covered_file_is_duplicated_into_output_if_resource(
    void* fixture_data, const void*) {
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
    fixture->coverage = gjs_coverage_new(coverage_scripts, fixture->context,
                                         fixture->lcov_output_dir);

    bool ok = gjs_context_eval_file(fixture->context, mock_resource_filename,
                                    nullptr, nullptr);
    g_assert_true(ok);

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

static void test_covered_file_is_duplicated_into_output_if_path(
    void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    eval_script(fixture->context, fixture->tmp_js_script);

    gjs_coverage_write_statistics(fixture->coverage);

    GFile *expected_temporary_js_script =
        get_output_file_for_script_on_disk(fixture->tmp_js_script,
                                           fixture->lcov_output_dir);

    g_assert_true(g_file_query_exists(expected_temporary_js_script, NULL));

    g_object_unref(expected_temporary_js_script);
}

static void test_previous_contents_preserved(void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    g_assert_nonnull(strstr(coverage_data_contents, existing_contents));
    g_free(coverage_data_contents);
}

static void test_new_contents_written(void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;
    const char *existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    /* We have new content in the coverage data */
    g_assert_cmpstr(existing_contents, !=, coverage_data_contents);
    g_free(coverage_data_contents);
}

static void test_expected_source_file_name_written_to_coverage_data(
    void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    char *expected_source_filename =
        get_output_path_for_script_on_disk(fixture->tmp_js_script, fixture->lcov_output_dir);

    assert_coverage_data_contains_value_for_key(coverage_data_contents, "SF:",
                                                expected_source_filename);

    g_free(expected_source_filename);
    g_free(coverage_data_contents);
}

static void test_expected_entry_not_written_for_nonexistent_file(
    void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *coverage_paths[] = {
        "doesnotexist",
        NULL
    };

    g_object_unref(fixture->coverage);
    fixture->coverage = gjs_coverage_new(coverage_paths, fixture->context,
                                         fixture->lcov_output_dir);

    GFile *doesnotexist = g_file_new_for_path("doesnotexist");
    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context, fixture->coverage,
                                          doesnotexist, fixture->lcov_output);

    const char *sf_line = line_starting_with(coverage_data_contents, "SF:");
    g_assert_null(sf_line);

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

static void
branch_at_line_should_be_taken(const char *line,
                               const void *user_data)
{
    auto branch_data = static_cast<const BranchLineData *>(user_data);
    int line_no, branch_id, block_no, hit_count_num, nmatches;
    char hit_count[20];  /* can hold maxint64 (19 digits) + nul terminator */

    /* Advance past "BRDA:" */
    line += 5;

    nmatches = sscanf(line, "%i,%i,%i,%19s", &line_no, &block_no, &branch_id, hit_count);
    g_assert_cmpint(nmatches, ==, 4);

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

    g_assert_cmpint(line_no, ==, branch_data->expected_branch_line);
    g_assert_cmpint(branch_id, ==, branch_data->expected_id);

    switch (branch_data->taken) {
    case NOT_EXECUTED:
        g_assert_cmpint(hit_count_num, ==, -1);
        break;
    case NOT_TAKEN:
        g_assert_cmpint(hit_count_num, ==, 0);
        break;
    case TAKEN:
        g_assert_cmpint(hit_count_num, >, 0);
        break;
    default:
        g_assert_true(false && "Invalid branch state");
    };
}

static void test_single_branch_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const BranchLineData expected_branches[] = {{2, 0, TAKEN},
                                                {2, 1, NOT_TAKEN}};
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "BRDA:",
                                                expected_branches_len,
                                                branch_at_line_should_be_taken,
                                                expected_branches,
                                                sizeof(BranchLineData));

    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "BRF:", "2");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "BRH:", "1");
    g_free(coverage_data_contents);
}

static void test_multiple_branch_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const BranchLineData expected_branches[] = {
        {2, 0, TAKEN}, {2, 1, TAKEN}, {3, 0, TAKEN},
        {3, 1, TAKEN}, {3, 2, TAKEN}, {3, 3, NOT_TAKEN},
    };
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "BRDA:",
                                                expected_branches_len,
                                                branch_at_line_should_be_taken,
                                                expected_branches,
                                                sizeof(BranchLineData));
    g_free(coverage_data_contents);
}

static void test_branches_for_multiple_case_statements_fallthrough(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const BranchLineData expected_branches[] = {
        {2, 0, TAKEN}, {2, 1, TAKEN},     {3, 0, TAKEN},
        {3, 1, TAKEN}, {3, 2, NOT_TAKEN}, {3, 3, NOT_TAKEN},
    };
    const gsize expected_branches_len = G_N_ELEMENTS(expected_branches);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "BRDA:",
                                                expected_branches_len,
                                                branch_at_line_should_be_taken,
                                                expected_branches,
                                                sizeof(BranchLineData));
    g_free(coverage_data_contents);
}

static void
any_line_matches_not_executed_branch(const char *data)
{
    const char *line = line_starting_with(data, "BRDA:");

    while (line) {
        int line_no, branch_id, block_no;
        char hit_count;

        /* Advance past "BRDA:" */
        line += 5;

        int nmatches = sscanf(line, "%i,%i,%i,%c", &line_no, &block_no,
                              &branch_id, &hit_count);
        g_assert_cmpint(nmatches, ==, 4);

        if (line_no == 3 && branch_id == 0 && hit_count == '-')
            return;

        line = line_starting_with(line + 1, "BRDA:");
    }

    g_assert_true(false && "BRDA line with line 3 not found");
}

static void test_branch_not_hit_written_to_coverage_data(void* fixture_data,
                                                         const void*) {
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
                                          fixture->lcov_output);

    any_line_matches_not_executed_branch(coverage_data_contents);
    g_free(coverage_data_contents);
}

static void
has_function_name(const char *line,
                  const void *user_data)
{
    const char *expected_function_name = *(static_cast<const char * const *>(user_data));

    /* Advance past "FN:" */
    line += 3;

    /* Advance past the first comma */
    while (*(line - 1) != ',')
        ++line;

    Gjs::AutoChar actual{g_strndup(line, strlen(expected_function_name))};
    g_assert_cmpstr(actual, ==, expected_function_name);
}

static void test_function_names_written_to_coverage_data(void* fixture_data,
                                                         const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    const char *script_with_named_and_unnamed_functions =
            "function f(){}\n"
            "let b = function(){}\n";

    replace_file(fixture->tmp_js_script, script_with_named_and_unnamed_functions);

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    const char* expected_function_names[] = {
        "top-level",
        "f",
        "b",
    };
    const gsize expected_function_names_len = G_N_ELEMENTS(expected_function_names);

    /* Just expect that we've got an FN matching out expected function names */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "FN:",
                                                expected_function_names_len,
                                                has_function_name,
                                                expected_function_names,
                                                sizeof(const char *));
    g_free(coverage_data_contents);
}

static void
has_function_line(const char *line,
                  const void *user_data)
{
    const char *expected_function_line = *(static_cast<const char * const *>(user_data));

    /* Advance past "FN:" */
    line += 3;

    Gjs::AutoChar actual{g_strndup(line, strlen(expected_function_line))};
    g_assert_cmpstr(actual, ==, expected_function_line);
}

static void test_function_lines_written_to_coverage_data(void* fixture_data,
                                                         const void*) {
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
                                          fixture->lcov_output);
    const char* const expected_function_lines[] = {
        "1",
        "1",
        "3",
    };
    const gsize expected_function_lines_len = G_N_ELEMENTS(expected_function_lines);

    assert_coverage_data_matches_values_for_key(coverage_data_contents, "FN:",
                                                expected_function_lines_len,
                                                has_function_line,
                                                expected_function_lines,
                                                sizeof(const char *));
    g_free(coverage_data_contents);
}

typedef struct _FunctionHitCountData {
    const char   *function;
    unsigned int hit_count_minimum;
} FunctionHitCountData;

static void
hit_count_is_more_than_for_function(const char *line,
                                    const void *user_data)
{
    auto data = static_cast<const FunctionHitCountData *>(user_data);
    char                 *detected_function = NULL;
    unsigned int         hit_count;
    size_t max_buf_size;
    int nmatches;

    /* Advance past "FNDA:" */
    line += 5;

    max_buf_size = strcspn(line, "\n");
    detected_function = g_new(char, max_buf_size + 1);
    Gjs::AutoChar format_string{g_strdup_printf("%%5u,%%%zus", max_buf_size)};

    // clang-format off
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wformat-nonliteral\"")
#endif
    nmatches = sscanf(line, format_string, &hit_count, detected_function);
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic pop")
#endif
// clang-format on

    g_assert_cmpint(nmatches, ==, 2);

    g_assert_cmpstr(data->function, ==, detected_function);
    g_assert_cmpuint(hit_count, >=, data->hit_count_minimum);

    g_free(detected_function);
}

/* For functions with whitespace between their definition and
 * first executable line, its possible that the JS engine might
 * enter their frame a little later in the script than where their
 * definition starts. We need to handle that case */
static void test_function_hit_counts_for_big_functions_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const FunctionHitCountData expected_hit_counts[] = {
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "FNDA:",
                                                expected_hit_count_len,
                                                hit_count_is_more_than_for_function,
                                                expected_hit_counts,
                                                sizeof(FunctionHitCountData));

    g_free(coverage_data_contents);
}

/* For functions which start executing at a function declaration
 * we also need to make sure that we roll back to the real function, */
static void
test_function_hit_counts_for_little_functions_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const FunctionHitCountData expected_hit_counts[] = {
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "FNDA:",
                                                expected_hit_count_len,
                                                hit_count_is_more_than_for_function,
                                                expected_hit_counts,
                                                sizeof(FunctionHitCountData));

    g_free(coverage_data_contents);
}

static void test_function_hit_counts_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    const FunctionHitCountData expected_hit_counts[] = {
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    const gsize expected_hit_count_len = G_N_ELEMENTS(expected_hit_counts);

    /* There are two possible branches here, the second should be taken
     * and the first should not have been */
    assert_coverage_data_matches_values_for_key(coverage_data_contents, "FNDA:",
                                                expected_hit_count_len,
                                                hit_count_is_more_than_for_function,
                                                expected_hit_counts,
                                                sizeof(FunctionHitCountData));

    g_free(coverage_data_contents);
}

static void test_total_function_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    /* More than one assert per test is bad, but we are testing interlinked concepts */
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "FNF:", "3");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "FNH:", "2");
    g_free(coverage_data_contents);
}

typedef struct _LineCountIsMoreThanData {
    unsigned int expected_lineno;
    unsigned int expected_to_be_more_than;
} LineCountIsMoreThanData;

static void
line_hit_count_is_more_than(const char *line,
                            const void *user_data)
{
    auto data = static_cast<const LineCountIsMoreThanData *>(user_data);

    const char *coverage_line = &line[3];
    char *comma_ptr = NULL;

    unsigned int lineno = strtol(coverage_line, &comma_ptr, 10);

    g_assert_cmpint(comma_ptr[0], ==, ',');

    char *end_ptr = NULL;

    unsigned int value = strtol(&comma_ptr[1], &end_ptr, 10);

    g_assert_true(end_ptr[0] == '\0' || end_ptr[0] == '\n');

    g_assert_cmpuint(lineno, ==, data->expected_lineno);
    g_assert_cmpuint(value, >, data->expected_to_be_more_than);
}

static void test_single_line_hit_written_to_coverage_data(void* fixture_data,
                                                          const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    const LineCountIsMoreThanData data = {1, 0};

    assert_coverage_data_matches_value_for_key(coverage_data_contents, "DA:",
                                               line_hit_count_is_more_than,
                                               &data);
    g_free(coverage_data_contents);
}

static void test_hits_on_multiline_if_cond(void* fixture_data, const void*) {
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
                                          fixture->lcov_output);

    /* Hits on all lines, including both lines with a condition (3 and 4) */
    const LineCountIsMoreThanData data[] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};

    assert_coverage_data_matches_value_for_key(coverage_data_contents, "DA:",
                                               line_hit_count_is_more_than,
                                               data);
    g_free(coverage_data_contents);
}

static void test_full_line_tally_written_to_coverage_data(void* fixture_data,
                                                          const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    /* More than one assert per test is bad, but we are testing interlinked concepts */
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "LF:", "1");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "LH:", "1");
    g_free(coverage_data_contents);
}

static void test_no_hits_to_coverage_data_for_unexecuted(void* fixture_data,
                                                         const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        write_statistics_and_get_coverage_data(fixture->coverage,
                                               fixture->lcov_output);

    /* No files were executed, so the coverage data is empty. */
    g_assert_cmpstr(coverage_data_contents, ==, "\n");
    g_free(coverage_data_contents);
}

static void test_end_of_record_section_written_to_coverage_data(
    void* fixture_data, const void*) {
    GjsCoverageFixture *fixture = (GjsCoverageFixture *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->context,
                                          fixture->coverage,
                                          fixture->tmp_js_script,
                                          fixture->lcov_output);

    g_assert_nonnull(strstr(coverage_data_contents, "end_of_record"));
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
    fixture->base_fixture.coverage =
        gjs_coverage_new(coverage_paths, fixture->base_fixture.context,
                         fixture->base_fixture.lcov_output_dir);

    g_free(output_path);
    g_free(first_js_script_path);
    g_free(second_js_script_path);

    char *base_name = g_file_get_basename(fixture->base_fixture.tmp_js_script);
    char *base_name_without_extension = g_strndup(base_name,
                                                  strlen(base_name) - 3);
    char* mock_script = g_strconcat("const FirstScript = imports.",
                                    base_name_without_extension, ";\n",
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

static void test_multiple_source_file_records_written_to_coverage_data(
    void* fixture_data, const void*) {
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->second_js_source_file,
                                          fixture->base_fixture.lcov_output);

    const char *first_sf_record = line_starting_with(coverage_data_contents, "SF:");
    g_assert_nonnull(first_sf_record);

    const char *second_sf_record = line_starting_with(first_sf_record + 1, "SF:");
    g_assert_nonnull(second_sf_record);

    g_free(coverage_data_contents);
}

typedef struct _ExpectedSourceFileCoverageData {
    const char              *source_file_path;
    LineCountIsMoreThanData *more_than;
    unsigned int            n_more_than_matchers;
    const char              expected_lines_hit_character;
    const char              expected_lines_found_character;
} ExpectedSourceFileCoverageData;

static void
assert_coverage_data_for_source_file(ExpectedSourceFileCoverageData *expected,
                                     const size_t                    expected_size,
                                     const char                     *section_start)
{
    gsize i;
    for (i = 0; i < expected_size; ++i) {
        if (strncmp(&section_start[3],
                    expected[i].source_file_path,
                    strlen (expected[i].source_file_path)) == 0) {
            assert_coverage_data_matches_values_for_key(section_start, "DA:",
                                                        expected[i].n_more_than_matchers,
                                                        line_hit_count_is_more_than,
                                                        expected[i].more_than,
                                                        sizeof (LineCountIsMoreThanData));
            const char *total_hits_record = line_starting_with(section_start, "LH:");
            g_assert_cmpint(total_hits_record[3], ==, expected[i].expected_lines_hit_character);
            const char *total_found_record = line_starting_with(section_start, "LF:");
            g_assert_cmpint(total_found_record[3], ==, expected[i].expected_lines_found_character);

            return;
        }
    }

    g_assert_true(false && "Expected source file path to be found in section");
}

static void
test_correct_line_coverage_data_written_for_both_source_file_sections(
    void* fixture_data, const void*) {
    GjsCoverageMultpleSourcesFixutre *fixture = (GjsCoverageMultpleSourcesFixutre *) fixture_data;

    char *coverage_data_contents =
        eval_script_and_get_coverage_data(fixture->base_fixture.context,
                                          fixture->base_fixture.coverage,
                                          fixture->second_js_source_file,
                                          fixture->base_fixture.lcov_output);

    LineCountIsMoreThanData first_script_matcher = {1, 0};

    LineCountIsMoreThanData second_script_matchers[] = {{1, 0}, {2, 0}};

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
    assert_coverage_data_for_source_file(expected, expected_len, first_sf_record);

    const char *second_sf_record = line_starting_with(first_sf_record + 3, "SF:");
    assert_coverage_data_for_source_file(expected, expected_len, second_sf_record);

    g_free(first_script_output_path);
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
                         test_correct_line_coverage_data_written_for_both_source_file_sections,
                         NULL);
}
