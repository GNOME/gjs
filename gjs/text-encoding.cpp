/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2021 Evan Welsh

#include <config.h>

#include <limits.h>  // for SSIZE_MAX
#include <stdint.h>
#include <string.h>  // for strcmp, memchr, strlen

#include <algorithm>
#include <cstddef>  // for nullptr_t, size_t
#include <iterator>  // for distance
#include <memory>    // for unique_ptr
#include <string>    // for u16string
#include <tuple>     // for tuple
#include <utility>   // for move

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <js/ArrayBuffer.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory, JSEXN_TYPEERR
#include <js/Exception.h>    // for JS_ClearPendingException, JS_...
#include <js/GCAPI.h>  // for AutoCheckCannotGC
#include <js/PropertyAndElement.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/experimental/TypedData.h>
#include <jsapi.h>        // for JS_NewPlainObject, JS_InstanceOf
#include <jsfriendapi.h>  // for ProtoKeyToClass
#include <jspubtd.h>      // for JSProto_InternalError
#include <mozilla/Maybe.h>
#include <mozilla/Span.h>
#include <mozilla/UniquePtr.h>

#include "gjs/auto.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/text-encoding.h"

// Callback to use with JS::NewExternalArrayBuffer()

static void gfree_arraybuffer_contents(void* contents, void*) {
    g_free(contents);
}

static std::nullptr_t gjs_throw_type_error_from_gerror(
    JSContext* cx, Gjs::AutoError const& error) {
    g_return_val_if_fail(error, nullptr);
    gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr, "%s", error->message);
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
static JSString* gjs_lossy_decode_from_uint8array_slow(
    JSContext* cx, const uint8_t* bytes, size_t bytes_len,
    const char* from_codeset) {
    Gjs::AutoError error;
    Gjs::AutoUnref<GCharsetConverter> converter{
        g_charset_converter_new(UTF16_CODESET, from_codeset, &error)};

    // This should only throw if an encoding is not available.
    if (error)
        return gjs_throw_type_error_from_gerror(cx, error);

    // This function converts *to* UTF-16, using a std::u16string
    // as its buffer.
    //
    // UTF-16 represents each character with 2 bytes or
    // 4 bytes, the best case scenario when converting to
    // UTF-16 is that every input byte encodes to two bytes,
    // this is typical for ASCII and non-supplementary characters.
    // Because we are converting from an unknown encoding
    // technically a single byte could be supplementary in
    // Unicode (4 bytes) or even represent multiple Unicode characters.
    //
    // std::u16string does not care about these implementation
    // details, its only concern is that is consists of byte pairs.
    // Given this, a single UTF-16 character could be represented
    // by one or two std::u16string characters.

    // Allocate bytes_len * 2 + 12 as our initial buffer.
    // bytes_len * 2 is the "best case" for LATIN1 strings
    // and strings which are in the basic multilingual plane.
    // Add 12 as a slight cushion and set the minimum allocation
    // at 256 to prefer running a single iteration for
    // small strings with supplemental plane characters.
    //
    // When converting Chinese characters, for example,
    // some dialectal characters are in the supplemental plane
    // Adding a padding of 12 prevents a few dialectal characters
    // from requiring a reallocation.
    size_t buffer_size = std::max(bytes_len * 2 + 12, static_cast<size_t>(256u));

    // Cast data to correct input types
    const char* input = reinterpret_cast<const char*>(bytes);
    size_t input_len = bytes_len;

    // The base string that we'll append to.
    std::u16string output_str = u"";

    do {
        Gjs::AutoError local_error;

        // Create a buffer to convert into.
        std::unique_ptr<char[]> buffer = std::make_unique<char[]>(buffer_size);
        size_t bytes_written = 0, bytes_read = 0;

        g_converter_convert(G_CONVERTER(converter.get()), input, input_len,
                            buffer.get(), buffer_size, G_CONVERTER_INPUT_AT_END,
                            &bytes_read, &bytes_written, &local_error);

        // If bytes were read, adjust input.
        if (bytes_read > 0) {
            input += bytes_read;
            input_len -= bytes_read;
        }

        // If bytes were written append the buffer contents to our string
        // accumulator
        if (bytes_written > 0) {
            char16_t* utf16_buffer = reinterpret_cast<char16_t*>(buffer.get());
            // std::u16string uses exactly 2 bytes for every character.
            output_str.append(utf16_buffer, bytes_written / 2);
        } else if (local_error) {
            // A PARTIAL_INPUT error can only occur if the user does not provide
            // the full sequence for a multi-byte character, we skip over the
            // next character and insert a unicode fallback.

            // An INVALID_DATA error occurs when there is no way to decode a
            // given byte into UTF-16 or the given byte does not exist in the
            // source encoding.
            if (g_error_matches(local_error, G_IO_ERROR,
                                G_IO_ERROR_INVALID_DATA) ||
                g_error_matches(local_error, G_IO_ERROR,
                                G_IO_ERROR_PARTIAL_INPUT)) {
                // If we're already at the end of the string, don't insert a
                // fallback.
                if (input_len > 0) {
                    // Skip the next byte and reduce length by one.
                    input += 1;
                    input_len -= 1;

                    // Append the unicode fallback character to the output
                    output_str.append(u"\ufffd", 1);
                }
            } else if (g_error_matches(local_error, G_IO_ERROR,
                                       G_IO_ERROR_NO_SPACE)) {
                // If the buffer was full increase the buffer
                // size and re-try the conversion.
                //
                // This logic allocates bytes_len * 3 first,
                // then bytes_len * 4 (the worst case scenario
                // is nearly impossible) and then continues appending
                // arbitrary padding because we'll trust Gio and give
                // it additional space.
                if (buffer_size > bytes_len * 4) {
                    buffer_size += 256;
                } else {
                    buffer_size += bytes_len;
                }
            } else {
                // Stop decoding if an unknown error occurs.
                return gjs_throw_type_error_from_gerror(cx, local_error);
            }
        }
    } while (input_len > 0);

    // Copy the accumulator's data into a JSString of Unicode (UTF-16) chars.
    return JS_NewUCStringCopyN(cx, output_str.c_str(), output_str.size());
}

GJS_JSAPI_RETURN_CONVENTION
static JSString* gjs_decode_from_uint8array_slow(JSContext* cx,
                                                 const uint8_t* input,
                                                 size_t input_len,
                                                 const char* encoding,
                                                 bool fatal) {
    // If the decoding is not fatal we use the lossy decoder.
    if (!fatal)
        return gjs_lossy_decode_from_uint8array_slow(cx, input, input_len,
                                                     encoding);

    // g_convert only handles up to SSIZE_MAX bytes, but we may have SIZE_MAX
    if (G_UNLIKELY(input_len > SSIZE_MAX)) {
        gjs_throw(cx, "Array too big to decode: %zu bytes", input_len);
        return nullptr;
    }

    size_t bytes_written, bytes_read;
    Gjs::AutoError error;

    Gjs::AutoChar bytes{g_convert(reinterpret_cast<const char*>(input),
                                  input_len, UTF16_CODESET, encoding,
                                  &bytes_read, &bytes_written, &error)};

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

    Gjs::AutoChar stripped{g_strdup(encoding)};
    g_strstrip(stripped);  // modifies in place
    return g_ascii_strcasecmp(stripped, "utf-8") == 0 ||
           g_ascii_strcasecmp(stripped, "utf8") == 0;
}

// Finds the length of a given data array, stopping at the first 0 byte.
template <class T>
[[nodiscard]] static size_t zero_terminated_length(const T* data, size_t len) {
    if (!data || len == 0)
        return 0;

    const T* start = data;
    auto* found = static_cast<const T*>(memchr(start, '\0', len));

    // If a null byte was not found, return the passed length.
    if (!found)
        return len;

    return std::distance(start, found);
}

// decode() function implementation
JSString* gjs_decode_from_uint8array(JSContext* cx, JS::HandleObject byte_array,
                                     const char* encoding,
                                     GjsStringTermination string_termination,
                                     bool fatal) {
    g_assert(encoding && "encoding must be non-null");

    if (!JS_IsUint8Array(byte_array)) {
        gjs_throw(cx, "Argument to decode() must be a Uint8Array");
        return nullptr;
    }

    uint8_t* data;
    size_t len;
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
        return gjs_decode_from_uint8array_slow(cx, data, len, encoding, fatal);

    JS::RootedString decoded(cx);
    if (!fatal) {
        decoded.set(gjs_lossy_string_from_utf8_n(
            cx, reinterpret_cast<char*>(data), len));
    } else {
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
                gjs_throw_custom(
                    cx, JSEXN_TYPEERR, nullptr,
                    "The provided encoded data was not valid UTF-8");
            }

            return nullptr;
        }
    }

    uint8_t* current_data;
    size_t current_len;
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
                                           "utf-8", fatal);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_decode(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject byte_array(cx);
    JS::UniqueChars encoding;
    bool fatal = false;
    if (!gjs_parse_call_args(cx, "decode", args, "os|b", "byteArray",
                             &byte_array, "encoding", &encoding, "fatal",
                             &fatal))
        return false;

    JS::RootedString decoded(
        cx, gjs_decode_from_uint8array(cx, byte_array, encoding.get(),
                                       GjsStringTermination::EXPLICIT_LENGTH,
                                       fatal));
    if (!decoded)
        return false;

    args.rval().setString(decoded);
    return true;
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

        array_buffer =
            JS::NewArrayBufferWithContents(cx, utf8_len, std::move(utf8));
    } else {
        Gjs::AutoError error;
        Gjs::AutoChar encoded;
        size_t bytes_written;

        /* Scope for AutoCheckCannotGC, will crash if a GC is triggered
         * while we are using the string's chars */
        {
            JS::AutoCheckCannotGC nogc;
            size_t len;

            if (JS::StringHasLatin1Chars(str)) {
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

        mozilla::UniquePtr<void, JS::BufferContentsDeleter> contents{
            encoded.release(), gfree_arraybuffer_contents};
        array_buffer =
            JS::NewExternalArrayBuffer(cx, bytes_written, std::move(contents));
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
        gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                         "Argument to encodeInto() must be a Uint8Array");
        return false;
    }

    uint32_t len = JS_GetTypedArrayByteLength(uint8array);
    bool shared = JS_GetTypedArraySharedness(uint8array);

    if (shared) {
        gjs_throw(cx, "Cannot encode data into shared memory.");
        return false;
    }

    mozilla::Maybe<std::tuple<size_t, size_t>> results;

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
    std::tie(read, written) = *results;

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

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_encode(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedString str(cx);
    JS::UniqueChars encoding;
    if (!gjs_parse_call_args(cx, "encode", args, "Ss", "string", &str,
                             "encoding", &encoding))
        return false;

    JS::RootedObject uint8array(
        cx, gjs_encode_to_uint8array(cx, str, encoding.get(),
                                     GjsStringTermination::EXPLICIT_LENGTH));
    if (!uint8array)
        return false;

    args.rval().setObject(*uint8array);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_encode_into(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedString str(cx);
    JS::RootedObject uint8array(cx);
    if (!gjs_parse_call_args(cx, "encodeInto", args, "So", "string", &str,
                             "byteArray", &uint8array))
        return false;

    return gjs_encode_into_uint8array(cx, str, uint8array, args.rval());
}

static JSFunctionSpec gjs_text_encoding_module_funcs[] = {
    JS_FN("decode", gjs_decode, 3, 0),
    JS_FN("encodeInto", gjs_encode_into, 2, 0),
    JS_FN("encode", gjs_encode, 2, 0), JS_FS_END};

bool gjs_define_text_encoding_stuff(JSContext* cx,
                                    JS::MutableHandleObject module) {
    JSObject* new_obj = JS_NewPlainObject(cx);
    if (!new_obj)
        return false;
    module.set(new_obj);

    return JS_DefineFunctions(cx, module, gjs_text_encoding_module_funcs);
}
