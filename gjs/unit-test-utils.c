/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010 Red Hat, Inc.
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
 */

#include <gjs/gjs.h>
#include "unit-test-utils.h"

static void
test_error_reporter(JSContext     *context,
                    const char    *message,
                    JSErrorReport *report)
{
    g_printerr("error reported by test: %s\n", message);
}

void
_gjs_unit_test_fixture_begin (GjsUnitTestFixture *fixture)
{
    fixture->runtime = JS_NewRuntime(1024*1024 /* max bytes */);
    fixture->context = JS_NewContext(fixture->runtime, 8192);
    JS_BeginRequest(fixture->context);
    if (!gjs_init_context_standard(fixture->context))
        g_error("failed to init context");
    JS_SetErrorReporter(fixture->context, test_error_reporter);
}

void
_gjs_unit_test_fixture_finish (GjsUnitTestFixture *fixture)
{
    JS_EndRequest(fixture->context);
    JS_DestroyContext(fixture->context);
    JS_DestroyRuntime(fixture->runtime);
}
