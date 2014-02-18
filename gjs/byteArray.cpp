/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <config.h>
#include <string.h>
#include <glib.h>
#include "byteArray.h"
#include "gi/boxed.h"
#include "jsapi-wrapper.h"
#include "jsapi-util-args.h"
#include <girepository.h>
#include <util/log.h>

typedef struct {
    GByteArray *array;
    GBytes     *bytes;
} ByteArrayInstance;

extern struct JSClass gjs_byte_array_class;
GJS_DEFINE_PRIV_FROM_JS(ByteArrayInstance, gjs_byte_array_class)

static bool   byte_array_get_prop      (JSContext    *context,
                                        JS::HandleObject obj,
                                        JS::HandleId id,
                                        JS::MutableHandleValue value_p);
static bool   byte_array_set_prop      (JSContext    *context,
                                        JS::HandleObject obj,
                                        JS::HandleId id,
                                        bool                   strict,
                                        JS::MutableHandleValue value_p);
GJS_NATIVE_CONSTRUCTOR_DECLARE(byte_array);
static void   byte_array_finalize      (JSFreeOp     *fop,
                                        JSObject     *obj);


struct JSClass gjs_byte_array_class = {
    "ByteArray",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_BACKGROUND_FINALIZE |
    JSCLASS_IMPLEMENTS_BARRIERS,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    (JSPropertyOp)byte_array_get_prop,
    (JSStrictPropertyOp)byte_array_set_prop,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    byte_array_finalize
};

bool
gjs_typecheck_bytearray(JSContext       *context,
                        JS::HandleObject object,
                        bool             throw_error)
{
    return do_base_typecheck(context, object, throw_error);
}

static JS::Value
gjs_value_from_gsize(gsize v)
{
    if (v <= (gsize) JSVAL_INT_MAX) {
        return JS::Int32Value(v);
    }
    return JS::NumberValue(v);
}

static void
byte_array_ensure_array (ByteArrayInstance  *priv)
{
    if (priv->bytes) {
        priv->array = g_bytes_unref_to_array(priv->bytes);
        priv->bytes = NULL;
    } else {
        g_assert(priv->array);
    }
}

static void
byte_array_ensure_gbytes (ByteArrayInstance  *priv)
{
    if (priv->array) {
        priv->bytes = g_byte_array_free_to_bytes(priv->array);
        priv->array = NULL;
    } else {
        g_assert(priv->bytes);
    }
}

static bool
gjs_value_to_gsize(JSContext         *context,
                   JS::HandleValue    value,
                   gsize             *v_p)
{
    guint32 val32;

    /* Just JS::ToUint32() would work. However, we special case ints for a nicer
     * error message on negative indices.
     */
    if (value.isInt32()) {
        int i = value.toInt32();
        if (i < 0) {
            gjs_throw(context, "Negative length or index %d is not allowed for ByteArray",
                      i);
            return false;
        }
        *v_p = i;
        return true;
    } else {
        bool ret;
        /* This is pretty liberal (it converts about anything to
         * a number) but it's what we use elsewhere in gjs too.
         */

        ret = JS::ToUint32(context, value, &val32);
        *v_p = val32;
        return ret;
    }
}

static bool
gjs_value_to_byte(JSContext         *context,
                  JS::HandleValue    value,
                  guint8            *v_p)
{
    gsize v;

    if (!gjs_value_to_gsize(context, value, &v))
        return false;

    if (v >= 256) {
        gjs_throw(context,
                  "Value %" G_GSIZE_FORMAT " is not a valid byte; must be in range [0,255]",
                  v);
        return false;
    }

    *v_p = v;
    return true;
}

static bool
byte_array_get_index(JSContext         *context,
                     JS::HandleObject obj,
                     ByteArrayInstance *priv,
                     gsize              idx,
                     JS::MutableHandleValue value_p)
{
    gsize len;
    guint8 *data;
    
    gjs_byte_array_peek_data(context, obj, &data, &len);

    if (idx >= len) {
        gjs_throw(context,
                  "Index %" G_GSIZE_FORMAT " is out of range for ByteArray length %lu",
                  idx,
                  (unsigned long)len);
        return false;
    }

    value_p.setInt32(data[idx]);

    return true;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is false on OOM/exception.
 */
static bool
byte_array_get_prop(JSContext *context,
                    JS::HandleObject obj,
                    JS::HandleId id,
                    JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return true; /* prototype, not an instance. */

    JS::RootedValue id_value(context);
    if (!JS_IdToValue(context, id, &id_value))
        return false;

    /* First handle array indexing */
    if (id_value.isNumber()) {
        gsize idx;
        if (!gjs_value_to_gsize(context, id_value, &idx))
            return false;
        return byte_array_get_index(context, obj, priv, idx, value_p);
    }

    /* We don't special-case anything else for now. Regular JS arrays
     * allow string versions of ints for the index, we don't bother.
     */

    return true;
}

static bool
byte_array_length_getter(JSContext *context,
                         unsigned   argc,
                         JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, to, ByteArrayInstance, priv);
    gsize len = 0;

    if (priv == NULL)
        return true; /* prototype, not an instance. */

    if (priv->array != NULL)
        len = priv->array->len;
    else if (priv->bytes != NULL)
        len = g_bytes_get_size (priv->bytes);
    args.rval().set(gjs_value_from_gsize(len));
    return true;
}

static bool
byte_array_length_setter(JSContext *context,
                         unsigned   argc,
                         JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, to, ByteArrayInstance, priv);
    gsize len = 0;

    if (priv == NULL)
        return true; /* prototype, not instance */

    byte_array_ensure_array(priv);

    if (!gjs_value_to_gsize(context, args[0], &len)) {
        gjs_throw(context,
                  "Can't set ByteArray length to non-integer");
        return false;
    }
    g_byte_array_set_size(priv->array, len);
    args.rval().setUndefined();
    return true;
}

static bool
byte_array_set_index(JSContext         *context,
                     JS::HandleObject obj,
                     ByteArrayInstance *priv,
                     gsize              idx,
                     JS::MutableHandleValue value_p)
{
    guint8 v;

    if (!gjs_value_to_byte(context, value_p, &v)) {
        return false;
    }

    byte_array_ensure_array(priv);

    /* grow the array if necessary */
    if (idx >= priv->array->len) {
        g_byte_array_set_size(priv->array,
                              idx + 1);
    }

    g_array_index(priv->array, guint8, idx) = v;

    /* Stop JS from storing a copy of the value */
    value_p.setUndefined();

    return true;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is false on OOM/exception.
 */
static bool
byte_array_set_prop(JSContext *context,
                    JS::HandleObject obj,
                    JS::HandleId id,
                    bool strict,
                    JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return true; /* prototype, not an instance. */

    JS::RootedValue id_value(context);
    if (!JS_IdToValue(context, id, &id_value))
        return false;

    /* First handle array indexing */
    if (id_value.isNumber()) {
        gsize idx;
        if (!gjs_value_to_gsize(context, id_value, &idx))
            return false;

        return byte_array_set_index(context, obj, priv, idx, value_p);
    }

    /* We don't special-case anything else for now */

    return true;
}

static GByteArray *
gjs_g_byte_array_new(int preallocated_length)
{
    GByteArray *array;

    /* can't use g_byte_array_new() because we need to clear to zero.
     * We nul-terminate too for ease of toString() and for security
     * paranoia.
     */
    array =  (GByteArray*) g_array_sized_new (true, /* nul-terminated */
                                              true, /* clear to zero */
                                              1, /* element size */
                                              preallocated_length);
   if (preallocated_length > 0) {
       /* we want to not only allocate the size, but have it
        * already be the array's length.
        */
       g_byte_array_set_size(array, preallocated_length);
   }

   return array;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(byte_array)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(byte_array)
    ByteArrayInstance *priv;
    gsize preallocated_length;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(byte_array);

    preallocated_length = 0;
    if (argc >= 1) {
        if (!gjs_value_to_gsize(context, argv[0], &preallocated_length)) {
            gjs_throw(context,
                      "Argument to ByteArray constructor should be a positive number for array length");
            return false;
        }
    }

    priv = g_slice_new0(ByteArrayInstance);
    priv->array = gjs_g_byte_array_new(preallocated_length);
    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    GJS_NATIVE_CONSTRUCTOR_FINISH(byte_array);

    return true;
}

static void
byte_array_finalize(JSFreeOp *fop,
                    JSObject *obj)
{
    ByteArrayInstance *priv;

    priv = (ByteArrayInstance*) JS_GetPrivate(obj);

    if (priv == NULL)
        return; /* prototype, not instance */

    if (priv->array) {
        g_byte_array_free(priv->array, true);
        priv->array = NULL;
    } else if (priv->bytes) {
        g_clear_pointer(&priv->bytes, g_bytes_unref);
    }

    g_slice_free(ByteArrayInstance, priv);
}

/* implement toString() with an optional encoding arg */
static bool
to_string_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, argv, to, ByteArrayInstance, priv);
    char *encoding;
    bool encoding_is_utf8;
    gchar *data;

    if (priv == NULL)
        return true; /* prototype, not instance */

    byte_array_ensure_array(priv);

    if (argc >= 1 && argv[0].isString()) {
        if (!gjs_string_to_utf8(context, argv[0], &encoding))
            return false;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            encoding_is_utf8 = true;
            g_free(encoding);
            encoding = NULL;
        } else {
            encoding_is_utf8 = false;
        }
    } else {
        encoding_is_utf8 = true;
    }

    if (priv->array->len == 0)
        /* the internal data pointer could be NULL in this case */
        data = (gchar*)"";
    else
        data = (gchar*)priv->array->data;

    if (encoding_is_utf8) {
        /* optimization, avoids iconv overhead and runs
         * libmozjs hardwired utf8-to-utf16
         */
        return gjs_string_from_utf8(context, data, priv->array->len,
                                    argv.rval());
    } else {
        bool ok = false;
        gsize bytes_written;
        GError *error;
        JSString *s;
        char *u16_str;
        char16_t *u16_out;

        error = NULL;
        u16_str = g_convert(data,
                           priv->array->len,
                           "UTF-16",
                           encoding,
                           NULL, /* bytes read */
                           &bytes_written,
                           &error);
        g_free(encoding);
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
    GJS_GET_PRIV(context, argc, vp, rec, to, ByteArrayInstance, priv);
    JSObject *ret_bytes_obj;
    GIBaseInfo *gbytes_info;

    if (priv == NULL)
        return true; /* prototype, not instance */

    byte_array_ensure_gbytes(priv);

    gbytes_info = g_irepository_find_by_gtype(NULL, G_TYPE_BYTES);
    ret_bytes_obj = gjs_boxed_from_c_struct(context, (GIStructInfo*)gbytes_info,
                                            priv->bytes, GJS_BOXED_CREATION_NONE);

    rec.rval().setObjectOrNull(ret_bytes_obj);
    return true;
}

/* Ensure that the module and class objects exists, and that in turn
 * ensures that JS_InitClass has been called. */
static JSObject *
byte_array_get_prototype(JSContext *context)
{
    JS::RootedValue retval(context,
        gjs_get_global_slot(context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE));

    if (!retval.isObject()) {
        if (!gjs_eval_with_scope(context, JS::NullPtr(),
                                 "imports.byteArray.ByteArray.prototype;", -1,
                                 "<internal>", &retval))
            g_error ("Could not import byte array prototype\n");
    }

    return &retval.toObject();
}

static JSObject*
byte_array_new(JSContext *context)
{
    ByteArrayInstance *priv;

    JS::RootedObject proto(context, byte_array_get_prototype(context));
    JS::RootedObject array(context,
        JS_NewObjectWithGivenProto(context, &gjs_byte_array_class, proto, JS::NullPtr()));

    priv = g_slice_new0(ByteArrayInstance);

    g_assert(priv_from_js(context, array) == NULL);
    JS_SetPrivate(array, priv);

    return array;
}

/* fromString() function implementation */
static bool
from_string_func(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    ByteArrayInstance *priv;
    char *encoding;
    bool encoding_is_utf8;
    JS::RootedObject obj(context, byte_array_new(context));

    if (obj == NULL)
        return false;

    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    g_assert(argc > 0); /* because we specified min args 1 */

    priv->array = gjs_g_byte_array_new(0);

    if (!argv[0].isString()) {
        gjs_throw(context,
                  "byteArray.fromString() called with non-string as first arg");
        return false;
    }

    if (argc > 1 && argv[1].isString()) {
        if (!gjs_string_to_utf8(context, argv[1], &encoding))
            return false;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            encoding_is_utf8 = true;
            g_free(encoding);
            encoding = NULL;
        } else {
            encoding_is_utf8 = false;
        }
    } else {
        encoding_is_utf8 = true;
    }

    if (encoding_is_utf8) {
        /* optimization? avoids iconv overhead and runs
         * libmozjs hardwired utf16-to-utf8.
         */
        char *utf8 = NULL;
        if (!gjs_string_to_utf8(context,
                                argv[0],
                                &utf8))
            return false;

        g_byte_array_set_size(priv->array, 0);
        g_byte_array_append(priv->array, (guint8*) utf8, strlen(utf8));
        g_free(utf8);
    } else {
        char *encoded;
        gsize bytes_written;
        GError *error;
        const char16_t *u16_chars;
        gsize u16_len;

        u16_chars = JS_GetStringCharsAndLength(context, argv[0].toString(), &u16_len);
        if (u16_chars == NULL)
            return false;

        error = NULL;
        encoded = g_convert((char*) u16_chars,
                            u16_len * 2,
                            encoding, /* to_encoding */
                            "UTF-16", /* from_encoding */
                            NULL, /* bytes read */
                            &bytes_written,
                            &error);
        g_free(encoding);
        if (encoded == NULL) {
            /* frees the GError */
            gjs_throw_g_error(context, error);
            return false;
        }

        g_byte_array_set_size(priv->array, 0);
        g_byte_array_append(priv->array, (guint8*) encoded, bytes_written);

        g_free(encoded);
    }

    argv.rval().setObject(*obj);
    return true;
}

/* fromArray() function implementation */
static bool
from_array_func(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    ByteArrayInstance *priv;
    guint32 len;
    guint32 i;
    JS::RootedObject obj(context, byte_array_new(context));

    if (obj == NULL)
        return false;

    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    g_assert(argc > 0); /* because we specified min args 1 */

    priv->array = gjs_g_byte_array_new(0);

    JS::RootedObject array_obj(context, &argv[0].toObject());
    if (!JS_IsArrayObject(context, array_obj)) {
        gjs_throw(context,
                  "byteArray.fromArray() called with non-array as first arg");
        return false;
    }

    if (!JS_GetArrayLength(context, array_obj, &len)) {
        gjs_throw(context,
                  "byteArray.fromArray() can't get length of first array arg");
        return false;
    }

    g_byte_array_set_size(priv->array, len);

    JS::RootedValue elem(context);
    for (i = 0; i < len; ++i) {
        guint8 b;

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array_obj, i, &elem)) {
            /* this means there was an exception, while elem.isUndefined()
             * means no element found
             */
            return false;
        }

        if (elem.isUndefined())
            continue;

        if (!gjs_value_to_byte(context, elem, &b))
            return false;

        g_array_index(priv->array, guint8, i) = b;
    }

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
    ByteArrayInstance *priv;

    if (!gjs_parse_call_args(context, "overrides_gbytes_to_array", argv, "o",
                             "bytes", &bytes_obj))
        return false;

    if (!gjs_typecheck_boxed(context, bytes_obj, NULL, G_TYPE_BYTES, true))
        return false;

    gbytes = (GBytes*) gjs_c_struct_from_boxed(context, bytes_obj);

    JS::RootedObject obj(context, byte_array_new(context));
    if (obj == NULL)
        return false;
    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    priv->bytes = g_bytes_ref(gbytes);

    argv.rval().setObject(*obj);
    return true;
}

JSObject *
gjs_byte_array_from_byte_array (JSContext *context,
                                GByteArray *array)
{
    ByteArrayInstance *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(array != NULL, NULL);

    JS::RootedObject proto(context, byte_array_get_prototype(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_byte_array_class, proto, JS::NullPtr()));

    if (!object) {
        gjs_throw(context, "failed to create byte array");
        return NULL;
    }

    priv = g_slice_new0(ByteArrayInstance);
    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);
    priv->array = g_byte_array_new();
    priv->array->data = (guint8*) g_memdup(array->data, array->len);
    priv->array->len = array->len;

    return object;
}

GBytes *
gjs_byte_array_get_bytes (JSContext       *context,
                          JS::HandleObject object)
{
    ByteArrayInstance *priv;
    priv = priv_from_js(context, object);
    g_assert(priv != NULL);

    byte_array_ensure_gbytes(priv);

    return g_bytes_ref (priv->bytes);
}

GByteArray *
gjs_byte_array_get_byte_array (JSContext       *context,
                               JS::HandleObject obj)
{
    ByteArrayInstance *priv;
    priv = priv_from_js(context, obj);
    g_assert(priv != NULL);

    byte_array_ensure_array(priv);

    return g_byte_array_ref (priv->array);
}

void
gjs_byte_array_peek_data (JSContext       *context,
                          JS::HandleObject obj,
                          guint8         **out_data,
                          gsize           *out_len)
{
    ByteArrayInstance *priv;
    priv = priv_from_js(context, obj);
    g_assert(priv != NULL);
    
    if (priv->array != NULL) {
        *out_data = (guint8*)priv->array->data;
        *out_len = (gsize)priv->array->len;
    } else if (priv->bytes != NULL) {
        *out_data = (guint8*)g_bytes_get_data(priv->bytes, out_len);
    } else {
        g_assert_not_reached();
    }
}

JSPropertySpec gjs_byte_array_proto_props[] = {
    JS_PSGS("length", byte_array_length_getter, byte_array_length_setter,
            JSPROP_PERMANENT),
    JS_PS_END
};

JSFunctionSpec gjs_byte_array_proto_funcs[] = {
    JS_FS("toString", to_string_func, 0, 0),
    JS_FS("toGBytes", to_gbytes_func, 0, 0),
    JS_FS_END
};

static JSFunctionSpec gjs_byte_array_module_funcs[] = {
    JS_FS("fromString", from_string_func, 1, 0),
    JS_FS("fromArray", from_array_func, 1, 0),
    JS_FS("fromGBytes", from_gbytes_func, 1, 0),
    JS_FS_END
};

bool
gjs_define_byte_array_stuff(JSContext              *context,
                            JS::MutableHandleObject module)
{
    JSObject *prototype;

    module.set(JS_NewObject(context, NULL, JS::NullPtr(), JS::NullPtr()));

    prototype = JS_InitClass(context, module, JS::NullPtr(),
                             &gjs_byte_array_class,
                             gjs_byte_array_constructor,
                             0,
                             &gjs_byte_array_proto_props[0],
                             &gjs_byte_array_proto_funcs[0],
                             NULL,
                             NULL);

    if (!JS_DefineFunctions(context, module, &gjs_byte_array_module_funcs[0]))
        return false;

    g_assert(gjs_get_global_slot(context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE).isUndefined());
    gjs_set_global_slot(context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE,
                        JS::ObjectOrNullValue(prototype));

    return true;
}
