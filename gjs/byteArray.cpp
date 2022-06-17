/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#include <config.h>

#include <stdint.h>

#include <algorithm>  // for copy_n

#include <glib-object.h>
#include <glib.h>

#include <js/ArrayBuffer.h>
#include <js/CallArgs.h>
#include <js/GCAPI.h>
#include <js/PropertyAndElement.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>   // for UniqueChars
#include <js/experimental/TypedData.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gi/struct.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/text-encoding.h"

GJS_JSAPI_RETURN_CONVENTION
static bool to_string_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars encoding;
    JS::RootedObject byte_array(cx);

    if (!gjs_parse_call_args(cx, "toString", args, "o|s", "byteArray",
                             &byte_array, "encoding", &encoding))
        return false;

    const char* actual_encoding = encoding ? encoding.get() : "utf-8";
    JS::RootedString str(
        cx, gjs_decode_from_uint8array(cx, byte_array, actual_encoding,
                                       GjsStringTermination::ZERO_TERMINATED, true));
    if (!str)
        return false;

    args.rval().setString(str);
    return true;
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

    const char* actual_encoding = encoding ? encoding.get() : "utf-8";
    JS::RootedString str(
        cx, gjs_decode_from_uint8array(cx, this_obj, actual_encoding,
                                       GjsStringTermination::ZERO_TERMINATED, true));
    if (!str)
        return false;

    args.rval().setString(str);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool define_legacy_tostring(JSContext* cx, JS::HandleObject array) {
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    return JS_DefineFunctionById(cx, array, atoms.to_string(),
                                 instance_to_string_func, 1, 0);
}

/* fromString() function implementation */
GJS_JSAPI_RETURN_CONVENTION
static bool from_string_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedString str(cx);
    JS::UniqueChars encoding;
    if (!gjs_parse_call_args(cx, "fromString", args, "S|s", "string", &str,
                             "encoding", &encoding))
        return false;

    const char* actual_encoding = encoding ? encoding.get() : "utf-8";
    JS::RootedObject uint8array(
        cx, gjs_encode_to_uint8array(cx, str, actual_encoding,
                                     GjsStringTermination::ZERO_TERMINATED));
    if (!uint8array || !define_legacy_tostring(cx, uint8array))
        return false;

    args.rval().setObject(*uint8array);
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

    if (!StructBase::typecheck(context, bytes_obj, G_TYPE_BYTES))
        return false;

    gbytes = StructBase::to_c_ptr<GBytes>(context, bytes_obj);
    if (!gbytes)
        return false;

    size_t len;
    const void* data = g_bytes_get_data(gbytes, &len);
    if (len == 0) {
        JS::RootedObject empty_array(context, JS_NewUint8Array(context, 0));
        if (!empty_array || !define_legacy_tostring(context, empty_array))
            return false;

        argv.rval().setObject(*empty_array);
        return true;
    }

    JS::RootedObject array_buffer{context, JS::NewArrayBuffer(context, len)};
    if (!array_buffer)
        return false;

    // Copy the data into the ArrayBuffer so that the copy is aligned, and
    // because the GBytes data pointer may point into immutable memory.
    {
        JS::AutoCheckCannotGC nogc;
        bool unused;
        uint8_t* storage = JS::GetArrayBufferData(array_buffer, &unused, nogc);
        std::copy_n(static_cast<const uint8_t*>(data), len, storage);
    }

    JS::RootedObject obj(
        context, JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1));
    if (!obj || !define_legacy_tostring(context, obj))
        return false;

    argv.rval().setObject(*obj);
    return true;
}

JSObject* gjs_byte_array_from_data_copy(JSContext* cx, size_t nbytes,
                                        void* data) {
    JS::RootedObject array_buffer(cx);
    // a null data pointer takes precedence over whatever `nbytes` says
    if (data) {
        array_buffer = JS::NewArrayBuffer(cx, nbytes);

        JS::AutoCheckCannotGC nogc{};
        bool unused;
        uint8_t* storage = JS::GetArrayBufferData(array_buffer, &unused, nogc);
        std::copy_n(static_cast<uint8_t*>(data), nbytes, storage);
    } else {
        array_buffer = JS::NewArrayBuffer(cx, 0);
    }
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
    return gjs_byte_array_from_data_copy(cx, array->len, array->data);
}

GBytes* gjs_byte_array_get_bytes(JSObject* obj) {
    bool is_shared_memory;
    size_t len;
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
    JS_FN("toString", to_string_func, 2, 0),
    JS_FS_END};

bool
gjs_define_byte_array_stuff(JSContext              *cx,
                            JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, gjs_byte_array_module_funcs);
}
