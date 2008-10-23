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
#include "boxed.h"
#include "value.h"
#include <gjs/jsapi-util.h>

#include <util/log.h>

static JSBool gjs_value_to_g_arg_with_type_info(JSContext  *context,
                                                jsval       value,
                                                GITypeInfo *type_info,
                                                const char *arg_name,
                                                gboolean    is_return_value,
                                                gboolean    may_be_null,
                                                GArgument  *arg);

JSBool
_gjs_flags_value_is_valid(JSContext   *context,
                          GFlagsClass *klass,
                          guint        value)
{
    GFlagsValue *v;
    guint32 tmpval;

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

static JSBool
gjs_array_to_g_list(JSContext   *context,
                    jsval        array_value,
                    unsigned int length,
                    GITypeInfo  *param_info,
                    GITypeTag    list_type,
                    GList      **list_p,
                    GSList     **slist_p)
{
    guint32 i;
    GList *list;
    GSList *slist;
    jsval elem;
    GITypeTag param_tag;

    param_tag = g_type_info_get_tag(param_info);

    list = NULL;
    slist = NULL;

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
        if (!gjs_value_to_g_arg_with_type_info(context,
                                               elem,
                                               param_info,
                                               "list element",
                                               FALSE,
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
gjs_value_to_g_arg_with_type_info(JSContext  *context,
                                  jsval       value,
                                  GITypeInfo *type_info,
                                  const char *arg_name,
                                  gboolean    is_return_value,
                                  gboolean    may_be_null,
                                  GArgument  *arg)
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

#if (GLIB_SIZEOF_LONG == 4)
    case GI_TYPE_TAG_LONG:
    case GI_TYPE_TAG_SSIZE:
#endif
    case GI_TYPE_TAG_INT:
    case GI_TYPE_TAG_INT32:
        if (!JS_ValueToInt32(context, value, &arg->v_int))
            wrong = TRUE;
        break;

#if (GLIB_SIZEOF_LONG == 4)
    case GI_TYPE_TAG_ULONG:
    case GI_TYPE_TAG_SIZE:
#endif
    case GI_TYPE_TAG_UINT:
    case GI_TYPE_TAG_UINT32: {
        gdouble i;
        if (!JS_ValueToNumber(context, value, &i))
            wrong = TRUE;
        if (i > G_MAXUINT32 || i < 0)
            out_of_range = TRUE;
        arg->v_uint32 = (guint32)i;
        break;
    }

#if (GLIB_SIZEOF_LONG == 8)
    case GI_TYPE_TAG_LONG:
    case GI_TYPE_TAG_SSIZE:
#endif
    case GI_TYPE_TAG_INT64: {
        double v;
        if (!JS_ValueToNumber(context, value, &v))
            wrong = TRUE;
        if (v > G_MAXINT64 || v < G_MININT64)
            out_of_range = TRUE;
        arg->v_int64 = v;
    }
        break;

#if (GLIB_SIZEOF_LONG == 8)
    case GI_TYPE_TAG_ULONG:
    case GI_TYPE_TAG_SIZE:
#endif
    case GI_TYPE_TAG_UINT64: {
        double v;
        if (!JS_ValueToNumber(context, value, &v))
            wrong = TRUE;
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
        if (v > G_MAXFLOAT || v < G_MINFLOAT)
            out_of_range = TRUE;
        arg->v_float = (gfloat)v;
    }
        break;

    case GI_TYPE_TAG_DOUBLE:
        if (!JS_ValueToNumber(context, value, &arg->v_double))
            wrong = TRUE;
        break;

    case GI_TYPE_TAG_UTF8:
        nullable_type = TRUE;
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
        } else if (JSVAL_IS_STRING(value)) {
            if (!gjs_string_to_utf8(context, value, (char **)&arg->v_pointer))
                wrong = TRUE;
        } else {
            wrong = TRUE;
            report_type_mismatch = TRUE;
        }
        break;

    case GI_TYPE_TAG_INTERFACE:
        nullable_type = TRUE;
        {
            GIBaseInfo* symbol_info;
            GType gtype;

            symbol_info = g_type_info_get_interface(type_info);
            g_assert(symbol_info != NULL);

            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)symbol_info);

            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of SYMBOL is %s", g_type_name(gtype));

            if (gtype == G_TYPE_VALUE) {
                GValue *gvalue;

                gvalue = g_slice_new0(GValue);
                if (!gjs_value_to_g_value(context, value, gvalue)) {
                    g_slice_free(GValue, gvalue);
                    arg->v_pointer = NULL;
                    wrong = TRUE;
                }

                arg->v_pointer = gvalue;

            } else if (JSVAL_IS_NULL(value)) {
                arg->v_pointer = NULL;
            } else if (JSVAL_IS_OBJECT(value)) {
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
                        }
                    }
                } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                    if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                        arg->v_pointer = gjs_closure_new_marshaled(context,
                                                                   JSVAL_TO_OBJECT(value),
                                                                   "boxed");
                    } else {
                        arg->v_pointer = gjs_g_boxed_from_boxed(context,
                                                                JSVAL_TO_OBJECT(value));
                    }
                } else {
                    gjs_throw(context, "Unhandled GType %s unpacking SYMBOL GArgument from Object",
                              g_type_name(gtype));
                }

                if (arg->v_pointer == NULL) {
                    gjs_debug(GJS_DEBUG_GFUNCTION,
                              "conversion of JSObject %p type %s to gtype %s failed",
                              JSVAL_TO_OBJECT(value),
                              JS_GetTypeName(context,
                                             JS_TypeOfValue(context, value)),
                              g_type_name(gtype));

                    /* bis_js_throw should have been called already */
                    wrong = TRUE;
                }

            } else if (JSVAL_IS_NUMBER(value)) {
                nullable_type = FALSE;

                if (g_base_info_get_type(symbol_info) == GI_INFO_TYPE_ENUM) {
                    if (!JS_ValueToInt32(context, value, &arg->v_int)) {
                        wrong = TRUE;
                    } else if (!_gjs_enum_value_is_valid(context, (GIEnumInfo *)symbol_info, arg->v_int)) {
                          wrong = TRUE;
                    }
                } else if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
                    if (!JS_ValueToInt32(context, value, &arg->v_int)) {
                        wrong = TRUE;
                    } else {
                        void *klass;

                        klass = g_type_class_ref(gtype);
                        if (!_gjs_flags_value_is_valid(context, klass, arg->v_int))
                            wrong = TRUE;
                        g_type_class_unref(klass);
                    }
                } else {
                    gjs_throw(context, "Unhandled GType %s unpacking SYMBOL GArgument from Number",
                              g_type_name(gtype));
                }

            } else {
                gjs_debug(GJS_DEBUG_GFUNCTION,
                          "JSObject type '%s' is neither null nor an object",
                          JS_GetTypeName(context,
                                         JS_TypeOfValue(context, value)));
                wrong = TRUE;
                report_type_mismatch = TRUE;
            }
            g_base_info_unref( (GIBaseInfo*) symbol_info);
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
                                             JSVAL_TO_OBJECT(value),
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

    case GI_TYPE_TAG_ARRAY:
        if (JSVAL_IS_NULL(value)) {
            arg->v_pointer = NULL;
        } else {
            gjs_throw(context, "FIXME: Only supporting null ARRAYs");
            wrong = TRUE;
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
            gjs_throw(context, "Expected type %s for %s '%s' but got type '%s' %p",
                      g_type_tag_to_string(type_tag),
                      is_return_value ? "return value" : "argument",
                      arg_name,
                      JS_GetTypeName(context,
                                     JS_TypeOfValue(context, value)),
                      JSVAL_IS_OBJECT(value) ? JSVAL_TO_OBJECT(value) : NULL);
        }
        return JS_FALSE;
    } else if (G_UNLIKELY(out_of_range)) {
        gjs_throw(context, "value is out of range for %s '%s' type %s",
                  is_return_value ? "return value" : "argument",
                  arg_name,
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    } else if (nullable_type &&
               arg->v_pointer == NULL &&
               !may_be_null) {
        gjs_throw(context,
                  "%s '%s' (type %s) may not be null",
                  is_return_value ? "Return value" : "Argument",
                  arg_name,
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    } else {
        return JS_TRUE;
    }
}

JSBool
gjs_value_to_g_arg(JSContext  *context,
                   jsval       value,
                   GIArgInfo  *arg_info,
                   GArgument  *arg)
{
    GITypeInfo *type_info;
    gboolean result;

    type_info = g_arg_info_get_type(arg_info);

    result =
        gjs_value_to_g_arg_with_type_info(context, value,
                                          type_info,
                                          g_base_info_get_name( (GIBaseInfo*) arg_info),
                                          g_arg_info_is_return_value(arg_info),
                                          g_arg_info_may_be_null(arg_info),
                                          arg);

    g_base_info_unref((GIBaseInfo*) type_info);

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
    GITypeTag param_tag;

    param_tag = g_type_info_get_tag(param_info);

    obj = JS_NewArrayObject(context, 0, JSVAL_NULL);
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

            if (!gjs_value_from_g_arg(context, &elem,
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

            if (!gjs_value_from_g_arg(context, &elem,
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

JSBool
gjs_value_from_g_arg (JSContext  *context,
                      jsval      *value_p,
                      GITypeInfo *type_info,
                      GArgument  *arg)
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
        *value_p = BOOLEAN_TO_JSVAL(arg->v_int);
        break;

#if (GLIB_SIZEOF_LONG == 4)
    case GI_TYPE_TAG_LONG:
    case GI_TYPE_TAG_SSIZE:
#endif
    case GI_TYPE_TAG_INT:
    case GI_TYPE_TAG_INT32:
        return JS_NewNumberValue(context, arg->v_int, value_p);

#if (GLIB_SIZEOF_LONG == 4)
    case GI_TYPE_TAG_ULONG:
    case GI_TYPE_TAG_SIZE:
#endif
    case GI_TYPE_TAG_UINT:
    case GI_TYPE_TAG_UINT32:
        return JS_NewNumberValue(context, arg->v_uint, value_p);

#if (GLIB_SIZEOF_LONG == 8)
    case GI_TYPE_TAG_LONG:
    case GI_TYPE_TAG_SSIZE:
#endif
    case GI_TYPE_TAG_INT64:
        return JS_NewNumberValue(context, arg->v_int64, value_p);

#if (GLIB_SIZEOF_LONG == 8)
    case GI_TYPE_TAG_ULONG:
    case GI_TYPE_TAG_SIZE:
#endif
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
        return JS_NewDoubleValue(context, arg->v_float, value_p);

    case GI_TYPE_TAG_DOUBLE:
        return JS_NewDoubleValue(context, arg->v_double, value_p);

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
            if (arg->v_pointer == NULL) {
                /* OK, but no conversion to do */
            } else {
                jsval value;
                GIBaseInfo* symbol_info;
                GType gtype;

                symbol_info = g_type_info_get_interface(type_info);
                g_assert(symbol_info != NULL);

                if (g_base_info_get_type(symbol_info) == GI_INFO_TYPE_UNRESOLVED) {
                    gjs_throw(context,
                              "Unable to resolve arg type '%s'",
                              g_base_info_get_name(symbol_info));
                    g_base_info_unref( (GIBaseInfo*) symbol_info);
                    return JS_FALSE;
                }

                gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)symbol_info);

                gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                                  "gtype of SYMBOL is %s", g_type_name(gtype));

                value = JSVAL_VOID;

                if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                    JSObject *obj;
                    obj = gjs_object_from_g_object(context, G_OBJECT(arg->v_pointer));
                    if (obj)
                        value = OBJECT_TO_JSVAL(obj);
                } else if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                    if (!gjs_value_from_g_value(context, &value, arg->v_pointer)) {
                        g_base_info_unref( (GIBaseInfo*) symbol_info);
                        return JS_FALSE;
                    }
                } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                    JSObject *obj;
                    obj = gjs_boxed_from_g_boxed(context, gtype, arg->v_pointer);
                    if (obj)
                        value = OBJECT_TO_JSVAL(obj);
                } else if (g_base_info_get_type(symbol_info) == GI_INFO_TYPE_ENUM) {
                    if (_gjs_enum_value_is_valid(context, (GIEnumInfo *)symbol_info, arg->v_int))
                        value = INT_TO_JSVAL(arg->v_int);
                } else if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
                    void *klass;

                    klass = g_type_class_ref(gtype);

                    if (_gjs_flags_value_is_valid(context, G_FLAGS_CLASS(klass), arg->v_int))
                        value = INT_TO_JSVAL(arg->v_int);

                    g_type_class_unref(klass);
                } else {
                    gjs_throw(context, "Unhandled GType %s packing SYMBOL GArgument into jsval",
                                 g_type_name(gtype));
                }

                g_base_info_unref( (GIBaseInfo*) symbol_info);

                if (JSVAL_IS_VOID(value))
                    return JS_FALSE;

                *value_p = value;
            }
        }
        break;

    case GI_TYPE_TAG_ARRAY:
        if (arg->v_pointer == NULL) {
            /* OK, but no conversion to do */
        } else {
            gjs_throw(context, "FIXME: Only supporting null ARRAYs");
            return JS_FALSE;
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

    default:
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s converting GArgument to JavaScript",
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    }

    return JS_TRUE;
}

JSBool
gjs_g_arg_release(JSContext  *context,
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

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
    case GI_TYPE_TAG_BOOLEAN:
    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_UINT8:
    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_UINT16:
    case GI_TYPE_TAG_INT:
    case GI_TYPE_TAG_INT32:
    case GI_TYPE_TAG_UINT:
    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_INT64:
    case GI_TYPE_TAG_UINT64:
    case GI_TYPE_TAG_LONG:
    case GI_TYPE_TAG_ULONG:
    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
    case GI_TYPE_TAG_SSIZE:
    case GI_TYPE_TAG_SIZE:
        break;

    case GI_TYPE_TAG_UTF8:
        g_free(arg->v_pointer);
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            if (arg->v_pointer == NULL) {
                /* nothing to do */
            } else {
                jsval value;
                GIBaseInfo* symbol_info;
                GType gtype;

                symbol_info = g_type_info_get_interface(type_info);
                g_assert(symbol_info != NULL);

                gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)symbol_info);

                gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                                  "gtype of SYMBOL is %s", g_type_name(gtype));

                value = JSVAL_NULL;

                if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                    g_object_unref(G_OBJECT(arg->v_pointer));
                } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                    g_boxed_free(gtype, arg->v_pointer);
                } else if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                    GValue *value = arg->v_pointer;
                    g_value_unset(value);
                    g_slice_free(GValue, value);
                } else if (g_type_is_a(gtype, G_TYPE_ENUM) || g_type_is_a(gtype, G_TYPE_FLAGS)) {
                    /* nothing to do */
                } else {
                    gjs_throw(context, "Unhandled GType %s releasing SYMBOL GArgument",
                              g_type_name(gtype));
                }

                g_base_info_unref( (GIBaseInfo*) symbol_info);
            }
        }
        break;

    case GI_TYPE_TAG_GLIST:
        if (transfer == GI_TRANSFER_EVERYTHING) {
            GITypeInfo *param_info;
            GList *list;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            for (list = arg->v_pointer;
                 list != NULL;
                 list = list->next) {
                GArgument elem;
                elem.v_pointer = list->data;

                if (!gjs_g_arg_release(context,
                                       GI_TRANSFER_EVERYTHING,
                                       param_info,
                                       &elem)) {
                    /* no way to recover here, and errors should
                     * not be possible.
                     */
                    g_error("Failed to release list element");
                }
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        }

        g_list_free(arg->v_pointer);
        break;

    case GI_TYPE_TAG_ARRAY:
        if (arg->v_pointer == NULL) {
            /* OK */
        } else {
            gjs_throw(context, "FIXME: Only supporting null ARRAYs");
            return JS_FALSE;
        }
        break;

    case GI_TYPE_TAG_GSLIST:
        if (transfer == GI_TRANSFER_EVERYTHING) {
            GITypeInfo *param_info;
            GSList *slist;

            param_info = g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != NULL);

            for (slist = arg->v_pointer;
                 slist != NULL;
                 slist = slist->next) {
                GArgument elem;
                elem.v_pointer = slist->data;

                if (!gjs_g_arg_release(context,
                                       GI_TRANSFER_EVERYTHING,
                                       param_info,
                                       &elem)) {
                    /* no way to recover here, and errors should
                     * not be possible.
                     */
                    g_error("Failed to release slist element");
                }
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        }

        g_slist_free(arg->v_pointer);
        break;

    default:
        gjs_debug(GJS_DEBUG_ERROR,
                  "Unhandled type %s releasing GArgument",
                  g_type_tag_to_string(type_tag));
        return JS_FALSE;
    }

    return JS_TRUE;
}
