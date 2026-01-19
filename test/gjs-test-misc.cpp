/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <string>

#include <glib.h>

#include "test/gjs-test-utils.h"
#include "util/misc.h"

namespace Gjs::Test {

static void statm_expected() {
    StatmParseResult result =
        parse_statm_file_rss("21601 1458 1376 73 0 2316 0\n");
    g_assert_ok(result);
    g_assert_cmpuint(result.unwrap(), ==, 1458);
}

static void statm_cutoff_line() {
    StatmParseResult result = parse_statm_file_rss("0 435");
    g_assert_ok(result);
    g_assert_cmpuint(result.unwrap(), ==, 435);
}

static void statm_max_value() {
    StatmParseResult result =
        parse_statm_file_rss("21601 18446744073709551615 1376 73 0 2316 0\n");
    g_assert_ok(result);
    g_assert_cmpuint(result.unwrap(), ==, G_MAXUINT64);
}

static void statm_failure_case(int*, const void* data) {
    auto* contents = static_cast<const char*>(data);
    g_assert_err(parse_statm_file_rss(contents));
}

void add_tests_for_misc_utils() {
    g_test_add_func("/misc/statm/expected", statm_expected);
    g_test_add_func("/misc/statm/cutoff-line", statm_cutoff_line);
    g_test_add_func("/misc/statm/max-value", statm_max_value);

#define ADD_STATM_FAILURE_CASE(path, contents)              \
    g_test_add("/misc/statm/" path, int, contents, nullptr, \
               statm_failure_case, nullptr)

    ADD_STATM_FAILURE_CASE("empty", "");
    ADD_STATM_FAILURE_CASE("empty-line", "\n");
    ADD_STATM_FAILURE_CASE("one-field", "21601\n");
    ADD_STATM_FAILURE_CASE("negative", "21601 -1458 1376 73 0 2316 0\n");
    ADD_STATM_FAILURE_CASE("junk-after-number", "21601 1458foobar 1376");
    ADD_STATM_FAILURE_CASE("non-numeric", "21601 foobar 1376 73 0 2316 0\n");
    ADD_STATM_FAILURE_CASE("exponential", "21601 1.23e4 1376 73 0 2316 0\n");
    ADD_STATM_FAILURE_CASE("too-big", "21601 18446744073709551616 1376 73 0");
    ADD_STATM_FAILURE_CASE("infinity", "21601 inf 1376 73 0");
    ADD_STATM_FAILURE_CASE("nan", "21601 NaN 1376 73 0");

#undef ADD_STATM_FAILURE_CASE
}

}  // namespace Gjs::Test
