/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include "compat.h"

/* Implementations of locale-specific operations; these are used
 * in the implementation of String.localeCompare(), Date.toLocaleDateString(),
 * and so forth. We take the straight-forward approach of converting
 * to UTF-8, using the appropriate GLib functions, and converting
 * back if necessary.
 */
static JSBool
gjs_locale_to_upper_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *upper_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    upper_case_utf8 = g_utf8_strup (utf8, -1);

    if (!gjs_string_from_utf8(context, upper_case_utf8, -1, retval.address()))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(upper_case_utf8);

    return success;
}

static JSBool
gjs_locale_to_lower_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *lower_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    lower_case_utf8 = g_utf8_strdown (utf8, -1);

    if (!gjs_string_from_utf8(context, lower_case_utf8, -1, retval.address()))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(lower_case_utf8);

    return success;
}

static JSBool
gjs_locale_compare (JSContext *context,
                    JS::HandleString src_1,
                    JS::HandleString src_2,
                    JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8_1 = NULL, *utf8_2 = NULL;
    int result;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src_1), &utf8_1) ||
        !gjs_string_to_utf8(context, STRING_TO_JSVAL(src_2), &utf8_2))
        goto out;

    result = g_utf8_collate (utf8_1, utf8_2);
    retval.set(INT_TO_JSVAL(result));

    success = JS_TRUE;

out:
    g_free(utf8_1);
    g_free(utf8_2);

    return success;
}

static JSBool
gjs_locale_to_unicode (JSContext  *context,
                       const char *src,
                       JS::MutableHandleValue retval)
{
    JSBool success;
    char *utf8;
    GError *error = NULL;

    utf8 = g_locale_to_utf8(src, -1, NULL, NULL, &error);
    if (!utf8) {
        gjs_throw(context,
                  "Failed to convert locale string to UTF8: %s",
                  error->message);
        g_error_free(error);
        return JS_FALSE;
    }

    success = gjs_string_from_utf8(context, utf8, -1, retval.address());
    g_free (utf8);

    return success;
}

static void
destroy_runtime(gpointer data)
{
    JSRuntime *runtime = (JSRuntime *) data;
    JS_DestroyRuntime(runtime);
}

static GPrivate thread_runtime = G_PRIVATE_INIT(destroy_runtime);

static JSLocaleCallbacks gjs_locale_callbacks =
{
    gjs_locale_to_upper_case,
    gjs_locale_to_lower_case,
    gjs_locale_compare,
    gjs_locale_to_unicode
};

JSRuntime *
gjs_runtime_for_current_thread(void)
{
    JSRuntime *runtime = (JSRuntime *) g_private_get(&thread_runtime);

    if (!runtime) {
        runtime = JS_NewRuntime(32*1024*1024 /* max bytes */, JS_USE_HELPER_THREADS);
        if (runtime == NULL)
            g_error("Failed to create javascript runtime");

        JS_SetNativeStackQuota(runtime, 1024*1024);
        JS_SetGCParameter(runtime, JSGC_MAX_BYTES, 0xffffffff);
        JS_SetLocaleCallbacks(runtime, &gjs_locale_callbacks);

        g_private_set(&thread_runtime, runtime);
    }

    return runtime;
}
