/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include <string.h>

#include <jsapi.h>
#include <jsdbgapi.h>

#include <glib.h>
#include <gjs/gjs.h>
#include <gi/closure.h>

#include "debugger.h"

static void
closure_invalidated(gpointer  data,
                    GClosure *closure)
{
    g_closure_remove_invalidate_notifier(closure, closure,
                                         closure_invalidated);
    g_closure_unref(closure);
}

static JSBool
gjs_debugger_debug_error_hook(JSContext     *context,
                              const char    *message,
                              JSErrorReport *report,
                              void          *user_data)
{
    const char *filename = NULL;
    static gboolean running = FALSE;
    guint line = 0, pos = 0, flags = 0, errnum = 0;
    jsval retval = JSVAL_NULL, exc;
    GClosure *closure = (GClosure*)user_data;

    if (running)
        return JS_FALSE;

    running = TRUE;
    if (report) {
        filename = report->filename;
        line = report->lineno;
        pos = report->tokenptr - report->linebuf;
        flags = report->flags;
        errnum = report->errorNumber;
    }

    if (JS_IsExceptionPending(context)) {
        JS_GetPendingException(context, &exc);
    } else {
        exc = JSVAL_NULL;
    }

    if (!gjs_closure_invoke_simple(context, closure, &retval, "ssiiiiv",
                                   message, filename, line, pos,
                                   flags, errnum, exc))
        return JS_FALSE;

    running = FALSE;
    return JS_TRUE;
}

static JSBool
gjs_debugger_set_debug_error_hook(JSContext *context,
                                  JSObject  *obj,
                                  uintN      argc,
                                  jsval     *argv,
                                  jsval     *retval)
{
    static GClosure *closure = NULL;
    JSRuntime *runtime;

    if (!(argc == 1) ||
        !JSVAL_IS_OBJECT(argv[0])) {
        gjs_throw(context, "setDebugErrorHook takes 1 argument, "
                  " the callback");
        return JS_FALSE;
    }

    if (closure != NULL) {
        *retval = OBJECT_TO_JSVAL(gjs_closure_get_callable(closure));
        g_closure_invalidate(closure);
        closure = NULL;
    }

    runtime = JS_GetRuntime(context);

    if (argv[0] == JSVAL_NULL) {
        JS_SetDebugErrorHook(runtime, NULL, NULL);
    } else {
        closure = gjs_closure_new(context,
                                  JSVAL_TO_OBJECT(argv[0]),
                                  "debugger DebugErrorHook");
        g_closure_ref(closure);
        g_closure_sink(closure);
        g_closure_add_invalidate_notifier(closure, closure,
                                          closure_invalidated);

        JS_SetDebugErrorHook(runtime, gjs_debugger_debug_error_hook, closure);
    }

  return JS_TRUE;
}

JSBool
gjs_define_debugger_stuff(JSContext *context,
                          JSObject  *module_obj)
{
    if (!JS_DefineFunction(context, module_obj,
                           "setDebugErrorHook",
                           gjs_debugger_set_debug_error_hook,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("debugger", gjs_define_debugger_stuff);
