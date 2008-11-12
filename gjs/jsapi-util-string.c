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

JSBool
gjs_string_to_utf8(JSContext  *context,
                   const jsval string_val,
                   char      **utf8_string_p)
{
    jschar *s;
    size_t s_length;
    char *utf8_string;
    GError *error;

    if (!JSVAL_IS_STRING(string_val)) {
        gjs_throw(context,
                     "Object is not a string, cannot convert to UTF-8");
        return JS_FALSE;
    }

    s = JS_GetStringChars(JSVAL_TO_STRING(string_val));
    s_length = JS_GetStringLength(JSVAL_TO_STRING(string_val));

    error = NULL;
    utf8_string = g_utf16_to_utf8(s,
                                  (glong)s_length,
                                  NULL, NULL,
                                  &error);

    if (!utf8_string) {
        gjs_throw(context,
                  "Failed to convert JS string to "
                  "UTF-8: %s",
                  error->message);
        g_error_free(error);
        return JS_FALSE;
    }

    *utf8_string_p = utf8_string;
    return JS_TRUE;
}

JSBool
gjs_string_from_utf8(JSContext  *context,
                     const char *utf8_string,
                     gsize       n_bytes,
                     jsval      *value_p)
{
    jschar *u16_string;
    glong u16_string_length;
    JSString *s;
    GError *error;

    /* intentionally using n_bytes even though glib api suggests n_chars; with
     * n_chars (from g_utf8_strlen()) the result appears truncated
     */

    error = NULL;
    u16_string = g_utf8_to_utf16(utf8_string,
                                 n_bytes,
                                 NULL,
                                 &u16_string_length,
                                 &error);

    if (!u16_string) {
        gjs_throw(context,
                     "Failed to convert UTF-8 string to "
                     "JS string: %s",
                     error->message);
        g_error_free(error);
        return JS_FALSE;
    }

    s = JS_NewUCStringCopyN(context,
                            (jschar*)u16_string,
                            u16_string_length);
    g_free(u16_string);

    if (!s)
        return JS_FALSE;

    *value_p = STRING_TO_JSVAL(s);
    return JS_TRUE;
}

JSBool
gjs_string_to_filename(JSContext    *context,
                       const jsval   filename_val,
                       char        **filename_string_p)
{
    GError *error;
    gchar *tmp, *filename_string;

    /* gjs_string_to_filename verifies that filename_val is a string */

    if (!gjs_string_to_utf8(context, filename_val, &tmp)) {
        /* exception already set */
        return JS_FALSE;
    }
    
    error = NULL;
    filename_string = g_filename_from_utf8(tmp, -1, NULL, NULL, &error);
    if (error) {
        gjs_throw(context,
                  "Could not convert filename '%s' to UTF8: '%s'",
                  tmp,
                  error->message);
        g_error_free(error);
        g_free(tmp);
        return JS_FALSE;
    }

    *filename_string_p = filename_string;
    
    g_free(tmp);
    return JS_TRUE;
}

JSBool
gjs_string_from_filename(JSContext  *context,
                         const char *filename_string,
                         gsize       n_bytes,
                         jsval      *value_p)
{
    gssize written;
    GError *error;
    gchar *utf8_string;

    error = NULL;
    utf8_string = g_filename_to_utf8(filename_string, n_bytes, NULL,
                                     &written, &error);
    if (error) {
        gjs_throw(context,
                  "Could not convert UTF-8 string '%s' to a filename: '%s'",
                  filename_string,
                  error->message);
        g_error_free(error);
        g_free(utf8_string);
        return JS_FALSE;
    }
    
    if (!gjs_string_from_utf8(context, utf8_string, written, value_p))
        return JS_FALSE;

    g_free(utf8_string);

    return JS_TRUE;
}


/**
 * gjs_string_get_ascii:
 * @value: a jsval
 *
 * Get the char array in the JSString contained in @value.
 * The string is expected to be encoded in ASCII, otherwise
 * you will get garbage out. See the documentation for
 * JS_GetStringBytes() for more details.
 *
 * Returns: an ASCII C string
 **/
const char*
gjs_string_get_ascii(jsval value)
{
    g_return_val_if_fail(JSVAL_IS_STRING(value), NULL);

    return JS_GetStringBytes(JSVAL_TO_STRING(value));
}

/**
 * gjs_string_get_ascii_checked:
 * @context: a JSContext
 * @value: a jsval
 *
 * If the given value is not a string, throw an exception and return %NULL.
 * Otherwise, return the ascii bytes of the string. If the string is not
 * ASCII, you will get corrupted garbage.
 *
 * Returns: an ASCII C string or %NULL on error
 **/
const char*
gjs_string_get_ascii_checked(JSContext       *context,
                             jsval            value)
{
    if (!JSVAL_IS_STRING(value)) {
        gjs_throw(context, "A string was expected, but value was not a string");
        return NULL;
    }

    return JS_GetStringBytes(JSVAL_TO_STRING(value));
}

/**
 * gjs_get_string_id:
 * @id_val: a jsval that is an object hash key (could be an int or string)
 * @name_p place to store ASCII string version of key
 *
 * If the id is not a string ID, return false and set *name_p to %NULL.
 * Otherwise, return true and fill in *name_p with ASCII name of id.
 *
 * Returns: true if *name_p is non-%NULL
 **/
JSBool
gjs_get_string_id (jsval            id_val,
                   const char     **name_p)
{
    if (JSVAL_IS_STRING(id_val)) {
        *name_p = JS_GetStringBytes(JSVAL_TO_STRING(id_val));
        return JS_TRUE;
    } else {
        *name_p = NULL;
        return JS_FALSE;
    }
}


#if GJS_BUILD_TESTS

static void
test_error_reporter(JSContext     *context,
                    const char    *message,
                    JSErrorReport *report)
{
    g_printerr("error reported by test: %s\n", message);
}

void
gjstest_test_func_gjs_jsapi_util_string_js_string_utf8(void)
{
    const char *utf8_string = "\303\211\303\226 foobar \343\203\237";
    char *utf8_result;
    JSRuntime *runtime;
    JSContext *context;
    JSObject *global;
    jsval js_string;

    runtime = JS_NewRuntime(1024*1024 /* max bytes */);
    context = JS_NewContext(runtime, 8192);
    global = JS_NewObject(context, NULL, NULL, NULL);
    JS_SetGlobalObject(context, global);
    JS_InitStandardClasses(context, global);

    JS_SetErrorReporter(context, test_error_reporter);

    g_assert(gjs_string_from_utf8(context, utf8_string, -1, &js_string) == JS_TRUE);
    g_assert(js_string);
    g_assert(JSVAL_IS_STRING(js_string));
    g_assert(gjs_string_to_utf8(context, js_string, &utf8_result) == JS_TRUE);

    JS_DestroyContext(context);
    JS_DestroyRuntime(runtime);

    g_assert(g_str_equal(utf8_string, utf8_result));

    g_free(utf8_result);
}

void
gjstest_test_func_gjs_jsapi_util_string_get_ascii(void)
{
    JSRuntime *runtime;
    JSContext *context;
    JSObject *global;
    const char *ascii_string = "Hello, world";
    JSString  *js_string;
    jsval      void_value;

    runtime = JS_NewRuntime(1024*1024 /* max bytes */);
    context = JS_NewContext(runtime, 8192);
    global = JS_NewObject(context, NULL, NULL, NULL);
    JS_SetGlobalObject(context, global);
    JS_InitStandardClasses(context, global);

    JS_SetErrorReporter(context, test_error_reporter);

    js_string = JS_NewStringCopyZ(context, ascii_string);
    g_assert(g_str_equal(gjs_string_get_ascii(STRING_TO_JSVAL(js_string)), ascii_string));
    void_value = JSVAL_VOID;
    g_assert(gjs_string_get_ascii_checked(context, void_value) == NULL);
    g_assert(JS_IsExceptionPending(context));

    JS_DestroyContext(context);
    JS_DestroyRuntime(runtime);
}

#endif /* GJS_BUILD_TESTS */
