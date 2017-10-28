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
#include "jsapi-util-args.h"
#include "jsapi-wrapper.h"

/* implement toString() with an optional encoding arg */
static bool
to_string_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    GjsAutoJSChar encoding(context);
    JS::RootedObject byte_array(context);
    bool encoding_is_utf8;
    uint8_t *data;

    if (!gjs_parse_call_args(context, "toString", argv, "o|s",
                             "byteArray", &byte_array,
                             "encoding", &encoding))
        return false;

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
        return gjs_string_from_utf8(context, reinterpret_cast<char *>(data), len,
                                    argv.rval());
    } else {
        bool ok = false;
        gsize bytes_written;
        GError *error;
        JSString *s;
        char *u16_str;
        char16_t *u16_out;

        error = NULL;
        u16_str = g_convert(reinterpret_cast<char *>(data), len,
                           "UTF-16",
                           encoding,
                           NULL, /* bytes read */
                           &bytes_written,
                           &error);
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
            argv.rval().setString(s);
        }

        g_free(u16_str);
        g_free(u16_out);
        return ok;
    }
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

    GBytes *bytes = gjs_byte_array_get_bytes(byte_array);
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
    GjsAutoJSChar encoding(context);
    GjsAutoJSChar utf8(context);
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
        /* FIXME: avoid copy. */
        array_buffer = JS_NewArrayBufferWithContents(context, strlen(utf8),
                                                     utf8.copy());
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

        // FIXME also assumes g_free() == free()
        array_buffer = JS_NewArrayBufferWithContents(context, bytes_written,
                                                     encoded);
    }

    if (!array_buffer)
        return false;
    obj = JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1);
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
    // FIXME this will not be the last reference so the data will be copied.
    // Would be better if mozjs could hold the reference and use the const data
    void *data = g_bytes_unref_to_data(g_bytes_ref(gbytes), &len);
    JS::RootedObject array_buffer(context,
        JS_NewArrayBufferWithContents(context, len, data));
    if (!array_buffer)
        return false;

    JS::RootedObject obj(context,
        JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1));
    if (!obj)
        return false;

    argv.rval().setObject(*obj);
    return true;
}

JSObject *
gjs_byte_array_from_data(JSContext *cx,
                         size_t     nbytes,
                         void      *data)
{
    JS::RootedObject array_buffer(cx,
        JS_NewArrayBufferWithContents(cx, nbytes, g_memdup(data, nbytes)));
    if (!array_buffer)
        return nullptr;

    return JS_NewUint8ArrayWithBuffer(cx, array_buffer, 0, -1);
}

JSObject *
gjs_byte_array_from_byte_array (JSContext *context,
                                GByteArray *array)
{
    return gjs_byte_array_from_data(context, array->len, array->data);
}

GBytes *
gjs_byte_array_get_bytes(JS::HandleObject obj)
{
    bool is_shared_memory;
    uint32_t len;
    uint8_t *data;

    js::GetUint8ArrayLengthAndData(obj, &len, &is_shared_memory, &data);
    return g_bytes_new(data, len);
}

GByteArray *
gjs_byte_array_get_byte_array(JS::HandleObject obj)
{
    return g_bytes_unref_to_array(gjs_byte_array_get_bytes(obj));
}

static JSFunctionSpec gjs_byte_array_module_funcs[] = {
    JS_FS("fromString", from_string_func, 2, 0),
    JS_FS("fromGBytes", from_gbytes_func, 1, 0),
    JS_FS("toGBytes", to_gbytes_func, 1, 0),
    JS_FS("toString", to_string_func, 2, 0),
    JS_FS_END
};

bool
gjs_define_byte_array_stuff(JSContext              *cx,
                            JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, gjs_byte_array_module_funcs);
}
