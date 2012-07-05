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

#include <config.h>

#include "jsapi-util.h"
#include "compat.h"
#include "gi/gerror.h"

#include <util/log.h>

#include <string.h>

/*
 * See:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=166436
 * https://bugzilla.mozilla.org/show_bug.cgi?id=215173
 *
 * Very surprisingly, jsapi.h lacks any way to "throw new Error()"
 *
 * So here is an awful hack inspired by
 * http://egachine.berlios.de/embedding-sm-best-practice/embedding-sm-best-practice.html#error-handling
 */
static void
gjs_throw_valist(JSContext       *context,
                 const char      *error_class,
                 const char      *format,
                 va_list          args)
{
    char *s;
    JSBool result;
    jsval v_constructor, v_message;
    JSObject *err_obj;

    s = g_strdup_vprintf(format, args);

    JS_BeginRequest(context);

    if (JS_IsExceptionPending(context)) {
        /* Often it's unclear whether a given jsapi.h function
         * will throw an exception, so we will throw ourselves
         * "just in case"; in those cases, we don't want to
         * overwrite an exception that already exists.
         * (Do log in case our second exception adds more info,
         * but don't log as topic ERROR because if the exception is
         * caught we don't want an ERROR in the logs.)
         */
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Ignoring second exception: '%s'",
                  s);
        g_free(s);
        JS_EndRequest(context);
        return;
    }

    result = JS_FALSE;

    (void)JS_EnterLocalRootScope(context);

    if (!gjs_string_from_utf8(context, s, -1, &v_message)) {
        JS_ReportError(context, "Failed to copy exception string");
        goto out;
    }

    if (!gjs_object_get_property(context, JS_GetGlobalObject(context),
                                 error_class, &v_constructor)) {
        JS_ReportError(context, "??? Missing Error constructor in global object?");
        goto out;
    }

    /* throw new Error(message) */
    err_obj = JS_New(context, JSVAL_TO_OBJECT(v_constructor), 1, &v_message);
    JS_SetPendingException(context, OBJECT_TO_JSVAL(err_obj));

    result = JS_TRUE;

 out:

    JS_LeaveLocalRootScope(context);

    if (!result) {
        /* try just reporting it to error handler? should not
         * happen though pretty much
         */
        JS_ReportError(context,
                       "Failed to throw exception '%s'",
                       s);
    }
    g_free(s);

    JS_EndRequest(context);
}

/* Throws an exception, like "throw new Error(message)"
 *
 * If an exception is already set in the context, this will
 * NOT overwrite it. That's an important semantic since
 * we want the "root cause" exception. To overwrite,
 * use JS_ClearPendingException() first.
 */
void
gjs_throw(JSContext       *context,
          const char      *format,
          ...)
{
    va_list args;

    va_start(args, format);
    gjs_throw_valist(context, "Error", format, args);
    va_end(args);
}

/*
 * Like gjs_throw, but allows to customize the error
 * class. Mainly used for throwing TypeError instead of
 * error.
 */
void
gjs_throw_custom(JSContext       *context,
                 const char      *error_class,
                 const char      *format,
                 ...)
{
    va_list args;

    va_start(args, format);
    gjs_throw_valist(context, error_class, format, args);
    va_end(args);
}

/**
 * gjs_throw_literal:
 *
 * Similar to gjs_throw(), but does not treat its argument as
 * a format string.
 */
void
gjs_throw_literal(JSContext       *context,
                  const char      *string)
{
    gjs_throw(context, "%s", string);
}

/**
 * gjs_throw_g_error:
 *
 * Convert a GError into a JavaScript Exception, and
 * frees the GError. Differently from gjs_throw(), it
 * will overwrite an existing exception, as it is used
 * to report errors from C functions.
 */
void
gjs_throw_g_error (JSContext       *context,
                   GError          *error)
{
    JSObject *err_obj;

    if (error == NULL)
        return;

    JS_BeginRequest(context);

    err_obj = gjs_error_from_gerror(context, error, TRUE);
    g_error_free (error);
    if (err_obj)
        JS_SetPendingException(context, OBJECT_TO_JSVAL(err_obj));

    JS_EndRequest(context);
}

#if GJS_BUILD_TESTS
#include "unit-test-utils.h"

void
gjstest_test_func_gjs_jsapi_util_error_throw(void)
{
    GjsUnitTestFixture fixture;
    JSContext *context;
    jsval exc, value, previous;
    char *s = NULL;
    int strcmp_result;

    _gjs_unit_test_fixture_begin(&fixture);
    context = fixture.context;

    /* Test that we can throw */

    gjs_throw(context, "This is an exception %d", 42);

    g_assert(JS_IsExceptionPending(context));

    exc = JSVAL_VOID;
    JS_GetPendingException(context, &exc);
    g_assert(!JSVAL_IS_VOID(exc));

    value = JSVAL_VOID;
    JS_GetProperty(context, JSVAL_TO_OBJECT(exc), "message",
                   &value);

    g_assert(JSVAL_IS_STRING(value));

    gjs_string_get_binary_data (context, value, &s, NULL);
    g_assert(s != NULL);
    strcmp_result = strcmp(s, "This is an exception 42");
    free(s);
    if (strcmp_result != 0)
        g_error("Exception has wrong message '%s'", s);

    /* keep this around before we clear it */
    previous = exc;
    JS_AddValueRoot(context, &previous);

    JS_ClearPendingException(context);

    g_assert(!JS_IsExceptionPending(context));

    /* Check that we don't overwrite a pending exception */
    JS_SetPendingException(context, previous);

    g_assert(JS_IsExceptionPending(context));

    gjs_throw(context, "Second different exception %s", "foo");

    g_assert(JS_IsExceptionPending(context));

    exc = JSVAL_VOID;
    JS_GetPendingException(context, &exc);
    g_assert(!JSVAL_IS_VOID(exc));
    g_assert(exc == previous);

    JS_RemoveValueRoot(context, &previous);

    _gjs_unit_test_fixture_finish(&fixture);
}

#endif /* GJS_BUILD_TESTS */
