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
#include "object.h"
#include "foreign.h"
#include "boxed.h"
#include "union.h"
#include "value.h"
#include "gjs/byteArray.h"
#include <gjs/gjs.h>
#include <gjs/compat.h>

#include <util/log.h>

JSBool
_gjs_flags_value_is_valid(JSContext   *context,
                          GType        gtype,
                          guint        value)
{
    GFlagsValue *v;
    guint32 tmpval;
    void *klass;

    /* FIXME: Do proper value check for flags with GType's */
    if (gtype == G_TYPE_NONE)
        return JS_TRUE;

    klass = g_type_class_ref(gtype);

    /* check all bits are defined for flags.. not necessarily desired */
    tmpval = value;
    while (tmpval) {
        v = g_flags_get_first_value(klass, tmpval);
        if (!v) {
            gjs_throw(context,
                      "0x%x is not a valid value for flags %s",
                      value, g_type_name(G_TYPE_FROM_CLASS(klass)));
            return JS_FALSE;
        }

        tmpval &= ~v->value;
    }
    g_type_class_unref(klass);

    return JS_TRUE;
}

static JSBool
_gjs_enum_value_is_valid(JSContext  *context,
                         GIEnumInfo *enum_info,
                         int         value)
{
    JSBool found;
    int n_values;
    int i;

    n_values = g_enum_info_get_n_values(enum_info);
    found = JS_FALSE;

    for (i = 0; i < n_values; ++i) {
        GIValueInfo *value_info;
        long enum_value;

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
                  "%d is not a valid value for enumeration %s",
                  value, g_base_info_get_name((GIBaseInfo *)enum_info));
    }

    return found;
}

/* The typelib used to have machine-independent types like
 * GI_TYPE_TAG_LONG that had to be converted; now we only
 * handle GType specially here.
 */
static inline GITypeTag
replace_gtype(GITypeTag type) {
    if (type == GI_TYPE_TAG_GTYPE) {
        /* Constant folding should handle this hopefully */
        switch (sizeof(GType)) {
        case 1: return GI_TYPE_TAG_UINT8;
        case 2: return GI_TYPE_TAG_UINT16;
        case 4: return GI_TYPE_TAG_UINT32;
        case 8: return GI_TYPE_TAG_UINT64;
        default: g_assert_not_reached ();
        }
    }
    return type;
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

        if (g_type_is_a(gtype, G_TYPE_CLOSURE) || g_type_is_a(gtype, G_TYPE_VALUE))
            needs_release = TRUE;
        else
            needs_release = FALSE;

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
        GArgument elem_arg;

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

    prop_id = JSVAL_VOID;
    if (!JS_NextProperty(context, iter, &prop_id))
        return JS_FALSE;

    /* Don't use key/value destructor functions here, because we can't
     * construct correct ones in general if the value type is complex.
     * Rely on the type-aware g_argument_release functions. */
   result = g_hash_table_new(g_str_hash, g_str_equal);

    while (prop_id != JSVAL_VOID) {
        jsval key_js, val_js;
        GArgument key_arg, val_arg;

        if (!JS_IdToValue(context, prop_id, &key_js))
            goto free_hash_and_fail;

        /* Type check key type. */
        if (!gjs_value_to_g_argument(context, key_js, key_param_info, NULL,
                                     GJS_ARGUMENT_HASH_ELEMENT,
                                     transfer,
                                     FALSE /* don't allow null */,
                                     &key_arg))
            goto free_hash_and_fail;

#if (JS_VERSION > 180)
        if (!JS_GetPropertyById(context, props, prop_id, &val_js))
            goto free_hash_and_fail;
#else
        if (!JS_GetProperty(context, props, key_arg.v_pointer, &val_js))
            goto free_hash_and_fail;
#endif

        /* Type check and convert value to a c type */
        if (!gjs_value_to_g_argument(context, val_js, val_param_info, NULL,
                                     GJS_ARGUMENT_HASH_ELEMENT,
                                     transfer,
                                     TRUE /* allow null */,
                                     &val_arg))
            goto free_hash_and_fail;

        g_hash_table_insert(result, key_arg.v_pointer, val_arg.v_pointer);

        prop_id = JSVAL_VOID;
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
    JS_AddRoot(context, &elem);

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
    JS_RemoveRoot(context, &elem);

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
                       void       **arr_p)
{
    GITypeTag element_type;
    char *result;
    guint16 *result16;
    gsize length;

    element_type = g_type_info_get_tag(param_info);
    element_type = replace_gtype(element_type);

    switch (element_type) {
    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_UINT8:
        if (!gjs_string_get_binary_data(context, string_val,
                                        &result, &length))
            return JS_FALSE;
        *arr_p = result;
        return JS_TRUE;

    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_UINT16:
        if (!gjs_string_get_uint16_data(context, string_val,
                                        &result16, &length))
            return JS_FALSE;
        *arr_p = result16;
        return JS_TRUE;

    default:
        // can't convert a string to this type.
        gjs_throw(context, "Cannot convert string to type '%s'",
                  g_base_info_get_name((GIBaseInfo*) param_info));
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
    union { guint8 u8[0]; guint16 u16[0]; guint32 u32[0]; } *result;
    unsigned i;

    result = g_malloc0(length * intsize);

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
                      "Invalid element in string array");
            return JS_FALSE;
        }
        /* Note that this is truncating assignment. */
        switch (intsize) {
        case 1:
            result->u8[i] = (gint8) intval.u; break;
        case 2:
            result->u16[i] = (gint16) intval.u; break;
        case 4:
            result->u32[i] = (gint32) intval.u; break;
        default:
            g_assert_not_reached();
        }
    }

    *arr_p = result;

    return JS_TRUE;
}

static JSBool
gjs_array_to_array(JSContext   *context,
                   jsval        array_value,
                   unsigned int length,
                   GITypeInfo  *param_info,
                   void       **arr_p)
{
    enum { UNSIGNED=FALSE, SIGNED=TRUE };
    GITypeTag element_type;

    element_type = g_type_info_get_tag(param_info);
    element_type = replace_gtype(element_type);

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
    default:
        gjs_throw(context,
                  "Unhandled array element type %d", element_type);
        return JS_FALSE;
    }
}

static JSBool
gjs_array_to_g_array(JSContext   *context,
                     jsval        array_value,
                     unsigned int length,
                     GITypeInfo  *param_info,
                     void       **arr_p)
{
    GArray *array;
    GITypeTag element_type;
    gpointer contents;
    guint element_size;

    element_type = g_type_info_get_tag(param_info);
    element_type = replace_gtype(element_type);

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
    default:
        gjs_throw(context,
                  "Unhandled GArray element-type %d", element_type);
        return JS_FALSE;
    }

    /* create a C array */
    if (!gjs_array_to_array (context,
                             array_value,
                             length,
                             param_info,
                             &contents))
      return JS_FALSE;

    /* append that array to the GArray */
    array = g_array_sized_new(TRUE, TRUE, element_size, length);
    g_array_append_vals(array, contents, length);

    g_free(contents);

    *arr_p = array;

    return JS_TRUE;
}

static JSBool
gjs_array_to_byte_array(JSContext  *context,
                        jsval       value,
                        void      **arr_p)
{
   GByteArray *byte_array;
   byte_array = gjs_byte_array_get_byte_array(context,
                                              JSVAL_TO_OBJECT(value));
   if (!byte_array)
       return JS_FALSE;

   *arr_p = byte_array;
   return JS_TRUE;
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
    }

    g_assert_not_reached ();
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
    type_tag = replace_gtype(type_tag);

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

    case GI_TYPE_TAG_INTERFACE:
        nullable_type = TRUE;
        {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;
            GType gtype;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            interface_type = g_base_info_get_type(interface_info);

            switch(interface_type) {
            case GI_INFO_TYPE_STRUCT:
                if (g_struct_info_is_foreign((GIStructInfo*)interface_info)) {
                    return gjs_struct_foreign_convert_to_g_argument(
                            context, value, type_info, arg_name,
                            arg_type, transfer, may_be_null, arg);
                }
                /* fall through */
            case GI_INFO_TYPE_ENUM:
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

            } else if (JSVAL_IS_NULL(value) &&
                       interface_type != GI_INFO_TYPE_ENUM &&
                       interface_type != GI_INFO_TYPE_FLAGS) {
                arg->v_pointer = NULL;
            } else if (JSVAL_IS_OBJECT(value)) {
                /* Handle Struct/Union first since we don't necessarily need a GType for them */
                if ((interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) &&
                    /* We special case Closures later, so skip them here */
                    !g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                    arg->v_pointer = gjs_c_struct_from_boxed(context,
                                                             JSVAL_TO_OBJECT(value));
                    if (transfer != GI_TRANSFER_NOTHING) {
                        if (g_type_is_a(gtype, G_TYPE_BOXED))
                            arg->v_pointer = g_boxed_copy (gtype, arg->v_pointer);
                        else {
                            gjs_throw(context,
                                      "Can't transfer ownership of a structure type not registered as boxed");
                            arg->v_pointer = NULL;
                            wrong = TRUE;
                        }
                    }

                } else if (interface_type == GI_INFO_TYPE_UNION) {
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

                } else if (gtype != G_TYPE_NONE) {

                    if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                        arg->v_pointer = gjs_g_object_from_object(context,
                                                                  JSVAL_TO_OBJECT(value));
                        if (arg->v_pointer != NULL) {
                            if (!g_type_is_a(G_TYPE_FROM_INSTANCE(arg->v_pointer),
                                             gtype)) {
                                gjs_throw(context,
                                          "Expected type '%s' but got '%s'",
                                          g_type_name(gtype),
                                          g_type_name(G_TYPE_FROM_INSTANCE(arg->v_pointer)));
                                arg->v_pointer = NULL;
                                wrong = TRUE;
                            } else if (transfer != GI_TRANSFER_NOTHING)
                                g_object_ref(G_OBJECT(arg->v_pointer));
                        }
                    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                        if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                            arg->v_pointer = gjs_closure_new_marshaled(context,
                                                                       JSVAL_TO_OBJECT(value),
                                                                       "boxed");
                            g_closure_ref(arg->v_pointer);
                            g_closure_sink(arg->v_pointer);
                        } else {
                            /* Should have been caught above as STRUCT/BOXED/UNION */
                            gjs_throw(context,
                                      "Boxed type %s registered for unexpected interface_type %d",
                                      g_type_name(gtype),
                                      interface_type);
                        }
                    } else {
                        gjs_throw(context, "Unhandled GType %s unpacking GArgument from Object",
                                  g_type_name(gtype));
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
                nullable_type = FALSE;

                if (interface_type == GI_INFO_TYPE_ENUM) {
                    if (!JS_ValueToInt32(context, value, &arg->v_int)) {
                        wrong = TRUE;
                    } else if (!_gjs_enum_value_is_valid(context, (GIEnumInfo *)interface_info, arg->v_int)) {
                        wrong = TRUE;
                    }
                } else if (interface_type == GI_INFO_TYPE_FLAGS) {
                    if (!JS_ValueToInt32(context, value, &arg->v_int)) {
                        wrong = TRUE;
                    } else if (!_gjs_flags_value_is_valid(context, gtype, arg->v_int)) {
                        wrong = TRUE;
                    }
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
    case GI_TYPE_TAG_GSLIST:
        /* nullable_type=FALSE; while a list can be NULL in C, that
         * means empty array in JavaScript, it doesn't mean null in
         * JavaScript.
         */
        if (!JSVAL_IS_NULL(value) &&
            JSVAL_IS_OBJECT(value) &&
            gjs_object_has_property(context,
                                    JSVAL_TO_OBJECT(value),
                                    "length")) {
            jsval length_value;
            guint32 length;

            if (!gjs_object_require_property(context,
                                             JSVAL_TO_OBJECT(value), NULL,
                                             "length",
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

    case GI_TYPE_TAG_ARRAY:
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
            if (!may_be_null) {
                wrong = TRUE;
                report_type_mismatch = TRUE;
            }
        } else if (JSVAL_IS_STRING(value)) {
            /* Allow strings as int8/uint8/int16/uint16 arrays */
            GITypeInfo *param_info;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            if (!gjs_string_to_intarray(context, value, param_info,
                                        &arg->v_pointer))
                wrong = TRUE;

            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (!JSVAL_IS_OBJECT(value)) {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        } else if (gjs_object_has_property(context,
                                           JSVAL_TO_OBJECT(value),
                                           "length")) {
            jsval length_value;
            guint32 length;

            if (!gjs_object_require_property(context,
                                             JSVAL_TO_OBJECT(value), NULL,
                                             "length",
                                             &length_value) ||
                !JS_ValueToECMAUint32(context, length_value, &length)) {
                wrong = TRUE;
            } else {
                GIArrayType array_type = g_type_info_get_array_type(type_info);
                GITypeInfo *param_info;

                param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(param_info != NULL);

                if (array_type == GI_ARRAY_TYPE_C) {
                    if (!gjs_array_to_array (context,
                                             value,
                                             length,
                                             param_info,
                                             &arg->v_pointer))
                      wrong = TRUE;
                } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
                    if (!gjs_array_to_g_array (context,
                                               value,
                                               length,
                                               param_info,
                                               &arg->v_pointer))
                      wrong = TRUE;
                } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                    if (!gjs_array_to_byte_array(context,
                                                 value,
                                                 &arg->v_pointer))
                        wrong = TRUE;
                /* FIXME: support PtrArray */
                } else {
                    g_assert_not_reached();
                }

                g_base_info_unref((GIBaseInfo*) param_info);
            }
        } else {
            GIArrayType array_type = g_type_info_get_array_type(type_info);
            if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                if (!gjs_array_to_byte_array(context,
                                             value,
                                             &arg->v_pointer))
                    wrong = TRUE;
            } else {
                wrong = TRUE;
                report_type_mismatch = TRUE;
            }
        }
        break;

    default:
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s for JavaScript to GArgument conversion",
                  g_type_tag_to_string(type_tag));
        wrong = TRUE;
        report_type_mismatch = TRUE;
        break;
    }

    if (G_UNLIKELY(wrong)) {
        if (report_type_mismatch) {
            gchar *display_name = get_argument_display_name (arg_name, arg_type);
            gjs_throw(context, "Expected type %s for %s but got type '%s' %p",
                      g_type_tag_to_string(type_tag),
                      display_name,
                      JS_GetTypeName(context,
                                     JS_TypeOfValue(context, value)),
                      JSVAL_IS_OBJECT(value) ? JSVAL_TO_OBJECT(value) : NULL);
            g_free (display_name);
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
    type_tag = replace_gtype(type_tag);

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

    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
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
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s for default GArgument initialization",
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
    gboolean result;

    g_arg_info_load_type(arg_info, &type_info);

    result =
        gjs_value_to_g_argument(context, value,
                                &type_info,
                                g_base_info_get_name( (GIBaseInfo*) arg_info),
                                (g_arg_info_is_return_value(arg_info) ?
                                 GJS_ARGUMENT_RETURN_VALUE : GJS_ARGUMENT_ARGUMENT),
                                g_arg_info_get_ownership_transfer(arg_info),
                                g_arg_info_may_be_null(arg_info),
                                arg);

    return result;
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
    JS_AddRoot(context, &elem);

    result = JS_FALSE;

    i = 0;
    if (list_tag == GI_TYPE_TAG_GLIST) {
        for ( ; list != NULL; list = list->next) {
            arg.v_pointer = list->data;

            if (!gjs_value_from_g_argument(context, &elem,
                                           param_info, &arg))
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
                                           param_info, &arg))
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
    JS_RemoveRoot(context, &elem);

    return result;
}

static JSBool
gjs_array_from_g_array (JSContext  *context,
                        jsval      *value_p,
                        GITypeInfo *param_info,
                        GArray     *array)
{
    JSObject *obj;
    jsval elem;
    GArgument arg;
    JSBool result;
    GITypeTag element_type;
    guint i;

    element_type = g_type_info_get_tag(param_info);
    element_type = replace_gtype(element_type);

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
      return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);

    elem = JSVAL_VOID;

#define ITERATE(type) \
    for (i = 0; i < array->len; i++) { \
        arg.v_##type = g_array_index(array, g##type, i); \
        if (!gjs_value_from_g_argument(context, &elem, param_info, &arg)) \
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
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_INTERFACE:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
          ITERATE(pointer);
          break;
        default:
          gjs_throw(context, "Unknown GArray element-type %d", element_type);
          goto finally;
    }

#undef ITERATE

    result = JS_TRUE;

finally:
    JS_RemoveRoot(context, &elem);

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
    JS_AddRoot(context, &obj);

    keyjs = JSVAL_VOID;
    JS_AddRoot(context, &keyjs);

    valjs = JSVAL_VOID;
    JS_AddRoot(context, &valjs);

    keystr = NULL;
    JS_AddRoot(context, &keystr);

    result = JS_FALSE;

    g_hash_table_iter_init(&iter, hash);
    while (g_hash_table_iter_next
           (&iter, &keyarg.v_pointer, &valarg.v_pointer)) {
        if (!gjs_value_from_g_argument(context, &keyjs,
                                       key_param_info, &keyarg))
            goto out;

        keystr = JS_ValueToString(context, keyjs);
        if (!keystr)
            goto out;

        if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(keystr), &keyutf8))
            goto out;

        if (!gjs_value_from_g_argument(context, &valjs,
                                       val_param_info, &valarg))
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
    JS_RemoveRoot(context, &obj);
    JS_RemoveRoot(context, &keyjs);
    JS_RemoveRoot(context, &valjs);
    JS_RemoveRoot(context, &keystr);

    return result;
}

JSBool
gjs_value_from_g_argument (JSContext  *context,
                           jsval      *value_p,
                           GITypeInfo *type_info,
                           GArgument  *arg)
{
    GITypeTag type_tag;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);
    type_tag = replace_gtype(type_tag);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GArgument %s to jsval",
                      g_type_tag_to_string(type_tag));

    *value_p = JSVAL_NULL;

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        *value_p = JSVAL_VOID; /* or JSVAL_NULL ? */
        break;

    case GI_TYPE_TAG_BOOLEAN:
        *value_p = BOOLEAN_TO_JSVAL(arg->v_int);
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

    case GI_TYPE_TAG_FILENAME:
        if (arg->v_pointer)
            return gjs_string_from_filename(context, arg->v_pointer, -1, value_p);
        else {
            /* For NULL we'll return JSVAL_NULL, which is already set
             * in *value_p
             */
            return JS_TRUE;
        }
    case GI_TYPE_TAG_UTF8:
        if (arg->v_pointer)
            return gjs_string_from_utf8(context, arg->v_pointer, -1, value_p);
        else {
            /* For NULL we'll return JSVAL_NULL, which is already set
             * in *value_p
             */
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
                if (_gjs_enum_value_is_valid(context, (GIEnumInfo *)interface_info, arg->v_int))
                    value = INT_TO_JSVAL(arg->v_int);

                goto out;
            } else if (interface_type == GI_INFO_TYPE_FLAGS) {
                gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
                if (_gjs_flags_value_is_valid(context, gtype, arg->v_int))
                    value = INT_TO_JSVAL(arg->v_int);

                goto out;
            } else if (interface_type == GI_INFO_TYPE_STRUCT &&
                       g_struct_info_is_foreign((GIStructInfo*)interface_info)) {
                return gjs_struct_foreign_convert_from_g_argument(context, value_p, type_info, arg);
            }

            /* Everything else is a pointer type, NULL is the easy case */
            if (arg->v_pointer == NULL) {
                value = JSVAL_NULL;
                goto out;
            }

            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));


            /* Test GValue before Struct, or it will be handled as the latter */
            if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                if (!gjs_value_from_g_value(context, &value, arg->v_pointer))
                    value = JSVAL_VOID; /* Make sure error is flagged */

                goto out;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) {
                JSObject *obj;
                obj = gjs_boxed_from_c_struct(context, (GIStructInfo *)interface_info, arg->v_pointer,
                                              GJS_BOXED_CREATION_NONE);
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

            if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
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
            } else if (gtype == G_TYPE_NONE) {
                gjs_throw(context, "Unexpected unregistered type packing GArgument into jsval");
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
                GITypeTag param_tag;
                JSBool result;

                param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(param_info != NULL);

                param_tag = g_type_info_get_tag((GITypeInfo*) param_info);

                if (param_tag == GI_TYPE_TAG_UTF8) {
                    result = gjs_array_from_strv(context,
                                                 value_p,
                                                 arg->v_pointer);
                } else {
                    gjs_throw(context, "FIXME: Only supporting null-terminated arrays of strings");
                    result = FALSE;
                }

                g_base_info_unref((GIBaseInfo*) param_info);

                return result;
            } else {
                gjs_throw(context, "FIXME: Only supporting zero-terminated ARRAYs");
                return JS_FALSE;
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

            result = gjs_array_from_g_array(context,
                                            value_p,
                                            param_info,
                                            arg->v_pointer);

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
                                           arg->v_pointer : NULL,
                                           type_tag == GI_TYPE_TAG_GSLIST ?
                                           arg->v_pointer : NULL);

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
                                            arg->v_pointer);

            g_base_info_unref((GIBaseInfo*) key_param_info);
            g_base_info_unref((GIBaseInfo*) val_param_info);

            return result;
        }
        break;

    default:
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s converting GArgument to JavaScript",
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
    GHR_closure *c = user_data;
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
        break;

    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_UTF8:
        g_free(arg->v_pointer);
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
                        transfer, type_info, arg);

            if (interface_type == GI_INFO_TYPE_ENUM || interface_type == GI_INFO_TYPE_FLAGS)
                goto out;

            /* Anything else is a pointer */
            if (arg->v_pointer == NULL)
                goto out;

            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));

            /* In gjs_value_from_g_argument we handle Struct/Union types without a
             * registered GType, but here we are specifically handling a GArgument that
             * *owns* its value, and that is non-sensical for such types, so we
             * don't have to worry about it.
             */

            if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_object_unref(G_OBJECT(arg->v_pointer));
            } else if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                g_closure_unref(arg->v_pointer);
            } else if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                /* G_TYPE_VALUE is-a G_TYPE_BOXED, but we special case it */
                g_boxed_free(gtype, arg->v_pointer);
            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (transfer != TRANSFER_IN_NOTHING)
                    g_boxed_free(gtype, arg->v_pointer);
            } else if (gtype == G_TYPE_NONE) {
                if (transfer != TRANSFER_IN_NOTHING) {
                    gjs_throw(context, "Don't know how to release GArgument: not an object or boxed type");
                    failed = JS_TRUE;
                }
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

            for (list = arg->v_pointer;
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

        g_list_free(arg->v_pointer);
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
            element_type = replace_gtype(element_type);

            switch (element_type) {
            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
                if (transfer == GI_TRANSFER_CONTAINER)
                    g_free(arg->v_pointer);
                else if (transfer == GI_TRANSFER_EVERYTHING)
                    g_strfreev (arg->v_pointer);
                break;

            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
                g_free (arg->v_pointer);
                break;

            default:
                g_assert_not_reached ();
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeInfo *param_info;
            GITypeTag element_type;

            param_info = g_type_info_get_param_type(type_info, 0);
            element_type = g_type_info_get_tag(param_info);
            element_type = replace_gtype(element_type);

            switch (element_type) {
            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_UINT64:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_INT64:
                g_array_free((GArray*) arg->v_pointer, TRUE);
                break;

            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_INTERFACE:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_GHASH:
                if (transfer == GI_TRANSFER_CONTAINER) {
                    g_array_free((GArray*) arg->v_pointer, TRUE);
                } else if (transfer == GI_TRANSFER_EVERYTHING) {
                    GArray *array = arg->v_pointer;
                    guint i;

                    for (i = 0; i < array->len; i++) {
                        GArgument arg;

                        arg.v_pointer = g_array_index (array, gpointer, i);
                        gjs_g_argument_release(context,
                                               GI_TRANSFER_EVERYTHING,
                                               param_info,
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
            if (transfer == GI_TRANSFER_CONTAINER) {
                g_byte_array_free ((GByteArray*)arg->v_pointer, FALSE);
            } else if (transfer == GI_TRANSFER_EVERYTHING) {
                g_byte_array_free ((GByteArray*)arg->v_pointer, TRUE);
            }
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

            for (slist = arg->v_pointer;
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

        g_slist_free(arg->v_pointer);
        break;

    case GI_TYPE_TAG_GHASH:
        if (arg->v_pointer) {
            if (transfer == GI_TRANSFER_CONTAINER)
                g_hash_table_steal_all (arg->v_pointer);
            else {
                GHR_closure c = {
                    .context = context,
                    .transfer = transfer,
                    .failed = JS_FALSE
                };

                c.key_param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(c.key_param_info != NULL);
                c.val_param_info = g_type_info_get_param_type(type_info, 1);
                g_assert(c.val_param_info != NULL);

                g_hash_table_foreach_steal (arg->v_pointer,
                                            gjs_ghr_helper, &c);

                failed = c.failed;

                g_base_info_unref ((GIBaseInfo *)c.key_param_info);
                g_base_info_unref ((GIBaseInfo *)c.val_param_info);
            }

            g_hash_table_destroy (arg->v_pointer);
        }
        break;

    default:
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s releasing GArgument",
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
        return gjs_g_arg_release_internal(context, TRANSFER_IN_NOTHING,
                                          type_info, type_tag, arg);

    return JS_TRUE;
}



