/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2021 Evan Welsh

#include <config.h>

#include <stdint.h>
#include <string.h>  // for strcmp, memchr, strlen

#include <algorithm>
#include <vector>

#include <gio/gio.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/ArrayBuffer.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/GCAPI.h>  // for AutoCheckCannotGC
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>   // for UniqueChars
#include <jsapi.h>        // for JS_DefineFunctionById, JS_DefineFun...
#include <jsfriendapi.h>  // for JS_NewUint8ArrayWithBuffer, GetUint...
#include <mozilla/Unused.h>

#include "gi/boxed.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/text-encoding.h"

// Callback to use with JS::NewExternalArrayBuffer()

static void gfree_arraybuffer_contents(void* contents, void*) {
    g_free(contents);
}

static std::nullptr_t gjs_throw_type_error_from_gerror(JSContext* cx,
                                                       GError* error) {
    g_return_val_if_fail(error, nullptr);
    gjs_throw_custom(cx, JSProto_TypeError, nullptr, "%s", error->message);
    g_error_free(error);
    return nullptr;
}

// UTF16_CODESET is used to encode and decode UTF-16 buffers with
// iconv. To ensure the output of iconv is laid out in memory correctly
// we have to use UTF-16LE on little endian systems and UTF-16BE on big
// endian systems.
//
// This ensures we can simply reinterpret_cast<char16_t> iconv's output.
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static const char* UTF16_CODESET = "UTF-16LE";
#else
static const char* UTF16_CODESET = "UTF-16BE";
#endif

GJS_JSAPI_RETURN_CONVENTION
static JSString* gjs_decode_from_uint8array_slow(JSContext* cx, uint8_t* input,
                                                 uint32_t input_len,
                                                 const char* encoding) {
    size_t bytes_written, bytes_read;
    GError* error = nullptr;

    GjsAutoChar bytes =
        g_convert(reinterpret_cast<const char*>(input), input_len,
                  UTF16_CODESET, encoding, &bytes_read, &bytes_written, &error);

    if (error)
        return gjs_throw_type_error_from_gerror(cx, error);

    // bytes_written should be bytes in a UTF-16 string so should be a
    // multiple of 2
    g_assert((bytes_written % 2) == 0);

    // Cast g_convert's output to char16_t and copy the data.
    const char16_t* unicode_bytes = reinterpret_cast<char16_t*>(bytes.get());
    return JS_NewUCStringCopyN(cx, unicode_bytes, bytes_written / 2);
}

[[nodiscard]] static bool is_utf8_label(const char* encoding) {
    // We could be smarter about utf8 synonyms here.
    // For now, we handle any casing and trailing/leading
    // whitespace.
    //
    // is_utf8_label is only an optimization, so if a label
    // doesn't match we just use the slower path.
    if (g_ascii_strcasecmp(encoding, "utf-8") == 0 ||
        g_ascii_strcasecmp(encoding, "utf8") == 0)
        return true;

    GjsAutoChar stripped(g_strdup(encoding));
    return g_ascii_strcasecmp(g_strstrip(stripped), "utf-8") == 0 ||
           g_ascii_strcasecmp(stripped, "utf8") == 0;
}

// Finds the length of a given data array, stopping at the first 0 byte.
template <class T, class L>
[[nodiscard]] static L zero_terminated_length(const T* data, L len) {
    if (!data || len == 0)
        return 0;

    const T* start = data;
    auto* found = static_cast<const T*>(
        std::memchr(start, '\0', static_cast<size_t>(len)));

    // If a null byte was not found, return the passed length.
    if (!found)
        return len;

    return std::distance(start, found);
}

// decode() function implementation
JSString* gjs_decode_from_uint8array(JSContext* cx, JS::HandleObject byte_array,
                                     const char* encoding,
                                     GjsStringTermination string_termination) {
    g_assert(encoding && "encoding must be non-null");

    if (!JS_IsUint8Array(byte_array)) {
        gjs_throw(cx, "Argument to decode() must be a Uint8Array");
        return nullptr;
    }

    uint8_t* data;
    // len should be size_t but SpiderMonkey defines it differently in mozjs78
    uint32_t len;
    bool is_shared_memory;
    js::GetUint8ArrayLengthAndData(byte_array, &len, &is_shared_memory, &data);

    // If the desired behavior is zero-terminated, calculate the
    // zero-terminated length of the given data.
    if (len && string_termination == GjsStringTermination::ZERO_TERMINATED)
        len = zero_terminated_length(data, len);

    // If the calculated length is 0 we can just return an empty string.
    if (len == 0)
        return JS_GetEmptyString(cx);

    // Optimization, only use glib's iconv-based converters if we're dealing
    // with a non-UTF8 encoding. SpiderMonkey has highly optimized UTF-8 decoder
    // and encoders.
    bool encoding_is_utf8 = is_utf8_label(encoding);
    if (!encoding_is_utf8)
        return gjs_decode_from_uint8array_slow(cx, data, len, encoding);

    JS::RootedString decoded(cx);
    JS::UTF8Chars chars(reinterpret_cast<char*>(data), len);
    JS::RootedString str(cx, JS_NewStringCopyUTF8N(cx, chars));

    // If an exception occurred, we need to check if the
    // exception was an InternalError. Unfortunately,
    // SpiderMonkey's decoder can throw InternalError for some
    // invalid UTF-8 sources, we have to convert this into a
    // TypeError to match the Encoding specification.
    if (str) {
        decoded.set(str);
    } else {
        JS::RootedValue exc(cx);
        if (!JS_GetPendingException(cx, &exc) || !exc.isObject())
            return nullptr;

        JS::RootedObject exc_obj(cx, &exc.toObject());
        const JSClass* internal_error =
            js::ProtoKeyToClass(JSProto_InternalError);
        if (JS_InstanceOf(cx, exc_obj, internal_error, nullptr)) {
            // Clear the existing exception.
            JS_ClearPendingException(cx);
            gjs_throw_custom(cx, JSProto_TypeError, nullptr,
                             "The provided encoded data was not valid UTF-8");
        }

        return nullptr;
    }

    uint8_t* current_data;
    uint32_t current_len;
    bool ignore_val;

    // If a garbage collection occurs between when we call
    // js::GetUint8ArrayLengthAndData and return from
    // gjs_decode_from_uint8array, a use-after-free corruption can occur if the
    // garbage collector shifts the location of the Uint8Array's private data.
    // To mitigate this we call js::GetUint8ArrayLengthAndData again and then
    // compare if the length and pointer are still the same. If the pointers
    // differ, we use the slow path to ensure no data corruption occurred. The
    // shared-ness of an array cannot change between calls, so we ignore it.
    js::GetUint8ArrayLengthAndData(byte_array, &current_len, &ignore_val,
                                   &current_data);

    // Ensure the private data hasn't changed
    if (current_data == data)
        return decoded;

    g_assert(current_len == len &&
             "Garbage collection should not affect data length.");

    // This was the UTF-8 optimized path, so we explicitly pass the encoding
    return gjs_decode_from_uint8array_slow(cx, current_data, current_len,
                                           "UTF-8");
}

// encode() function implementation
JSObject* gjs_encode_to_uint8array(JSContext* cx, JS::HandleString str,
                                   const char* encoding,
                                   GjsStringTermination string_termination) {
    JS::RootedObject array_buffer(cx);

    bool encoding_is_utf8 = is_utf8_label(encoding);
    if (encoding_is_utf8) {
        JS::UniqueChars utf8;
        size_t utf8_len;

        if (!gjs_string_to_utf8_n(cx, str, &utf8, &utf8_len))
            return nullptr;

        if (string_termination == GjsStringTermination::ZERO_TERMINATED) {
            // strlen is safe because gjs_string_to_utf8_n returns
            // a null-terminated string.
            utf8_len = strlen(utf8.get());
        }

        array_buffer = JS::NewArrayBufferWithContents(cx, utf8_len, utf8.get());

        // array_buffer only assumes ownership if the call succeeded,
        // if array_buffer assumes ownership we must release our ownership
        // without freeing the data.
        if (array_buffer)
            mozilla::Unused << utf8.release();
    } else {
        GError* error = nullptr;
        GjsAutoChar encoded = nullptr;
        size_t bytes_written;

        /* Scope for AutoCheckCannotGC, will crash if a GC is triggered
         * while we are using the string's chars */
        {
            JS::AutoCheckCannotGC nogc;
            size_t len;

            if (JS_StringHasLatin1Chars(str)) {
                const JS::Latin1Char* chars =
                    JS_GetLatin1StringCharsAndLength(cx, nogc, str, &len);
                if (!chars)
                    return nullptr;

                encoded = g_convert(reinterpret_cast<const char*>(chars), len,
                                    encoding,  // to_encoding
                                    "LATIN1",  // from_encoding
                                    nullptr,   // bytes_read
                                    &bytes_written, &error);
            } else {
                const char16_t* chars =
                    JS_GetTwoByteStringCharsAndLength(cx, nogc, str, &len);
                if (!chars)
                    return nullptr;

                encoded =
                    g_convert(reinterpret_cast<const char*>(chars), len * 2,
                              encoding,  // to_encoding
                              "UTF-16",  // from_encoding
                              nullptr,   // bytes_read
                              &bytes_written, &error);
            }
        }

        if (!encoded)
            return gjs_throw_type_error_from_gerror(cx, error);  // frees GError

        if (string_termination == GjsStringTermination::ZERO_TERMINATED) {
            bytes_written =
                zero_terminated_length(encoded.get(), bytes_written);
        }

        if (bytes_written == 0)
            return JS_NewUint8Array(cx, 0);

        array_buffer =
            JS::NewExternalArrayBuffer(cx, bytes_written, encoded.release(),
                                       gfree_arraybuffer_contents, nullptr);
    }

    if (!array_buffer)
        return nullptr;

    return JS_NewUint8ArrayWithBuffer(cx, array_buffer, 0, -1);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_encode_into_uint8array(JSContext* cx, JS::HandleString str,
                                       JS::HandleObject uint8array,
                                       JS::MutableHandleValue rval) {
    if (!JS_IsUint8Array(uint8array)) {
        gjs_throw_custom(cx, JSProto_TypeError, nullptr,
                         "Argument to encodeInto() must be a Uint8Array");
        return false;
    }

    uint32_t len = JS_GetTypedArrayByteLength(uint8array);
    bool shared = JS_GetTypedArraySharedness(uint8array);

    if (shared) {
        gjs_throw(cx, "Cannot encode data into shared memory.");
        return false;
    }

    mozilla::Maybe<mozilla::Tuple<size_t, size_t>> results;

    {
        JS::AutoCheckCannotGC nogc(cx);
        uint8_t* data = JS_GetUint8ArrayData(uint8array, &shared, nogc);

        // We already checked for sharedness with JS_GetTypedArraySharedness
        g_assert(!shared);

        results = JS_EncodeStringToUTF8BufferPartial(
            cx, str, mozilla::AsWritableChars(mozilla::Span(data, len)));
    }

    if (!results) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    size_t read, written;
    mozilla::Tie(read, written) = *results;

    g_assert(written <= len);

    JS::RootedObject result(cx, JS_NewPlainObject(cx));
    if (!result)
        return false;

    JS::RootedValue v_read(cx, JS::NumberValue(read)),
        v_written(cx, JS::NumberValue(written));

    if (!JS_SetProperty(cx, result, "read", v_read) ||
        !JS_SetProperty(cx, result, "written", v_written))
        return false;

    rval.setObject(*result);
    return true;
}

static JSFunctionSpec gjs_text_encoding_module_funcs[] = {JS_FS_END};

bool gjs_define_text_encoding_stuff(JSContext* cx,
                                    JS::MutableHandleObject module) {
    JSObject* new_obj = JS_NewPlainObject(cx);
    if (!new_obj)
        return false;
    module.set(new_obj);

    return JS_DefineFunctions(cx, module, gjs_text_encoding_module_funcs);
}
