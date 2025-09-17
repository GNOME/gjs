/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>
#include <string.h>     // for size_t, strlen
#include <sys/types.h>  // for ssize_t

#include <algorithm>  // for copy
#include <iomanip>    // for operator<<, setfill, setw
#include <sstream>    // for operator<<, basic_ostream, ostring...
#include <string>     // for allocator, char_traits

#include <glib.h>

#include <js/BigInt.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/ErrorReport.h>
#include <js/GCAPI.h>  // for AutoCheckCannotGC
#include <js/Id.h>
#include <js/Object.h>  // for GetClass
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>        // for JS_GetFunctionDisplayId
#include <jsfriendapi.h>  // for IdToValue, IsFunctionObject, ...
#include <mozilla/CheckedInt.h>
#include <mozilla/Span.h>

#include "gjs/auto.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

class JSLinearString;

Gjs::AutoChar gjs_hyphen_to_underscore(const char* str) {
    char *s = g_strdup(str);
    char *retval = s;
    while (*(s++) != '\0') {
        if (*s == '-')
            *s = '_';
    }
    return retval;
}

Gjs::AutoChar gjs_hyphen_to_camel(const char* str) {
    Gjs::AutoChar retval{static_cast<char*>(g_malloc(strlen(str) + 1))};
    const char* input_iter = str;
    char* output_iter = retval.get();
    bool uppercase_next = false;
    while (*input_iter != '\0') {
        if (*input_iter == '-') {
            uppercase_next = true;
        } else if (uppercase_next) {
            *output_iter++ = g_ascii_toupper(*input_iter);
            uppercase_next = false;
        } else {
            *output_iter++ = *input_iter;
        }
        input_iter++;
    }
    *output_iter = '\0';
    return retval;
}

/**
 * gjs_string_to_utf8:
 * @cx: JSContext
 * @value: a JS::Value containing a string
 *
 * Converts the JSString in @value to UTF-8 and puts it in @utf8_string_p.
 *
 * This function is a convenience wrapper around JS_EncodeStringToUTF8() that
 * typechecks the JS::Value and throws an exception if it's the wrong type.
 * Don't use this function if you already have a JS::RootedString, or if you
 * know the value already holds a string; use JS_EncodeStringToUTF8() instead.
 *
 * Returns: Unique UTF8 chars, empty on exception throw.
 */
JS::UniqueChars gjs_string_to_utf8(JSContext* cx, const JS::Value value) {
    if (!value.isString()) {
        gjs_throw(cx, "Value is not a string, cannot convert to UTF-8");
        return nullptr;
    }

    JS::RootedString str(cx, value.toString());
    return JS_EncodeStringToUTF8(cx, str);
}

/**
 * gjs_string_to_utf8_n:
 * @cx: the current #JSContext
 * @str: string to encode in UTF-8
 * @output: (out): return location for the UTF-8-encoded string
 * @output_len: (out): return location for the length of @output
 *
 * Converts a JSString to UTF-8 and puts the char array in @output and its
 * length in @output_len.
 *
 * This function handles the boilerplate for unpacking @str, determining its
 * length, and returning the appropriate JS::UniqueChars. This function should
 * generally be preferred over using JS::DeflateStringToUTF8Buffer() directly as
 * it correctly handles allocation in a JS_free compatible manner.
 *
 * Returns: false if an exception is pending, otherwise true.
 */
bool gjs_string_to_utf8_n(JSContext* cx, JS::HandleString str, JS::UniqueChars* output,
                          size_t* output_len) {
    JSLinearString* linear = JS_EnsureLinearString(cx, str);
    if (!linear)
        return false;

    size_t length = JS::GetDeflatedUTF8StringLength(linear);
    char* bytes = js_pod_malloc<char>(length + 1);
    if (!bytes)
        return false;

    // Append a zero-terminator to the string.
    bytes[length] = '\0';

    size_t deflated_length [[maybe_unused]] =
        JS::DeflateStringToUTF8Buffer(linear, mozilla::Span(bytes, length));
    g_assert(deflated_length == length);

    *output_len = length;
    *output = JS::UniqueChars(bytes);
    return true;
}

/**
 * gjs_lossy_string_from_utf8:
 * @cx: the current #JSContext
 * @utf8_string: a zero-terminated array of UTF-8 characters to decode
 *
 * Converts @utf8_string to a JS string. Instead of throwing, any invalid
 * characters will be converted to the UTF-8 invalid character fallback.
 *
 * Returns: The decoded string.
 */
JSString* gjs_lossy_string_from_utf8(JSContext* cx, const char* utf8_string) {
    JS::UTF8Chars chars{utf8_string, strlen(utf8_string)};
    size_t outlen;
    JS::UniqueTwoByteChars twobyte_chars(
        JS::LossyUTF8CharsToNewTwoByteCharsZ(cx, chars, &outlen,
                                             js::MallocArena)
            .get());
    if (!twobyte_chars)
        return nullptr;

    return JS_NewUCStringCopyN(cx, twobyte_chars.get(), outlen);
}

/**
 * gjs_lossy_string_from_utf8_n:
 * @cx: the current #JSContext
 * @utf8_string: an array of UTF-8 characters to decode
 * @len: length of @utf8_string
 *
 * Provides the same conversion behavior as gjs_lossy_string_from_utf8
 * with a fixed length. See gjs_lossy_string_from_utf8().
 *
 * Returns: The decoded string.
 */
JSString* gjs_lossy_string_from_utf8_n(JSContext* cx, const char* utf8_string,
                                       size_t len) {
    JS::UTF8Chars chars(utf8_string, len);
    size_t outlen;
    JS::UniqueTwoByteChars twobyte_chars(
        JS::LossyUTF8CharsToNewTwoByteCharsZ(cx, chars, &outlen,
                                             js::MallocArena)
            .get());
    if (!twobyte_chars)
        return nullptr;

    return JS_NewUCStringCopyN(cx, twobyte_chars.get(), outlen);
}

bool
gjs_string_from_utf8(JSContext             *context,
                     const char            *utf8_string,
                     JS::MutableHandleValue value_p)
{
    JS::ConstUTF8CharsZ chars(utf8_string, strlen(utf8_string));
    JS::RootedString str(context, JS_NewStringCopyUTF8Z(context, chars));
    if (!str)
        return false;

    value_p.setString(str);
    return true;
}

bool
gjs_string_from_utf8_n(JSContext             *cx,
                       const char            *utf8_chars,
                       size_t                 len,
                       JS::MutableHandleValue out)
{
    JS::UTF8Chars chars(utf8_chars, len);
    JS::RootedString str(cx, JS_NewStringCopyUTF8N(cx, chars));
    if (str)
        out.setString(str);

    return !!str;
}

bool gjs_string_to_filename(JSContext* context, const JS::Value filename_val,
                            Gjs::AutoChar* filename_string) {
    Gjs::AutoError error;

    /* gjs_string_to_filename verifies that filename_val is a string */

    JS::UniqueChars tmp = gjs_string_to_utf8(context, filename_val);
    if (!tmp)
        return false;

    error = NULL;
    *filename_string =
        g_filename_from_utf8(tmp.get(), -1, nullptr, nullptr, &error);
    if (!*filename_string)
        return gjs_throw_gerror_message(context, error);

    return true;
}

bool
gjs_string_from_filename(JSContext             *context,
                         const char            *filename_string,
                         ssize_t                n_bytes,
                         JS::MutableHandleValue value_p)
{
    gsize written;
    Gjs::AutoError error;

    error = NULL;
    Gjs::AutoChar utf8_string{g_filename_to_utf8(filename_string, n_bytes,
                                                 nullptr, &written, &error)};
    if (error) {
        Gjs::AutoChar escaped_char{g_strescape(filename_string, nullptr)};
        gjs_throw(context,
                  "Could not convert filename string to UTF-8 for string: %s. "
                  "If string is "
                  "invalid UTF-8 and used for display purposes, try GLib "
                  "attribute standard::display-name. The reason is: %s. ",
                  escaped_char.get(), error->message);
        return false;
    }

    return gjs_string_from_utf8_n(context, utf8_string, written, value_p);
}

/* Converts a JSString's array of Latin-1 chars to an array of a wider integer
 * type, by what the compiler believes is the most efficient method possible */
template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool from_latin1(JSContext* cx,
                                                    JSString* str, T** data_p,
                                                    size_t* len_p) {
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
 * @str: a rooted JSString
 * @data_p: address to return allocated data buffer
 * @len_p: address to return length of data (number of 16-bit characters)
 *
 * Get the binary data (as a sequence of 16-bit characters) in @str.
 *
 * Returns: false if exception thrown
 **/
bool
gjs_string_get_char16_data(JSContext       *context,
                           JS::HandleString str,
                           char16_t       **data_p,
                           size_t          *len_p)
{
    if (JS::StringHasLatin1Chars(str))
        return from_latin1(context, str, data_p, len_p);

    /* From this point on, crash if a GC is triggered while we are using
     * the string's chars */
    JS::AutoCheckCannotGC nogc;

    const char16_t *js_data =
        JS_GetTwoByteStringCharsAndLength(context, nogc, str, len_p);

    if (js_data == NULL)
        return false;

    mozilla::CheckedInt<size_t> len_bytes =
        mozilla::CheckedInt<size_t>(*len_p) * sizeof(*js_data);
    if (!len_bytes.isValid()) {
        JS_ReportOutOfMemory(context);  // cannot call gjs_throw, it may GC
        return false;
    }

    *data_p = static_cast<char16_t*>(g_memdup2(js_data, len_bytes.value()));

    return true;
}

/**
 * gjs_string_to_ucs4:
 * @cx: a #JSContext
 * @str: rooted JSString
 * @ucs4_string_p: return location for a #gunichar array
 * @len_p: return location for @ucs4_string_p length
 *
 * Returns: true on success, false otherwise in which case a JS error is thrown
 */
bool
gjs_string_to_ucs4(JSContext       *cx,
                   JS::HandleString str,
                   gunichar       **ucs4_string_p,
                   size_t          *len_p)
{
    if (ucs4_string_p == NULL)
        return true;

    size_t len;
    Gjs::AutoError error;

    if (JS::StringHasLatin1Chars(str))
        return from_latin1(cx, str, ucs4_string_p, len_p);

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
    // a null array pointer takes precedence over whatever `n_chars` says
    if (!ucs4_string) {
        value_p.setString(JS_GetEmptyString(cx));
        return true;
    }

    long u16_string_length;
    Gjs::AutoError error;

    gunichar2* u16_string = g_ucs4_to_utf16(ucs4_string, n_chars, nullptr,
                                            &u16_string_length, &error);
    if (!u16_string) {
        gjs_throw(cx, "Failed to convert UCS-4 string to UTF-16: %s",
                  error->message);
        return false;
    }

    // Sadly, must copy, because js::UniquePtr forces that chars passed to
    // JS_NewUCString() must have been allocated by the JS engine.
    JS::RootedString str(
        cx, JS_NewUCStringCopyN(cx, reinterpret_cast<char16_t*>(u16_string),
                                u16_string_length));

    g_free(u16_string);

    if (!str) {
        gjs_throw(cx, "Failed to convert UCS-4 string to UTF-16");
        return false;
    }

    value_p.setString(str);
    return true;
}

/**
 * gjs_get_string_id:
 * @cx: a #JSContext
 * @id: a jsid that is an object hash key (could be an int or string)
 * @name_p place to store ASCII string version of key
 *
 * If the id is not a string ID, return true and set *name_p to nullptr.
 * Otherwise, return true and fill in *name_p with ASCII name of id.
 *
 * Returns: false on error, otherwise true
 **/
bool gjs_get_string_id(JSContext* cx, jsid id, JS::UniqueChars* name_p) {
    if (!id.isString()) {
        name_p->reset();
        return true;
    }

    JSLinearString* lstr = id.toLinearString();
    JS::RootedString s(cx, JS_FORGET_STRING_LINEARNESS(lstr));
    *name_p = JS_EncodeStringToUTF8(cx, s);
    return !!*name_p;
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
    JS::UniqueChars utf8_str = gjs_string_to_utf8(context, value);
    if (utf8_str) {
        *result = g_utf8_get_char(utf8_str.get());
        return true;
    }
    return false;
}

jsid
gjs_intern_string_to_id(JSContext  *cx,
                        const char *string)
{
    JS::RootedString str(cx, JS_AtomizeAndPinString(cx, string));
    if (!str)
        return JS::PropertyKey::Void();
    return JS::PropertyKey::fromPinnedString(str);
}

std::string gjs_debug_bigint(JS::BigInt* bi) {
    // technically this prints the value % INT64_MAX, cast into an int64_t if
    // the value is negative, otherwise cast into uint64_t
    std::ostringstream out;
    if (JS::BigIntIsNegative(bi))
        out << JS::ToBigInt64(bi);
    else
        out << JS::ToBigUint64(bi);
    out << "n (modulo 2^64)";
    return out.str();
}

enum Quotes {
    DoubleQuotes,
    NoQuotes,
};

[[nodiscard]] static std::string gjs_debug_linear_string(JSLinearString* str,
                                                         Quotes quotes) {
    size_t len = JS::GetLinearStringLength(str);

    std::ostringstream out;
    if (quotes == DoubleQuotes)
        out << '"';

    JS::AutoCheckCannotGC nogc;
    if (JS::LinearStringHasLatin1Chars(str)) {
        const JS::Latin1Char* chars = JS::GetLatin1LinearStringChars(nogc, str);
        out << std::string(reinterpret_cast<const char*>(chars), len);
        if (quotes == DoubleQuotes)
            out << '"';
        return out.str();
    }

    const char16_t* chars = JS::GetTwoByteLinearStringChars(nogc, str);
    for (size_t ix = 0; ix < len; ix++) {
        char16_t c = chars[ix];
        if (c == '\n')
            out << "\\n";
        else if (c == '\t')
            out << "\\t";
        else if (c >= 32 && c < 127)
            out << c;
        else if (c <= 255)
            out << "\\x" << std::setfill('0') << std::setw(2) << unsigned(c);
        else
            out << "\\x" << std::setfill('0') << std::setw(4) << unsigned(c);
    }
    if (quotes == DoubleQuotes)
        out << '"';
    return out.str();
}

std::string
gjs_debug_string(JSString *str)
{
    if (!str)
        return "<null string>";
    if (!JS_StringIsLinear(str)) {
        std::ostringstream out("<non-flat string of length ",
                               std::ios_base::ate);
        out << JS_GetStringLength(str) << '>';
        return out.str();
    }
    return gjs_debug_linear_string(JS_ASSERT_STRING_IS_LINEAR(str),
                                   DoubleQuotes);
}

std::string
gjs_debug_symbol(JS::Symbol * const sym)
{
    if (!sym)
        return "<null symbol>";

    /* This is OK because JS::GetSymbolCode() and JS::GetSymbolDescription()
     * can't cause a garbage collection */
    JS::HandleSymbol handle = JS::HandleSymbol::fromMarkedLocation(&sym);
    JS::SymbolCode code = JS::GetSymbolCode(handle);
    JSString *descr = JS::GetSymbolDescription(handle);

    if (size_t(code) < JS::WellKnownSymbolLimit)
        return gjs_debug_string(descr);

    std::ostringstream out;
    if (code == JS::SymbolCode::InSymbolRegistry) {
        out << "Symbol.for(";
        if (descr)
            out << gjs_debug_string(descr);
        else
            out << "undefined";
        out << ")";
        return out.str();
    }
    if (code == JS::SymbolCode::UniqueSymbol) {
        if (descr)
            out << "Symbol(" << gjs_debug_string(descr) << ")";
        else
            out << "<Symbol at " << sym << ">";
        return out.str();
    }

    out << "<unexpected symbol code " << uint32_t(code) << ">";
    return out.str();
}

std::string
gjs_debug_object(JSObject * const obj)
{
    if (!obj)
        return "<null object>";

    std::ostringstream out;

    if (js::IsFunctionObject(obj)) {
        JSFunction* fun = JS_GetObjectFunction(obj);
        JSString* display_name = JS_GetMaybePartialFunctionDisplayId(fun);
        if (display_name && JS_GetStringLength(display_name))
            out << "<function " << gjs_debug_string(display_name);
        else
            out << "<anonymous function";
        out << " at " << fun << '>';
        return out.str();
    }

    // This is OK because the promise methods can't cause a garbage collection
    JS::HandleObject handle = JS::HandleObject::fromMarkedLocation(&obj);
    if (JS::IsPromiseObject(handle)) {
        out << '<';
        JS::PromiseState state = JS::GetPromiseState(handle);
        if (state == JS::PromiseState::Pending)
            out << "pending ";
        out << "promise " << JS::GetPromiseID(handle) << " at " << obj;
        if (state != JS::PromiseState::Pending) {
            out << ' ';
            out << (state == JS::PromiseState::Rejected ? "rejected"
                                                        : "resolved");
            out << " with " << gjs_debug_value(JS::GetPromiseResult(handle));
        }
        out << '>';
        return out.str();
    }

    const JSClass* clasp = JS::GetClass(obj);
    out << "<object " << clasp->name << " at " << obj <<  '>';
    return out.str();
}

std::string gjs_debug_callable(JSObject* callable) {
    if (JSFunction* fn = JS_GetObjectFunction(callable)) {
        if (JSString* display_id = JS_GetMaybePartialFunctionDisplayId(fn))
            return {"function " + gjs_debug_string(display_id)};
        return {"unnamed function"};
    }
    return {"callable object " + gjs_debug_object(callable)};
}

std::string
gjs_debug_value(JS::Value v)
{
    if (v.isNull())
        return "null";
    if (v.isUndefined())
        return "undefined";
    if (v.isInt32()) {
        std::ostringstream out;
        out << v.toInt32();
        return out.str();
    }
    if (v.isDouble()) {
        std::ostringstream out;
        out << v.toDouble();
        return out.str();
    }
    if (v.isBigInt())
        return gjs_debug_bigint(v.toBigInt());
    if (v.isString())
        return gjs_debug_string(v.toString());
    if (v.isSymbol())
        return gjs_debug_symbol(v.toSymbol());
    if (v.isObject())
        return gjs_debug_object(&v.toObject());
    if (v.isBoolean())
        return (v.toBoolean() ? "true" : "false");
    if (v.isMagic())
        return "<magic>";
    return "unexpected value";
}

std::string
gjs_debug_id(jsid id)
{
    if (id.isString())
        return gjs_debug_linear_string(id.toLinearString(), NoQuotes);
    return gjs_debug_value(js::IdToValue(id));
}
