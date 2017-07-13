/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <algorithm>
#include <string.h>

#include "jsapi-util.h"
#include "jsapi-wrapper.h"

bool
gjs_string_to_utf8 (JSContext      *context,
                    const JS::Value value,
                    GjsAutoJSChar  *utf8_string_p)
{
    JS_BeginRequest(context);

    if (!value.isString()) {
        gjs_throw(context,
                  "Value is not a string, cannot convert to UTF-8");
        JS_EndRequest(context);
        return false;
    }

    JS::RootedString str(context, value.toString());
    utf8_string_p->reset(context, JS_EncodeStringToUTF8(context, str));

    JS_EndRequest(context);

    return true;
}

bool
gjs_string_from_utf8(JSContext             *context,
                     const char            *utf8_string,
                     ssize_t                n_bytes,
                     JS::MutableHandleValue value_p)
{
    char16_t *u16_string;
    glong u16_string_length;
    GError *error;

    /* intentionally using n_bytes even though glib api suggests n_chars; with
    * n_chars (from g_utf8_strlen()) the result appears truncated
    */

    error = NULL;
    u16_string =
        reinterpret_cast<char16_t *>(g_utf8_to_utf16(utf8_string, n_bytes, NULL,
                                                     &u16_string_length, &error));
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

bool
gjs_string_to_filename(JSContext      *context,
                       const JS::Value filename_val,
                       GjsAutoChar    *filename_string)
{
    GError *error;
    GjsAutoJSChar tmp(context);

    /* gjs_string_to_filename verifies that filename_val is a string */

    if (!gjs_string_to_utf8(context, filename_val, &tmp)) {
        /* exception already set */
        return false;
    }

    error = NULL;
    *filename_string = g_filename_from_utf8(tmp, -1, NULL, NULL, &error);
    if (!*filename_string) {
        gjs_throw_g_error(context, error);
        return false;
    }

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

/* Converts a JSString's array of Latin-1 chars to an array of a wider integer
 * type, by what the compiler believes is the most efficient method possible */
template<typename T>
static bool
from_latin1(JSContext *cx,
            JSString  *str,
            T        **data_p,
            size_t    *len_p)
{
    /* No garbage collection should be triggered while we are using the string's
     * chars. Crash if that happens. */
    JS::AutoCheckCannotGC nogc;

    const JS::Latin1Char *js_data =
        JS_GetLatin1StringCharsAndLength(cx, nogc, str, len_p);
    if (js_data == NULL)
        return false;

    /* Unicode codepoints 0x00-0xFF are the same as Latin-1
     * codepoints, so we can preserve the string length and simply
     * copy the codepoints to an array of different-sized ints */

    *data_p = g_new(T, *len_p);

    /* This will probably use a loop, unfortunately */
    std::copy(js_data, js_data + *len_p, *data_p);
    return true;
}

/**
 * gjs_string_get_char16_data:
 * @context: js context
 * @value: a JS::Value
 * @data_p: address to return allocated data buffer
 * @len_p: address to return length of data (number of 16-bit characters)
 *
 * Get the binary data (as a sequence of 16-bit characters) in the JSString
 * contained in @value.
 * Throws a JS exception if value is not a string.
 *
 * Returns: false if exception thrown
 **/
bool
gjs_string_get_char16_data(JSContext       *context,
                           JS::Value        value,
                           char16_t       **data_p,
                           size_t          *len_p)
{
    JSAutoRequest ar(context);

    if (!value.isString()) {
        gjs_throw(context,
                  "Value is not a string, can't return binary data from it");
        return false;
    }

    if (JS_StringHasLatin1Chars(value.toString()))
        return from_latin1(context, value.toString(), data_p, len_p);

    /* From this point on, crash if a GC is triggered while we are using
     * the string's chars */
    JS::AutoCheckCannotGC nogc;

    const char16_t *js_data =
        JS_GetTwoByteStringCharsAndLength(context, nogc,
                                          value.toString(), len_p);

    if (js_data == NULL)
        return false;

    *data_p = (char16_t *) g_memdup(js_data, sizeof(*js_data) * (*len_p));

    return true;
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
    size_t len;
    GError *error = NULL;

    if (JS_StringHasLatin1Chars(str))
        return from_latin1(cx, value.toString(), ucs4_string_p, len_p);

    /* From this point on, crash if a GC is triggered while we are using
     * the string's chars */
    JS::AutoCheckCannotGC nogc;

    const char16_t *utf16 =
        JS_GetTwoByteStringCharsAndLength(cx, nogc, str, &len);

    if (utf16 == NULL) {
        gjs_throw(cx, "Failed to get UTF-16 string data");
        return false;
    }

    if (ucs4_string_p != NULL) {
        long length;
        *ucs4_string_p = g_utf16_to_ucs4(reinterpret_cast<const gunichar2 *>(utf16),
                                         len, NULL, &length, &error);
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
    long u16_string_length;
    GError *error = NULL;

    char16_t *u16_string =
        reinterpret_cast<char16_t *>(g_ucs4_to_utf16(ucs4_string, n_chars, NULL,
                                                     &u16_string_length, &error));
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
                   GjsAutoJSChar   *name_p)
{
    JS::RootedValue id_val(context);

    if (!JS_IdToValue(context, id, &id_val))
        return false;

    if (id_val.isString()) {
        return gjs_string_to_utf8(context, id_val, name_p);
    } else {
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
    GjsAutoJSChar utf8_str(context);
    if (gjs_string_to_utf8(context, value, &utf8_str)) {
        *result = g_utf8_get_char(utf8_str);
        return true;
    }
    return false;
}

jsid
gjs_intern_string_to_id(JSContext  *cx,
                        const char *string)
{
    JSAutoRequest ar(cx);
    JS::RootedString str(cx, JS_AtomizeAndPinString(cx, string));
    JS::RootedId id(cx, INTERNED_STRING_TO_JSID(cx, str));
    return id;
}
