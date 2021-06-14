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

#include "gi/boxed.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/text-encoding.h"

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
static bool to_string_impl_slow(JSContext* cx, uint8_t* data, uint32_t len,
                                const char* encoding,
                                JS::MutableHandleValue rval) {
    size_t bytes_written;
    GError* error = nullptr;
    GjsAutoChar u16_str =
        g_convert(reinterpret_cast<char*>(data), len, UTF16_CODESET, encoding,
                  /* bytes_read = */ nullptr, &bytes_written, &error);
    if (!u16_str)
        return gjs_throw_gerror_message(cx, error);  // frees GError

    // bytes_written should be bytes in a UTF-16 string so should be a multiple
    // of 2
    g_assert((bytes_written % 2) == 0);

    // g_convert 0-terminates the string, although the 0 isn't included in
    // bytes_written
    JSString* s =
        JS_NewUCStringCopyZ(cx, reinterpret_cast<char16_t*>(u16_str.get()));
    if (!s)
        return false;

    rval.setString(s);
    return true;
}

// implement ByteArray.toString() with an optional encoding arg
bool bytearray_to_string(JSContext* context, JS::HandleObject byte_array,
                         const char* encoding, JS::MutableHandleValue rval) {
    if (!JS_IsUint8Array(byte_array)) {
        gjs_throw(context,
                  "Argument to ByteArray.toString() must be a Uint8Array");
        return false;
    }

    bool encoding_is_utf8;
    uint8_t* data;

    if (encoding) {
        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        encoding_is_utf8 = (strcmp(encoding, "UTF-8") == 0);
    } else {
        encoding_is_utf8 = true;
    }

    uint32_t len;
    bool is_shared_memory;
    js::GetUint8ArrayLengthAndData(byte_array, &len, &is_shared_memory, &data);

    if (len == 0) {
        rval.setString(JS_GetEmptyString(context));
        return true;
    }

    if (!encoding_is_utf8)
        return to_string_impl_slow(context, data, len, encoding, rval);

    // optimization, avoids iconv overhead and runs libmozjs hardwired
    // utf8-to-utf16

    // If there are any 0 bytes, including the terminating byte, stop at the
    // first one
    if (data[len - 1] == 0 || memchr(data, 0, len)) {
        if (!gjs_string_from_utf8(context, reinterpret_cast<char*>(data), rval))
            return false;
    } else {
        if (!gjs_string_from_utf8_n(context, reinterpret_cast<char*>(data), len,
                                    rval))
            return false;
    }

    uint8_t* current_data;
    uint32_t current_len;
    bool ignore_val;

    // If a garbage collection occurs between when we call
    // js::GetUint8ArrayLengthAndData and return from gjs_string_from_utf8, a
    // use-after-free corruption can occur if the garbage collector shifts the
    // location of the Uint8Array's private data. To mitigate this we call
    // js::GetUint8ArrayLengthAndData again and then compare if the length and
    // pointer are still the same. If the pointers differ, we use the slow path
    // to ensure no data corruption occurred. The shared-ness of an array cannot
    // change between calls, so we ignore it.
    js::GetUint8ArrayLengthAndData(byte_array, &current_len, &ignore_val,
                                   &current_data);

    // Ensure the private data hasn't changed
    if (current_len == len && current_data == data)
        return true;

    // This was the UTF-8 optimized path, so we explicitly pass the encoding
    return to_string_impl_slow(context, current_data, current_len, "UTF-8",
                               rval);
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
