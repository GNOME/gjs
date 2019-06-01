/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */

#ifndef TEST_GJS_TEST_UTILS_H_
#define TEST_GJS_TEST_UTILS_H_

#include "gjs/context.h"
#include "gjs/jsapi-wrapper.h"

typedef struct _GjsUnitTestFixture GjsUnitTestFixture;
struct _GjsUnitTestFixture {
    GjsContext *gjs_context;
    JSContext *cx;
    JSCompartment *compartment;
};

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void* unused);

void gjs_unit_test_destroy_context(GjsUnitTestFixture *fx);

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void* unused);

void gjs_test_add_tests_for_coverage ();

void gjs_test_add_tests_for_parse_call_args(void);

void gjs_test_add_tests_for_rooting(void);

#endif  // TEST_GJS_TEST_UTILS_H_
