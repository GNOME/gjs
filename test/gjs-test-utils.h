/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Endless Mobile, Inc.
// SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>

#ifndef TEST_GJS_TEST_UTILS_H_
#define TEST_GJS_TEST_UTILS_H_

#include <config.h>

#include "gjs/context.h"

#include <js/TypeDecls.h>

struct GjsUnitTestFixture {
    GjsContext *gjs_context;
    JSContext *cx;
    JS::Realm* realm;
};

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void* unused);

void gjs_unit_test_destroy_context(GjsUnitTestFixture *fx);

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void* unused);

void gjs_test_add_tests_for_coverage ();

void gjs_test_add_tests_for_parse_call_args(void);

void gjs_test_add_tests_for_rooting(void);

#endif  // TEST_GJS_TEST_UTILS_H_
