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

#include <string.h>

#include "jsapi-util.h"
#include "compat.h"

gboolean
gjs_try_string_to_utf8 (JSContext  *context,
                        const jsval string_val,
                        char      **utf8_string_p,
                        GError    **error)
{
    const jschar *s;
    size_t s_length;
    char *utf8_string;
    long read_items;
    long utf8_length;
    GError *convert_error = NULL;

    JS_BeginRequest(context);

    if (!JSVAL_IS_STRING(string_val)) {
        g_set_error_literal(error, GJS_UTIL_ERROR, GJS_UTIL_ERROR_ARGUMENT_TYPE_MISMATCH,
                            "Object is not a string, cannot convert to UTF-8");
        JS_EndRequest(context);
        return FALSE;
    }

    s = JS_GetStringCharsAndLength(context, JSVAL_TO_STRING(string_val), &s_length);
    if (s == NULL) {
        JS_EndRequest(context);
        return FALSE;
    }

    utf8_string = g_utf16_to_utf8(s,
                                  (glong)s_length,
                                  &read_items, &utf8_length,
                                  &convert_error);

    /* ENDING REQUEST - no JSAPI after this point */
    JS_EndRequest(context);

    if (!utf8_string) {
        g_set_error(error, GJS_UTIL_ERROR, GJS_UTIL_ERROR_ARGUMENT_INVALID,
                    "Failed to convert JS string to UTF-8: %s",
                    convert_error->message);
        g_error_free(convert_error);
        return FALSE;
    }

    if ((size_t)read_items != s_length) {
        g_set_error_literal(error, GJS_UTIL_ERROR, GJS_UTIL_ERROR_ARGUMENT_INVALID,
                            "JS string contains embedded NULs");
        g_free(utf8_string);
        return FALSE;
    }

    /* Our assumption is that the string is being converted to UTF-8
     * in order to use with GLib-style APIs; Javascript has a looser
     * sense of validate-Unicode than GLib, so validate here to
     * prevent problems later on. Given the success of the above,
     * the only thing that could really be wrong here is including
     * non-characters like a byte-reversed BOM. If the validation
     * ever becomes a bottleneck, we could do an inline-special
     * case of all-ASCII.
     */
    if (!g_utf8_validate (utf8_string, utf8_length, NULL)) {
        g_set_error_literal(error, GJS_UTIL_ERROR, GJS_UTIL_ERROR_ARGUMENT_INVALID,
                            "JS string contains invalid Unicode characters");
        g_free(utf8_string);
        return FALSE;
    }

    *utf8_string_p = utf8_string;
    return TRUE;
}

JSBool
gjs_string_to_utf8 (JSContext  *context,
                    const jsval string_val,
                    char      **utf8_string_p)
{
  GError *error = NULL;

  if (!gjs_try_string_to_utf8(context, string_val, utf8_string_p, &error))
    {
      gjs_throw_g_error(context, error);
      return JS_FALSE;
    }
  return JS_TRUE;
}

JSBool
gjs_string_from_utf8(JSContext  *context,
                     const char *utf8_string,
                     gssize      n_bytes,
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

    JS_BeginRequest(context);

    s = JS_NewUCStringCopyN(context,
                            (jschar*)u16_string,
                            u16_string_length);
    g_free(u16_string);

    if (!s) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    *value_p = STRING_TO_JSVAL(s);
    JS_EndRequest(context);
    return JS_TRUE;
}

gboolean
gjs_try_string_to_filename(JSContext    *context,
                           const jsval   filename_val,
                           char        **filename_string_p,
                           GError      **error)
{
  gchar *tmp, *filename_string;

  /* gjs_string_to_filename verifies that filename_val is a string */

  if (!gjs_try_string_to_utf8(context, filename_val, &tmp, error)) {
      /* exception already set */
      return JS_FALSE;
  }

  error = NULL;
  filename_string = g_filename_from_utf8(tmp, -1, NULL, NULL, error);
  if (!filename_string) {
    g_free(tmp);
    return FALSE;
  }

  *filename_string_p = filename_string;

  g_free(tmp);
  return TRUE;
}

JSBool
gjs_string_to_filename(JSContext    *context,
                       const jsval   filename_val,
                       char        **filename_string_p)
{
  GError *error = NULL;

  if (!gjs_try_string_to_filename(context, filename_val, filename_string_p, &error)) {
      gjs_throw(context,
                "Could not convert filename to UTF8: '%s'",
                error->message);
      g_error_free(error);
      return JS_FALSE;
  }
  return JS_TRUE;
}

JSBool
gjs_string_from_filename(JSContext  *context,
                         const char *filename_string,
                         gssize      n_bytes,
                         jsval      *value_p)
{
    gsize written;
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
 * @context: a JSContext
 * @value: a jsval
 *
 * If the given value is not a string, throw an exception and return %NULL.
 * Otherwise, return the ascii bytes of the string. If the string is not
 * ASCII, you will get corrupted garbage.
 *
 * Returns: an ASCII C string or %NULL on error
 **/
char*
gjs_string_get_ascii(JSContext       *context,
                     jsval            value)
{
    char *ascii;

    if (!JSVAL_IS_STRING(value)) {
        gjs_throw(context, "A string was expected, but value was not a string");
        return NULL;
    }

    gjs_string_get_binary_data(context, value, &ascii, NULL);

    return ascii;
}

static JSBool
throw_if_binary_strings_broken(JSContext *context)
{
    /* JS_GetStringBytes() returns low byte of each jschar,
     * unless JS_CStringsAreUTF8() in which case we're screwed.
     */
    if (JS_CStringsAreUTF8()) {
        gjs_throw(context, "If JS_CStringsAreUTF8(), gjs doesn't know how to do binary strings");
        return JS_TRUE;
    }

    return JS_FALSE;
}

/**
 * gjs_string_get_binary_data:
 * @context: js context
 * @value: a jsval
 * @data_p: address to return allocated data buffer
 * @len_p: address to return length of data
 *
 * Get the binary data in the JSString contained in @value.
 * Throws a JS exception if value is not a string.
 *
 * Returns: JS_FALSE if exception thrown
 **/
JSBool
gjs_string_get_binary_data(JSContext       *context,
                           jsval            value,
                           char           **data_p,
                           gsize           *len_p)
{
    JSString *str;
    gsize len;
    char *bytes;

    JS_BeginRequest(context);

    if (!JSVAL_IS_STRING(value)) {
        gjs_throw(context,
                  "Value is not a string, can't return binary data from it");
        JS_EndRequest(context);
        return JS_FALSE;
    }

    if (throw_if_binary_strings_broken(context)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    str = JSVAL_TO_STRING(value);

    len = JS_GetStringEncodingLength(context, str);
    if (len == (gsize)(-1))
        return JS_FALSE;

    if (data_p) {
        bytes = g_malloc((len + 1) * sizeof(char));
        JS_EncodeStringToBuffer(str, bytes, len);
        bytes[len] = '\0';
        *data_p = bytes;
    }

    if (len_p)
        *len_p = len;

    JS_EndRequest(context);

    return JS_TRUE;
}

/**
 * gjs_string_from_binary_data:
 * @context: js context
 * @data: binary data buffer
 * @len: length of data
 * @value_p: a jsval location, should be GC rooted
 *
 * Gets a string representing the passed-in binary data.
 *
 * Returns: JS_FALSE if exception thrown
 **/
JSBool
gjs_string_from_binary_data(JSContext       *context,
                            const char      *data,
                            gsize            len,
                            jsval           *value_p)
{
    JSString *s;

    JS_BeginRequest(context);

    if (throw_if_binary_strings_broken(context)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    /* store each byte in a 16-bit jschar so all high bytes are 0;
     * we can't put two bytes per jschar because then we'd lose
     * track of the string length if it was an odd number.
     */
    s = JS_NewStringCopyN(context, data, len);
    if (s == NULL) {
        /* gjs_throw() does nothing if exception already set */
        gjs_throw(context, "Failed to allocate binary string");
        JS_EndRequest(context);
        return JS_FALSE;
    }
    *value_p = STRING_TO_JSVAL(s);

    JS_EndRequest(context);
    return JS_TRUE;
}

/**
 * gjs_string_get_uint16_data:
 * @context: js context
 * @value: a jsval
 * @data_p: address to return allocated data buffer
 * @len_p: address to return length of data (number of 16-bit integers)
 *
 * Get the binary data (as a sequence of 16-bit integers) in the JSString
 * contained in @value.
 * Throws a JS exception if value is not a string.
 *
 * Returns: JS_FALSE if exception thrown
 **/
JSBool
gjs_string_get_uint16_data(JSContext       *context,
                           jsval            value,
                           guint16        **data_p,
                           gsize           *len_p)
{
    const jschar *js_data;
    JSBool retval = JS_FALSE;

    JS_BeginRequest(context);

    if (!JSVAL_IS_STRING(value)) {
        gjs_throw(context,
                  "Value is not a string, can't return binary data from it");
        goto out;
    }

    js_data = JS_GetStringCharsAndLength(context, JSVAL_TO_STRING(value), len_p);
    if (js_data == NULL)
        goto out;

    *data_p = g_memdup(js_data, sizeof(*js_data)*(*len_p));

    retval = JS_TRUE;
out:
    JS_EndRequest(context);
    return retval;
}

/**
 * gjs_get_string_id:
 * @context: a #JSContext
 * @id: a jsid that is an object hash key (could be an int or string)
 * @name_p place to store ASCII string version of key
 *
 * If the id is not a string ID, return false and set *name_p to %NULL.
 * Otherwise, return true and fill in *name_p with ASCII name of id.
 *
 * Returns: true if *name_p is non-%NULL
 **/
JSBool
gjs_get_string_id (JSContext       *context,
                   jsid             id,
                   char           **name_p)
{
    jsval id_val;

    if (!JS_IdToValue(context, id, &id_val))
        return JS_FALSE;

    if (JSVAL_IS_STRING(id_val)) {
        gjs_string_get_binary_data(context, id_val, name_p, NULL);
        return JS_TRUE;
    } else {
        *name_p = NULL;
        return JS_FALSE;
    }
}

/**
 * gjs_unichar_from_string:
 * @string: A string
 * @result: (out): A unicode character
 *
 * If successful, @result is assigned the Unicode codepoint
 * corresponding to the first full character in @string.  This
 * function handles characters outside the BMP.
 *
 * If @string is empty, @result will be 0.  An exception will
 * be thrown if @string can not be represented as UTF-8.
 */
gboolean
gjs_unichar_from_string (JSContext *context,
                         jsval      value,
                         gunichar  *result)
{
    char *utf8_str;
    if (gjs_string_to_utf8(context, value, &utf8_str)) {
        *result = g_utf8_get_char(utf8_str);
        g_free(utf8_str);
        return TRUE;
    }
    return FALSE;
}

#if GJS_BUILD_TESTS
#include "unit-test-utils.h"
#include <string.h>


void
gjstest_test_func_gjs_jsapi_util_string_js_string_utf8(void)
{
    GjsUnitTestFixture fixture;
    JSContext *context;
    const char *utf8_string = "\303\211\303\226 foobar \343\203\237";
    char *utf8_result;
    jsval js_string;

    _gjs_unit_test_fixture_begin(&fixture);
    context = fixture.context;

    g_assert(gjs_string_from_utf8(context, utf8_string, -1, &js_string) == JS_TRUE);
    g_assert(js_string);
    g_assert(JSVAL_IS_STRING(js_string));
    g_assert(gjs_string_to_utf8(context, js_string, &utf8_result) == JS_TRUE);

    _gjs_unit_test_fixture_finish(&fixture);

    g_assert(g_str_equal(utf8_string, utf8_result));

    g_free(utf8_result);
}

void
gjstest_test_func_gjs_jsapi_util_string_get_ascii(void)
{
    GjsUnitTestFixture fixture;
    JSContext *context;
    const char *ascii_string = "Hello, world";
    JSString  *js_string;
    jsval      void_value;
    char *test;

    _gjs_unit_test_fixture_begin(&fixture);
    context = fixture.context;

    js_string = JS_NewStringCopyZ(context, ascii_string);
    test = gjs_string_get_ascii(context, STRING_TO_JSVAL(js_string));
    g_assert(g_str_equal(test, ascii_string));
    g_free(test);
    void_value = JSVAL_VOID;
    test = gjs_string_get_ascii(context, void_value);
    g_assert(test == NULL);
    g_free(test);
    g_assert(JS_IsExceptionPending(context));

    _gjs_unit_test_fixture_finish(&fixture);
}

void
gjstest_test_func_gjs_jsapi_util_string_get_binary(void)
{
    GjsUnitTestFixture fixture;
    JSContext *context;
    const char binary_string[] = "foo\0bar\0baz";
    const char binary_string_odd[] = "foo\0bar\0baz123";
    jsval js_string;
    jsval void_value;
    char *data;
    gsize len;

    g_assert_cmpuint(sizeof(binary_string), ==, 12);
    g_assert_cmpuint(sizeof(binary_string_odd), ==, 15);

    _gjs_unit_test_fixture_begin(&fixture);
    context = fixture.context;

    js_string = JSVAL_VOID;
    JS_AddValueRoot(context, &js_string);

    /* Even-length string (maps nicely to len/2 jschar) */
    if (!gjs_string_from_binary_data(context,
                                     binary_string, sizeof(binary_string),
                                     &js_string))
        g_error("Failed to create binary data string");

    if (!gjs_string_get_binary_data(context,
                                    js_string,
                                    &data, &len))
        g_error("Failed to get binary data from string");
    g_assert_cmpuint(len, ==, sizeof(binary_string));
    g_assert(memcmp(data, binary_string, len) == 0);
    g_free(data);


    /* Odd-length string (does not map nicely to len/2 jschar) */
    if (!gjs_string_from_binary_data(context,
                                     binary_string_odd, sizeof(binary_string_odd),
                                     &js_string))
        g_error("Failed to create odd-length binary data string");

    if (!gjs_string_get_binary_data(context,
                                    js_string,
                                    &data, &len))
        g_error("Failed to get binary data from string");
    g_assert_cmpuint(len, ==, sizeof(binary_string_odd));
    g_assert(memcmp(data, binary_string_odd, len) == 0);
    g_free(data);

    JS_RemoveValueRoot(context, &js_string);

    void_value = JSVAL_VOID;
    g_assert(!JS_IsExceptionPending(context));
    g_assert(!gjs_string_get_binary_data(context, void_value,
                                        &data, &len));
    g_assert(JS_IsExceptionPending(context));

    _gjs_unit_test_fixture_finish(&fixture);
}

#endif /* GJS_BUILD_TESTS */
