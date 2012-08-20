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
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <util/log.h>
#include <jsapi.h>

typedef struct {
    GByteArray *array;
} ByteArrayInstance;

static struct JSClass gjs_byte_array_class;
static struct JSObject* gjs_byte_array_prototype;
GJS_DEFINE_PRIV_FROM_JS(ByteArrayInstance, gjs_byte_array_class)

static JSBool byte_array_get_prop      (JSContext    *context,
                                        JSObject     *obj,
                                        jsid          id,
                                        jsval        *value_p);
static JSBool byte_array_set_prop      (JSContext    *context,
                                        JSObject     *obj,
                                        jsid          id,
                                        JSBool        strict,
                                        jsval        *value_p);
static JSBool byte_array_new_resolve   (JSContext    *context,
                                        JSObject     *obj,
                                        jsid          id,
                                        uintN         flags,
                                        JSObject    **objp);
GJS_NATIVE_CONSTRUCTOR_DECLARE(byte_array);
static void   byte_array_finalize      (JSContext    *context,
                                        JSObject     *obj);


/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_byte_array_class = {
    "ByteArray",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_CONSTRUCT_PROTOTYPE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START,
    JS_PropertyStub,
    JS_PropertyStub,
    byte_array_get_prop,
    byte_array_set_prop,
    NULL,
    (JSResolveOp) byte_array_new_resolve, /* cast due to new sig */
    JS_ConvertStub,
    byte_array_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};


static JSBool
gjs_value_from_gsize(JSContext         *context,
                     gsize              v,
                     jsval             *value_p)
{
    if (v > (gsize) JSVAL_INT_MAX) {
        *value_p = INT_TO_JSVAL(v);
        return JS_TRUE;
    } else {
        return JS_NewNumberValue(context, v, value_p);
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
                     JSObject          *obj,
                     ByteArrayInstance *priv,
                     gsize              idx,
                     jsval             *value_p)
{
    guint8 v;

    if (idx >= priv->array->len) {
        gjs_throw(context,
                  "Index %" G_GSIZE_FORMAT " is out of range for ByteArray length %u",
                  idx,
                  priv->array->len);
        return JS_FALSE;
    }

    v = g_array_index(priv->array, guint8, idx);
    *value_p = INT_TO_JSVAL(v);

    return JS_TRUE;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
byte_array_get_prop(JSContext *context,
                    JSObject  *obj,
                    jsid       id,
                    jsval     *value_p)
{
    ByteArrayInstance *priv;
    jsval id_value;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->array == NULL)
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
                         JSObject  *obj,
                         jsid       id,
                         jsval     *value_p)
{
    ByteArrayInstance *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->array == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    return gjs_value_from_gsize(context, priv->array->len,
                                value_p);
}

static JSBool
byte_array_length_setter(JSContext *context,
                         JSObject  *obj,
                         jsid       id,
                         JSBool     strict,
                         jsval     *value_p)
{
    ByteArrayInstance *priv;
    gsize len = 0;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->array == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    if (!gjs_value_to_gsize(context, *value_p,
                            &len)) {
        gjs_throw(context,
                  "Can't set ByteArray length to non-integer");
        return JS_FALSE;
    }
    g_byte_array_set_size(priv->array, len);
    return JS_TRUE;
}

static JSBool
byte_array_set_index(JSContext         *context,
                     JSObject          *obj,
                     ByteArrayInstance *priv,
                     gsize              idx,
                     jsval             *value_p)
{
    guint8 v;

    if (!gjs_value_to_byte(context, *value_p,
                           &v)) {
        return JS_FALSE;
    }

    /* grow the array if necessary */
    if (idx >= priv->array->len) {
        g_byte_array_set_size(priv->array,
                              idx + 1);
    }

    g_array_index(priv->array, guint8, idx) = v;

    /* we could have coerced a double or something, be sure
     * *value_p is set to our actual set value
     */
    *value_p = INT_TO_JSVAL(v);

    return JS_TRUE;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is JS_FALSE on OOM/exception.
 */
static JSBool
byte_array_set_prop(JSContext *context,
                    JSObject  *obj,
                    jsid       id,
                    JSBool     strict,
                    jsval     *value_p)
{
    ByteArrayInstance *priv;
    jsval id_value;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->array == NULL)
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

    /* FIXME: note that the prop will also have been set in JS in the
     * usual hash table... this is pretty wasteful and bloated. But I
     * don't know how to turn it off. The set property function
     * is only a hook, not a replacement.
     */
    return JS_TRUE;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 *
 * *objp will be the original object the property access was on, rather than the
 * prototype that "obj" may be, due to JSCLASS_NEW_RESOLVE_GETS_START
 */
static JSBool
byte_array_new_resolve(JSContext *context,
                       JSObject  *obj,
                       jsid       id,
                       uintN      flags,
                       JSObject **objp)
{
    ByteArrayInstance *priv;
    jsval id_val;

    priv = priv_from_js(context, *objp);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->array == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    if (!JS_IdToValue(context, id, &id_val))
        return JS_FALSE;

    if (JSVAL_IS_NUMBER(id_val)) {
        gsize idx;
        if (!gjs_value_to_gsize(context, id_val, &idx))
            return JS_FALSE;
        if (idx >= priv->array->len) {
            *objp = NULL;
        } else {
            /* leave objp set */
            g_assert(*objp != NULL);

            /* define the property - AAARGH. Best I can tell from
             * reading the source, this is unavoidable...
             * which means using "for each" or "for ... in" on byte
             * arrays is a horrible, horrible idea. FIXME - but how?
             *
             * The issue is that spidermonkey only calls resolve,
             * not get, as it iterates. So you can lazy-define
             * a property but must define it.
             */
            if (!JS_DefinePropertyById(context,
                                       *objp,
                                       id,
                                       JSVAL_VOID,
                                       byte_array_get_prop,
                                       byte_array_set_prop,
                                       JSPROP_ENUMERATE))
                return JS_FALSE;
        }
    }

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

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(byte_array)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(byte_array)
    ByteArrayInstance *priv;
    JSObject *proto;
    gboolean is_proto;
    JSClass *obj_class;
    JSClass *proto_class;
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

    g_assert(priv_from_js(context, object) == NULL);

    JS_SetPrivate(context, object, priv);

    proto = JS_GetPrototype(context, object);

    /* If we're constructing the prototype, its __proto__ is not the same
     * class as us, but if we're constructing an instance, the prototype
     * has the same class.
     */
    obj_class = JS_GetClass(context, object);
    proto_class = JS_GetClass(context, proto);

    is_proto = (obj_class != proto_class);

    if (!is_proto) {
        priv->array = gjs_g_byte_array_new(preallocated_length);
    }

    GJS_NATIVE_CONSTRUCTOR_FINISH(byte_array);

    return JS_TRUE;
}

static void
byte_array_finalize(JSContext *context,
                    JSObject  *obj)
{
    ByteArrayInstance *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return; /* possible? probably not */

    if (priv->array) {
        g_byte_array_free(priv->array, TRUE);
        priv->array = NULL;
    }

    g_slice_free(ByteArrayInstance, priv);
}

/* implement toString() with an optional encoding arg */
static JSBool
to_string_func(JSContext *context,
               uintN      argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *object = JS_THIS_OBJECT(context, vp);
    ByteArrayInstance *priv;
    char *encoding;
    gboolean encoding_is_utf8;
    gchar *data;

    priv = priv_from_js(context, object);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */

    encoding_is_utf8 = TRUE;
    if (argc >= 1 &&
        JSVAL_IS_STRING(argv[0])) {
        encoding = gjs_string_get_ascii(context, argv[0]);
        if (encoding == NULL)
            return JS_FALSE;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            g_free(encoding);
            encoding_is_utf8 = TRUE;
        } else {
            encoding_is_utf8 = FALSE;
        }
    } else {
        encoding = "UTF-8";
    }

    if (priv->array->len == 0)
        /* the internal data pointer could be NULL in this case */
        data = "";
    else
        data = (gchar*)priv->array->data;

    if (encoding_is_utf8) {
        /* optimization, avoids iconv overhead and runs
         * glib's hardwired utf8-to-utf16
         */
        jsval retval;
        JSBool ok;

        ok = gjs_string_from_utf8(context,
                                  data,
                                  priv->array->len,
                                  &retval);
        if (ok)
            JS_SET_RVAL(context, vp, retval);
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
            JS_SET_RVAL(context, vp, STRING_TO_JSVAL(s));
        }

        g_free(u16_str);
        return ok;
    }
}

static JSObject*
byte_array_new(JSContext *context)
{
    JSObject *array;
    ByteArrayInstance *priv;

    array = JS_NewObject(context, &gjs_byte_array_class, gjs_byte_array_prototype, NULL);

    priv = g_slice_new0(ByteArrayInstance);
    priv->array = gjs_g_byte_array_new(0);

    g_assert(priv_from_js(context, array) == NULL);
    JS_SetPrivate(context, array, priv);

    return array;
}

/* fromString() function implementation */
static JSBool
from_string_func(JSContext *context,
                 uintN      argc,
                 jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
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

    if (!JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context,
                  "byteArray.fromString() called with non-string as first arg");
        goto out;
    }

    encoding_is_utf8 = TRUE;
    if (argc > 1 &&
        JSVAL_IS_STRING(argv[1])) {
        encoding = gjs_string_get_ascii(context, argv[1]);
        if (encoding == NULL)
            goto out;

        /* maybe we should be smarter about utf8 synonyms here.
         * doesn't matter much though. encoding_is_utf8 is
         * just an optimization anyway.
         */
        if (strcmp(encoding, "UTF-8") == 0) {
            g_free(encoding);
            encoding_is_utf8 = TRUE;
        } else {
            encoding_is_utf8 = FALSE;
        }
    } else {
        encoding = "UTF-8";
    }

    if (encoding_is_utf8) {
        /* optimization? avoids iconv overhead and runs
         * glib's hardwired utf16-to-utf8.
         * Does a gratuitous copy/strlen, but
         * the generic path below also has
         * gratuitous copy. Could be fixed for this path,
         * if it ever turns out to matter.
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
    
    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(obj));

    retval = JS_TRUE;
 out:
    JS_RemoveObjectRoot(context, &obj);
    return retval;
}

/* fromArray() function implementation */
static JSBool
from_array_func(JSContext *context,
                uintN      argc,
                jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    ByteArrayInstance *priv;
    jsuint len;
    jsuint i;
    JSObject *obj;
    JSBool ret = JS_FALSE;

    obj = byte_array_new(context);
    if (obj == NULL)
        return JS_FALSE;

    JS_AddObjectRoot(context, &obj);

    priv = priv_from_js(context, obj);
    g_assert (priv != NULL);

    g_assert(argc > 0); /* because we specified min args 1 */

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
    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(obj));
 out:
    JS_RemoveObjectRoot(context, &obj);
    return ret;
}

JSObject *
gjs_byte_array_from_byte_array (JSContext *context,
                                GByteArray *array)
{
    JSObject *object;
    ByteArrayInstance *priv;
    static gboolean init = FALSE;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(array != NULL, NULL);

    if (!init) {
        jsval rval;
        JS_EvaluateScript(context, JS_GetGlobalObject(context),
                          "imports.byteArray.ByteArray;", 27,
                          "<internal>", 1, &rval);
        init = TRUE;
    }
    object = JS_NewObject(context, &gjs_byte_array_class,
                          gjs_byte_array_prototype, NULL);
    if (!object) {
        gjs_throw(context, "failed to create byte array");
        return NULL;
    }

    priv = g_slice_new0(ByteArrayInstance);
    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);
    priv->array = g_byte_array_new();
    priv->array->data = g_memdup(array->data, array->len);
    priv->array->len = array->len;

    return object;
}

GByteArray*
gjs_byte_array_get_byte_array (JSContext  *context,
                               JSObject   *object)
{
    ByteArrayInstance *priv;

    priv = priv_from_js(context, object);
    if (priv == NULL)
        return NULL; /* wrong class passed in */

    return priv->array;
}

/* no idea what this is used for. examples in
 * spidermonkey use -1, -2, -3, etc. for tinyids.
 */
enum ByteArrayTinyId {
    BYTE_ARRAY_TINY_ID_LENGTH = -1
};

static JSPropertySpec gjs_byte_array_proto_props[] = {
    { "length", BYTE_ARRAY_TINY_ID_LENGTH,
      JSPROP_PERMANENT | JSPROP_SHARED,
      byte_array_length_getter,
      byte_array_length_setter
    },
    { NULL }
};

static JSFunctionSpec gjs_byte_array_proto_funcs[] = {
    { "toString", (JSNative) to_string_func, 0, 0 },
    { NULL }
};

static JSFunctionSpec gjs_byte_array_module_funcs[] = {
    { "fromString", (JSNative)from_string_func, 1, 0 },
    { "fromArray", (JSNative)from_array_func, 1, 0 },
    { NULL }
};

JSBool
gjs_define_byte_array_stuff(JSContext      *context,
                            JSObject       *in_object)
{
    JSObject *global = gjs_get_import_global(context);
    gjs_byte_array_prototype = JS_InitClass(context, global,
                             NULL,
                             &gjs_byte_array_class,
                             gjs_byte_array_constructor,
                             0,
                             &gjs_byte_array_proto_props[0],
                             &gjs_byte_array_proto_funcs[0],
                             NULL,
                             NULL);
    jsval rval;

    if (gjs_byte_array_prototype == NULL)
        return JS_FALSE;

    if (!gjs_object_require_property(
            context, global, NULL,
            "ByteArray", &rval))
        return JS_FALSE;

    if (!JS_DefineProperty(context, in_object, "ByteArray",
                           rval, NULL, NULL, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunctions(context, in_object, &gjs_byte_array_module_funcs[0]))
        return JS_FALSE;

    return JS_TRUE;
}
