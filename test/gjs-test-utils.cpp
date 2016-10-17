/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
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

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <glib.h>

#include "gjs/compat.h"
#include "gjs/context.h"
#include "gjs-test-utils.h"

static void
test_error_reporter(JSContext     *context,
                    const char    *message,
                    JSErrorReport *report)
{
    GjsContext *gjs_context = gjs_context_get_current();
    GjsUnitTestFixture *fx =
        (GjsUnitTestFixture *) g_object_get_data(G_OBJECT(gjs_context),
                                                 "test fixture");
    g_free(fx->message);
    fx->message = g_strdup(message);
}

void
gjs_unit_test_fixture_setup(GjsUnitTestFixture *fx,
                            gconstpointer       unused)
{
    fx->gjs_context = gjs_context_new();
    fx->cx = (JSContext *) gjs_context_get_native_context(fx->gjs_context);

    /* This is for shoving private data into the error reporter callback */
    g_object_set_data(G_OBJECT(fx->gjs_context), "test fixture", fx);
    JS_SetErrorReporter(fx->cx, test_error_reporter);

    JS_BeginRequest(fx->cx);

    JS::RootedObject global(fx->cx, gjs_get_global_object(fx->cx));
    fx->compartment = JS_EnterCompartment(fx->cx, global);
}

void
gjs_unit_test_fixture_teardown(GjsUnitTestFixture *fx,
                               gconstpointer      unused)
{
    JS_LeaveCompartment(fx->cx, fx->compartment);
    JS_EndRequest(fx->cx);

    g_object_unref(fx->gjs_context);

    if (fx->message != NULL)
        g_printerr("**\n%s\n", fx->message);
    g_free(fx->message);
}
