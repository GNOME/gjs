/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Endless Mobile, Inc.
// SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>

#include <config.h>

#include <errno.h>   // for errno
#include <stdint.h>
#include <stdio.h>   // for sscanf, size_t
#include <stdlib.h>  // for strtol, atoi, mkdtemp
#include <string.h>  // for strlen, strstr, strncmp, strcspn

#include <algorithm>  // for find, min
#include <charconv>   // for from_chars
#include <string>
#include <string_view>
#include <system_error>  // for errc
#include <vector>

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/auto.h"
#include "gjs/context.h"
#include "gjs/coverage.h"
#include "gjs/gerror-result.h"

struct GjsCoverageFixture {
    GjsContext* gjs_context;
    GjsCoverage* coverage;

    GFile* tmp_output_dir;
    GFile* tmp_js_script;
    GFile* lcov_output_dir;
    GFile* lcov_output;
};

static void replace_file(GFile* file, const char* contents) {
    Gjs::AutoError error;
    g_file_replace_contents(
        file, contents, strlen(contents), /* etag = */ nullptr,
        /* make backup = */ false, G_FILE_CREATE_NONE,
        /* etag out = */ nullptr, /* cancellable = */ nullptr, &error);
    g_assert_no_error(error);
}

static void recursive_delete_dir(GFile* dir) {
    GFileEnumerator* files =
        g_file_enumerate_children(dir, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                  G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    while (true) {
        GFile* file;
        GFileInfo* info;
        if (!g_file_enumerator_iterate(files, &info, &file, nullptr, nullptr) ||
            !file || !info)
            break;
        if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
            recursive_delete_dir(file);
            continue;
        }
        g_file_delete(file, nullptr, nullptr);
    }
    g_file_delete(dir, nullptr, nullptr);
    g_object_unref(files);
}

static void gjs_coverage_fixture_set_up(void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);
    const char* js_script = "var f = function () { return 1; }\n";

    Gjs::AutoChar tmp_output_dir_name{g_strdup("/tmp/gjs_coverage_tmp.XXXXXX")};
    tmp_output_dir_name = mkdtemp(tmp_output_dir_name.release());

    if (!tmp_output_dir_name)
        g_error("Failed to create temporary directory for test files: %s\n",
                strerror(errno));

    fixture->tmp_output_dir = g_file_new_for_path(tmp_output_dir_name);
    fixture->tmp_js_script = g_file_get_child(fixture->tmp_output_dir,
                                              "gjs_coverage_script.js");
    fixture->lcov_output_dir = g_file_get_child(fixture->tmp_output_dir,
                                                "gjs_coverage_test_coverage");
    fixture->lcov_output = g_file_get_child(fixture->lcov_output_dir,
                                            "coverage.lcov");

    g_file_make_directory_with_parents(fixture->lcov_output_dir, nullptr,
                                       nullptr);

    Gjs::AutoChar tmp_js_script_filename{
        g_file_get_path(fixture->tmp_js_script)};

    // Allocate a strv that we can pass over to gjs_coverage_new()
    char* coverage_paths[] = {tmp_js_script_filename, nullptr};
    char* search_paths[] = {tmp_output_dir_name, nullptr};

    gjs_coverage_enable();
    fixture->gjs_context =
        gjs_context_new_with_search_path(static_cast<char**>(search_paths));
    fixture->coverage = gjs_coverage_new(coverage_paths, fixture->gjs_context,
                                         fixture->lcov_output_dir);

    replace_file(fixture->tmp_js_script, js_script);
}

static void gjs_coverage_fixture_tear_down(void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    recursive_delete_dir(fixture->tmp_output_dir);

    g_object_unref(fixture->tmp_js_script);
    g_object_unref(fixture->tmp_output_dir);
    g_object_unref(fixture->lcov_output_dir);
    g_object_unref(fixture->lcov_output);
    g_object_unref(fixture->coverage);
    g_object_unref(fixture->gjs_context);
}

static const char* line_starting_with(const char* data, const char* needle) {
    const size_t needle_length = strlen(needle);
    const char* iter = data;

    while (iter) {
        if (strncmp(iter, needle, needle_length) == 0)
          return iter;

        iter = strstr(iter, "\n");

        if (iter)
          iter += 1;
    }

    return nullptr;
}

static std::string_view line_starting_with(const std::string_view& data,
                                           const std::string& needle) {
    const size_t needle_length = needle.size();
    std::string_view iter = data;

    while (!iter.empty()) {
        // COMPAT: Use starts_with in C++20
        std::string_view prefix{iter.data(),
                                std::min(iter.size(), needle_length)};
        if (prefix == needle)
            return iter;

        size_t newline = iter.find('\n');
        iter.remove_prefix(std::min(iter.size(), newline + 1));
    }

    return iter;
}

static char* write_statistics_and_get_coverage_data(GjsCoverage* coverage,
                                                    GFile* lcov_output) {
    gjs_coverage_write_statistics(coverage);

    char* coverage_data_contents;

    g_file_load_contents(lcov_output, /* cancellable = */ nullptr,
                         &coverage_data_contents, /* length out = */ nullptr,
                         /* etag_out = */ nullptr, /* error = */ nullptr);

    return coverage_data_contents;
}

static char* get_script_identifier(GFile* script) {
    char* filename = g_file_get_path(script);
    if (!filename)
        filename = g_file_get_uri(script);
    return filename;
}

static bool eval_script(GjsContext* gjs_context, GFile* script) {
    Gjs::AutoChar filename{get_script_identifier(script)};
    return gjs_context_eval_file(gjs_context, filename, nullptr, nullptr);
}

static char* eval_script_and_get_coverage_data(GjsContext* gjs_context,
                                               GjsCoverage* coverage,
                                               GFile* script,
                                               GFile* lcov_output) {
    eval_script(gjs_context, script);
    return write_statistics_and_get_coverage_data(coverage, lcov_output);
}

static void assert_coverage_data_contains_value_for_key(const char* data,
                                                        const char* key,
                                                        const char* value) {
    const char* sf_line = line_starting_with(data, key);

    g_assert_nonnull(sf_line);

    Gjs::AutoChar actual{g_strndup(&sf_line[strlen(key)], strlen(value))};
    g_assert_cmpstr(value, ==, actual);
}

template <typename T>
using CoverageDataMatchFunc = void (*)(const char* value, const T& data);

template <typename T>
static void assert_coverage_data_matches_value_for_key(
    const char* data, const char* key, CoverageDataMatchFunc<T> match,
    const T& user_data) {
    const char* line = line_starting_with(data, key);

    g_assert_nonnull(line);
    (*match)(line, user_data);
}

template <typename T>
static void assert_coverage_data_matches_values_for_key(
    const char* data, const char* key, CoverageDataMatchFunc<T> match,
    const std::vector<T>& user_data) {
    const char* line = line_starting_with(data, key);
    // Keep matching. If we fail to match one of them then bail out
    for (auto& data_iterator : user_data) {
        g_assert_nonnull(line);

        (*match)(line, data_iterator);

        line = line_starting_with(line + 1, key);
    }
}

template <typename T>
using CoverageDataExtractFunc = T (*)(std::string_view&);

template <typename T>
static void assert_coverage_data_matches_values_for_key(
    std::string_view& data, const std::string& key,
    CoverageDataExtractFunc<T> extract,
    const std::vector<T>& expected_matches) {
    std::vector<T> remaining_matches{expected_matches};
    std::string_view line = line_starting_with(data, key);

    while (!line.empty() && !remaining_matches.empty()) {
        T entry = (*extract)(line);

        auto found = std::find(remaining_matches.begin(),
                               remaining_matches.end(), entry);
        g_assert_false(found == remaining_matches.end());
        remaining_matches.erase(found);

        line = line_starting_with(line, key);
    }

    g_assert_cmpuint(remaining_matches.size(), ==, 0);
}

static void test_covered_file_is_duplicated_into_output_if_resource(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* mock_resource_filename =
        "resource:///org/gnome/gjs/mock/test/gjs-test-coverage/"
        "loadedJSFromResource.js";
    const char* coverage_scripts[] = {mock_resource_filename, nullptr};

    g_object_unref(fixture->gjs_context);
    g_object_unref(fixture->coverage);
    Gjs::AutoChar js_script_dirname{g_file_get_path(fixture->tmp_output_dir)};
    char* search_paths[] = {js_script_dirname, nullptr};

    fixture->gjs_context = gjs_context_new_with_search_path(search_paths);
    fixture->coverage = gjs_coverage_new(coverage_scripts, fixture->gjs_context,
                                         fixture->lcov_output_dir);

    bool ok = gjs_context_eval_file(fixture->gjs_context,
                                    mock_resource_filename, nullptr, nullptr);
    g_assert_true(ok);

    gjs_coverage_write_statistics(fixture->coverage);

    GFile* expected_temporary_js_script = g_file_resolve_relative_path(
        fixture->lcov_output_dir,
        "org/gnome/gjs/mock/test/gjs-test-coverage/loadedJSFromResource.js");

    g_assert_true(g_file_query_exists(expected_temporary_js_script, nullptr));
    g_object_unref(expected_temporary_js_script);
}

static GFile* get_output_file_for_script_on_disk(GFile* script,
                                                 GFile* output_dir) {
    Gjs::AutoChar base{g_file_get_basename(script)};
    return g_file_get_child(output_dir, base);
}

static char* get_output_path_for_script_on_disk(GFile* script,
                                                GFile* output_dir) {
    Gjs::AutoUnref<GFile> output{
        get_output_file_for_script_on_disk(script, output_dir)};
    return g_file_get_path(output);
}

static void test_covered_file_is_duplicated_into_output_if_path(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    eval_script(fixture->gjs_context, fixture->tmp_js_script);

    gjs_coverage_write_statistics(fixture->coverage);

    Gjs::AutoUnref<GFile> expected_temporary_js_script{
        get_output_file_for_script_on_disk(fixture->tmp_js_script,
                                           fixture->lcov_output_dir)};

    g_assert_true(g_file_query_exists(expected_temporary_js_script, nullptr));
}

static void test_previous_contents_preserved(void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);
    const char* existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    g_assert_nonnull(strstr(coverage_data_contents, existing_contents));
    g_free(coverage_data_contents);
}

static void test_new_contents_written(void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);
    const char* existing_contents = "existing_contents\n";
    replace_file(fixture->lcov_output, existing_contents);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    // We have new content in the coverage data
    g_assert_cmpstr(existing_contents, !=, coverage_data_contents);
    g_free(coverage_data_contents);
}

static void test_expected_source_file_name_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    Gjs::AutoChar expected_source_filename{get_output_path_for_script_on_disk(
        fixture->tmp_js_script, fixture->lcov_output_dir)};

    assert_coverage_data_contains_value_for_key(coverage_data_contents, "SF:",
                                                expected_source_filename);

    g_free(coverage_data_contents);
}

static void test_expected_entry_not_written_for_nonexistent_file(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* coverage_paths[] = {"doesnotexist", nullptr};

    g_object_unref(fixture->coverage);
    fixture->coverage = gjs_coverage_new(coverage_paths, fixture->gjs_context,
                                         fixture->lcov_output_dir);

    Gjs::AutoUnref<GFile> doesnotexist{g_file_new_for_path("doesnotexist")};
    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, doesnotexist,
        fixture->lcov_output);

    const char* sf_line = line_starting_with(coverage_data_contents, "SF:");
    g_assert_null(sf_line);

    g_free(coverage_data_contents);
}

enum BranchTaken : uint8_t { NOT_EXECUTED, NOT_TAKEN, TAKEN };

struct BranchLineData {
    int expected_branch_line;
    int expected_id;
    BranchTaken taken;
};

static void branch_at_line_should_be_taken(const char* line,
                                           const BranchLineData& branch_data) {
    int line_no, branch_id, block_no, hit_count_num;
    char hit_count[20];  // can hold maxint64 (19 digits) + nul terminator

    // Advance past "BRDA:"
    line += 5;

    int nmatches = sscanf(line, "%i,%i,%i,%19s", &line_no, &block_no,
                          &branch_id, hit_count);
    g_assert_cmpint(nmatches, ==, 4);

    /* Determine the branch hit count. It will be either -1 if the line
     * containing the branch was never executed, or N times the branch was
     * taken.
     *
     * The value of -1 is represented by a single "-" character, so we should
     * detect this case and set the value based on that */
    if (strlen(hit_count) == 1 && *hit_count == '-')
        hit_count_num = -1;
    else
        hit_count_num = atoi(hit_count);

    g_assert_cmpint(line_no, ==, branch_data.expected_branch_line);
    g_assert_cmpint(branch_id, ==, branch_data.expected_id);

    switch (branch_data.taken) {
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
    }
}

static void test_single_branch_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_basic_branch =
        "let x = 0;\n"
        "if (x > 0)\n"
        "    x++;\n"
        "else\n"
        "    x++;\n";

    replace_file(fixture->tmp_js_script, script_with_basic_branch);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<BranchLineData> expected_branches = {{2, 0, TAKEN},
                                                     {2, 1, NOT_TAKEN}};

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    assert_coverage_data_matches_values_for_key(
        coverage_data_contents, "BRDA:", branch_at_line_should_be_taken,
        expected_branches);

    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "BRF:", "2");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "BRH:", "1");
    g_free(coverage_data_contents);
}

static void test_multiple_branch_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_case_statements_branch =
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

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<BranchLineData> expected_branches{
        {2, 0, TAKEN}, {2, 1, TAKEN}, {3, 0, TAKEN},
        {3, 1, TAKEN}, {3, 2, TAKEN}, {3, 3, NOT_TAKEN},
    };

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    assert_coverage_data_matches_values_for_key(
        coverage_data_contents, "BRDA:", branch_at_line_should_be_taken,
        expected_branches);
    g_free(coverage_data_contents);
}

static void test_branches_for_multiple_case_statements_fallthrough(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_case_statements_branch =
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

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<BranchLineData> expected_branches = {
        {2, 0, TAKEN}, {2, 1, TAKEN},     {3, 0, TAKEN},
        {3, 1, TAKEN}, {3, 2, NOT_TAKEN}, {3, 3, NOT_TAKEN},
    };

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    assert_coverage_data_matches_values_for_key(
        coverage_data_contents, "BRDA:", branch_at_line_should_be_taken,
        expected_branches);
    g_free(coverage_data_contents);
}

static void any_line_matches_not_executed_branch(const char* data) {
    const char* line = line_starting_with(data, "BRDA:");

    while (line) {
        int line_no, branch_id, block_no;
        char hit_count;

        // Advance past "BRDA:"
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
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_never_executed_branch =
        "let x = 0;\n"
        "if (x > 0) {\n"
        "    if (x > 0)\n"
        "        x++;\n"
        "} else {\n"
        "    x++;\n"
        "}\n";

    replace_file(fixture->tmp_js_script, script_with_never_executed_branch);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    any_line_matches_not_executed_branch(coverage_data_contents);
    g_free(coverage_data_contents);
}

static std::string extract_function_name(std::string_view& line) {
    // Advance past "FN:"
    line.remove_prefix(3);

    // Advance past the first comma
    line.remove_prefix(line.find(',') + 1);

    return std::string{line.substr(0, line.find('\n'))};
}

static void test_function_names_written_to_coverage_data(void* fixture_data,
                                                         const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_named_and_unnamed_functions =
        "function f(){}\n"
        "let b = function(){}\n";

    replace_file(fixture->tmp_js_script,
                 script_with_named_and_unnamed_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<std::string> expected_function_names{
        "top-level",
        "f",
        "b",
    };

    // Just expect that we've got an FN matching our expected function names
    std::string_view contents_view{coverage_data_contents};
    assert_coverage_data_matches_values_for_key(
        contents_view, "FN:", extract_function_name, expected_function_names);

    g_free(coverage_data_contents);
}

static std::string extract_function_line(std::string_view& line) {
    // Advance past "FN:"
    line.remove_prefix(3);

    return std::string{line.substr(0, line.find(','))};
}

static void test_function_lines_written_to_coverage_data(void* fixture_data,
                                                         const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_functions =
        "function f(){}\n"
        "\n"
        "function g(){}\n";

    replace_file(fixture->tmp_js_script, script_with_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);
    std::vector<std::string> expected_function_lines{
        "1",
        "1",
        "3",
    };

    std::string_view contents_view{coverage_data_contents};
    assert_coverage_data_matches_values_for_key(
        contents_view, "FN:", extract_function_line, expected_function_lines);
    g_free(coverage_data_contents);
}

struct FunctionHitCountData {
    std::string function;
    unsigned hit_count_minimum;

    bool operator==(const FunctionHitCountData& other) const {
        return function == other.function &&
               other.hit_count_minimum >= hit_count_minimum;
    }
};

static FunctionHitCountData extract_hit_count(std::string_view& line) {
    // Advance past "FNDA:"
    line.remove_prefix(5);

    size_t comma_pos = line.find(',');
    std::string hit_count_string{line.substr(0, comma_pos)};
    line.remove_prefix(comma_pos + 1);

    FunctionHitCountData retval;
    auto result =
        std::from_chars(hit_count_string.data(),
                        hit_count_string.data() + hit_count_string.size(),
                        retval.hit_count_minimum);
    g_assert_true(result.ec == std::errc{});
    retval.function = line.substr(0, line.find('\n'));
    return retval;
}

/* For functions with whitespace between their definition and first executable
 * line, its possible that the JS engine might enter their frame a little later
 * in the script than where their definition starts. We need to handle that
 * case. */
static void test_function_hit_counts_for_big_functions_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_executed_functions =
        "function f(){\n"
        "\n"
        "\n"
        "var x = 1;\n"
        "}\n"
        "let b = function(){}\n"
        "f();\n"
        "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<FunctionHitCountData> expected_hit_counts{
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    std::string_view contents_view{coverage_data_contents};
    assert_coverage_data_matches_values_for_key(
        contents_view, "FNDA:", extract_hit_count, expected_hit_counts);

    g_free(coverage_data_contents);
}

/* For functions which start executing at a function declaration we also need to
 * make sure that we roll back to the real function. */
static void
test_function_hit_counts_for_little_functions_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_executed_functions =
        "function f(){\n"
        "var x = function(){};\n"
        "}\n"
        "let b = function(){}\n"
        "f();\n"
        "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<FunctionHitCountData> expected_hit_counts{
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    std::string_view contents_view{coverage_data_contents};
    assert_coverage_data_matches_values_for_key(
        contents_view, "FNDA:", extract_hit_count, expected_hit_counts);

    g_free(coverage_data_contents);
}

static void test_function_hit_counts_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_executed_functions =
        "function f(){}\n"
        "let b = function(){}\n"
        "f();\n"
        "b();\n";

    replace_file(fixture->tmp_js_script, script_with_executed_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    std::vector<FunctionHitCountData> expected_hit_counts{
        {"top-level", 1},
        {"f", 1},
        {"b", 1},
    };

    /* There are two possible branches here, the second should be taken and the
     * first should not have been */
    std::string_view contents_view{coverage_data_contents};
    assert_coverage_data_matches_values_for_key(
        contents_view, "FNDA:", extract_hit_count, expected_hit_counts);

    g_free(coverage_data_contents);
}

static void test_total_function_coverage_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_some_executed_functions =
        "function f(){}\n"
        "let b = function(){}\n"
        "f();\n";

    replace_file(fixture->tmp_js_script, script_with_some_executed_functions);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "FNF:", "3");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "FNH:", "2");
    g_free(coverage_data_contents);
}

struct LineCountIsMoreThanData {
    unsigned expected_lineno;
    unsigned expected_to_be_more_than;
};

static void line_hit_count_is_more_than(const char* line,
                                        const LineCountIsMoreThanData& data) {
    const char* coverage_line = &line[3];
    char* comma_ptr = nullptr;

    unsigned lineno = strtol(coverage_line, &comma_ptr, 10);

    g_assert_cmpint(comma_ptr[0], ==, ',');

    char* end_ptr = nullptr;

    unsigned value = strtol(&comma_ptr[1], &end_ptr, 10);

    g_assert_true(end_ptr[0] == '\0' || end_ptr[0] == '\n');

    g_assert_cmpuint(lineno, ==, data.expected_lineno);
    g_assert_cmpuint(value, >, data.expected_to_be_more_than);
}

static void test_single_line_hit_written_to_coverage_data(void* fixture_data,
                                                          const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    const LineCountIsMoreThanData data = {1, 0};

    assert_coverage_data_matches_value_for_key(
        coverage_data_contents, "DA:", line_hit_count_is_more_than, data);
    g_free(coverage_data_contents);
}

static void test_hits_on_multiline_if_cond(void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    const char* script_with_multine_if_cond =
        "let a = 1;\n"
        "let b = 1;\n"
        "if (a &&\n"
        "    b) {\n"
        "}\n";

    replace_file(fixture->tmp_js_script, script_with_multine_if_cond);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    // Hits on all lines, including both lines with a condition (3 and 4)
    std::vector<LineCountIsMoreThanData> data{{1, 0}, {2, 0}, {3, 0}, {4, 0}};

    assert_coverage_data_matches_values_for_key(
        coverage_data_contents, "DA:", line_hit_count_is_more_than, data);
    g_free(coverage_data_contents);
}

static void test_full_line_tally_written_to_coverage_data(void* fixture_data,
                                                          const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "LF:", "1");
    assert_coverage_data_contains_value_for_key(coverage_data_contents,
                                                "LH:", "1");
    g_free(coverage_data_contents);
}

static void test_no_hits_to_coverage_data_for_unexecuted(void* fixture_data,
                                                         const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    Gjs::AutoChar coverage_data_contents{write_statistics_and_get_coverage_data(
        fixture->coverage, fixture->lcov_output)};

    // No files were executed, so the coverage data is empty.
    g_assert_cmpstr(coverage_data_contents, ==, "\n");
}

static void test_end_of_record_section_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture = static_cast<GjsCoverageFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->tmp_js_script,
        fixture->lcov_output);

    g_assert_nonnull(strstr(coverage_data_contents, "end_of_record"));
    g_free(coverage_data_contents);
}

struct GjsCoverageMultipleSourcesFixture : GjsCoverageFixture {
    GFile* second_js_source_file;
};

static void gjs_coverage_multiple_source_files_to_single_output_fixture_set_up(
    void* fixture_data, const void* user_data) {
    gjs_coverage_fixture_set_up(fixture_data, user_data);

    auto* fixture =
        static_cast<GjsCoverageMultipleSourcesFixture*>(fixture_data);
    fixture->second_js_source_file = g_file_get_child(
        fixture->tmp_output_dir, "gjs_coverage_second_source_file.js");

    /* Because GjsCoverage searches the coverage paths at object-creation time,
     * we need to destroy the previously constructed one and construct it again
     */
    Gjs::AutoChar first_js_script_path{g_file_get_path(fixture->tmp_js_script)};
    Gjs::AutoChar second_js_script_path{
        g_file_get_path(fixture->second_js_source_file)};
    char* coverage_paths[] = {first_js_script_path, second_js_script_path,
                              nullptr};

    g_object_unref(fixture->gjs_context);
    g_object_unref(fixture->coverage);
    Gjs::AutoChar output_path{g_file_get_path(fixture->tmp_output_dir)};
    char* search_paths[] = {output_path, nullptr};

    fixture->gjs_context = gjs_context_new_with_search_path(search_paths);
    fixture->coverage = gjs_coverage_new(coverage_paths, fixture->gjs_context,
                                         fixture->lcov_output_dir);

    Gjs::AutoChar base_name{g_file_get_basename(fixture->tmp_js_script)};
    Gjs::AutoChar base_name_without_extension{
        g_strndup(base_name, strlen(base_name) - 3)};
    char* mock_script = g_strconcat("const FirstScript = imports.",
                                    base_name_without_extension.get(),
                                    ";\n"
                                    "let a = FirstScript.f;\n"
                                    "\n",
                                    nullptr);

    replace_file(fixture->second_js_source_file, mock_script);

    g_free(mock_script);
}

static void
gjs_coverage_multiple_source_files_to_single_output_fixture_tear_down(
    void* fixture_data, const void* user_data) {
    auto* fixture =
        static_cast<GjsCoverageMultipleSourcesFixture*>(fixture_data);
    g_object_unref(fixture->second_js_source_file);

    gjs_coverage_fixture_tear_down(fixture_data, user_data);
}

static void test_multiple_source_file_records_written_to_coverage_data(
    void* fixture_data, const void*) {
    auto* fixture =
        static_cast<GjsCoverageMultipleSourcesFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->second_js_source_file,
        fixture->lcov_output);

    const char* first_sf_record =
        line_starting_with(coverage_data_contents, "SF:");
    g_assert_nonnull(first_sf_record);

    const char* second_sf_record =
        line_starting_with(first_sf_record + 1, "SF:");
    g_assert_nonnull(second_sf_record);

    g_free(coverage_data_contents);
}

struct ExpectedSourceFileCoverageData {
    const char* source_file_path;
    std::vector<LineCountIsMoreThanData> more_than;
    const char expected_lines_hit_character;
    const char expected_lines_found_character;
};

static void assert_coverage_data_for_source_file(
    const std::vector<ExpectedSourceFileCoverageData>& expected_data,
    const char* section_start) {
    for (const auto& expected : expected_data) {
        if (strncmp(&section_start[3], expected.source_file_path,
                    strlen(expected.source_file_path)) == 0) {
            assert_coverage_data_matches_values_for_key(
                section_start, "DA:", line_hit_count_is_more_than,
                expected.more_than);
            const char* total_hits_record =
                line_starting_with(section_start, "LH:");
            g_assert_cmpint(total_hits_record[3], ==,
                            expected.expected_lines_hit_character);
            const char* total_found_record =
                line_starting_with(section_start, "LF:");
            g_assert_cmpint(total_found_record[3], ==,
                            expected.expected_lines_found_character);

            return;
        }
    }

    g_assert_true(false && "Expected source file path to be found in section");
}

static void
test_correct_line_coverage_data_written_for_both_source_file_sections(
    void* fixture_data, const void*) {
    auto* fixture =
        static_cast<GjsCoverageMultipleSourcesFixture*>(fixture_data);

    char* coverage_data_contents = eval_script_and_get_coverage_data(
        fixture->gjs_context, fixture->coverage, fixture->second_js_source_file,
        fixture->lcov_output);

    Gjs::AutoChar first_script_output_path{get_output_path_for_script_on_disk(
        fixture->tmp_js_script, fixture->lcov_output_dir)};
    Gjs::AutoChar second_script_output_path{get_output_path_for_script_on_disk(
        fixture->second_js_source_file, fixture->lcov_output_dir)};

    std::vector<ExpectedSourceFileCoverageData> expected{
        {first_script_output_path, {{1, 0}}, '1', '1'},
        {second_script_output_path, {{1, 0}, {2, 0}}, '2', '2'}};

    const char* first_sf_record =
        line_starting_with(coverage_data_contents, "SF:");
    assert_coverage_data_for_source_file(expected, first_sf_record);

    const char* second_sf_record =
        line_starting_with(first_sf_record + 3, "SF:");
    assert_coverage_data_for_source_file(expected, second_sf_record);

    g_free(coverage_data_contents);
}

struct FixturedTest {
    size_t fixture_size;
    GTestFixtureFunc set_up;
    GTestFixtureFunc tear_down;
};

static void add_test_for_fixture(const char* name, FixturedTest* fixture,
                                 GTestFixtureFunc test_func) {
    g_test_add_vtable(name, fixture->fixture_size, nullptr, fixture->set_up,
                      test_func, fixture->tear_down);
}

void gjs_test_add_tests_for_coverage() {
    FixturedTest coverage_fixture = {
        sizeof(GjsCoverageFixture),
        gjs_coverage_fixture_set_up,
        gjs_coverage_fixture_tear_down
    };

    add_test_for_fixture("/gjs/coverage/file_duplicated_into_output_path",
                         &coverage_fixture,
                         test_covered_file_is_duplicated_into_output_if_path);
    add_test_for_fixture(
        "/gjs/coverage/file_duplicated_full_resource_path", &coverage_fixture,
        test_covered_file_is_duplicated_into_output_if_resource);
    add_test_for_fixture("/gjs/coverage/contents_preserved_accumulate_mode",
                         &coverage_fixture, test_previous_contents_preserved);
    add_test_for_fixture("/gjs/coverage/new_contents_appended_accumulate_mode",
                         &coverage_fixture, test_new_contents_written);
    add_test_for_fixture(
        "/gjs/coverage/expected_source_file_name_written_to_coverage_data",
        &coverage_fixture,
        test_expected_source_file_name_written_to_coverage_data);
    add_test_for_fixture("/gjs/coverage/entry_not_written_for_nonexistent_file",
                         &coverage_fixture,
                         test_expected_entry_not_written_for_nonexistent_file);
    add_test_for_fixture(
        "/gjs/coverage/single_branch_coverage_written_to_coverage_data",
        &coverage_fixture,
        test_single_branch_coverage_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/multiple_branch_coverage_written_to_coverage_data",
        &coverage_fixture,
        test_multiple_branch_coverage_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/branches_for_multiple_case_statements_fallthrough",
        &coverage_fixture,
        test_branches_for_multiple_case_statements_fallthrough);
    add_test_for_fixture(
        "/gjs/coverage/not_hit_branch_point_written_to_coverage_data",
        &coverage_fixture, test_branch_not_hit_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/function_names_written_to_coverage_data",
        &coverage_fixture, test_function_names_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/function_lines_written_to_coverage_data",
        &coverage_fixture, test_function_lines_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/function_hit_counts_written_to_coverage_data",
        &coverage_fixture, test_function_hit_counts_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/big_function_hit_counts_written_to_coverage_data",
        &coverage_fixture,
        test_function_hit_counts_for_big_functions_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/little_function_hit_counts_written_to_coverage_data",
        &coverage_fixture,
        test_function_hit_counts_for_little_functions_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/total_function_coverage_written_to_coverage_data",
        &coverage_fixture,
        test_total_function_coverage_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/single_line_hit_written_to_coverage_data",
        &coverage_fixture, test_single_line_hit_written_to_coverage_data);
    add_test_for_fixture("/gjs/coverage/hits_on_multiline_if_cond",
                         &coverage_fixture, test_hits_on_multiline_if_cond);
    add_test_for_fixture(
        "/gjs/coverage/full_line_tally_written_to_coverage_data",
        &coverage_fixture, test_full_line_tally_written_to_coverage_data);
    add_test_for_fixture("/gjs/coverage/no_hits_for_unexecuted_file",
                         &coverage_fixture,
                         test_no_hits_to_coverage_data_for_unexecuted);
    add_test_for_fixture(
        "/gjs/coverage/end_of_record_section_written_to_coverage_data",
        &coverage_fixture, test_end_of_record_section_written_to_coverage_data);

    FixturedTest coverage_for_multiple_files_to_single_output_fixture = {
        sizeof(GjsCoverageMultipleSourcesFixture),
        gjs_coverage_multiple_source_files_to_single_output_fixture_set_up,
        gjs_coverage_multiple_source_files_to_single_output_fixture_tear_down};

    add_test_for_fixture(
        "/gjs/coverage/multiple_source_file_records_written_to_coverage_data",
        &coverage_for_multiple_files_to_single_output_fixture,
        test_multiple_source_file_records_written_to_coverage_data);
    add_test_for_fixture(
        "/gjs/coverage/correct_line_coverage_data_written_for_both_sections",
        &coverage_for_multiple_files_to_single_output_fixture,
        test_correct_line_coverage_data_written_for_both_source_file_sections);
}
