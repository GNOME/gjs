/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Canonical, Ltd.
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>
#include <glib.h>

#include "test/gjs-test-utils.h"

int main(int argc, char** argv) {
    /* Avoid interference in the tests from stray environment variable */
    g_unsetenv("GJS_ENABLE_PROFILER");
    g_unsetenv("GJS_TRACE_FD");

    g_test_init(&argc, &argv, nullptr);

    gjs_test_add_tests_for_rooting();
    gjs_test_add_tests_for_parse_call_args();
    gjs_test_add_tests_for_jsapi_utils();
    Gjs::Test::add_tests_for_toggle_queue();

    g_test_run();

    return 0;
}
