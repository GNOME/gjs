/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include "arg.h"
#include "gtype.h"
#include "object.h"
#include "foreign.h"
#include "fundamental.h"
#include "boxed.h"
#include "union.h"
#include "param.h"
#include "value.h"
#include "gerror.h"
#include "gjs/byteArray.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>

JSBool
_gjs_flags_value_is_valid(JSContext   *context,
                          GType        gtype,
                          gint64       value)
{
    GFlagsValue *v;
    guint32 tmpval;
    void *klass;

    /* FIXME: Do proper value check for flags with GType's */
    if (gtype == G_TYPE_NONE)
        return JS_TRUE;

    klass = g_type_class_ref(gtype);

    /* check all bits are defined for flags.. not necessarily desired */
    tmpval = (guint32)value;
    if (tmpval != value) { /* Not a guint32 */
        gjs_throw(context,
                  "0x%" G_GINT64_MODIFIER "x is not a valid value for flags %s",
                  value, g_type_name(G_TYPE_FROM_CLASS(klass)));
        return JS_FALSE;
    }

    while (tmpval) {
        v = g_flags_get_first_value((GFlagsClass *) klass, tmpval);
        if (!v) {
            gjs_throw(context,
                      "0x%x is not a valid value for flags %s",
                      (guint32)value, g_type_name(G_TYPE_FROM_CLASS(klass)));
            return JS_FALSE;
        }

        tmpval &= ~v->value;
    }
    g_type_class_unref(klass);

    return JS_TRUE;
}

JSBool
_gjs_enum_value_is_valid(JSContext  *context,
                         GIEnumInfo *enum_info,
                         gint64      value)
{
    JSBool found;
    int n_values;
    int i;

    n_values = g_enum_info_get_n_values(enum_info);
    found = JS_FALSE;

    for (i = 0; i < n_values; ++i) {
        GIValueInfo *value_info;
        gint64 enum_value;

        value_info = g_enum_info_get_value(enum_info, i);
        enum_value = g_value_info_get_value(value_info);
        g_base_info_unref((GIBaseInfo *)value_info);

        if (enum_value == value) {
            found = JS_TRUE;
            break;
        }
    }

    if (!found) {
        gjs_throw(context,
                  "%" G_GINT64_MODIFIER "d is not a valid value for enumeration %s",
                  value, g_base_info_get_name((GIBaseInfo *)enum_info));
    }

    return found;
}

static gboolean
_gjs_enum_uses_signed_type (GIEnumInfo *enum_info)
{
    switch (g_enum_info_get_storage_type (enum_info)) {
    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_INT32:
    case GI_TYPE_TAG_INT64:
        return TRUE;
    default:
        return FALSE;
    }
}

/* This is hacky - g_function_info_invoke() and g_field_info_get/set_field() expect
 * arg->v_int to have the enum value in arg->v_int and depend on all flags and
 * enumerations being passed on the stack in a 32-bit field. See FIXME comment in
 * g_field_info_get_field. The same assumption of enums cast to 32-bit signed integers
 * is found in g_value_set_enum/g_value_set_flags().
 */

gint64
_gjs_enum_from_int (GIEnumInfo *enum_info,
                    int         int_value)
{
    if (_gjs_enum_uses_signed_type (enum_info))
        return (gint64)int_value;
    else
        return (gint64)(guint32)int_value;
}

/* Here for symmetry, but result is the same for the two cases */
static int
_gjs_enum_to_int (GIEnumInfo *enum_info,
                  gint64      value)
{
    return (int)value;
}

/* Check if an argument of the given needs to be released if we created it
 * from a JS value to pass it into a function and aren't transfering ownership.
 */
static gboolean
type_needs_release (GITypeInfo *type_info,
                    GITypeTag   type_tag)
{
    switch (type_tag) {
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
        return TRUE;
    case GI_TYPE_TAG_INTERFACE: {
        GIBaseInfo* interface_info;
        GIInfoType interface_type;
        GType gtype;
        gboolean needs_release;

        interface_info = g_type_info_get_interface(type_info);
        g_assert(interface_info != NULL);

        interface_type = g_base_info_get_type(interface_info);

        switch(interface_type) {

        case GI_INFO_TYPE_STRUCT:
        case GI_INFO_TYPE_ENUM:
        case GI_INFO_TYPE_FLAGS:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_UNION:
        case GI_INFO_TYPE_BOXED:
            /* These are subtypes of GIRegisteredTypeInfo for which the
             * cast is safe */
            gtype = g_registered_type_info_get_g_type
                ((GIRegisteredTypeInfo*)interface_info);
            break;

        case GI_INFO_TYPE_VALUE:
            /* Special case for GValues */
            gtype = G_TYPE_VALUE;
            break;

        default:
            /* Everything else */
            gtype = G_TYPE_NONE;
            break;
        }

        if (g_type_is_a(gtype, G_TYPE_CLOSURE))
            needs_release = TRUE;
        else if (g_type_is_a(gtype, G_TYPE_VALUE))
            needs_release = g_type_info_is_pointer(type_info);
        else
            needs_release = FALSE;

        g_base_info_unref(interface_info);

        return needs_release;
    }
    default:
        return FALSE;
    }
}

/* Check if an argument of the given needs to be released if we obtained it
 * from out argument (or the return value), and we're transferring ownership
 */
static JSBool
type_needs_out_release(GITypeInfo *type_info,
                       GITypeTag   type_tag)
{
    switch (type_tag) {
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
        return TRUE;
    case GI_TYPE_TAG_INTERFACE: {
        GIBaseInfo* interface_info;
        GIInfoType interface_type;
        gboolean needs_release;

        interface_info = g_type_info_get_interface(type_info);
        g_assert(interface_info != NULL);

        interface_type = g_base_info_get_type(interface_info);

        switch(interface_type) {
        case GI_INFO_TYPE_ENUM:
        case GI_INFO_TYPE_FLAGS:
            needs_release = FALSE;
            break;

        default:
            needs_release = TRUE;
        }

        g_base_info_unref(interface_info);

        return needs_release;
    }
    default:
        return FALSE;
    }
}

static JSBool
gjs_array_to_g_list(JSContext   *context,
                    jsval        array_value,
                    unsigned int length,
                    GITypeInfo  *param_info,
                    GITransfer   transfer,
                    GITypeTag    list_type,
                    GList      **list_p,
                    GSList     **slist_p)
{
    guint32 i;
    GList *list;
    GSList *slist;
    jsval elem;

    list = NULL;
    slist = NULL;

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release (param_info, g_type_info_get_tag(param_info))) {
            /* FIXME: to make this work, we'd have to keep a list of temporary
             * GArguments for the function call so we could free them after
             * the surrounding container had been freed by the callee.
             */
            gjs_throw(context,
                      "Container transfer for in parameters not supported");
            return JS_FALSE;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    for (i = 0; i < length; ++i) {
        GArgument elem_arg = { 0 };

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        /* FIXME we don't know if the list elements can be NULL.
         * gobject-introspection needs to tell us this.
         * Always say they can't for now.
         */
        if (!gjs_value_to_g_argument(context,
                                     elem,
                                     param_info,
                                     NULL,
                                     GJS_ARGUMENT_LIST_ELEMENT,
                                     transfer,
                                     FALSE,
                                     &elem_arg)) {
            return JS_FALSE;
        }

        if (list_type == GI_TYPE_TAG_GLIST) {
            /* GList */
            list = g_list_prepend(list, elem_arg.v_pointer);
        } else {
            /* GSList */
            slist = g_slist_prepend(slist, elem_arg.v_pointer);
        }
    }

    list = g_list_reverse(list);
    slist = g_slist_reverse(slist);

    *list_p = list;
    *slist_p = slist;

    return JS_TRUE;
}

static JSBool
gjs_object_to_g_hash(JSContext   *context,
                     jsval        hash_value,
                     GITypeInfo  *key_param_info,
                     GITypeInfo  *val_param_info,
                     GITransfer   transfer,
                     GHashTable **hash_p)
{
    GHashTable *result = NULL;
    JSObject *props;
    JSObject *iter;
    jsid prop_id;

    g_assert(JSVAL_IS_OBJECT(hash_value));
    props = JSVAL_TO_OBJECT(hash_value);

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release (key_param_info, g_type_info_get_tag(key_param_info)) ||
            type_needs_release (val_param_info, g_type_info_get_tag(val_param_info))) {
            /* FIXME: to make this work, we'd have to keep a list of temporary
             * GArguments for the function call so we could free them after
             * the surrounding container had been freed by the callee.
             */
            gjs_throw(context,
                      "Container transfer for in parameters not supported");
            return JS_FALSE;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    iter = JS_NewPropertyIterator(context, props);
    if (iter == NULL)
        return JS_FALSE;

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, iter, &prop_id))
        return JS_FALSE;

    /* Don't use key/value destructor functions here, because we can't
     * construct correct ones in general if the value type is complex.
     * Rely on the type-aware g_argument_release functions. */
   result = g_hash_table_new(g_str_hash, g_str_equal);

   while (!JSID_IS_VOID(prop_id)) {
        jsval key_js, val_js;
        GArgument key_arg = { 0 }, val_arg = { 0 };

        if (!JS_IdToValue(context, prop_id, &key_js))
            goto free_hash_and_fail;

        /* Type check key type. */
        if (!gjs_value_to_g_argument(context, key_js, key_param_info, NULL,
                                     GJS_ARGUMENT_HASH_ELEMENT,
                                     transfer,
                                     FALSE /* don't allow null */,
                                     &key_arg))
            goto free_hash_and_fail;

        if (!JS_GetPropertyById(context, props, prop_id, &val_js))
            goto free_hash_and_fail;

        /* Type check and convert value to a c type */
        if (!gjs_value_to_g_argument(context, val_js, val_param_info, NULL,
                                     GJS_ARGUMENT_HASH_ELEMENT,
                                     transfer,
                                     TRUE /* allow null */,
                                     &val_arg))
            goto free_hash_and_fail;

        g_hash_table_insert(result, key_arg.v_pointer, val_arg.v_pointer);

        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, iter, &prop_id))
            goto free_hash_and_fail;
    }

    *hash_p = result;
    return JS_TRUE;

 free_hash_and_fail:
    g_hash_table_destroy(result);
    return JS_FALSE;
}

JSBool
gjs_array_from_strv(JSContext   *context,
                    jsval       *value_p,
                    const char **strv)
{
    JSObject *obj;
    jsval elem;
    guint i;
    JSBool result = JS_FALSE;

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
        return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);

    elem = JSVAL_VOID;
    JS_AddValueRoot(context, &elem);

    for (i = 0; strv[i] != NULL; i++) {
        if (!gjs_string_from_utf8 (context, strv[i], -1, &elem))
            goto out;

        if (!JS_DefineElement(context, obj, i, elem,
                              NULL, NULL, JSPROP_ENUMERATE)) {
            goto out;
        }
    }

    result = JS_TRUE;

out:
    JS_RemoveValueRoot(context, &elem);

    return result;
}

JSBool
gjs_array_to_strv(JSContext   *context,
                  jsval        array_value,
                  unsigned int length,
                  void       **arr_p)
{
    char **result;
    guint32 i;

    result = g_new0(char *, length+1);

    for (i = 0; i < length; ++i) {
        jsval elem;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        if (!JSVAL_IS_STRING(elem)) {
            gjs_throw(context,
                      "Invalid element in string array");
            g_strfreev(result);
            return JS_FALSE;
        }
        if (!gjs_string_to_utf8(context, elem, (char **)&(result[i]))) {
            g_strfreev(result);
            return JS_FALSE;
        }
    }

    *arr_p = result;

    return JS_TRUE;
}

static JSBool
gjs_string_to_intarray(JSContext   *context,
                       jsval        string_val,
                       GITypeInfo  *param_info,
                       void       **arr_p,
                       gsize       *length)
{
    GITypeTag element_type;
    char *result;
    guint16 *result16;

    element_type = g_type_info_get_tag(param_info);

    switch (element_type) {
    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_UINT8:
        if (!gjs_string_to_utf8(context, string_val, &result))
            return JS_FALSE;
        *arr_p = result;
        *length = strlen(result);
        return JS_TRUE;

    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_UINT16:
        if (!gjs_string_get_uint16_data(context, string_val,
                                        &result16, length))
            return JS_FALSE;
        *arr_p = result16;
        return JS_TRUE;

    default:
        /* can't convert a string to this type */
        gjs_throw(context, "Cannot convert string to array of '%s'",
                  g_type_tag_to_string (element_type));
        return JS_FALSE;
    }
}

static JSBool
gjs_array_to_intarray(JSContext   *context,
                      jsval        array_value,
                      unsigned int length,
                      void       **arr_p,
                      unsigned intsize,
                      gboolean is_signed)
{
    /* nasty union types in an attempt to unify the various int types */
    union { guint32 u; gint32 i; } intval;
    void *result;
    unsigned i;

    /* add one so we're always zero terminated */
    result = g_malloc0((length+1) * intsize);

    for (i = 0; i < length; ++i) {
        jsval elem;
        JSBool success;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        /* do whatever sign extension is appropriate */
        success = (is_signed) ?
            JS_ValueToECMAInt32(context, elem, &(intval.i)) :
            JS_ValueToECMAUint32(context, elem, &(intval.u));

        if (!success) {
            g_free(result);
            gjs_throw(context,
                      "Invalid element in int array");
            return JS_FALSE;
        }
        /* Note that this is truncating assignment. */
        switch (intsize) {
        case 1:
            ((guint8*)result)[i] = (gint8) intval.u; break;
        case 2:
            ((guint16*)result)[i] = (gint16) intval.u; break;
        case 4:
            ((guint32*)result)[i] = (gint32) intval.u; break;
        default:
            g_assert_not_reached();
        }
    }

    *arr_p = result;

    return JS_TRUE;
}

static JSBool
gjs_gtypearray_to_array(JSContext   *context,
                        jsval        array_value,
                        unsigned int length,
                        void       **arr_p)
{
    GType *result;
    unsigned i;

    /* add one so we're always zero terminated */
    result = (GType *) g_malloc0((length+1) * sizeof(GType));

    for (i = 0; i < length; ++i) {
        jsval elem;
        GType gtype;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(result);
            gjs_throw(context, "Missing array element %u", i);
            return JS_FALSE;
        }

        if (!JSVAL_IS_OBJECT(elem))
            goto err;

        gtype = gjs_gtype_get_actual_gtype(context, JSVAL_TO_OBJECT(elem));
        if (gtype == G_TYPE_INVALID)
            goto err;

        result[i] = gtype;
    }

    *arr_p = result;

    return JS_TRUE;

 err:
    g_free(result);
    gjs_throw(context, "Invalid element in GType array");
    return JS_FALSE;
}

static JSBool
gjs_array_to_floatarray(JSContext   *context,
                        jsval        array_value,
                        unsigned int length,
                        void       **arr_p,
                        gboolean     is_double)
{
    unsigned int i;
    void *result;

    /* add one so we're always zero terminated */
    result = g_malloc0((length+1) * (is_double ? sizeof(double) : sizeof(float)));

    for (i = 0; i < length; ++i) {
        jsval elem;
        double val;
        JSBool success;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        /* do whatever sign extension is appropriate */
        success = JS_ValueToNumber(context, elem, &val);

        if (!success) {
            g_free(result);
            gjs_throw(context,
                      "Invalid element in array");
            return JS_FALSE;
        }

        /* Note that this is truncating assignment. */
        if (is_double) {
            double *darray = (double*)result;
            darray[i] = val;
        } else {
            float *farray = (float*)result;
            farray[i] = val;
        }
    }

    *arr_p = result;

    return JS_TRUE;
}

static JSBool
gjs_array_to_ptrarray(JSContext   *context,
                      jsval        array_value,
                      unsigned int length,
                      GITransfer   transfer,
                      GITypeInfo  *param_info,
                      void       **arr_p)
{
    unsigned int i;

    /* Always one extra element, to cater for null terminated arrays */
    void **array = (void **) g_malloc((length + 1) * sizeof(gpointer));
    array[length] = NULL;

    for (i = 0; i < length; i++) {
        jsval elem;
        GIArgument arg;
        arg.v_pointer = NULL;

        JSBool success;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(array);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        success = gjs_value_to_g_argument (context,
                                           elem,
                                           param_info,
                                           NULL, /* arg name */
                                           GJS_ARGUMENT_ARRAY_ELEMENT,
                                           transfer,
                                           FALSE, /* absent better information, FALSE for now */
                                           &arg);

        if (!success) {
            g_free(array);
            gjs_throw(context,
                      "Invalid element in array");
            return JS_FALSE;
        }

        array[i] = arg.v_pointer;
    }

    *arr_p = array;
    return JS_TRUE;
}

static JSBool
gjs_array_to_flat_gvalue_array(JSContext   *context,
                               jsval        array_value,
                               unsigned int length,
                               void       **arr_p)
{
    GValue *values = g_new0(GValue, length);
    unsigned int i;
    JSBool result = JS_TRUE;

    for (i = 0; i < length; i ++) {
        jsval elem;
        elem = JSVAL_VOID;

        if (!JS_GetElement(context, JSVAL_TO_OBJECT(array_value),
                           i, &elem)) {
            g_free(values);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return JS_FALSE;
        }

        result = gjs_value_to_g_value(context, elem, &values[i]);

        if (!result)
            break;
    }

    if (result)
        *arr_p = values;

    return result;
}

static JSBool
gjs_array_from_flat_gvalue_array(JSContext   *context,
                                 gpointer     array,
                                 unsigned int length,
                                 jsval       *value)
{
    GValue *values = (GValue *)array;
    unsigned int i;
    jsval *elems = g_newa(jsval, length);
    JSBool result = JS_TRUE;

    for (i = 0; i < length; i ++) {
        GValue *value = &values[i];
        result = gjs_value_from_g_value(context, &elems[i], value);
        if (!result)
            break;
    }

    if (result) {
        JSObject *jsarray;
        jsarray = JS_NewArrayObject(context, length, elems);
        *value = OBJECT_TO_JSVAL(jsarray);
    }

    return result;
}

static gboolean
is_gvalue(GIBaseInfo *info,
          GIInfoType  info_type)
{
    gboolean result = FALSE;

    switch(info_type) {
    case GI_INFO_TYPE_VALUE:
        result = TRUE;
        break;
    case GI_INFO_TYPE_STRUCT:
    case GI_INFO_TYPE_OBJECT:
    case GI_INFO_TYPE_INTERFACE:
    case GI_INFO_TYPE_BOXED:
        {
            GType gtype;
            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo *) info);

            result = g_type_is_a(gtype, G_TYPE_VALUE);
        }
        break;
    default:
        break;
    }

    return result;
}

static gboolean
is_gvalue_flat_array(GITypeInfo *param_info,
                     GITypeTag   element_type)
{
    GIBaseInfo *interface_info;
    GIInfoType info_type;
    gboolean result;

    if (element_type != GI_TYPE_TAG_INTERFACE)
        return FALSE;

    interface_info = g_type_info_get_interface(param_info);
    info_type = g_base_info_get_type(interface_info);

    /* Special case for GValue "flat arrays" */
    result = (is_gvalue(interface_info, info_type) &&
              !g_type_info_is_pointer(param_info));
    g_base_info_unref(interface_info);

    return result;
}

static JSBool
gjs_array_to_array(JSContext   *context,
                   jsval        array_value,
                   gsize        length,
                   GITransfer   transfer,
                   GITypeInfo  *param_info,
                   void       **arr_p)
{
    enum { UNSIGNED=FALSE, SIGNED=TRUE };
    GITypeTag element_type;

    element_type = g_type_info_get_tag(param_info);

    /* Special case for GValue "flat arrays" */
    if (is_gvalue_flat_array(param_info, element_type))
        return gjs_array_to_flat_gvalue_array(context, array_value, length, arr_p);

    if (element_type == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo *interface_info = g_type_info_get_interface(param_info);
        GIInfoType info_type = g_base_info_get_type(interface_info);
        if (info_type == GI_INFO_TYPE_ENUM || info_type == GI_INFO_TYPE_FLAGS)
            element_type = g_enum_info_get_storage_type ((GIEnumInfo*) interface_info);
        g_base_info_unref(interface_info);
    }

    switch (element_type) {
    case GI_TYPE_TAG_UTF8:
        return gjs_array_to_strv (context, array_value, length, arr_p);
    case GI_TYPE_TAG_UINT8:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 1, UNSIGNED);
    case GI_TYPE_TAG_INT8:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 1, SIGNED);
    case GI_TYPE_TAG_UINT16:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 2, UNSIGNED);
    case GI_TYPE_TAG_INT16:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 2, SIGNED);
    case GI_TYPE_TAG_UINT32:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 4, UNSIGNED);
    case GI_TYPE_TAG_INT32:
        return gjs_array_to_intarray
            (context, array_value, length, arr_p, 4, SIGNED);
    case GI_TYPE_TAG_FLOAT:
        return gjs_array_to_floatarray
            (context, array_value, length, arr_p, FALSE);
    case GI_TYPE_TAG_DOUBLE:
        return gjs_array_to_floatarray
            (context, array_value, length, arr_p, TRUE);
    case GI_TYPE_TAG_GTYPE:
        return gjs_gtypearray_to_array
            (context, array_value, length, arr_p);

    /* Everything else is a pointer type */
    case GI_TYPE_TAG_INTERFACE:
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
    case GI_TYPE_TAG_FILENAME:
        return gjs_array_to_ptrarray(context,
                                     array_value,
                                     length,
                                     transfer == GI_TRANSFER_CONTAINER ? GI_TRANSFER_NOTHING : transfer,
                                     param_info,
                                     arr_p);
    default:
        gjs_throw(context,
                  "Unhandled array element type %d", element_type);
        return JS_FALSE;
    }
}

static GArray*
gjs_g_array_new_for_type(JSContext    *context,
                         unsigned int  length,
                         GITypeInfo   *param_info)
{
    GITypeTag element_type;
    guint element_size;

    element_type = g_type_info_get_tag(param_info);

    if (element_type == GI_TYPE_TAG_INTERFACE) {
        GIInterfaceInfo *interface_info = g_type_info_get_interface(param_info);
        GIInfoType interface_type = g_base_info_get_type(interface_info);

        if (interface_type == GI_INFO_TYPE_ENUM
            || interface_type == GI_INFO_TYPE_FLAGS)
            element_type = g_enum_info_get_storage_type((GIEnumInfo*) interface_info);

        g_base_info_unref((GIBaseInfo*) interface_info);
    }

    switch (element_type) {
    case GI_TYPE_TAG_UINT8:
    case GI_TYPE_TAG_INT8:
      element_size = sizeof(guint8);
      break;
    case GI_TYPE_TAG_UINT16:
    case GI_TYPE_TAG_INT16:
      element_size = sizeof(guint16);
      break;
    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_INT32:
      element_size = sizeof(guint32);
      break;
    case GI_TYPE_TAG_UINT64:
    case GI_TYPE_TAG_INT64:
      element_size = sizeof(guint64);
      break;
    case GI_TYPE_TAG_FLOAT:
      element_size = sizeof(gfloat);
      break;
    case GI_TYPE_TAG_DOUBLE:
      element_size = sizeof(gdouble);
      break;
    case GI_TYPE_TAG_GTYPE:
      element_size = sizeof(GType);
      break;
    case GI_TYPE_TAG_INTERFACE:
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
      element_size = sizeof(gpointer);
      break;
    default:
        gjs_throw(context,
                  "Unhandled GArray element-type %d", element_type);
        return NULL;
    }

    return g_array_sized_new(TRUE, FALSE, element_size, length);
}

static gchar *
get_argument_display_name(const char     *arg_name,
                          GjsArgumentType arg_type)
{
    switch (arg_type) {
    case GJS_ARGUMENT_ARGUMENT:
        return g_strdup_printf("Argument '%s'", arg_name);
    case GJS_ARGUMENT_RETURN_VALUE:
        return g_strdup("Return value");
    case GJS_ARGUMENT_FIELD:
        return g_strdup_printf("Field '%s'", arg_name);
    case GJS_ARGUMENT_LIST_ELEMENT:
        return g_strdup("List element");
    case GJS_ARGUMENT_HASH_ELEMENT:
        return g_strdup("Hash element");
    case GJS_ARGUMENT_ARRAY_ELEMENT:
        return g_strdup("Array element");
    }

    g_assert_not_reached ();
}

static const char *
type_tag_to_human_string(GITypeInfo *type_info)
{
    GITypeTag tag;

    tag = g_type_info_get_tag(type_info);

    if (tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo *interface;
        const char *ret;

        interface = g_type_info_get_interface(type_info);
        ret = g_info_type_to_string(g_base_info_get_type(interface));

        g_base_info_unref(interface);
        return ret;
    } else {
        return g_type_tag_to_string(tag);
    }
}

static void
throw_invalid_argument(JSContext      *context,
                       jsval           value,
                       GITypeInfo     *arginfo,
                       const char     *arg_name,
                       GjsArgumentType arg_type)
{
    gchar *display_name = get_argument_display_name(arg_name, arg_type);

    gjs_throw(context, "Expected type %s for %s but got type '%s'",
              type_tag_to_human_string(arginfo),
              display_name,
              JS_GetTypeName(context, JS_TypeOfValue(context, value)));
    g_free(display_name);
}

static JSBool
gjs_array_to_explicit_array_internal(JSContext       *context,
                                     jsval            value,
                                     GITypeInfo      *type_info,
                                     const char      *arg_name,
                                     GjsArgumentType  arg_type,
                                     GITransfer       transfer,
                                     gboolean         may_be_null,
                                     gpointer        *contents,
                                     gsize           *length_p)
{
    JSBool ret = JS_FALSE;
    GITypeInfo *param_info;
    jsid length_name;
    JSBool found_length;

    param_info = g_type_info_get_param_type(type_info, 0);

    if ((JSVAL_IS_NULL(value) && !may_be_null) ||
        (!JSVAL_IS_STRING(value) && !JSVAL_IS_OBJECT(value) && !JSVAL_IS_NULL(value))) {
        throw_invalid_argument(context, value, param_info, arg_name, arg_type);
        goto out;
    }

    length_name = gjs_context_get_const_string(context, GJS_STRING_LENGTH);

    if (JSVAL_IS_NULL(value)) {
        *contents = NULL;
        *length_p = 0;
    } else if (JSVAL_IS_STRING(value)) {
        /* Allow strings as int8/uint8/int16/uint16 arrays */
        if (!gjs_string_to_intarray(context, value, param_info,
                                    contents, length_p))
            goto out;
    } else if (JS_HasPropertyById(context, JSVAL_TO_OBJECT(value), length_name, &found_length) &&
               found_length) {
        jsval length_value;
        guint32 length;

        if (!gjs_object_require_property(context,
                                         JSVAL_TO_OBJECT(value), NULL,
                                         length_name,
                                         &length_value) ||
            !JS_ValueToECMAUint32(context, length_value, &length)) {
            goto out;
        } else {
            if (!gjs_array_to_array(context,
                                    value,
                                    length,
                                    transfer,
                                    param_info,
                                    contents))
                goto out;

            *length_p = length;
        }
    } else {
        throw_invalid_argument(context, value, param_info, arg_name, arg_type);
        goto out;
    }

    ret = JS_TRUE;
 out:
    g_base_info_unref((GIBaseInfo*) param_info);

    return ret;
}

JSBool
gjs_value_to_g_argument(JSContext      *context,
                        jsval           value,
                        GITypeInfo     *type_info,
                        const char     *arg_name,
                        GjsArgumentType arg_type,
                        GITransfer      transfer,
                        gboolean        may_be_null,
                        GArgument      *arg)
{
    GITypeTag type_tag;
    gboolean wrong;
    gboolean out_of_range;
    gboolean report_type_mismatch;
    gboolean nullable_type;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting jsval to GArgument %s",
                      g_type_tag_to_string(type_tag));

    nullable_type = FALSE;
    wrong = FALSE; /* return JS_FALSE */
    out_of_range = FALSE;
    report_type_mismatch = FALSE; /* wrong=TRUE, and still need to gjs_throw a type problem */

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        nullable_type = TRUE;
        arg->v_pointer = NULL; /* just so it isn't uninitialized */
        break;

    case GI_TYPE_TAG_INT8: {
        gint32 i;
        if (!JS_ValueToInt32(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXINT8 || i < G_MININT8)
            out_of_range = TRUE;
        arg->v_int8 = (gint8)i;
        break;
    }
    case GI_TYPE_TAG_UINT8: {
        guint32 i;
        if (!JS_ValueToECMAUint32(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXUINT8)
            out_of_range = TRUE;
        arg->v_uint8 = (guint8)i;
        break;
    }
    case GI_TYPE_TAG_INT16: {
        gint32 i;
        if (!JS_ValueToInt32(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXINT16 || i < G_MININT16)
            out_of_range = TRUE;
        arg->v_int16 = (gint16)i;
        break;
    }

    case GI_TYPE_TAG_UINT16: {
        guint32 i;
        if (!JS_ValueToECMAUint32(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXUINT16)
            out_of_range = TRUE;
        arg->v_uint16 = (guint16)i;
        break;
    }

    case GI_TYPE_TAG_INT32:
        if (!JS_ValueToInt32(context, value, &arg->v_int))
            wrong = TRUE;
        break;

    case GI_TYPE_TAG_UINT32: {
        gdouble i;
        if (!JS_ValueToNumber(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXUINT32 || i < 0)
            out_of_range = TRUE;
        arg->v_uint32 = (guint32)i;
        break;
    }

    case GI_TYPE_TAG_INT64: {
        double v;
        if (!JS_ValueToNumber(context, value, &v))
            wrong = TRUE;
        if (v > G_MAXINT64 || v < G_MININT64)
            out_of_range = TRUE;
        arg->v_int64 = v;
    }
        break;

    case GI_TYPE_TAG_UINT64: {
        double v;
        if (!JS_ValueToNumber(context, value, &v))
            wrong = TRUE;
        if (v < 0)
            out_of_range = TRUE;
        /* XXX we fail with values close to G_MAXUINT64 */
        arg->v_uint64 = v;
    }
        break;

    case GI_TYPE_TAG_BOOLEAN:
        if (!JS_ValueToBoolean(context, value, &arg->v_boolean))
            wrong = TRUE;
        break;

    case GI_TYPE_TAG_FLOAT: {
        double v;
        if (!JS_ValueToNumber(context, value, &v))
            wrong = TRUE;
        if (v > G_MAXFLOAT || v < - G_MAXFLOAT)
            out_of_range = TRUE;
        arg->v_float = (gfloat)v;
    }
        break;

    case GI_TYPE_TAG_DOUBLE:
        if (!JS_ValueToNumber(context, value, &arg->v_double))
            wrong = TRUE;
        break;

    case GI_TYPE_TAG_UNICHAR:
        if (JSVAL_IS_STRING(value)) {
            if (!gjs_unichar_from_string(context, value, &arg->v_uint32))
                wrong = TRUE;
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;

    case GI_TYPE_TAG_GTYPE:
        if (JSVAL_IS_OBJECT(value)) {
            GType gtype;
            gtype = gjs_gtype_get_actual_gtype(context, JSVAL_TO_OBJECT(value));
            if (gtype == G_TYPE_INVALID)
                wrong = TRUE;
            arg->v_ssize = gtype;
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;

    case GI_TYPE_TAG_FILENAME:
        nullable_type = TRUE;
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
        } else if (JSVAL_IS_STRING(value)) {
            char *filename_str;
            if (gjs_string_to_filename(context, value, &filename_str))
                // doing this as a separate step to avoid type-punning
                arg->v_pointer = filename_str;
            else
                wrong = TRUE;
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;
    case GI_TYPE_TAG_UTF8:
        nullable_type = TRUE;
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
        } else if (JSVAL_IS_STRING(value)) {
            char *utf8_str;
            if (gjs_string_to_utf8(context, value, &utf8_str))
                // doing this as a separate step to avoid type-punning
                arg->v_pointer = utf8_str;
            else
                wrong = TRUE;
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;

    case GI_TYPE_TAG_ERROR:
        nullable_type = TRUE;
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
        } else if (JSVAL_IS_OBJECT(value)) {
            if (gjs_typecheck_gerror(context, JSVAL_TO_OBJECT(value),
                                      JS_TRUE)) {
                arg->v_pointer = gjs_gerror_from_error(context,
                                                       JSVAL_TO_OBJECT(value));

                if (transfer != GI_TRANSFER_NOTHING)
                    arg->v_pointer = g_error_copy ((const GError *) arg->v_pointer);
            } else {
                wrong = TRUE;
            }
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;
            GType gtype;
            gboolean expect_object;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS) {
                nullable_type = FALSE;
                expect_object = FALSE;
            } else {
                nullable_type = TRUE;
                expect_object = TRUE;
            }

            switch(interface_type) {
            case GI_INFO_TYPE_STRUCT:
                if (g_struct_info_is_foreign((GIStructInfo*)interface_info)) {
                    JSBool ret;
                    ret = gjs_struct_foreign_convert_to_g_argument(
                            context, value, interface_info, arg_name,
                            arg_type, transfer, may_be_null, arg);
                    g_base_info_unref(interface_info);
                    return ret;
                }
                /* fall through */
            case GI_INFO_TYPE_ENUM:
            case GI_INFO_TYPE_FLAGS:
            case GI_INFO_TYPE_OBJECT:
            case GI_INFO_TYPE_INTERFACE:
            case GI_INFO_TYPE_UNION:
            case GI_INFO_TYPE_BOXED:
                /* These are subtypes of GIRegisteredTypeInfo for which the
                 * cast is safe */
                gtype = g_registered_type_info_get_g_type
                    ((GIRegisteredTypeInfo*)interface_info);
                break;
            case GI_INFO_TYPE_VALUE:
                /* Special case for GValues */
                gtype = G_TYPE_VALUE;
                break;

            default:
                /* Everything else */
                gtype = G_TYPE_NONE;
                break;
            }

            if (gtype != G_TYPE_NONE)
                gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                                  "gtype of INTERFACE is %s", g_type_name(gtype));

            if (gtype == G_TYPE_VALUE) {
                GValue gvalue = { 0, };

                if (gjs_value_to_g_value(context, value, &gvalue)) {
                    arg->v_pointer = g_boxed_copy (G_TYPE_VALUE, &gvalue);
                    g_value_unset (&gvalue);
                } else {
                    arg->v_pointer = NULL;
                    wrong = TRUE;
                }
            } else if (expect_object != JSVAL_IS_OBJECT(value)) {
                /* JSVAL_IS_OBJECT handles null too */
                wrong = TRUE;
                report_type_mismatch = TRUE;
                break;
            } else if (JSVAL_IS_NULL(value)) {
                arg->v_pointer = NULL;
            } else if (JSVAL_IS_OBJECT(value)) {
                /* Handle Struct/Union first since we don't necessarily need a GType for them */
                if ((interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) &&
                    /* We special case Closures later, so skip them here */
                    !g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                    JSObject *obj = JSVAL_TO_OBJECT(value);

                    if (g_type_is_a(gtype, G_TYPE_BYTES)
                        && gjs_typecheck_bytearray(context, obj, FALSE)) {
                        arg->v_pointer = gjs_byte_array_get_bytes(context, obj);
                    } else if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                        if (!gjs_typecheck_gerror(context, JSVAL_TO_OBJECT(value), JS_TRUE)) {
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        } else {
                            arg->v_pointer = gjs_gerror_from_error(context,
                                                                   JSVAL_TO_OBJECT(value));
                        }
                    } else {
                        if (!gjs_typecheck_boxed(context, JSVAL_TO_OBJECT(value),
                                                 interface_info, gtype,
                                                 JS_TRUE)) {
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        } else {
                            arg->v_pointer = gjs_c_struct_from_boxed(context,
                                                                     JSVAL_TO_OBJECT(value));
                        }
                    }

                    if (!wrong && transfer != GI_TRANSFER_NOTHING) {
                        if (g_type_is_a(gtype, G_TYPE_BOXED))
                            arg->v_pointer = g_boxed_copy (gtype, arg->v_pointer);
                        else if (g_type_is_a(gtype, G_TYPE_VARIANT))
                            g_variant_ref ((GVariant *) arg->v_pointer);
                        else {
                            gjs_throw(context,
                                      "Can't transfer ownership of a structure type not registered as boxed");
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    }

                } else if (interface_type == GI_INFO_TYPE_UNION) {
                    if (gjs_typecheck_union(context, JSVAL_TO_OBJECT(value),
                                            interface_info, gtype, JS_TRUE)) {
                        arg->v_pointer = gjs_c_union_from_union(context,
                                                                JSVAL_TO_OBJECT(value));

                        if (transfer != GI_TRANSFER_NOTHING) {
                            if (g_type_is_a(gtype, G_TYPE_BOXED))
                                arg->v_pointer = g_boxed_copy (gtype, arg->v_pointer);
                            else {
                                gjs_throw(context,
                                          "Can't transfer ownership of a union type not registered as boxed");

                                arg->v_pointer = NULL;
                                wrong = TRUE;
                            }
                        }
                    } else {
                        arg->v_pointer = NULL;
                        wrong = TRUE;
                    }

                } else if (gtype != G_TYPE_NONE) {
                    if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                        if (gjs_typecheck_object(context, JSVAL_TO_OBJECT(value), gtype, JS_TRUE)) {
                            arg->v_pointer = gjs_g_object_from_object(context,
                                                                      JSVAL_TO_OBJECT(value));

                            if (transfer != GI_TRANSFER_NOTHING)
                                g_object_ref(G_OBJECT(arg->v_pointer));
                        } else {
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                        if (gjs_typecheck_param(context, JSVAL_TO_OBJECT(value), gtype, JS_TRUE)) {
                            arg->v_pointer = gjs_g_param_from_param(context, JSVAL_TO_OBJECT(value));
                            if (transfer != GI_TRANSFER_NOTHING)
                                g_param_spec_ref(G_PARAM_SPEC(arg->v_pointer));
                        } else {
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                        if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                            arg->v_pointer = gjs_closure_new_marshaled(context,
                                                                       JSVAL_TO_OBJECT(value),
                                                                       "boxed");
                            g_closure_ref((GClosure *) arg->v_pointer);
                            g_closure_sink((GClosure *) arg->v_pointer);
                        } else {
                            /* Should have been caught above as STRUCT/BOXED/UNION */
                            gjs_throw(context,
                                      "Boxed type %s registered for unexpected interface_type %d",
                                      g_type_name(gtype),
                                      interface_type);
                        }
                    } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                        if (gjs_typecheck_fundamental(context, JSVAL_TO_OBJECT(value), gtype, JS_TRUE)) {
                            arg->v_pointer = gjs_g_fundamental_from_object(context,
                                                                           JSVAL_TO_OBJECT(value));

                            if (transfer != GI_TRANSFER_NOTHING)
                                gjs_fundamental_ref(context, arg->v_pointer);
                        } else {
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    } else if (G_TYPE_IS_INTERFACE(gtype)) {
                        /* Could be a GObject interface that's missing a prerequisite, or could
                           be a fundamental */
                        if (gjs_typecheck_object(context, JSVAL_TO_OBJECT(value), gtype, JS_FALSE)) {
                            arg->v_pointer = gjs_g_object_from_object(context, JSVAL_TO_OBJECT(value));

                            if (transfer != GI_TRANSFER_NOTHING)
                                g_object_ref(arg->v_pointer);
                        } else if (gjs_typecheck_fundamental(context, JSVAL_TO_OBJECT(value), gtype, JS_FALSE)) {
                            arg->v_pointer = gjs_g_fundamental_from_object(context, JSVAL_TO_OBJECT(value));

                            if (transfer != GI_TRANSFER_NOTHING)
                                gjs_fundamental_ref(context, arg->v_pointer);
                        } else {
                            /* Call again with throw=TRUE to set the exception */
                            gjs_typecheck_object(context, JSVAL_TO_OBJECT(value), gtype, JS_TRUE);
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    } else {
                        gjs_throw(context, "Unhandled GType %s unpacking GArgument from Object",
                                  g_type_name(gtype));
                        arg->v_pointer = NULL;
                        wrong = TRUE;
                    }
                } else {
                    gjs_throw(context, "Unexpected unregistered type unpacking GArgument from Object");
                }

                if (arg->v_pointer == NULL) {
                    gjs_debug(GJS_DEBUG_GFUNCTION,
                              "conversion of JSObject %p type %s to type %s failed",
                              JSVAL_TO_OBJECT(value),
                              JS_GetTypeName(context,
                                             JS_TypeOfValue(context, value)),
                              g_base_info_get_name ((GIBaseInfo *)interface_info));

                    /* gjs_throw should have been called already */
                    wrong = TRUE;
                }

            } else if (JSVAL_IS_NUMBER(value)) {
                if (interface_type == GI_INFO_TYPE_ENUM) {
                    gint64 value_int64;

                    if (!gjs_value_to_int64 (context, value, &value_int64))
                        wrong = TRUE;
                    else if (!_gjs_enum_value_is_valid(context, (GIEnumInfo *)interface_info, value_int64))
                        wrong = TRUE;
                    else
                        arg->v_int = _gjs_enum_to_int ((GIEnumInfo *)interface_info, value_int64);

                } else if (interface_type == GI_INFO_TYPE_FLAGS) {
                    gint64 value_int64;

                    if (!gjs_value_to_int64 (context, value, &value_int64))
                        wrong = TRUE;
                    else if (!_gjs_flags_value_is_valid(context, gtype, value_int64))
                        wrong = TRUE;
                    else
                        arg->v_int = _gjs_enum_to_int ((GIEnumInfo *)interface_info, value_int64);

                } else if (gtype == G_TYPE_NONE) {
                    gjs_throw(context, "Unexpected unregistered type unpacking GArgument from Number");
                    wrong = TRUE;
                } else {
                    gjs_throw(context, "Unhandled GType %s unpacking GArgument from Number",
                              g_type_name(gtype));
                    wrong = TRUE;
                }

            } else {
                gjs_debug(GJS_DEBUG_GFUNCTION,
                          "JSObject type '%s' is neither null nor an object",
                          JS_GetTypeName(context,
                                         JS_TypeOfValue(context, value)));
                wrong = TRUE;
                report_type_mismatch = TRUE;
            }
            g_base_info_unref( (GIBaseInfo*) interface_info);
        }
        break;

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST: {
        jsid length_name;
        JSBool found_length;

        length_name = gjs_context_get_const_string(context, GJS_STRING_LENGTH);

        /* nullable_type=FALSE; while a list can be NULL in C, that
         * means empty array in JavaScript, it doesn't mean null in
         * JavaScript.
         */
        if (!JSVAL_IS_NULL(value) &&
            JSVAL_IS_OBJECT(value) &&
            JS_HasPropertyById(context, JSVAL_TO_OBJECT(value), length_name, &found_length) &&
            found_length) {
            jsval length_value;
            guint32 length;

            if (!gjs_object_require_property(context,
                                             JSVAL_TO_OBJECT(value), NULL,
                                             length_name,
                                             &length_value) ||
                !JS_ValueToECMAUint32(context, length_value, &length)) {
                wrong = TRUE;
            } else {
                GList *list;
                GSList *slist;
                GITypeInfo *param_info;

                param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(param_info != NULL);

                list = NULL;
                slist = NULL;

                if (!gjs_array_to_g_list(context,
                                         value,
                                         length,
                                         param_info,
                                         transfer,
                                         type_tag,
                                         &list, &slist)) {
                    wrong = TRUE;
                }

                if (type_tag == GI_TYPE_TAG_GLIST) {
                    arg->v_pointer = list;
                } else {
                    arg->v_pointer = slist;
                }

                g_base_info_unref((GIBaseInfo*) param_info);
            }
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;
    }

    case GI_TYPE_TAG_GHASH:
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
            if (!may_be_null) {
                wrong = TRUE;
                report_type_mismatch = TRUE;
            }
        } else if (!JSVAL_IS_OBJECT(value)) {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        } else {
            GITypeInfo *key_param_info, *val_param_info;
            GHashTable *ghash;

            key_param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(key_param_info != NULL);
            val_param_info = g_type_info_get_param_type(type_info, 1);
            g_assert(val_param_info != NULL);

            if (!gjs_object_to_g_hash(context,
                                      value,
                                      key_param_info,
                                      val_param_info,
                                      transfer,
                                      &ghash)) {
                wrong = TRUE;
            } else {
                arg->v_pointer = ghash;
            }

            g_base_info_unref((GIBaseInfo*) key_param_info);
            g_base_info_unref((GIBaseInfo*) val_param_info);
        }
        break;

    case GI_TYPE_TAG_ARRAY: {
        gpointer data;
        gsize length;
        GIArrayType array_type = g_type_info_get_array_type(type_info);
        GITypeTag element_type;
        GITypeInfo *param_info;

        param_info = g_type_info_get_param_type(type_info, 0);
        element_type = g_type_info_get_tag(param_info);
        g_base_info_unref(param_info);

        /* First, let's handle the case where we're passed an instance
         * of our own byteArray class.
         */
        if (JSVAL_IS_OBJECT(value) &&
            gjs_typecheck_bytearray(context,
                                    JSVAL_TO_OBJECT(value),
                                    FALSE))
            {
                JSObject *bytearray_obj = JSVAL_TO_OBJECT(value);
                if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                    arg->v_pointer = gjs_byte_array_get_byte_array(context, bytearray_obj);
                    break;
                } else {
                    /* Fall through, !handled */
                }
            }

        if (!gjs_array_to_explicit_array_internal(context,
                                                  value,
                                                  type_info,
                                                  arg_name,
                                                  arg_type,
                                                  transfer,
                                                  may_be_null,
                                                  &data,
                                                  &length)) {
            wrong = TRUE;
            break;
        }

        if (array_type == GI_ARRAY_TYPE_C) {
            arg->v_pointer = data;
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeInfo *param_info = g_type_info_get_param_type(type_info, 0);
            GArray *array = gjs_g_array_new_for_type(context, length, param_info);

            if (!array)
                wrong = TRUE;
            else {
                g_array_append_vals(array, data, length);
                arg->v_pointer = array;
            }

            g_free(data);
            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            GByteArray *byte_array = g_byte_array_sized_new(length);

            g_byte_array_append(byte_array, (const guint8 *) data, length);
            arg->v_pointer = byte_array;

            g_free(data);
        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GPtrArray *array = g_ptr_array_sized_new(length);

            g_ptr_array_set_size(array, length);
            memcpy(array->pdata, data, sizeof(gpointer) * length);
            arg->v_pointer = array;

            g_free(data);
        }
        break;
    }
    default:
        g_warning("Unhandled type %s for JavaScript to GArgument conversion",
                  g_type_tag_to_string(type_tag));
        wrong = TRUE;
        report_type_mismatch = TRUE;
        break;
    }

    if (G_UNLIKELY(wrong)) {
        if (report_type_mismatch) {
            throw_invalid_argument(context, value, type_info, arg_name, arg_type);
        }
        return JS_FALSE;
    } else if (G_UNLIKELY(out_of_range)) {
        gchar *display_name = get_argument_display_name (arg_name, arg_type);
        gjs_throw(context, "value is out of range for %s (type %s)",
                  display_name,
                  g_type_tag_to_string(type_tag));
        g_free (display_name);
        return JS_FALSE;
    } else if (nullable_type &&
               arg->v_pointer == NULL &&
               !may_be_null) {
        gchar *display_name = get_argument_display_name (arg_name, arg_type);
        gjs_throw(context,
                  "%s (type %s) may not be null",
                  display_name,
                  g_type_tag_to_string(type_tag));
        g_free (display_name);
        return JS_FALSE;
    } else {
        return JS_TRUE;
    }
}

/* If a callback function with a return value throws, we still have
 * to return something to C. This function defines what that something
 * is. It basically boils down to memset(arg, 0, sizeof(*arg)), but
 * gives as a bit more future flexibility and also will work if
 * libffi passes us a buffer that only has room for the appropriate
 * branch of GArgument. (Currently it appears that the return buffer
 * has a fixed size large enough for the union of all types.)
 */
void
gjs_g_argument_init_default(JSContext      *context,
                            GITypeInfo     *type_info,
                            GArgument      *arg)
{
    GITypeTag type_tag;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        arg->v_pointer = NULL; /* just so it isn't uninitialized */
        break;

    case GI_TYPE_TAG_INT8:
        arg->v_int8 = 0;
        break;

    case GI_TYPE_TAG_UINT8:
        arg->v_uint8 = 0;
        break;

    case GI_TYPE_TAG_INT16:
        arg->v_int16 = 0;
        break;

    case GI_TYPE_TAG_UINT16:
        arg->v_uint16 = 0;
        break;

    case GI_TYPE_TAG_INT32:
        arg->v_int = 0;
        break;

    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_UNICHAR:
        arg->v_uint32 = 0;
        break;

    case GI_TYPE_TAG_INT64:
        arg->v_int64 = 0;
        break;

    case GI_TYPE_TAG_UINT64:
        arg->v_uint64 = 0;

    case GI_TYPE_TAG_BOOLEAN:
        arg->v_boolean = FALSE;
        break;

    case GI_TYPE_TAG_FLOAT:
        arg->v_float = 0.0f;
        break;

    case GI_TYPE_TAG_DOUBLE:
        arg->v_double = 0.0;
        break;

    case GI_TYPE_TAG_GTYPE:
        arg->v_ssize = 0;
        break;

    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_ERROR:
        arg->v_pointer = NULL;
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            interface_type = g_base_info_get_type(interface_info);

            switch(interface_type) {
            case GI_INFO_TYPE_ENUM:
            case GI_INFO_TYPE_FLAGS:
                arg->v_int = 0;
                break;
            case GI_INFO_TYPE_VALUE:
                /* Better to use a non-NULL value holding NULL? */
                arg->v_pointer = NULL;
                break;
            default:
                arg->v_pointer = NULL;
                break;
            }

            g_base_info_unref( (GIBaseInfo*) interface_info);
        }
        break;

    case GI_TYPE_TAG_GHASH:
        /* Possibly better to return an empty hash table? */
        arg->v_pointer = NULL;
        break;

    case GI_TYPE_TAG_ARRAY:
        arg->v_pointer = NULL;
        break;

    default:
        g_warning("Unhandled type %s for default GArgument initialization",
                  g_type_tag_to_string(type_tag));
        break;
    }
}

JSBool
gjs_value_to_arg(JSContext  *context,
                 jsval       value,
                 GIArgInfo  *arg_info,
                 GArgument  *arg)
{
    GITypeInfo type_info;

    g_arg_info_load_type(arg_info, &type_info);

    return gjs_value_to_g_argument(context, value,
                                   &type_info,
                                   g_base_info_get_name( (GIBaseInfo*) arg_info),
                                   (g_arg_info_is_return_value(arg_info) ?
                                    GJS_ARGUMENT_RETURN_VALUE : GJS_ARGUMENT_ARGUMENT),
                                   g_arg_info_get_ownership_transfer(arg_info),
                                   g_arg_info_may_be_null(arg_info),
                                   arg);
}

JSBool
gjs_value_to_explicit_array (JSContext  *context,
                             jsval       value,
                             GIArgInfo  *arg_info,
                             GArgument  *arg,
                             gsize      *length_p)
{
    GITypeInfo type_info;

    g_arg_info_load_type(arg_info, &type_info);

    return gjs_array_to_explicit_array_internal(context,
                                                value,
                                                &type_info,
                                                g_base_info_get_name((GIBaseInfo*) arg_info),
                                                GJS_ARGUMENT_ARGUMENT,
                                                g_arg_info_get_ownership_transfer(arg_info),
                                                g_arg_info_may_be_null(arg_info),
                                                &arg->v_pointer,
                                                length_p);
}

static JSBool
gjs_array_from_g_list (JSContext  *context,
                       jsval      *value_p,
                       GITypeTag   list_tag,
                       GITypeInfo *param_info,
                       GList      *list,
                       GSList     *slist)
{
    JSObject *obj;
    unsigned int i;
    jsval elem;
    GArgument arg;
    JSBool result;

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
        return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);

    elem = JSVAL_VOID;
    JS_AddValueRoot(context, &elem);

    result = JS_FALSE;

    i = 0;
    if (list_tag == GI_TYPE_TAG_GLIST) {
        for ( ; list != NULL; list = list->next) {
            arg.v_pointer = list->data;

            if (!gjs_value_from_g_argument(context, &elem,
                                           param_info, &arg,
                                           TRUE))
                goto out;

            if (!JS_DefineElement(context, obj,
                                  i, elem,
                                  NULL, NULL, JSPROP_ENUMERATE)) {
                goto out;
            }
            ++i;
        }
    } else {
        for ( ; slist != NULL; slist = slist->next) {
            arg.v_pointer = slist->data;

            if (!gjs_value_from_g_argument(context, &elem,
                                           param_info, &arg,
                                           TRUE))
                goto out;

            if (!JS_DefineElement(context, obj,
                                  i, elem,
                                  NULL, NULL, JSPROP_ENUMERATE)) {
                goto out;
            }
            ++i;
        }
    }

    result = JS_TRUE;

 out:
    JS_RemoveValueRoot(context, &elem);

    return result;
}

static JSBool
gjs_array_from_carray_internal (JSContext  *context,
                                jsval      *value_p,
                                GITypeInfo *param_info,
                                guint       length,
                                gpointer    array)
{
    JSObject *obj;
    jsval elem;
    GArgument arg;
    JSBool result;
    GITypeTag element_type;
    guint i;

    result = JS_FALSE;

    element_type = g_type_info_get_tag(param_info);

    if (is_gvalue_flat_array(param_info, element_type))
        return gjs_array_from_flat_gvalue_array(context, array, length, value_p);

    /* Special case array(guint8) */
    if (element_type == GI_TYPE_TAG_UINT8) {
        GByteArray gbytearray;

        gbytearray.data = (guint8 *) array;
        gbytearray.len = length;
        
        obj = gjs_byte_array_from_byte_array (context, &gbytearray);
        if (obj == NULL)
            return JS_FALSE;
        *value_p = OBJECT_TO_JSVAL(obj);
        return JS_TRUE;
    } 

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
      return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);

    elem = JSVAL_VOID;
    JS_AddValueRoot(context, &elem);

#define ITERATE(type) \
    for (i = 0; i < length; i++) { \
        arg.v_##type = *(((g##type*)array) + i);                         \
        if (!gjs_value_from_g_argument(context, &elem, param_info, &arg, TRUE)) \
          goto finally; \
        if (!JS_DefineElement(context, obj, i, elem, NULL, NULL, \
              JSPROP_ENUMERATE)) \
          goto finally; \
    }

    switch (element_type) {
        case GI_TYPE_TAG_UINT8:
          ITERATE(uint8);
          break;
        case GI_TYPE_TAG_INT8:
          ITERATE(int8);
          break;
        case GI_TYPE_TAG_UINT16:
          ITERATE(uint16);
          break;
        case GI_TYPE_TAG_INT16:
          ITERATE(int16);
          break;
        case GI_TYPE_TAG_UINT32:
          ITERATE(uint32);
          break;
        case GI_TYPE_TAG_INT32:
          ITERATE(int32);
          break;
        case GI_TYPE_TAG_UINT64:
          ITERATE(uint64);
          break;
        case GI_TYPE_TAG_INT64:
          ITERATE(int64);
          break;
        case GI_TYPE_TAG_FLOAT:
          ITERATE(float);
          break;
        case GI_TYPE_TAG_DOUBLE:
          ITERATE(double);
          break;
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_INTERFACE:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
          ITERATE(pointer);
          break;
        default:
          gjs_throw(context, "Unknown Array element-type %d", element_type);
          goto finally;
    }

#undef ITERATE

    result = JS_TRUE;

finally:
    JS_RemoveValueRoot(context, &elem);

    return result;
}

static JSBool
gjs_array_from_fixed_size_array (JSContext  *context,
                                 jsval      *value_p,
                                 GITypeInfo *type_info,
                                 gpointer    array)
{
    gint length;
    GITypeInfo *param_info;
    JSBool res;

    length = g_type_info_get_array_fixed_size(type_info);

    g_assert (length != -1);

    param_info = g_type_info_get_param_type(type_info, 0);

    res = gjs_array_from_carray_internal(context, value_p, param_info, length, array);

    g_base_info_unref((GIBaseInfo*)param_info);

    return res;
}

JSBool
gjs_value_from_explicit_array(JSContext  *context,
                              jsval      *value_p,
                              GITypeInfo *type_info,
                              GArgument  *arg,
                              int         length)
{
    GITypeInfo *param_info;
    JSBool res;

    param_info = g_type_info_get_param_type(type_info, 0);

    res = gjs_array_from_carray_internal(context, value_p, param_info, length, arg->v_pointer);

    g_base_info_unref((GIBaseInfo*)param_info);

    return res;
}

static JSBool
gjs_array_from_boxed_array (JSContext   *context,
                            jsval       *value_p,
                            GIArrayType  array_type,
                            GITypeInfo  *param_info,
                            GArgument   *arg)
{
    GArray *array;
    GPtrArray *ptr_array;
    gpointer data = NULL;
    gsize length = 0;

    if (arg->v_pointer == NULL) {
        *value_p = JSVAL_NULL;
        return TRUE;
    }

    switch(array_type) {
    case GI_ARRAY_TYPE_BYTE_ARRAY:
        /* GByteArray is just a typedef for GArray internally */
    case GI_ARRAY_TYPE_ARRAY:
        array = (GArray*)(arg->v_pointer);
        data = array->data;
        length = array->len;
        break;
    case GI_ARRAY_TYPE_PTR_ARRAY:
        ptr_array = (GPtrArray*)(arg->v_pointer);
        data = ptr_array->pdata;
        length = ptr_array->len;
        break;
    default:
        g_assert_not_reached();
    }

    return gjs_array_from_carray_internal(context, value_p, param_info, length, data);
}

static JSBool
gjs_array_from_zero_terminated_c_array (JSContext  *context,
                                        jsval      *value_p,
                                        GITypeInfo *param_info,
                                        gpointer    c_array)
{
    JSObject *obj;
    jsval elem;
    GArgument arg;
    JSBool result;
    GITypeTag element_type;
    guint i;

    result = JS_FALSE;

    element_type = g_type_info_get_tag(param_info);

    /* Special case array(guint8) */
    if (element_type == GI_TYPE_TAG_UINT8) {
        GByteArray gbytearray;

        gbytearray.data = (guint8 *) c_array;
        gbytearray.len = strlen((const char *) c_array);

        obj = gjs_byte_array_from_byte_array (context, &gbytearray);
        if (obj == NULL)
            return JS_FALSE;
        *value_p = OBJECT_TO_JSVAL(obj);
        return JS_TRUE;
    } 

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
      return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);

    elem = JSVAL_VOID;
    JS_AddValueRoot(context, &elem);

#define ITERATE(type) \
    do { \
        g##type *array = (g##type *) c_array; \
        for (i = 0; array[i]; i++) { \
            arg.v_##type = array[i]; \
            if (!gjs_value_from_g_argument(context, &elem, param_info, &arg, TRUE)) \
                goto finally; \
            if (!JS_DefineElement(context, obj, i, elem, NULL, NULL, \
                                  JSPROP_ENUMERATE)) \
                goto finally; \
        } \
    } while(0);

    switch (element_type) {
        /* We handle GI_TYPE_TAG_UINT8 above. */
        case GI_TYPE_TAG_INT8:
          ITERATE(int8);
          break;
        case GI_TYPE_TAG_UINT16:
          ITERATE(uint16);
          break;
        case GI_TYPE_TAG_INT16:
          ITERATE(int16);
          break;
        case GI_TYPE_TAG_UINT32:
          ITERATE(uint32);
          break;
        case GI_TYPE_TAG_INT32:
          ITERATE(int32);
          break;
        case GI_TYPE_TAG_UINT64:
          ITERATE(uint64);
          break;
        case GI_TYPE_TAG_INT64:
          ITERATE(int64);
          break;
        case GI_TYPE_TAG_FLOAT:
          ITERATE(float);
          break;
        case GI_TYPE_TAG_DOUBLE:
          ITERATE(double);
          break;
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_INTERFACE:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
          ITERATE(pointer);
          break;
        default:
          gjs_throw(context, "Unknown element-type %d", element_type);
          goto finally;
    }

#undef ITERATE

    result = JS_TRUE;

finally:
    JS_RemoveValueRoot(context, &elem);

    return result;
}


static JSBool
gjs_object_from_g_hash (JSContext  *context,
                        jsval      *value_p,
                        GITypeInfo *key_param_info,
                        GITypeInfo *val_param_info,
                        GHashTable *hash)
{
    GHashTableIter iter;
    JSObject *obj;
    JSString *keystr;
    char     *keyutf8 = NULL;
    jsval     keyjs,  valjs;
    GArgument keyarg, valarg;
    JSBool result;

    // a NULL hash table becomes a null JS value
    if (hash==NULL) {
        *value_p = JSVAL_NULL;
        return JS_TRUE;
    }

    obj = JS_NewObject(context, NULL, NULL, NULL);
    if (obj == NULL)
        return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);
    JS_AddObjectRoot(context, &obj);

    keyjs = JSVAL_VOID;
    JS_AddValueRoot(context, &keyjs);

    valjs = JSVAL_VOID;
    JS_AddValueRoot(context, &valjs);

    keystr = NULL;
    JS_AddStringRoot(context, &keystr);

    result = JS_FALSE;

    g_hash_table_iter_init(&iter, hash);
    while (g_hash_table_iter_next
           (&iter, &keyarg.v_pointer, &valarg.v_pointer)) {
        if (!gjs_value_from_g_argument(context, &keyjs,
                                       key_param_info, &keyarg,
                                       TRUE))
            goto out;

        keystr = JS_ValueToString(context, keyjs);
        if (!keystr)
            goto out;

        if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(keystr), &keyutf8))
            goto out;

        if (!gjs_value_from_g_argument(context, &valjs,
                                       val_param_info, &valarg,
                                       TRUE))
            goto out;

        if (!JS_DefineProperty(context, obj, keyutf8, valjs,
                               NULL, NULL, JSPROP_ENUMERATE))
            goto out;

        g_free(keyutf8);
        keyutf8 = NULL;
    }

    result = JS_TRUE;

 out:
    if (keyutf8) g_free(keyutf8);
    JS_RemoveObjectRoot(context, &obj);
    JS_RemoveValueRoot(context, &keyjs);
    JS_RemoveValueRoot(context, &valjs);
    JS_RemoveStringRoot(context, &keystr);

    return result;
}

JSBool
gjs_value_from_g_argument (JSContext  *context,
                           jsval      *value_p,
                           GITypeInfo *type_info,
                           GArgument  *arg,
                           gboolean    copy_structs)
{
    GITypeTag type_tag;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GArgument %s to jsval",
                      g_type_tag_to_string(type_tag));

    *value_p = JSVAL_NULL;

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        *value_p = JSVAL_VOID; /* or JSVAL_NULL ? */
        break;

    case GI_TYPE_TAG_BOOLEAN:
        *value_p = BOOLEAN_TO_JSVAL(!!arg->v_int);
        break;

    case GI_TYPE_TAG_INT32:
        return JS_NewNumberValue(context, arg->v_int, value_p);

    case GI_TYPE_TAG_UINT32:
        return JS_NewNumberValue(context, arg->v_uint, value_p);

    case GI_TYPE_TAG_INT64:
        return JS_NewNumberValue(context, arg->v_int64, value_p);

    case GI_TYPE_TAG_UINT64:
        return JS_NewNumberValue(context, arg->v_uint64, value_p);

    case GI_TYPE_TAG_UINT16:
        return JS_NewNumberValue(context, arg->v_uint16, value_p);

    case GI_TYPE_TAG_INT16:
        return JS_NewNumberValue(context, arg->v_int16, value_p);

    case GI_TYPE_TAG_UINT8:
        return JS_NewNumberValue(context, arg->v_uint8, value_p);

    case GI_TYPE_TAG_INT8:
        return JS_NewNumberValue(context, arg->v_int8, value_p);

    case GI_TYPE_TAG_FLOAT:
        return JS_NewNumberValue(context, arg->v_float, value_p);

    case GI_TYPE_TAG_DOUBLE:
        return JS_NewNumberValue(context, arg->v_double, value_p);

    case GI_TYPE_TAG_GTYPE:
        {
            JSObject *obj;
            obj = gjs_gtype_create_gtype_wrapper(context, arg->v_ssize);
            *value_p = OBJECT_TO_JSVAL(obj);
        }
        break;

    case GI_TYPE_TAG_UNICHAR:
        {
            char utf8[7];
            gint bytes;

            /* Preserve the bidirectional mapping between 0 and "" */
            if (arg->v_uint32 == 0) {
                return gjs_string_from_utf8 (context, "", 0, value_p);
            } else if (!g_unichar_validate (arg->v_uint32)) {
                gjs_throw(context,
                          "Invalid unicode codepoint %" G_GUINT32_FORMAT,
                          arg->v_uint32);
                return JS_FALSE;
            } else {
                bytes = g_unichar_to_utf8 (arg->v_uint32, utf8);
                return gjs_string_from_utf8 (context, (char*)utf8, bytes, value_p);
            }
        }

    case GI_TYPE_TAG_FILENAME:
        if (arg->v_pointer)
            return gjs_string_from_filename(context, (const char *) arg->v_pointer, -1, value_p);
        else {
            /* For NULL we'll return JSVAL_NULL, which is already set
             * in *value_p
             */
            return JS_TRUE;
        }
    case GI_TYPE_TAG_UTF8:
        if (arg->v_pointer)
            return gjs_string_from_utf8(context, (const char *) arg->v_pointer, -1, value_p);
        else {
            /* For NULL we'll return JSVAL_NULL, which is already set
             * in *value_p
             */
            return JS_TRUE;
        }

    case GI_TYPE_TAG_ERROR:
        {
            if (arg->v_pointer) {
                JSObject *obj = gjs_error_from_gerror(context, (GError *) arg->v_pointer, FALSE);
                if (obj) {
                    *value_p = OBJECT_TO_JSVAL(obj);
                    return JS_TRUE;
                }

                return JS_FALSE;
            }
            return JS_TRUE;
        }

    case GI_TYPE_TAG_INTERFACE:
        {
            jsval value;
            GIBaseInfo* interface_info;
            GIInfoType interface_type;
            GType gtype;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            value = JSVAL_VOID;

            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_UNRESOLVED) {
                gjs_throw(context,
                          "Unable to resolve arg type '%s'",
                          g_base_info_get_name(interface_info));
                goto out;
            }

            /* Enum/Flags are aren't pointer types, unlike the other interface subtypes */
            if (interface_type == GI_INFO_TYPE_ENUM) {
                gint64 value_int64 = _gjs_enum_from_int ((GIEnumInfo *)interface_info, arg->v_int);

                if (_gjs_enum_value_is_valid(context, (GIEnumInfo *)interface_info, value_int64)) {
                    jsval tmp;
                    if (JS_NewNumberValue(context, value_int64, &tmp))
                        value = tmp;
                }

                goto out;
            } else if (interface_type == GI_INFO_TYPE_FLAGS) {
                gint64 value_int64 = _gjs_enum_from_int ((GIEnumInfo *)interface_info, arg->v_int);

                gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
                if (_gjs_flags_value_is_valid(context, gtype, value_int64)) {
                    jsval tmp;
                    if (JS_NewNumberValue(context, value_int64, &tmp))
                        value = tmp;
                }

                goto out;
            } else if (interface_type == GI_INFO_TYPE_STRUCT &&
                       g_struct_info_is_foreign((GIStructInfo*)interface_info)) {
                JSBool ret;
                ret = gjs_struct_foreign_convert_from_g_argument(context, value_p, interface_info, arg);
                g_base_info_unref(interface_info);
                return ret;
            }

            /* Everything else is a pointer type, NULL is the easy case */
            if (arg->v_pointer == NULL) {
                value = JSVAL_NULL;
                goto out;
            }

            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
            if (G_TYPE_IS_INSTANTIATABLE(gtype) ||
                G_TYPE_IS_INTERFACE(gtype))
                gtype = G_TYPE_FROM_INSTANCE(arg->v_pointer);

            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));


            /* Test GValue and GError before Struct, or it will be handled as the latter */
            if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                if (!gjs_value_from_g_value(context, &value, (const GValue *) arg->v_pointer))
                    value = JSVAL_VOID; /* Make sure error is flagged */

                goto out;
            }
            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                JSObject *obj;

                obj = gjs_error_from_gerror(context, (GError *) arg->v_pointer, FALSE);
                if (obj)
                    value = OBJECT_TO_JSVAL(obj);
                else
                    value = JSVAL_VOID;

                goto out;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) {
                JSObject *obj;
                GjsBoxedCreationFlags flags;

                if (copy_structs)
                    flags = GJS_BOXED_CREATION_NONE;
                else if (g_type_is_a(gtype, G_TYPE_VARIANT))
                    flags = GJS_BOXED_CREATION_NONE;
                else
                    flags = GJS_BOXED_CREATION_NO_COPY;

                obj = gjs_boxed_from_c_struct(context,
                                              (GIStructInfo *)interface_info,
                                              arg->v_pointer,
                                              flags);

                if (obj)
                    value = OBJECT_TO_JSVAL(obj);

                goto out;
            } else if (interface_type == GI_INFO_TYPE_UNION) {
                JSObject *obj;
                obj = gjs_union_from_c_union(context, (GIUnionInfo *)interface_info, arg->v_pointer);
                if (obj)
                        value = OBJECT_TO_JSVAL(obj);

                goto out;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                JSObject *obj;
                obj = gjs_object_from_g_object(context, G_OBJECT(arg->v_pointer));
                if (obj)
                    value = OBJECT_TO_JSVAL(obj);
            } else if (g_type_is_a(gtype, G_TYPE_BOXED) ||
                       g_type_is_a(gtype, G_TYPE_ENUM) ||
                       g_type_is_a(gtype, G_TYPE_FLAGS)) {
                /* Should have been handled above */
                gjs_throw(context,
                          "Type %s registered for unexpected interface_type %d",
                          g_type_name(gtype),
                          interface_type);
                return JS_FALSE;
            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                JSObject *obj;
                obj = gjs_param_from_g_param(context, G_PARAM_SPEC(arg->v_pointer));
                if (obj)
                    value = OBJECT_TO_JSVAL(obj);
            } else if (gtype == G_TYPE_NONE) {
                gjs_throw(context, "Unexpected unregistered type packing GArgument into jsval");
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype) || G_TYPE_IS_INTERFACE(gtype)) {
                JSObject *obj;
                obj = gjs_object_from_g_fundamental(context, (GIObjectInfo *)interface_info, arg->v_pointer);
                if (obj)
                    value = OBJECT_TO_JSVAL(obj);
            } else {
                gjs_throw(context, "Unhandled GType %s packing GArgument into jsval",
                          g_type_name(gtype));
            }

         out:
            g_base_info_unref( (GIBaseInfo*) interface_info);

            if (JSVAL_IS_VOID(value))
                return JS_FALSE;

            *value_p = value;
        }
        break;

    case GI_TYPE_TAG_ARRAY:
        if (arg->v_pointer == NULL) {
            /* OK, but no conversion to do */
        } else if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {

            if (g_type_info_is_zero_terminated(type_info)) {
                GITypeInfo *param_info;
                JSBool result;

                param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(param_info != NULL);

                result = gjs_array_from_zero_terminated_c_array(context,
                                                                value_p,
                                                                param_info,
                                                                arg->v_pointer);

                g_base_info_unref((GIBaseInfo*) param_info);

                return result;
            } else {
                /* arrays with length are handled outside of this function */
                return gjs_array_from_fixed_size_array(context, value_p, type_info, arg->v_pointer);
            }
        } else if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_BYTE_ARRAY) {
            JSObject *array = gjs_byte_array_from_byte_array(context,
                                                             (GByteArray*)arg->v_pointer);
            if (!array) {
                gjs_throw(context, "Couldn't convert GByteArray to a ByteArray");
                return JS_FALSE;
            }
            *value_p = OBJECT_TO_JSVAL(array);
        } else {
            /* this assumes the array type is one of GArray, GPtrArray or
             * GByteArray */
            GITypeInfo *param_info;
            gboolean result;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            result = gjs_array_from_boxed_array(context,
                                                value_p,
                                                g_type_info_get_array_type(type_info),
                                                param_info,
                                                arg);

            g_base_info_unref((GIBaseInfo*) param_info);

            return result;
        }
        break;

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
        {
            GITypeInfo *param_info;
            gboolean result;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            result = gjs_array_from_g_list(context,
                                           value_p,
                                           type_tag,
                                           param_info,
                                           type_tag == GI_TYPE_TAG_GLIST ?
                                           (GList *) arg->v_pointer : NULL,
                                           type_tag == GI_TYPE_TAG_GSLIST ?
                                           (GSList *) arg->v_pointer : NULL);

            g_base_info_unref((GIBaseInfo*) param_info);

            return result;
        }
        break;

    case GI_TYPE_TAG_GHASH:
        {
            GITypeInfo *key_param_info, *val_param_info;
            gboolean result;

            key_param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(key_param_info != NULL);
            val_param_info = g_type_info_get_param_type(type_info, 1);
            g_assert(val_param_info != NULL);

            result = gjs_object_from_g_hash(context,
                                            value_p,
                                            key_param_info,
                                            val_param_info,
                                            (GHashTable *) arg->v_pointer);

            g_base_info_unref((GIBaseInfo*) key_param_info);
            g_base_info_unref((GIBaseInfo*) val_param_info);

            return result;
        }
        break;

    default:
        g_warning("Unhandled type %s converting GArgument to JavaScript",
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool gjs_g_arg_release_internal(JSContext  *context,
                                         GITransfer  transfer,
                                         GITypeInfo *type_info,
                                         GITypeTag   type_tag,
                                         GArgument  *arg);

typedef struct {
    JSContext *context;
    GITypeInfo *key_param_info, *val_param_info;
    GITransfer transfer;
    JSBool failed;
} GHR_closure;

static gboolean
gjs_ghr_helper(gpointer key, gpointer val, gpointer user_data) {
    GHR_closure *c = (GHR_closure *) user_data;
    GArgument key_arg, val_arg;
    key_arg.v_pointer = key;
    val_arg.v_pointer = val;
    if (!gjs_g_arg_release_internal(c->context, c->transfer,
                                    c->key_param_info,
                                    g_type_info_get_tag(c->key_param_info),
                                    &key_arg))
        c->failed = JS_TRUE;

    if (!gjs_g_arg_release_internal(c->context, c->transfer,
                                    c->val_param_info,
                                    g_type_info_get_tag(c->val_param_info),
                                    &val_arg))
        c->failed = JS_TRUE;
    return TRUE;
}

/* We need to handle GI_TRANSFER_NOTHING differently for out parameters
 * (free nothing) and for in parameters (free any temporaries we've
 * allocated
 */
#define TRANSFER_IN_NOTHING (GI_TRANSFER_EVERYTHING + 1)

static JSBool
gjs_g_arg_release_internal(JSContext  *context,
                           GITransfer  transfer,
                           GITypeInfo *type_info,
                           GITypeTag   type_tag,
                           GArgument  *arg)
{
    JSBool failed;

    g_assert(transfer != GI_TRANSFER_NOTHING);

    failed = JS_FALSE;

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
    case GI_TYPE_TAG_BOOLEAN:
    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_UINT8:
    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_UINT16:
    case GI_TYPE_TAG_INT32:
    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_INT64:
    case GI_TYPE_TAG_UINT64:
    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
    case GI_TYPE_TAG_UNICHAR:
    case GI_TYPE_TAG_GTYPE:
        break;

    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_UTF8:
        g_free(arg->v_pointer);
        break;

    case GI_TYPE_TAG_ERROR:
        if (transfer != TRANSFER_IN_NOTHING)
            g_error_free ((GError *) arg->v_pointer);
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;
            GType gtype;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_foreign((GIStructInfo*)interface_info))
                return gjs_struct_foreign_release_g_argument(context,
                        transfer, interface_info, arg);

            if (interface_type == GI_INFO_TYPE_ENUM || interface_type == GI_INFO_TYPE_FLAGS)
                goto out;

            /* Anything else is a pointer */
            if (arg->v_pointer == NULL)
                goto out;

            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
            if (G_TYPE_IS_INSTANTIATABLE(gtype) ||
                G_TYPE_IS_INTERFACE(gtype))
                gtype = G_TYPE_FROM_INSTANCE(arg->v_pointer);

            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));

            /* In gjs_value_from_g_argument we handle Struct/Union types without a
             * registered GType, but here we are specifically handling a GArgument that
             * *owns* its value, and that is non-sensical for such types, so we
             * don't have to worry about it.
             */

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_object_unref(G_OBJECT(arg->v_pointer));
            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_param_spec_unref(G_PARAM_SPEC(arg->v_pointer));
            } else if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                g_closure_unref((GClosure *) arg->v_pointer);
            } else if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                /* G_TYPE_VALUE is-a G_TYPE_BOXED, but we special case it */
                if (g_type_info_is_pointer (type_info))
                    g_boxed_free(gtype, arg->v_pointer);
                else
                    g_value_unset ((GValue *) arg->v_pointer);
            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_boxed_free(gtype, arg->v_pointer);
            } else if (g_type_is_a(gtype, G_TYPE_VARIANT)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_variant_unref ((GVariant *) arg->v_pointer);
            } else if (gtype == G_TYPE_NONE) {
                if (transfer != TRANSFER_IN_NOTHING) {
                    gjs_throw(context, "Don't know how to release GArgument: not an object or boxed type");
                    failed = JS_TRUE;
                }
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    gjs_fundamental_unref(context, arg->v_pointer);
            } else {
                gjs_throw(context, "Unhandled GType %s releasing GArgument",
                          g_type_name(gtype));
                failed = JS_TRUE;
            }

        out:
            g_base_info_unref( (GIBaseInfo*) interface_info);
        }
        break;

    case GI_TYPE_TAG_GLIST:
        if (transfer != GI_TRANSFER_CONTAINER) {
            GITypeInfo *param_info;
            GList *list;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            for (list = (GList *) arg->v_pointer;
                 list != NULL;
                 list = list->next) {
                GArgument elem;
                elem.v_pointer = list->data;

                if (!gjs_g_arg_release_internal(context,
                                                transfer,
                                                param_info,
                                                g_type_info_get_tag(param_info),
                                                &elem)) {
                    failed = JS_TRUE;
                }
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        }

        g_list_free((GList *) arg->v_pointer);
        break;

    case GI_TYPE_TAG_ARRAY:
    {
        GIArrayType array_type = g_type_info_get_array_type(type_info);

        if (arg->v_pointer == NULL) {
            /* OK */
        } else if (array_type == GI_ARRAY_TYPE_C) {
            GITypeInfo *param_info;
            GITypeTag element_type;

            param_info = g_type_info_get_param_type(type_info, 0);
            element_type = g_type_info_get_tag(param_info);

            if (is_gvalue_flat_array(param_info, element_type)) {
                if (transfer != GI_TRANSFER_CONTAINER) {
                    gint len = g_type_info_get_array_fixed_size(type_info);
                    gint i;

                    if (len < 0) {
                        gjs_throw(context,
                                  "Releasing a flat GValue array that was not fixed-size or was nested"
                                  "inside another container. This is not supported (and will leak)");
                        g_base_info_unref(param_info);
                        return JS_FALSE;
                    }

                    for (i = 0; i < len; i++) {
                        GValue *v = ((GValue*)arg->v_pointer) + i;
                        g_value_unset(v);
                    }
                }

                g_free(arg->v_pointer);
                g_base_info_unref(param_info);
                return JS_TRUE;
            }

            switch (element_type) {
            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
                if (transfer == GI_TRANSFER_CONTAINER)
                    g_free(arg->v_pointer);
                else
                    g_strfreev ((gchar **) arg->v_pointer);
                break;

            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_GTYPE:
                g_free (arg->v_pointer);
                break;

            case GI_TYPE_TAG_INTERFACE:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_GHASH:
            case GI_TYPE_TAG_ERROR:
                if (transfer != GI_TRANSFER_CONTAINER
                    && type_needs_out_release(param_info, element_type)) {
                    if (g_type_info_is_zero_terminated (type_info)) {
                        gpointer *array;
                        GArgument elem;

                        for (array = (void **) arg->v_pointer; *array; array++) {
                            elem.v_pointer = *array;
                            if (!gjs_g_arg_release_internal(context,
                                                            GI_TRANSFER_EVERYTHING,
                                                            param_info,
                                                            element_type,
                                                            &elem)) {
                                failed = JS_TRUE;
                            }
                        }
                    } else {
                        gint len = g_type_info_get_array_fixed_size(type_info);
                        gint i;
                        GArgument elem;

                        g_assert(len != -1);

                        for (i = 0; i < len; i++) {
                            elem.v_pointer = ((gpointer*)arg->v_pointer)[i];
                            if (!gjs_g_arg_release_internal(context,
                                                            GI_TRANSFER_EVERYTHING,
                                                            param_info,
                                                            element_type,
                                                            &elem)) {
                                failed = TRUE;
                            }
                        }
                    }
                }
                g_free (arg->v_pointer);
                break;

            default:
                gjs_throw(context,
                          "Releasing a C array with explicit length, that was nested"
                          "inside another container. This is not supported (and will leak)");
                failed = JS_TRUE;
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeInfo *param_info;
            GITypeTag element_type;

            param_info = g_type_info_get_param_type(type_info, 0);
            element_type = g_type_info_get_tag(param_info);

            switch (element_type) {
            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_UINT64:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_INT64:
            case GI_TYPE_TAG_GTYPE:
                g_array_free((GArray*) arg->v_pointer, TRUE);
                break;

            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_INTERFACE:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_GHASH:
            case GI_TYPE_TAG_ERROR:
                if (transfer == GI_TRANSFER_CONTAINER) {
                    g_array_free((GArray*) arg->v_pointer, TRUE);
                } else if (type_needs_out_release (param_info, element_type)) {
                    GArray *array = (GArray *) arg->v_pointer;
                    guint i;

                    for (i = 0; i < array->len; i++) {
                        GArgument arg;

                        arg.v_pointer = g_array_index (array, gpointer, i);
                        gjs_g_arg_release_internal(context,
                                                   transfer,
                                                   param_info,
                                                   element_type,
                                                   &arg);
                    }

                    g_array_free (array, TRUE);
                }

                break;

            default:
                gjs_throw(context,
                          "Don't know how to release GArray element-type %d",
                          element_type);
                failed = JS_TRUE;
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            g_byte_array_unref ((GByteArray*)arg->v_pointer);
        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GITypeInfo *param_info;
            GPtrArray *array;

            param_info = g_type_info_get_param_type(type_info, 0);
            array = (GPtrArray *) arg->v_pointer;

            if (transfer != GI_TRANSFER_CONTAINER) {
                guint i;

                for (i = 0; i < array->len; i++) {
                    GArgument arg;

                    arg.v_pointer = g_ptr_array_index (array, i);
                    gjs_g_argument_release(context,
                                           transfer,
                                           param_info,
                                           &arg);
                }
            }

            g_ptr_array_free(array, TRUE);

            g_base_info_unref((GIBaseInfo*) param_info);
        } else {
            g_assert_not_reached();
        }
        break;
    }

    case GI_TYPE_TAG_GSLIST:
        if (transfer != GI_TRANSFER_CONTAINER) {
            GITypeInfo *param_info;
            GSList *slist;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            for (slist = (GSList *) arg->v_pointer;
                 slist != NULL;
                 slist = slist->next) {
                GArgument elem;
                elem.v_pointer = slist->data;

                if (!gjs_g_arg_release_internal(context,
                                                transfer,
                                                param_info,
                                                g_type_info_get_tag(param_info),
                                                &elem)) {
                    failed = JS_TRUE;
                }
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        }

        g_slist_free((GSList *) arg->v_pointer);
        break;

    case GI_TYPE_TAG_GHASH:
        if (arg->v_pointer) {
            if (transfer == GI_TRANSFER_CONTAINER)
                g_hash_table_steal_all ((GHashTable *) arg->v_pointer);
            else {
                GHR_closure c = {
                    context, NULL, NULL,
                    transfer,
                    JS_FALSE
                };

                c.key_param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(c.key_param_info != NULL);
                c.val_param_info = g_type_info_get_param_type(type_info, 1);
                g_assert(c.val_param_info != NULL);

                g_hash_table_foreach_steal ((GHashTable *) arg->v_pointer,
                                            gjs_ghr_helper, &c);

                failed = c.failed;

                g_base_info_unref ((GIBaseInfo *)c.key_param_info);
                g_base_info_unref ((GIBaseInfo *)c.val_param_info);
            }

            g_hash_table_destroy ((GHashTable *) arg->v_pointer);
        }
        break;

    default:
        g_warning("Unhandled type %s releasing GArgument",
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    }

    return !failed;
}

JSBool
gjs_g_argument_release(JSContext  *context,
                       GITransfer  transfer,
                       GITypeInfo *type_info,
                       GArgument  *arg)
{
    GITypeTag type_tag;

    if (transfer == GI_TRANSFER_NOTHING)
        return JS_TRUE;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument %s out param or return value",
                      g_type_tag_to_string(type_tag));

    return gjs_g_arg_release_internal(context, transfer, type_info, type_tag, arg);
}

JSBool
gjs_g_argument_release_in_arg(JSContext  *context,
                              GITransfer  transfer,
                              GITypeInfo *type_info,
                              GArgument  *arg)
{
    GITypeTag type_tag;

    /* GI_TRANSFER_EVERYTHING: we don't own the argument anymore.
     * GI_TRANSFER_CONTAINER:
     * - non-containers: treated as GI_TRANSFER_EVERYTHING
     * - containers: See FIXME in gjs_array_to_g_list(); currently
     *   an error and we won't get here.
     */
    if (transfer != GI_TRANSFER_NOTHING)
        return JS_TRUE;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument %s in param",
                      g_type_tag_to_string(type_tag));

    if (type_needs_release (type_info, type_tag))
        return gjs_g_arg_release_internal(context, (GITransfer) TRANSFER_IN_NOTHING,
                                          type_info, type_tag, arg);

    return JS_TRUE;
}

JSBool
gjs_g_argument_release_in_array (JSContext  *context,
                                 GITransfer  transfer,
                                 GITypeInfo *type_info,
                                 guint       length,
                                 GArgument  *arg)
{
    GITypeInfo *param_type;
    gpointer *array;
    GArgument elem;
    guint i;
    JSBool ret = JS_TRUE;
    GITypeTag type_tag;

    if (transfer != GI_TRANSFER_NOTHING)
        return JS_TRUE;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument array in param");

    array = (gpointer *) arg->v_pointer;

    param_type = g_type_info_get_param_type(type_info, 0);
    type_tag = g_type_info_get_tag(param_type);

    if (is_gvalue_flat_array(param_type, type_tag)) {
        for (i = 0; i < length; i++) {
            GValue *v = ((GValue*)array) + i;
            g_value_unset(v);
        }
    }

    if (type_needs_release(param_type, type_tag)) {
        for (i = 0; i < length; i++) {
            elem.v_pointer = array[i];
            if (!gjs_g_arg_release_internal(context, (GITransfer) TRANSFER_IN_NOTHING,
                                            param_type, type_tag, &elem)) {
                ret = JS_FALSE;
                break;
            }
        }
    }

    g_base_info_unref(param_type);
    g_free(array);

    return ret;
}

JSBool
gjs_g_argument_release_out_array (JSContext  *context,
                                  GITransfer  transfer,
                                  GITypeInfo *type_info,
                                  guint       length,
                                  GArgument  *arg)
{
    GITypeInfo *param_type;
    gpointer *array;
    GArgument elem;
    guint i;
    JSBool ret = JS_TRUE;
    GITypeTag type_tag;

    if (transfer == GI_TRANSFER_NOTHING)
        return JS_TRUE;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument array out param");

    array = (gpointer *) arg->v_pointer;

    param_type = g_type_info_get_param_type(type_info, 0);
    type_tag = g_type_info_get_tag(param_type);

    if (transfer != GI_TRANSFER_CONTAINER &&
        type_needs_out_release(param_type, type_tag)) {
        for (i = 0; i < length; i++) {
            elem.v_pointer = array[i];
            if (!gjs_g_arg_release_internal(context,
                                            GI_TRANSFER_EVERYTHING,
                                            param_type,
                                            type_tag,
                                            &elem)) {
                ret = JS_FALSE;
            }
        }
    }

    g_base_info_unref(param_type);
    g_free(array);

    return ret;
}
