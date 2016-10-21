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
#include "jsapi-wrapper.h"

JSBool
gjs_string_to_utf8 (JSContext      *context,
                    const JS::Value value,
                    char          **utf8_string_p)
{
    JSString *str;
    gsize len;
    char *bytes;

    JS_BeginRequest(context);

    if (!value.isString()) {
        gjs_throw(context,
                  "Value is not a string, cannot convert to UTF-8");
        JS_EndRequest(context);
        return false;
    }

    str = value.toString();

    len = JS_GetStringEncodingLength(context, str);
    if (len == (gsize)(-1)) {
        JS_EndRequest(context);
        return false;
    }

    if (utf8_string_p) {
        bytes = JS_EncodeStringToUTF8(context, str);
        *utf8_string_p = bytes;
    }

    JS_EndRequest(context);

    return true;
}

bool
gjs_string_from_utf8(JSContext             *context,
                     const char            *utf8_string,
                     ssize_t                n_bytes,
                     JS::MutableHandleValue value_p)
{
    jschar *u16_string;
    glong u16_string_length;
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
        return false;
    }

    JS_BeginRequest(context);

    /* Avoid a copy - assumes that g_malloc == js_malloc == malloc */
    JS::RootedString str(context,
                         JS_NewUCString(context, u16_string, u16_string_length));
    if (str)
        value_p.setString(str);

    JS_EndRequest(context);
    return str != NULL;
}

JSBool
gjs_string_to_filename(JSContext      *context,
                       const JS::Value filename_val,
                       char          **filename_string_p)
{
    GError *error;
    gchar *tmp, *filename_string;

    /* gjs_string_to_filename verifies that filename_val is a string */

    if (!gjs_string_to_utf8(context, filename_val, &tmp)) {
        /* exception already set */
        return false;
    }

    error = NULL;
    filename_string = g_filename_from_utf8(tmp, -1, NULL, NULL, &error);
    if (!filename_string) {
        gjs_throw_g_error(context, error);
        g_free(tmp);
        return false;
    }

    *filename_string_p = filename_string;
    g_free(tmp);
    return true;
}

bool
gjs_string_from_filename(JSContext             *context,
                         const char            *filename_string,
                         ssize_t                n_bytes,
                         JS::MutableHandleValue value_p)
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
        return false;
    }

    if (!gjs_string_from_utf8(context, utf8_string, written, value_p))
        return false;

    g_free(utf8_string);

    return true;
}

/**
 * gjs_string_get_uint16_data:
 * @context: js context
 * @value: a JS::Value
 * @data_p: address to return allocated data buffer
 * @len_p: address to return length of data (number of 16-bit integers)
 *
 * Get the binary data (as a sequence of 16-bit integers) in the JSString
 * contained in @value.
 * Throws a JS exception if value is not a string.
 *
 * Returns: false if exception thrown
 **/
bool
gjs_string_get_uint16_data(JSContext       *context,
                           JS::Value        value,
                           guint16        **data_p,
                           gsize           *len_p)
{
    const jschar *js_data;
    bool retval = false;

    JS_BeginRequest(context);

    if (!value.isString()) {
        gjs_throw(context,
                  "Value is not a string, can't return binary data from it");
        goto out;
    }

    js_data = JS_GetStringCharsAndLength(context, value.toString(), len_p);
    if (js_data == NULL)
        goto out;

    *data_p = (guint16*) g_memdup(js_data, sizeof(*js_data)*(*len_p));

    retval = true;
out:
    JS_EndRequest(context);
    return retval;
}

/**
 * gjs_string_to_ucs4:
 * @cx: a #JSContext
 * @value: JS::Value containing a string
 * @ucs4_string_p: return location for a #gunichar array
 * @len_p: return location for @ucs4_string_p length
 *
 * Returns: true on success, false otherwise in which case a JS error is thrown
 */
bool
gjs_string_to_ucs4(JSContext      *cx,
                   JS::HandleValue value,
                   gunichar      **ucs4_string_p,
                   size_t         *len_p)
{
    if (ucs4_string_p == NULL)
        return true;

    if (!value.isString()) {
        gjs_throw(cx, "Value is not a string, cannot convert to UCS-4");
        return false;
    }

    JSAutoRequest ar(cx);
    JS::RootedString str(cx, value.toString());
    size_t utf16_len;
    GError *error = NULL;

    const jschar *utf16 = JS_GetStringCharsAndLength(cx, str, &utf16_len);
    if (utf16 == NULL) {
        gjs_throw(cx, "Failed to get UTF-16 string data");
        return false;
    }

    if (ucs4_string_p != NULL) {
        long length;
        *ucs4_string_p = g_utf16_to_ucs4(utf16, utf16_len, NULL, &length, &error);
        if (*ucs4_string_p == NULL) {
            gjs_throw(cx, "Failed to convert UTF-16 string to UCS-4: %s",
                      error->message);
            g_clear_error(&error);
            return false;
        }
        if (len_p != NULL)
            *len_p = (size_t) length;
    }

    return true;
}

/**
 * gjs_string_from_ucs4:
 * @cx: a #JSContext
 * @ucs4_string: string of #gunichar
 * @n_chars: number of characters in @ucs4_string or -1 for zero-terminated
 * @value_p: JS::Value that will be filled with a string
 *
 * Returns: true on success, false otherwise in which case a JS error is thrown
 */
bool
gjs_string_from_ucs4(JSContext             *cx,
                     const gunichar        *ucs4_string,
                     ssize_t                n_chars,
                     JS::MutableHandleValue value_p)
{
    uint16_t *u16_string;
    long u16_string_length;
    GError *error = NULL;

    u16_string = g_ucs4_to_utf16(ucs4_string, n_chars, NULL,
                                 &u16_string_length, &error);
    if (!u16_string) {
        gjs_throw(cx, "Failed to convert UCS-4 string to UTF-16: %s",
                  error->message);
        g_error_free(error);
        return false;
    }

    JSAutoRequest ar(cx);
    /* Avoid a copy - assumes that g_malloc == js_malloc == malloc */
    JS::RootedString str(cx, JS_NewUCString(cx, u16_string, u16_string_length));

    if (str == NULL) {
        gjs_throw(cx, "Failed to convert UCS-4 string to UTF-16");
        return false;
    }

    value_p.setString(str);
    return true;
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
bool
gjs_get_string_id (JSContext       *context,
                   jsid             id,
                   char           **name_p)
{
    JS::Value id_val;

    if (!JS_IdToValue(context, id, &id_val))
        return false;

    if (id_val.isString()) {
        return gjs_string_to_utf8(context, id_val, name_p);
    } else {
        *name_p = NULL;
        return false;
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
bool
gjs_unichar_from_string (JSContext *context,
                         JS::Value  value,
                         gunichar  *result)
{
    char *utf8_str;
    if (gjs_string_to_utf8(context, value, &utf8_str)) {
        *result = g_utf8_get_char(utf8_str);
        g_free(utf8_str);
        return true;
    }
    return false;
}

jsid
gjs_intern_string_to_id (JSContext  *context,
                         const char *string)
{
    JSString *str;
    jsid id;
    JS_BeginRequest(context);
    str = JS_InternString(context, string);
    id = INTERNED_STRING_TO_JSID(context, str);
    JS_EndRequest(context);
    return id;
}
