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
#include "../gi/boxed.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <girepository.h>
#include <util/log.h>

typedef struct {
    GByteArray *array;
    GBytes     *bytes;
} ByteArrayInstance;

extern struct JSClass gjs_byte_array_class;
GJS_DEFINE_PRIV_FROM_JS(ByteArrayInstance, gjs_byte_array_class)

static JSBool byte_array_get_prop      (JSContext    *context,
                                        JS::HandleObject obj,
                                        JS::HandleId id,
                                        JS::MutableHandleValue value_p);
static JSBool byte_array_set_prop      (JSContext    *context,
                                        JS::HandleObject obj,
                                        JS::HandleId id,
                                        JSBool        strict,
                                        JS::MutableHandleValue value_p);
GJS_NATIVE_CONSTRUCTOR_DECLARE(byte_array);
static void   byte_array_finalize      (JSFreeOp     *fop,
                                        JSObject     *obj);


struct JSClass gjs_byte_array_class = {
    "ByteArray",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_BACKGROUND_FINALIZE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    (JSPropertyOp)byte_array_get_prop,
    (JSStrictPropertyOp)byte_array_set_prop,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    byte_array_finalize,
    NULL,
    NULL,
    NULL, NULL, NULL
};

JSBool
gjs_typecheck_bytearray(JSContext     *context,
                        JSObject      *object,
                        JSBool         throw_error)
{
    return do_base_typecheck(context, object, throw_error);
}

static JSBool
gjs_value_from_gsize(JSContext         *context,
                     gsize              v,
                     JS::MutableHandleValue value_p)
{
    if (v > (gsize) JSVAL_INT_MAX) {
        value_p.set(INT_TO_JSVAL(v));
        return JS_TRUE;
    } else {
        return JS_NewNumberValue(context, v, value_p.address());
    }
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

static JSBool
gjs_value_to_gsize(JSContext         *context,
                   jsval              value,
                   gsize             *v_p)
{
    guint32 val32;

    /* Just JS_ValueToECMAUint32() would work. However,
     * we special case ints for two reasons:
     *  - JS_ValueToECMAUint32() always goes via a double which is slow
     *  - nicer error message on negative indices
     */
    if (JSVAL_IS_INT(value)) {
        int i = JSVAL_TO_INT(value);
        if (i < 0) {
            gjs_throw(context, "Negative length or index %d is not allowed for ByteArray",
                      i);
            return JS_FALSE;
        }
        *v_p = i;
        return JS_TRUE;
    } else {
        JSBool ret;
        /* This is pretty liberal (it converts about anything to
         * a number) but it's what we use elsewhere in gjs too.
         */

        ret = JS_ValueToECMAUint32(context, value,
                                   &val32);
        *v_p = val32;
        return ret;
    }
}

static JSBool
gjs_value_to_byte(JSContext         *context,
                  jsval              value,
                  guint8            *v_p)
{
    gsize v;

    if (!gjs_value_to_gsize(context, value, &v))
        return JS_FALSE;

    if (v >= 256) {
        gjs_throw(context,
                  "Value %" G_GSIZE_FORMAT " is not a valid byte; must be in range [0,255]",
                  v);
        return JS_FALSE;
    }

    *v_p = v;
    return JS_TRUE;
}

static JSBool
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
        return JS_FALSE;
    }

    value_p.set(INT_TO_JSVAL(data[idx]));

    return JS_TRUE;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
byte_array_get_prop(JSContext *context,
                    JS::HandleObject obj,
                    JS::HandleId id,
                    JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;
    jsval id_value;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    if (!JS_IdToValue(context, id, &id_value))
        return JS_FALSE;

    /* First handle array indexing */
    if (JSVAL_IS_NUMBER(id_value)) {
        gsize idx;
        if (!gjs_value_to_gsize(context, id_value, &idx))
            return JS_FALSE;
        return byte_array_get_index(context, obj, priv, idx, value_p);
    }

    /* We don't special-case anything else for now. Regular JS arrays
     * allow string versions of ints for the index, we don't bother.
     */

    return JS_TRUE;
}

static JSBool
byte_array_length_getter(JSContext *context,
                         JS::HandleObject obj,
                         JS::HandleId id,
                         JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;
    gsize len = 0;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    if (priv->array != NULL)
        len = priv->array->len;
    else if (priv->bytes != NULL)
        len = g_bytes_get_size (priv->bytes);
    return gjs_value_from_gsize(context, len, value_p);
}

static JSBool
byte_array_length_setter(JSContext *context,
                         JS::HandleObject obj,
                         JS::HandleId id,
                         JSBool strict,
                         JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;
    gsize len = 0;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_TRUE; /* prototype, not instance */

    byte_array_ensure_array(priv);

    if (!gjs_value_to_gsize(context, value_p, &len)) {
        gjs_throw(context,
                  "Can't set ByteArray length to non-integer");
        return JS_FALSE;
    }
    g_byte_array_set_size(priv->array, len);
    return JS_TRUE;
}

static JSBool
byte_array_set_index(JSContext         *context,
                     JS::HandleObject obj,
                     ByteArrayInstance *priv,
                     gsize              idx,
                     JS::MutableHandleValue value_p)
{
    guint8 v;

    if (!gjs_value_to_byte(context, value_p, &v)) {
        return JS_FALSE;
    }

    byte_array_ensure_array(priv);

    /* grow the array if necessary */
    if (idx >= priv->array->len) {
        g_byte_array_set_size(priv->array,
                              idx + 1);
    }

    g_array_index(priv->array, guint8, idx) = v;

    /* Stop JS from storing a copy of the value */
    value_p.set(JSVAL_VOID);

    return JS_TRUE;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is JS_FALSE on OOM/exception.
 */
static JSBool
byte_array_set_prop(JSContext *context,
                    JS::HandleObject obj,
                    JS::HandleId id,
                    JSBool strict,
                    JS::MutableHandleValue value_p)
{
    ByteArrayInstance *priv;
    jsval id_value;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    if (!JS_IdToValue(context, id, &id_value))
        return JS_FALSE;

    /* First handle array indexing */
    if (JSVAL_IS_NUMBER(id_value)) {
        gsize idx;
        if (!gjs_value_to_gsize(context, id_value, &idx))
            return JS_FALSE;

        return byte_array_set_index(context, obj, priv, idx, value_p);
    }

    /* We don't special-case anything else for now */

    return JS_TRUE;
}

static GByteArray *
gjs_g_byte_array_new(int preallocated_length)
{
    GByteArray *array;

    /* can't use g_byte_array_new() because we need to clear to zero.
     * We nul-terminate too for ease of toString() and for security
     * paranoia.
     */
    array =  (GByteArray*) g_array_sized_new (TRUE, /* nul-terminated */
                                              TRUE, /* clear to zero */
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
            return JS_FALSE;
        }
    }

    priv = g_slice_new0(ByteArrayInstance);
    priv->array = gjs_g_byte_array_new(preallocated_length);
    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    GJS_NATIVE_CONSTRUCTOR_FINISH(byte_array);

    return JS_TRUE;
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
        g_byte_array_free(priv->array, TRUE);
        priv->array = NULL;
    } else if (priv->bytes) {
        g_clear_pointer(&priv->bytes, g_bytes_unref);
    }

    g_slice_free(ByteArrayInstance, priv);
}

/* implement toString() with an optional encoding arg */
static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JSObject *object = JSVAL_TO_OBJECT(argv.thisv());
    ByteArrayInstance *priv;
    char *encoding;
    gboolean encoding_is_utf8;
    gchar *data;

    priv = priv_from_js(context, object);

    if (priv == NULL)
        return JS_TRUE; /* prototype, not instance */

    byte_array_ensure_array(priv);

    if (argc >= 1 &&
        JSVAL_IS_STRING(argv[0])) {
        if (!gjs_string_to_utf8(context, argv[0], &encoding))
            return JS_FALSE;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            encoding_is_utf8 = TRUE;
            g_free(encoding);
            encoding = NULL;
        } else {
            encoding_is_utf8 = FALSE;
        }
    } else {
        encoding_is_utf8 = TRUE;
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
        jsval retval;
        JSBool ok;

        ok = gjs_string_from_utf8(context,
                                  data,
                                  priv->array->len,
                                  &retval);
        if (ok)
            argv.rval().set(retval);
        return ok;
    } else {
        JSBool ok = JS_FALSE;
        gsize bytes_written;
        GError *error;
        JSString *s;
        char *u16_str;

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
            return JS_FALSE;
        }

        /* bytes_written should be bytes in a UTF-16 string so
         * should be a multiple of 2
         */
        g_assert((bytes_written % 2) == 0);

        s = JS_NewUCStringCopyN(context,
                                (jschar*) u16_str,
                                bytes_written / 2);
        if (s != NULL) {
            ok = JS_TRUE;
            argv.rval().set(STRING_TO_JSVAL(s));
        }

        g_free(u16_str);
        return ok;
    }
}

static JSBool
to_gbytes_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JS::CallReceiver rec = JS::CallReceiverFromVp(vp);
    JSObject *object = JSVAL_TO_OBJECT(rec.thisv());
    ByteArrayInstance *priv;
    JSObject *ret_bytes_obj;
    GIBaseInfo *gbytes_info;

    priv = priv_from_js(context, object);
    if (priv == NULL)
        return JS_TRUE; /* prototype, not instance */
    
    byte_array_ensure_gbytes(priv);

    gbytes_info = g_irepository_find_by_gtype(NULL, G_TYPE_BYTES);
    ret_bytes_obj = gjs_boxed_from_c_struct(context, (GIStructInfo*)gbytes_info,
                                            priv->bytes, GJS_BOXED_CREATION_NONE);

    rec.rval().set(OBJECT_TO_JSVAL(ret_bytes_obj));
    return JS_TRUE;
}

/* Ensure that the module and class objects exists, and that in turn
 * ensures that JS_InitClass has been called. */
static JSObject *
byte_array_get_prototype(JSContext *context)
{
    jsval retval;
    JSObject *prototype;

    retval = gjs_get_global_slot (context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE);

    if (!JSVAL_IS_OBJECT (retval)) {
        if (!gjs_eval_with_scope(context, NULL,
                                 "imports.byteArray.ByteArray.prototype;", -1,
                                 "<internal>", &retval))
            g_error ("Could not import byte array prototype\n");
    }

    return JSVAL_TO_OBJECT(retval);
}

static JSObject*
byte_array_new(JSContext *context)
{
    JSObject *array;
    ByteArrayInstance *priv;

    array = JS_NewObject(context, &gjs_byte_array_class,
                         byte_array_get_prototype(context), NULL);

    priv = g_slice_new0(ByteArrayInstance);

    g_assert(priv_from_js(context, array) == NULL);
    JS_SetPrivate(array, priv);

    return array;
}

/* fromString() function implementation */
static JSBool
from_string_func(JSContext *context,
                 unsigned   argc,
                 jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    ByteArrayInstance *priv;
    char *encoding;
    gboolean encoding_is_utf8;
    JSObject *obj;
    JSBool retval = JS_FALSE;

    obj = byte_array_new(context);
    if (obj == NULL)
        return JS_FALSE;

    JS_AddObjectRoot(context, &obj);

    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    g_assert(argc > 0); /* because we specified min args 1 */

    priv->array = gjs_g_byte_array_new(0);

    if (!JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context,
                  "byteArray.fromString() called with non-string as first arg");
        goto out;
    }

    if (argc > 1 &&
        JSVAL_IS_STRING(argv[1])) {
        if (!gjs_string_to_utf8(context, argv[1], &encoding))
            goto out;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            encoding_is_utf8 = TRUE;
            g_free(encoding);
            encoding = NULL;
        } else {
            encoding_is_utf8 = FALSE;
        }
    } else {
        encoding_is_utf8 = TRUE;
    }

    if (encoding_is_utf8) {
        /* optimization? avoids iconv overhead and runs
         * libmozjs hardwired utf16-to-utf8.
         */
        char *utf8 = NULL;
        if (!gjs_string_to_utf8(context,
                                argv[0],
                                &utf8))
            goto out;

        g_byte_array_set_size(priv->array, 0);
        g_byte_array_append(priv->array, (guint8*) utf8, strlen(utf8));
        g_free(utf8);
    } else {
        char *encoded;
        gsize bytes_written;
        GError *error;
        const jschar *u16_chars;
        gsize u16_len;

        u16_chars = JS_GetStringCharsAndLength(context, JSVAL_TO_STRING(argv[0]), &u16_len);
        if (u16_chars == NULL)
            goto out;

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
            goto out;
        }

        g_byte_array_set_size(priv->array, 0);
        g_byte_array_append(priv->array, (guint8*) encoded, bytes_written);

        g_free(encoded);
    }

    argv.rval().set(OBJECT_TO_JSVAL(obj));

    retval = JS_TRUE;
 out:
    JS_RemoveObjectRoot(context, &obj);
    return retval;
}

/* fromArray() function implementation */
static JSBool
from_array_func(JSContext *context,
                unsigned   argc,
                jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    ByteArrayInstance *priv;
    guint32 len;
    guint32 i;
    JSObject *obj;
    JSBool ret = JS_FALSE;

    obj = byte_array_new(context);
    if (obj == NULL)
        return JS_FALSE;

    JS_AddObjectRoot(context, &obj);

    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    g_assert(argc > 0); /* because we specified min args 1 */

    priv->array = gjs_g_byte_array_new(0);

    if (!JS_IsArrayObject(context, JSVAL_TO_OBJECT(argv[0]))) {
        gjs_throw(context,
                  "byteArray.fromArray() called with non-array as first arg");
        goto out;
    }

    if (!JS_GetArrayLength(context, JSVAL_TO_OBJECT(argv[0]), &len)) {
        gjs_throw(context,
                  "byteArray.fromArray() can't get length of first array arg");
        goto out;
    }

    g_byte_array_set_size(priv->array, len);

    for (i = 0; i < len; ++i) {
        jsval elem;
        guint8 b;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(argv[0]), i, &elem)) {
            /* this means there was an exception, while elem == JSVAL_VOID
             * means no element found
             */
            goto out;
        }

        if (JSVAL_IS_VOID(elem))
            continue;

        if (!gjs_value_to_byte(context, elem, &b))
            goto out;

        g_array_index(priv->array, guint8, i) = b;
    }

    ret = JS_TRUE;
    argv.rval().set(OBJECT_TO_JSVAL(obj));
 out:
    JS_RemoveObjectRoot(context, &obj);
    return ret;
}

static JSBool
from_gbytes_func(JSContext *context,
                 unsigned   argc,
                 jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JSObject *bytes_obj;
    GBytes *gbytes;
    ByteArrayInstance *priv;
    JSObject *obj;
    JSBool ret = JS_FALSE;

    if (!gjs_parse_call_args(context, "overrides_gbytes_to_array", "o", argv,
                        "bytes", &bytes_obj))
        return JS_FALSE;

    if (!gjs_typecheck_boxed(context, bytes_obj, NULL, G_TYPE_BYTES, TRUE))
        return JS_FALSE;

    gbytes = (GBytes*) gjs_c_struct_from_boxed(context, bytes_obj);

    obj = byte_array_new(context);
    if (obj == NULL)
        return JS_FALSE;
    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    priv->bytes = g_bytes_ref(gbytes);

    ret = JS_TRUE;
    argv.rval().set(OBJECT_TO_JSVAL(obj));
    return ret;
}

JSObject *
gjs_byte_array_from_byte_array (JSContext *context,
                                GByteArray *array)
{
    JSObject *object;
    ByteArrayInstance *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(array != NULL, NULL);

    object = JS_NewObject(context, &gjs_byte_array_class,
                          byte_array_get_prototype(context), NULL);
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

JSObject *
gjs_byte_array_from_bytes (JSContext *context,
                           GBytes    *bytes)
{
    JSObject *object;
    ByteArrayInstance *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(bytes != NULL, NULL);

    object = JS_NewObject(context, &gjs_byte_array_class,
                          byte_array_get_prototype(context), NULL);
    if (!object) {
        gjs_throw(context, "failed to create byte array");
        return NULL;
    }

    priv = g_slice_new0(ByteArrayInstance);
    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);
    priv->bytes = g_bytes_ref (bytes);

    return object;
}

GBytes *
gjs_byte_array_get_bytes (JSContext  *context,
                          JSObject   *object)
{
    ByteArrayInstance *priv;
    priv = priv_from_js(context, object);
    g_assert(priv != NULL);

    byte_array_ensure_gbytes(priv);

    return g_bytes_ref (priv->bytes);
}

GByteArray *
gjs_byte_array_get_byte_array (JSContext   *context,
                               JSObject    *obj)
{
    ByteArrayInstance *priv;
    priv = priv_from_js(context, obj);
    g_assert(priv != NULL);

    byte_array_ensure_array(priv);

    return g_byte_array_ref (priv->array);
}

void
gjs_byte_array_peek_data (JSContext  *context,
                          JSObject   *obj,
                          guint8    **out_data,
                          gsize      *out_len)
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

/* no idea what this is used for. examples in
 * spidermonkey use -1, -2, -3, etc. for tinyids.
 */
enum ByteArrayTinyId {
    BYTE_ARRAY_TINY_ID_LENGTH = -1
};

JSPropertySpec gjs_byte_array_proto_props[] = {
    { "length", BYTE_ARRAY_TINY_ID_LENGTH,
      JSPROP_PERMANENT,
      JSOP_WRAPPER ((JSPropertyOp) byte_array_length_getter),
      JSOP_WRAPPER ((JSStrictPropertyOp) byte_array_length_setter),
    },
    { NULL }
};

JSFunctionSpec gjs_byte_array_proto_funcs[] = {
    { "toString", JSOP_WRAPPER ((JSNative) to_string_func), 0, 0 },
    { "toGBytes", JSOP_WRAPPER ((JSNative) to_gbytes_func), 0, 0 },
    { NULL }
};

static JSFunctionSpec gjs_byte_array_module_funcs[] = {
    { "fromString", JSOP_WRAPPER (from_string_func), 1, 0 },
    { "fromArray", JSOP_WRAPPER (from_array_func), 1, 0 },
    { "fromGBytes", JSOP_WRAPPER (from_gbytes_func), 1, 0 },
    { NULL }
};

JSBool
gjs_define_byte_array_stuff(JSContext  *context,
                            JSObject  **module_out)
{
    JSObject *module;
    JSObject *prototype;

    module = JS_NewObject (context, NULL, NULL, NULL);

    prototype = JS_InitClass(context, module,
                             NULL,
                             &gjs_byte_array_class,
                             gjs_byte_array_constructor,
                             0,
                             &gjs_byte_array_proto_props[0],
                             &gjs_byte_array_proto_funcs[0],
                             NULL,
                             NULL);

    if (!JS_DefineFunctions(context, module, &gjs_byte_array_module_funcs[0]))
        return JS_FALSE;

    g_assert(JSVAL_IS_VOID(gjs_get_global_slot(context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE)));
    gjs_set_global_slot(context, GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE,
                        OBJECT_TO_JSVAL(prototype));

    *module_out = module;
    return JS_TRUE;
}
