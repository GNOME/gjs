/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#include <config.h>

#include <stdint.h>
#include <string.h>  // for strcmp, memchr, strlen

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/ArrayBuffer.h>
#include <js/CallArgs.h>
#include <js/GCAPI.h>  // for AutoCheckCannotGC
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>   // for UniqueChars
#include <jsapi.h>        // for JS_DefineFunctionById, JS_DefineFun...
#include <jsfriendapi.h>  // for JS_NewUint8ArrayWithBuffer, GetUint...

#include "gi/boxed.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "util/misc.h"  // for _gjs_memdup2

/* Callbacks to use with JS::NewExternalArrayBuffer() */

static void gfree_arraybuffer_contents(void* contents, void*) {
    g_free(contents);
}

static void bytes_unref_arraybuffer(void* contents [[maybe_unused]],
                                    void* user_data) {
    auto* gbytes = static_cast<GBytes*>(user_data);
    g_bytes_unref(gbytes);
}

GJS_JSAPI_RETURN_CONVENTION
bool to_string_impl_slow(JSContext* cx, uint8_t* data, uint32_t len,
                         const char* encoding, JS::MutableHandleValue rval) {
    size_t bytes_written;
    GError* error = nullptr;
    GjsAutoChar u16_str = g_convert(reinterpret_cast<char*>(data), len,
    // Make sure the bytes of the UTF-16 string are laid out in memory
    // such that we can simply reinterpret_cast<char16_t> them.
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                    "UTF-16LE",
#else
                                    "UTF-16BE",
#endif
                                    encoding, nullptr, /* bytes read */
                                    &bytes_written, &error);
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

/* implement toString() with an optional encoding arg */
GJS_JSAPI_RETURN_CONVENTION
static bool to_string_impl(JSContext* context, JS::HandleObject byte_array,
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

GJS_JSAPI_RETURN_CONVENTION
static bool to_string_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars encoding;
    JS::RootedObject byte_array(cx);

    if (!gjs_parse_call_args(cx, "toString", args, "o|s", "byteArray",
                             &byte_array, "encoding", &encoding))
        return false;

    return to_string_impl(cx, byte_array, encoding.get(), args.rval());
}

/* Workaround to keep existing code compatible. This function is tacked onto
 * any Uint8Array instances created in situations where previously a ByteArray
 * would have been created. It logs a compatibility warning. */
GJS_JSAPI_RETURN_CONVENTION
static bool instance_to_string_func(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, this_obj);
    JS::UniqueChars encoding;

    _gjs_warn_deprecated_once_per_callsite(
        cx, GjsDeprecationMessageId::ByteArrayInstanceToString);

    if (!gjs_parse_call_args(cx, "toString", args, "|s", "encoding", &encoding))
        return false;

    return to_string_impl(cx, this_obj, encoding.get(), args.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool
to_gbytes_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs rec = JS::CallArgsFromVp(argc, vp);
    GIBaseInfo *gbytes_info;
    JS::RootedObject byte_array(context);

    if (!gjs_parse_call_args(context, "toGBytes", rec, "o",
                             "byteArray", &byte_array))
        return false;

    if (!JS_IsUint8Array(byte_array)) {
        gjs_throw(context,
                  "Argument to ByteArray.toGBytes() must be a Uint8Array");
        return false;
    }

    GBytes* bytes = gjs_byte_array_get_bytes(byte_array);

    g_irepository_require(nullptr, "GLib", "2.0", GIRepositoryLoadFlags(0),
                          nullptr);
    gbytes_info = g_irepository_find_by_gtype(NULL, G_TYPE_BYTES);
    JSObject* ret_bytes_obj =
        BoxedInstance::new_for_c_struct(context, gbytes_info, bytes);
    g_bytes_unref(bytes);
    if (!ret_bytes_obj)
        return false;

    rec.rval().setObject(*ret_bytes_obj);
    return true;
}

/* fromString() function implementation */
GJS_JSAPI_RETURN_CONVENTION
static bool
from_string_func(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JS::UniqueChars encoding;
    JS::UniqueChars utf8;
    bool encoding_is_utf8;
    JS::RootedObject obj(context), array_buffer(context);

    if (!gjs_parse_call_args(context, "fromString", argv, "s|s",
                             "string", &utf8,
                             "encoding", &encoding))
        return false;

    if (argc > 1) {
        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        encoding_is_utf8 = (strcmp(encoding.get(), "UTF-8") == 0);
    } else {
        encoding_is_utf8 = true;
    }

    if (encoding_is_utf8) {
        /* optimization? avoids iconv overhead and runs
         * libmozjs hardwired utf16-to-utf8.
         */
        size_t len = strlen(utf8.get());
        array_buffer =
            JS::NewArrayBufferWithContents(context, len, utf8.release());
    } else {
        JSString *str = argv[0].toString();  /* Rooted by argv */
        GError *error = NULL;
        char *encoded = NULL;
        gsize bytes_written;

        /* Scope for AutoCheckCannotGC, will crash if a GC is triggered
         * while we are using the string's chars */
        {
            JS::AutoCheckCannotGC nogc;
            size_t len;

            if (JS_StringHasLatin1Chars(str)) {
                const JS::Latin1Char *chars =
                    JS_GetLatin1StringCharsAndLength(context, nogc, str, &len);
                if (chars == NULL)
                    return false;

                encoded = g_convert((char *) chars, len,
                                    encoding.get(),  // to_encoding
                                    "LATIN1",  /* from_encoding */
                                    NULL,  /* bytes read */
                                    &bytes_written, &error);
            } else {
                const char16_t *chars =
                    JS_GetTwoByteStringCharsAndLength(context, nogc, str, &len);
                if (chars == NULL)
                    return false;

                encoded = g_convert((char *) chars, len * 2,
                                    encoding.get(),  // to_encoding
                                    "UTF-16",  /* from_encoding */
                                    NULL,  /* bytes read */
                                    &bytes_written, &error);
            }
        }

        if (!encoded)
            return gjs_throw_gerror_message(context, error);  // frees GError

        array_buffer =
            JS::NewExternalArrayBuffer(context, bytes_written, encoded,
                                       gfree_arraybuffer_contents, nullptr);
    }

    if (!array_buffer)
        return false;
    obj = JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1);

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (!JS_DefineFunctionById(context, obj, atoms.to_string(),
                               instance_to_string_func, 1, 0))
        return false;

    argv.rval().setObject(*obj);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
from_gbytes_func(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JS::RootedObject bytes_obj(context);
    GBytes *gbytes;

    if (!gjs_parse_call_args(context, "fromGBytes", argv, "o",
                             "bytes", &bytes_obj))
        return false;

    if (!BoxedBase::typecheck(context, bytes_obj, nullptr, G_TYPE_BYTES))
        return false;

    gbytes = BoxedBase::to_c_ptr<GBytes>(context, bytes_obj);
    if (!gbytes)
        return false;

    size_t len;
    const void* data = g_bytes_get_data(gbytes, &len);
    JS::RootedObject array_buffer(
        context,
        JS::NewExternalArrayBuffer(
            context, len,
            const_cast<void*>(data),  // the ArrayBuffer won't modify the data
            bytes_unref_arraybuffer, gbytes));
    if (!array_buffer)
        return false;
    g_bytes_ref(gbytes);  // now owned by both ArrayBuffer and BoxedBase

    JS::RootedObject obj(
        context, JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1));
    if (!obj)
        return false;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (!JS_DefineFunctionById(context, obj, atoms.to_string(),
                               instance_to_string_func, 1, 0))
        return false;

    argv.rval().setObject(*obj);
    return true;
}

JSObject* gjs_byte_array_from_data(JSContext* cx, size_t nbytes, void* data) {
    JS::RootedObject array_buffer(cx);
    // a null data pointer takes precedence over whatever `nbytes` says
    if (data)
        array_buffer = JS::NewArrayBufferWithContents(
            cx, nbytes, _gjs_memdup2(data, nbytes));
    else
        array_buffer = JS::NewArrayBuffer(cx, 0);
    if (!array_buffer)
        return nullptr;

    JS::RootedObject array(cx,
                           JS_NewUint8ArrayWithBuffer(cx, array_buffer, 0, -1));

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_DefineFunctionById(cx, array, atoms.to_string(),
                               instance_to_string_func, 1, 0))
        return nullptr;
    return array;
}

JSObject* gjs_byte_array_from_byte_array(JSContext* cx, GByteArray* array) {
    return gjs_byte_array_from_data(cx, array->len, array->data);
}

GBytes* gjs_byte_array_get_bytes(JSObject* obj) {
    bool is_shared_memory;
    uint32_t len;
    uint8_t* data;

    js::GetUint8ArrayLengthAndData(obj, &len, &is_shared_memory, &data);
    return g_bytes_new(data, len);
}

GByteArray* gjs_byte_array_get_byte_array(JSObject* obj) {
    return g_bytes_unref_to_array(gjs_byte_array_get_bytes(obj));
}

static JSFunctionSpec gjs_byte_array_module_funcs[] = {
    JS_FN("fromString", from_string_func, 2, 0),
    JS_FN("fromGBytes", from_gbytes_func, 1, 0),
    JS_FN("toGBytes", to_gbytes_func, 1, 0),
    JS_FN("toString", to_string_func, 2, 0),
    JS_FS_END};

bool
gjs_define_byte_array_stuff(JSContext              *cx,
                            JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, gjs_byte_array_module_funcs);
}
