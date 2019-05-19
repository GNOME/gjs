/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008 litl, LLC
 * Copyright (c) 2016 Endless Mobile, Inc.
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

#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "test/gjs-test-common.h"
#include "test/gjs-test-utils.h"

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void*) {
    fx->gjs_context = gjs_context_new();
    fx->cx = (JSContext *) gjs_context_get_native_context(fx->gjs_context);

    JS::RootedObject global(fx->cx, gjs_get_import_global(fx->cx));
    fx->realm = JS::EnterRealm(fx->cx, global);
}

void
gjs_unit_test_destroy_context(GjsUnitTestFixture *fx)
{
    GjsAutoChar message = gjs_test_get_exception_message(fx->cx);
    if (message)
        g_printerr("**\n%s\n", message.get());

    JS::LeaveRealm(fx->cx, fx->realm);

    g_object_unref(fx->gjs_context);
}

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void*) {
    gjs_unit_test_destroy_context(fx);
}
