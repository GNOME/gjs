/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010  litl, LLC
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

#include <glib.h>

#include "byteArray.h"
#include "gi/boxed.h"
#include "gjs/deprecation.h"
#include "jsapi-util-args.h"
#include "jsapi-wrapper.h"

/* Callbacks to use with JS_NewExternalArrayBuffer() */

static void gfree_arraybuffer_contents(void* contents, void* unused) {
    g_free(contents);
}

static void bytes_ref_arraybuffer(void* contents, void* user_data) {
    auto* gbytes = static_cast<GBytes*>(user_data);
    g_bytes_ref(gbytes);
}

static void bytes_unref_arraybuffer(void* contents, void* user_data) {
    auto* gbytes = static_cast<GBytes*>(user_data);
    g_bytes_unref(gbytes);
}

/* implement toString() with an optional encoding arg */
static bool to_string_impl(JSContext* context, JS::HandleObject byte_array,
                           const char* encoding, JS::MutableHandleValue rval) {
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

    if (encoding_is_utf8) {
        /* optimization, avoids iconv overhead and runs
         * libmozjs hardwired utf8-to-utf16
         */
        return gjs_string_from_utf8_n(context, reinterpret_cast<char*>(data),
                                      len, rval);
    } else {
        bool ok = false;
        gsize bytes_written;
        GError *error;
        JSString *s;
        char *u16_str;
        char16_t *u16_out;

        error = NULL;
        u16_str = g_convert(reinterpret_cast<char*>(data), len, "UTF-16",
                            encoding, nullptr, /* bytes read */
                            &bytes_written, &error);
        if (u16_str == NULL) {
            /* frees the GError */
            gjs_throw_g_error(context, error);
            return false;
        }

        /* bytes_written should be bytes in a UTF-16 string so
         * should be a multiple of 2
         */
        g_assert((bytes_written % 2) == 0);

        u16_out = g_new(char16_t, bytes_written / 2);
        memcpy(u16_out, u16_str, bytes_written);
        s = JS_NewUCStringCopyN(context, u16_out, bytes_written / 2);
        if (s != NULL) {
            ok = true;
            rval.setString(s);
        }

        g_free(u16_str);
        g_free(u16_out);
        return ok;
    }
}

static bool to_string_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsAutoJSChar encoding;
    JS::RootedObject byte_array(cx);

    if (!gjs_parse_call_args(cx, "toString", args, "o|s", "byteArray",
                             &byte_array, "encoding", &encoding))
        return false;

    return to_string_impl(cx, byte_array, encoding, args.rval());
}

/* Workaround to keep existing code compatible. This function is tacked onto
 * any Uint8Array instances created in situations where previously a ByteArray
 * would have been created. It logs a compatibility warning. */
static bool instance_to_string_func(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, this_obj);
    GjsAutoJSChar encoding;

    _gjs_warn_deprecated_once_per_callsite(
        cx, GjsDeprecationMessageId::ByteArrayInstanceToString);

    if (!gjs_parse_call_args(cx, "toString", args, "|s", "encoding", &encoding))
        return false;

    return to_string_impl(cx, this_obj, encoding, args.rval());
}

static bool
to_gbytes_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs rec = JS::CallArgsFromVp(argc, vp);
    JSObject *ret_bytes_obj;
    GIBaseInfo *gbytes_info;
    JS::RootedObject byte_array(context);

    if (!gjs_parse_call_args(context, "toGBytes", rec, "o",
                             "byteArray", &byte_array))
        return false;

    GBytes* bytes = gjs_byte_array_get_bytes(byte_array);
    gbytes_info = g_irepository_find_by_gtype(NULL, G_TYPE_BYTES);
    ret_bytes_obj = gjs_boxed_from_c_struct(context, (GIStructInfo*)gbytes_info,
                                            bytes, GJS_BOXED_CREATION_NONE);
    g_bytes_unref(bytes);

    rec.rval().setObjectOrNull(ret_bytes_obj);
    return true;
}

/* fromString() function implementation */
static bool
from_string_func(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoJSChar encoding;
    GjsAutoJSChar utf8;
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
        encoding_is_utf8 = (strcmp(encoding, "UTF-8") == 0);
    } else {
        encoding_is_utf8 = true;
    }

    if (encoding_is_utf8) {
        /* optimization? avoids iconv overhead and runs
         * libmozjs hardwired utf16-to-utf8.
         */
        size_t len = strlen(utf8);
        array_buffer =
            JS_NewArrayBufferWithContents(context, len, utf8.release());
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
                                    encoding,  /* to_encoding */
                                    "LATIN1",  /* from_encoding */
                                    NULL,  /* bytes read */
                                    &bytes_written, &error);
            } else {
                const char16_t *chars =
                    JS_GetTwoByteStringCharsAndLength(context, nogc, str, &len);
                if (chars == NULL)
                    return false;

                encoded = g_convert((char *) chars, len * 2,
                                    encoding,  /* to_encoding */
                                    "UTF-16",  /* from_encoding */
                                    NULL,  /* bytes read */
                                    &bytes_written, &error);
            }
        }

        if (encoded == NULL) {
            /* frees the GError */
            gjs_throw_g_error(context, error);
            return false;
        }

        array_buffer =
            JS_NewExternalArrayBuffer(context, bytes_written, encoded, nullptr,
                                      gfree_arraybuffer_contents, nullptr);
    }

    if (!array_buffer)
        return false;
    obj = JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1);
    JS_DefineFunction(context, obj, "toString", instance_to_string_func, 1, 0);
    argv.rval().setObject(*obj);
    return true;
}

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

    if (!gjs_typecheck_boxed(context, bytes_obj, NULL, G_TYPE_BYTES, true))
        return false;

    gbytes = (GBytes*) gjs_c_struct_from_boxed(context, bytes_obj);

    size_t len;
    const void* data = g_bytes_get_data(gbytes, &len);
    JS::RootedObject array_buffer(
        context,
        JS_NewExternalArrayBuffer(
            context, len,
            const_cast<void*>(data),  // the ArrayBuffer won't modify the data
            bytes_ref_arraybuffer, bytes_unref_arraybuffer, gbytes));
    if (!array_buffer)
        return false;

    JS::RootedObject obj(
        context, JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1));
    if (!obj)
        return false;
    JS_DefineFunction(context, obj, "toString", instance_to_string_func, 1, 0);

    argv.rval().setObject(*obj);
    return true;
}

JSObject* gjs_byte_array_from_data(JSContext* cx, size_t nbytes, void* data) {
    JS::RootedObject array_buffer(cx);
    // a null data pointer takes precedence over whatever `nbytes` says
    if (data)
        array_buffer = JS_NewArrayBufferWithContents(cx, nbytes, g_memdup(data, nbytes));
    else
        array_buffer = JS_NewArrayBuffer(cx, 0);
    if (!array_buffer)
        return nullptr;

    JS::RootedObject array(cx,
                           JS_NewUint8ArrayWithBuffer(cx, array_buffer, 0, -1));
    JS_DefineFunction(cx, array, "toString", instance_to_string_func, 1, 0);
    return array;
}

JSObject* gjs_byte_array_from_byte_array(JSContext* cx, GByteArray* array) {
    return gjs_byte_array_from_data(cx, array->len, array->data);
}

GBytes* gjs_byte_array_get_bytes(JS::HandleObject obj) {
    bool is_shared_memory;
    uint32_t len;
    uint8_t* data;

    js::GetUint8ArrayLengthAndData(obj, &len, &is_shared_memory, &data);
    return g_bytes_new(data, len);
}

GByteArray* gjs_byte_array_get_byte_array(JS::HandleObject obj) {
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
