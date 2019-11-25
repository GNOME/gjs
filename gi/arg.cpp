/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <string.h>  // for strcmp, strlen, memcpy

#include <cmath>   // for std::abs
#include <limits>  // for numeric_limits
#include <string>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/foreign.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/interface.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"

bool _gjs_flags_value_is_valid(JSContext* context, GType gtype, int64_t value) {
    GFlagsValue *v;
    guint32 tmpval;

    /* FIXME: Do proper value check for flags with GType's */
    if (gtype == G_TYPE_NONE)
        return true;

    GjsAutoTypeClass<GTypeClass> klass(gtype);

    /* check all bits are defined for flags.. not necessarily desired */
    tmpval = (guint32)value;
    if (tmpval != value) { /* Not a guint32 */
        gjs_throw(context,
                  "0x%" G_GINT64_MODIFIER "x is not a valid value for flags %s",
                  value, g_type_name(G_TYPE_FROM_CLASS(klass)));
        return false;
    }

    while (tmpval) {
        v = g_flags_get_first_value(klass.as<GFlagsClass>(), tmpval);
        if (!v) {
            gjs_throw(context,
                      "0x%x is not a valid value for flags %s",
                      (guint32)value, g_type_name(G_TYPE_FROM_CLASS(klass)));
            return false;
        }

        tmpval &= ~v->value;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool _gjs_enum_value_is_valid(JSContext* context, GIEnumInfo* enum_info,
                                     int64_t value) {
    bool found;
    int n_values;
    int i;

    n_values = g_enum_info_get_n_values(enum_info);
    found = false;

    for (i = 0; i < n_values; ++i) {
        GIValueInfo *value_info;

        value_info = g_enum_info_get_value(enum_info, i);
        int64_t enum_value = g_value_info_get_value(value_info);
        g_base_info_unref((GIBaseInfo *)value_info);

        if (enum_value == value) {
            found = true;
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

GJS_USE
static bool
_gjs_enum_uses_signed_type (GIEnumInfo *enum_info)
{
    GITypeTag storage = g_enum_info_get_storage_type(enum_info);
    return (storage == GI_TYPE_TAG_INT8 ||
        storage == GI_TYPE_TAG_INT16 ||
        storage == GI_TYPE_TAG_INT32 ||
        storage == GI_TYPE_TAG_INT64);
}

/* This is hacky - g_function_info_invoke() and g_field_info_get/set_field() expect
 * arg->v_int to have the enum value in arg->v_int and depend on all flags and
 * enumerations being passed on the stack in a 32-bit field. See FIXME comment in
 * g_field_info_get_field. The same assumption of enums cast to 32-bit signed integers
 * is found in g_value_set_enum/g_value_set_flags().
 */

GJS_USE
int64_t _gjs_enum_from_int(GIEnumInfo* enum_info, int int_value) {
    if (_gjs_enum_uses_signed_type (enum_info))
        return int64_t(int_value);
    else
        return int64_t(uint32_t(int_value));
}

/* Here for symmetry, but result is the same for the two cases */
GJS_USE
static int _gjs_enum_to_int(int64_t value) { return static_cast<int>(value); }

/* Check if an argument of the given needs to be released if we created it
 * from a JS value to pass it into a function and aren't transfering ownership.
 */
GJS_USE
static bool
type_needs_release (GITypeInfo *type_info,
                    GITypeTag   type_tag)
{
    if (type_tag == GI_TYPE_TAG_UTF8 ||
        type_tag == GI_TYPE_TAG_FILENAME ||
        type_tag == GI_TYPE_TAG_ARRAY ||
        type_tag == GI_TYPE_TAG_GLIST ||
        type_tag == GI_TYPE_TAG_GSLIST ||
        type_tag == GI_TYPE_TAG_GHASH ||
        type_tag == GI_TYPE_TAG_ERROR)
        return true;

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* interface_info;
        GIInfoType interface_type;
        GType gtype;
        bool needs_release;

        interface_info = g_type_info_get_interface(type_info);
        g_assert(interface_info != NULL);

        interface_type = g_base_info_get_type(interface_info);

        if (interface_type == GI_INFO_TYPE_STRUCT ||
            interface_type == GI_INFO_TYPE_ENUM ||
            interface_type == GI_INFO_TYPE_FLAGS ||
            interface_type == GI_INFO_TYPE_OBJECT ||
            interface_type == GI_INFO_TYPE_INTERFACE ||
            interface_type == GI_INFO_TYPE_UNION ||
            interface_type == GI_INFO_TYPE_BOXED) {
            /* These are subtypes of GIRegisteredTypeInfo for which the
             * cast is safe */
            gtype = g_registered_type_info_get_g_type
                ((GIRegisteredTypeInfo*)interface_info);
        } else if (interface_type == GI_INFO_TYPE_VALUE) {
            /* Special case for GValues */
            gtype = G_TYPE_VALUE;
        } else {
            /* Everything else */
            gtype = G_TYPE_NONE;
        }

        if (g_type_is_a(gtype, G_TYPE_CLOSURE))
            needs_release = true;
        else if (g_type_is_a(gtype, G_TYPE_VALUE))
            needs_release = g_type_info_is_pointer(type_info);
        else
            needs_release = false;

        g_base_info_unref(interface_info);

        return needs_release;
    }

    return false;
}

/* Check if an argument of the given needs to be released if we obtained it
 * from out argument (or the return value), and we're transferring ownership
 */
GJS_USE
static bool
type_needs_out_release(GITypeInfo *type_info,
                       GITypeTag   type_tag)
{
    if (type_tag == GI_TYPE_TAG_UTF8 ||
        type_tag == GI_TYPE_TAG_FILENAME ||
        type_tag == GI_TYPE_TAG_ARRAY ||
        type_tag == GI_TYPE_TAG_GLIST ||
        type_tag == GI_TYPE_TAG_GSLIST ||
        type_tag == GI_TYPE_TAG_GHASH ||
        type_tag == GI_TYPE_TAG_ERROR)
        return true;

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* interface_info;
        GIInfoType interface_type;
        bool needs_release = true;

        interface_info = g_type_info_get_interface(type_info);
        g_assert(interface_info != NULL);

        interface_type = g_base_info_get_type(interface_info);

        if (interface_type == GI_INFO_TYPE_ENUM ||
            interface_type == GI_INFO_TYPE_FLAGS)
            needs_release = false;
        else if (interface_type == GI_INFO_TYPE_STRUCT ||
            interface_type == GI_INFO_TYPE_UNION)
            needs_release = g_type_info_is_pointer(type_info);

        g_base_info_unref(interface_info);

        return needs_release;
    }

    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_g_list(JSContext   *context,
                    JS::Value    array_value,
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
            return false;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    JS::RootedObject array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);
    for (i = 0; i < length; ++i) {
        GArgument elem_arg = { 0 };

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array, i, &elem)) {
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
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
                                     false,
                                     &elem_arg)) {
            return false;
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

    return true;
}

GJS_USE
static GHashTable *
create_hash_table_for_key_type(GITypeInfo  *key_param_info)
{
    /* Don't use key/value destructor functions here, because we can't
     * construct correct ones in general if the value type is complex.
     * Rely on the type-aware g_argument_release functions. */

    GITypeTag key_type = g_type_info_get_tag(key_param_info);

    if (key_type == GI_TYPE_TAG_UTF8 || key_type == GI_TYPE_TAG_FILENAME)
        return g_hash_table_new(g_str_hash, g_str_equal);
    return g_hash_table_new(NULL, NULL);
}

/* Converts a JS::Value to a GHashTable key, stuffing it into @pointer_out if
 * possible, otherwise giving the location of an allocated key in @pointer_out.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
value_to_ghashtable_key(JSContext      *cx,
                        JS::HandleValue value,
                        GITypeInfo     *type_info,
                        gpointer       *pointer_out)
{
    GITypeTag type_tag = g_type_info_get_tag((GITypeInfo*) type_info);
    bool out_of_range = false;
    bool unsupported = false;

    g_return_val_if_fail(value.isString() || value.isInt32(), false);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting JS::Value to GHashTable key %s",
                      g_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_BOOLEAN:
        /* This doesn't seem particularly useful, but it's easy */
        *pointer_out = GUINT_TO_POINTER(JS::ToBoolean(value));
        break;

    case GI_TYPE_TAG_UNICHAR:
        if (value.isInt32()) {
            *pointer_out = GINT_TO_POINTER(value.toInt32());
        } else {
            uint32_t ch;
            if (!gjs_unichar_from_string(cx, value, &ch))
                return false;
            *pointer_out = GUINT_TO_POINTER(ch);
        }
        break;

#define HANDLE_SIGNED_INT(bits)                        \
    case GI_TYPE_TAG_INT##bits: {                      \
        int32_t i;                                     \
        if (!JS::ToInt32(cx, value, &i))               \
            return false;                              \
        if (i > G_MAXINT##bits || i < G_MININT##bits)  \
            out_of_range = true;                       \
        *pointer_out = GINT_TO_POINTER(i);             \
        break;                                         \
    }

    HANDLE_SIGNED_INT(8);
    HANDLE_SIGNED_INT(16);
    HANDLE_SIGNED_INT(32);

#undef HANDLE_SIGNED_INT

#define HANDLE_UNSIGNED_INT(bits)                      \
    case GI_TYPE_TAG_UINT##bits: {                     \
        uint32_t i;                                    \
        if (!JS::ToUint32(cx, value, &i))              \
            return false;                              \
        if (i > G_MAXUINT##bits)                       \
            out_of_range = true;                       \
        *pointer_out = GUINT_TO_POINTER(i);            \
        break;                                         \
    }

    HANDLE_UNSIGNED_INT(8);
    HANDLE_UNSIGNED_INT(16);
    HANDLE_UNSIGNED_INT(32);

#undef HANDLE_UNSIGNED_INT

    case GI_TYPE_TAG_FILENAME: {
        GjsAutoChar cstr;
        JS::RootedValue str_val(cx, value);
        if (!str_val.isString()) {
            JS::RootedString str(cx, JS::ToString(cx, str_val));
            str_val.setString(str);
        }
        if (!gjs_string_to_filename(cx, str_val, &cstr))
            return false;
        *pointer_out = cstr.release();
        break;
    }

    case GI_TYPE_TAG_UTF8: {
        JS::RootedString str(cx);
        if (!value.isString())
            str = JS::ToString(cx, value);
        else
            str = value.toString();

        JS::UniqueChars cstr(JS_EncodeStringToUTF8(cx, str));
        if (!cstr)
            return false;
        *pointer_out = g_strdup(cstr.get());
        break;
    }

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
    case GI_TYPE_TAG_INT64:
    case GI_TYPE_TAG_UINT64:
    /* FIXME: The above four could be supported, but are currently not. The ones
     * below cannot be key types in a regular JS object; we would need to allow
     * marshalling Map objects into GHashTables to support those. */
    case GI_TYPE_TAG_VOID:
    case GI_TYPE_TAG_GTYPE:
    case GI_TYPE_TAG_ERROR:
    case GI_TYPE_TAG_INTERFACE:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ARRAY:
        unsupported = true;
        break;

    default:
        g_warning("Unhandled type %s for GHashTable key conversion",
                  g_type_tag_to_string(type_tag));
        unsupported = true;
        break;
    }

    if (G_UNLIKELY(unsupported)) {
        gjs_throw(cx, "Type %s not supported for hash table keys",
                  g_type_tag_to_string(type_tag));
        return false;
    }

    if (G_UNLIKELY(out_of_range)) {
        gjs_throw(cx, "value is out of range for hash table key of type %s",
                  g_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_object_to_g_hash(JSContext   *context,
                     JS::Value    hash_value,
                     GITypeInfo  *key_param_info,
                     GITypeInfo  *val_param_info,
                     GITransfer   transfer,
                     GHashTable **hash_p)
{
    GHashTable *result = NULL;
    size_t id_ix, id_len;

    g_assert(hash_value.isObjectOrNull());
    JS::RootedObject props(context, hash_value.toObjectOrNull());

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release (key_param_info, g_type_info_get_tag(key_param_info)) ||
            type_needs_release (val_param_info, g_type_info_get_tag(val_param_info))) {
            /* FIXME: to make this work, we'd have to keep a list of temporary
             * GArguments for the function call so we could free them after
             * the surrounding container had been freed by the callee.
             */
            gjs_throw(context,
                      "Container transfer for in parameters not supported");
            return false;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    JS::Rooted<JS::IdVector> ids(context, context);
    if (!JS_Enumerate(context, props, &ids))
        return false;

    result = create_hash_table_for_key_type(key_param_info);

    JS::RootedValue key_js(context), val_js(context);
    JS::RootedId cur_id(context);
    for (id_ix = 0, id_len = ids.length(); id_ix < id_len; ++id_ix) {
        cur_id = ids[id_ix];
        gpointer key_ptr, val_ptr;
        GIArgument val_arg = { 0 };

        if (!JS_IdToValue(context, cur_id, &key_js))
            goto free_hash_and_fail;

        /* Type check key type. */
        if (!value_to_ghashtable_key(context, key_js, key_param_info, &key_ptr))
            goto free_hash_and_fail;

        if (!JS_GetPropertyById(context, props, cur_id, &val_js))
            goto free_hash_and_fail;

        /* Type check and convert value to a c type */
        if (!gjs_value_to_g_argument(context, val_js, val_param_info, NULL,
                                     GJS_ARGUMENT_HASH_ELEMENT,
                                     transfer,
                                     true /* allow null */,
                                     &val_arg))
            goto free_hash_and_fail;

        GITypeTag val_type = g_type_info_get_tag(val_param_info);
        /* Use heap-allocated values for types that don't fit in a pointer */
        if (val_type == GI_TYPE_TAG_INT64) {
            int64_t *heap_val = g_new(int64_t, 1);
            *heap_val = val_arg.v_int64;
            val_ptr = heap_val;
        } else if (val_type == GI_TYPE_TAG_UINT64) {
            uint64_t *heap_val = g_new(uint64_t, 1);
            *heap_val = val_arg.v_uint64;
            val_ptr = heap_val;
        } else if (val_type == GI_TYPE_TAG_FLOAT) {
            float *heap_val = g_new(float, 1);
            *heap_val = val_arg.v_float;
            val_ptr = heap_val;
        } else if (val_type == GI_TYPE_TAG_DOUBLE) {
            double *heap_val = g_new(double, 1);
            *heap_val = val_arg.v_double;
            val_ptr = heap_val;
        } else {
            /* Other types are simply stuffed inside v_pointer */
            val_ptr = val_arg.v_pointer;
        }

        g_hash_table_insert(result, key_ptr, val_ptr);
    }

    *hash_p = result;
    return true;

 free_hash_and_fail:
    g_hash_table_destroy(result);
    return false;
}

bool
gjs_array_from_strv(JSContext             *context,
                    JS::MutableHandleValue value_p,
                    const char           **strv)
{
    guint i;
    JS::AutoValueVector elems(context);

    /* We treat a NULL strv as an empty array, since this function should always
     * set an array value when returning true.
     * Another alternative would be to set value_p to JS::NullValue, but clients
     * would need to always check for both an empty array and null if that was
     * the case.
     */
    for (i = 0; strv != NULL && strv[i] != NULL; i++) {
        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(context);
            return false;
        }

        if (!gjs_string_from_utf8(context, strv[i], elems[i]))
            return false;
    }

    JS::RootedObject obj(context, JS_NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

bool
gjs_array_to_strv(JSContext   *context,
                  JS::Value    array_value,
                  unsigned int length,
                  void       **arr_p)
{
    char **result;
    guint32 i;
    JS::RootedObject array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    result = g_new0(char *, length+1);

    for (i = 0; i < length; ++i) {
        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array, i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        JS::UniqueChars tmp_result;
        if (!gjs_string_to_utf8(context, elem, &tmp_result)) {
            g_strfreev(result);
            return false;
        }
        result[i] = g_strdup(tmp_result.get());
    }

    *arr_p = result;

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_string_to_intarray(JSContext       *context,
                       JS::HandleString str,
                       GITypeInfo      *param_info,
                       void           **arr_p,
                       size_t          *length)
{
    GITypeTag element_type;
    char16_t *result16;

    element_type = g_type_info_get_tag(param_info);

    if (element_type == GI_TYPE_TAG_INT8 || element_type == GI_TYPE_TAG_UINT8) {
        JS::UniqueChars result(JS_EncodeStringToUTF8(context, str));
        if (!result)
            return false;
        *length = strlen(result.get());
        *arr_p = g_strdup(result.get());
        return true;
    }

    if (element_type == GI_TYPE_TAG_INT16 || element_type == GI_TYPE_TAG_UINT16) {
        if (!gjs_string_get_char16_data(context, str, &result16, length))
            return false;
        *arr_p = result16;
        return true;
    }

    if (element_type == GI_TYPE_TAG_UNICHAR) {
        gunichar *result_ucs4;
        if (!gjs_string_to_ucs4(context, str, &result_ucs4, length))
            return false;
        *arr_p = result_ucs4;
        return true;
    }

    /* can't convert a string to this type */
    gjs_throw(context, "Cannot convert string to array of '%s'",
              g_type_tag_to_string (element_type));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_gboolean_array(JSContext      *cx,
                            JS::Value       array_value,
                            unsigned        length,
                            void          **arr_p)
{
    unsigned i;
    JS::RootedObject array(cx, array_value.toObjectOrNull());
    JS::RootedValue elem(cx);

    gboolean *result = g_new0(gboolean, length);

    for (i = 0; i < length; i++) {
        if (!JS_GetElement(cx, array, i, &elem)) {
            g_free(result);
            gjs_throw(cx, "Missing array element %u", i);
            return false;
        }
        bool val = JS::ToBoolean(elem);
        result[i] = val;
    }

    *arr_p = result;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_intarray(JSContext   *context,
                      JS::Value    array_value,
                      unsigned int length,
                      void       **arr_p,
                      unsigned     intsize,
                      bool         is_signed)
{
    /* nasty union types in an attempt to unify the various int types */
    union { uint64_t u; int64_t i; } intval;
    void *result;
    unsigned i;
    JS::RootedObject array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    /* add one so we're always zero terminated */
    result = g_malloc0((length+1) * intsize);

    for (i = 0; i < length; ++i) {
        bool success;

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array, i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        /* do whatever sign extension is appropriate */
        success = (is_signed) ?
            JS::ToInt64(context, elem, &(intval.i)) :
            JS::ToUint64(context, elem, &(intval.u));

        if (!success) {
            g_free(result);
            gjs_throw(context,
                      "Invalid element in int array");
            return false;
        }
        /* Note that this is truncating assignment. */
        switch (intsize) {
        case 1:
            ((guint8*)result)[i] = (gint8) intval.u; break;
        case 2:
            ((guint16*)result)[i] = (gint16) intval.u; break;
        case 4:
            ((guint32*)result)[i] = (gint32) intval.u; break;
        case 8:
            ((uint64_t *)result)[i] = (int64_t) intval.u; break;
        default:
            g_assert_not_reached();
        }
    }

    *arr_p = result;

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_gtypearray_to_array(JSContext   *context,
                        JS::Value    array_value,
                        unsigned int length,
                        void       **arr_p)
{
    unsigned i;

    /* add one so we're always zero terminated */
    GjsAutoPointer<GType, void, g_free> result =
        static_cast<GType*>(g_malloc0((length + 1) * sizeof(GType)));

    JS::RootedObject elem_obj(context), array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);
    for (i = 0; i < length; ++i) {
        GType gtype;

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array, i, &elem))
            return false;

        if (!elem.isObject()) {
            gjs_throw(context, "Invalid element in GType array");
            return false;
        }

        elem_obj = &elem.toObject();
        if (!gjs_gtype_get_actual_gtype(context, elem_obj, &gtype))
            return false;
        if (gtype == G_TYPE_INVALID) {
            gjs_throw(context, "Invalid element in GType array");
            return false;
        }

        result[i] = gtype;
    }

    *arr_p = result.release();

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_floatarray(JSContext   *context,
                        JS::Value    array_value,
                        unsigned int length,
                        void       **arr_p,
                        bool         is_double)
{
    unsigned int i;
    void *result;
    JS::RootedObject array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    /* add one so we're always zero terminated */
    result = g_malloc0((length+1) * (is_double ? sizeof(double) : sizeof(float)));

    for (i = 0; i < length; ++i) {
        double val;
        bool success;

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array, i, &elem)) {
            g_free(result);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        /* do whatever sign extension is appropriate */
        success = JS::ToNumber(context, elem, &val);

        if (!success) {
            g_free(result);
            gjs_throw(context,
                      "Invalid element in array");
            return false;
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

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_ptrarray(JSContext   *context,
                      JS::Value    array_value,
                      unsigned int length,
                      GITransfer   transfer,
                      GITypeInfo  *param_info,
                      void       **arr_p)
{
    unsigned int i;
    JS::RootedObject array_obj(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    /* Always one extra element, to cater for null terminated arrays */
    void **array = (void **) g_malloc((length + 1) * sizeof(gpointer));
    array[length] = NULL;

    for (i = 0; i < length; i++) {
        GIArgument arg;
        arg.v_pointer = NULL;

        bool success;

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array_obj, i, &elem)) {
            g_free(array);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        success = gjs_value_to_g_argument (context,
                                           elem,
                                           param_info,
                                           NULL, /* arg name */
                                           GJS_ARGUMENT_ARRAY_ELEMENT,
                                           transfer,
                                           false, /* absent better information, false for now */
                                           &arg);

        if (!success) {
            g_free(array);
            gjs_throw(context,
                      "Invalid element in array");
            return false;
        }

        array[i] = arg.v_pointer;
    }

    *arr_p = array;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_flat_gvalue_array(JSContext   *context,
                               JS::Value    array_value,
                               unsigned int length,
                               void       **arr_p)
{
    GValue *values = g_new0(GValue, length);
    unsigned int i;
    bool result = true;
    JS::RootedObject array(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    for (i = 0; i < length; i ++) {
        elem = JS::UndefinedValue();

        if (!JS_GetElement(context, array, i, &elem)) {
            g_free(values);
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        result = gjs_value_to_g_value(context, elem, &values[i]);

        if (!result)
            break;
    }

    if (result)
        *arr_p = values;

    return result;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_flat_gvalue_array(JSContext             *context,
                                 gpointer               array,
                                 unsigned               length,
                                 JS::MutableHandleValue value)
{
    GValue *values = (GValue *)array;

    // a null array pointer takes precedence over whatever `length` says
    if (!values) {
        JSObject* jsarray = JS_NewArrayObject(context, 0);
        if (!jsarray)
            return false;
        value.setObject(*jsarray);
        return true;
    }

    unsigned int i;
    JS::AutoValueVector elems(context);
    if (!elems.resize(length)) {
        JS_ReportOutOfMemory(context);
        return false;
    }

    bool result = true;

    for (i = 0; i < length; i ++) {
        GValue *gvalue = &values[i];
        result = gjs_value_from_g_value(context, elems[i], gvalue);
        if (!result)
            break;
    }

    if (result) {
        JSObject *jsarray;
        jsarray = JS_NewArrayObject(context, elems);
        value.setObjectOrNull(jsarray);
    }

    return result;
}

GJS_USE
static bool
is_gvalue(GIBaseInfo *info,
          GIInfoType  info_type)
{
    if (info_type == GI_INFO_TYPE_VALUE)
        return true;

    if (info_type == GI_INFO_TYPE_STRUCT ||
        info_type == GI_INFO_TYPE_OBJECT ||
        info_type == GI_INFO_TYPE_INTERFACE ||
        info_type == GI_INFO_TYPE_BOXED) {
        GType gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo *) info);
        return g_type_is_a(gtype, G_TYPE_VALUE);
    }

    return false;
}

GJS_USE
static bool
is_gvalue_flat_array(GITypeInfo *param_info,
                     GITypeTag   element_type)
{
    GIBaseInfo *interface_info;
    GIInfoType info_type;
    bool result;

    if (element_type != GI_TYPE_TAG_INTERFACE)
        return false;

    interface_info = g_type_info_get_interface(param_info);
    info_type = g_base_info_get_type(interface_info);

    /* Special case for GValue "flat arrays" */
    result = (is_gvalue(interface_info, info_type) &&
              !g_type_info_is_pointer(param_info));
    g_base_info_unref(interface_info);

    return result;
}

GJS_JSAPI_RETURN_CONVENTION
static bool is_empty(JSContext* cx, JS::HandleValue array_value, bool* empty) {
    if (array_value.isNull()) {
        *empty = true;
        return true;
    }

    bool is_array;
    if (!JS_IsArrayObject(cx, array_value, &is_array))
        return false;
    if (!is_array) {
        *empty = false;
        return true;
    }

    JS::RootedObject array_object(cx, &array_value.toObject());
    uint32_t length;
    if (!JS_GetArrayLength(cx, array_object, &length))
        return false;
    *empty = (length == 0);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_array(JSContext* context, JS::HandleValue array_value,
                               size_t length, GITransfer transfer,
                               GITypeInfo* param_info, void** arr_p) {
    enum { UNSIGNED=false, SIGNED=true };
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
    case GI_TYPE_TAG_BOOLEAN:
        return gjs_array_to_gboolean_array(context, array_value, length, arr_p);
    case GI_TYPE_TAG_UNICHAR:
        return gjs_array_to_intarray(context, array_value, length, arr_p,
            sizeof(gunichar), UNSIGNED);
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
    case GI_TYPE_TAG_INT64:
        return gjs_array_to_intarray(context, array_value, length, arr_p, 8,
            SIGNED);
    case GI_TYPE_TAG_UINT64:
        return gjs_array_to_intarray(context, array_value, length, arr_p, 8,
            UNSIGNED);
    case GI_TYPE_TAG_FLOAT:
        return gjs_array_to_floatarray
            (context, array_value, length, arr_p, false);
    case GI_TYPE_TAG_DOUBLE:
        return gjs_array_to_floatarray
            (context, array_value, length, arr_p, true);
    case GI_TYPE_TAG_GTYPE:
        return gjs_gtypearray_to_array
            (context, array_value, length, arr_p);

    /* Everything else is a pointer type */
    case GI_TYPE_TAG_INTERFACE:
        // Flat arrays of structures are not supported yet; see
        // https://gitlab.gnome.org/GNOME/gjs/issues/44
        if (!g_type_info_is_pointer(param_info)) {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(param_info);
            GIInfoType info_type = g_base_info_get_type(interface_info);

            bool array_is_empty;
            if (!is_empty(context, array_value, &array_is_empty))
                return false;

            if (!array_is_empty && (info_type == GI_INFO_TYPE_STRUCT ||
                                    info_type == GI_INFO_TYPE_UNION)) {
                gjs_throw(context,
                      "Flat array of type %s is not currently supported",
                      interface_info.name());
                return false;
            }
        }
        /* fall through */
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
    case GI_TYPE_TAG_VOID:
    default:
        gjs_throw(context,
                  "Unhandled array element type %d", element_type);
        return false;
    }
}

GJS_JSAPI_RETURN_CONVENTION
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
    case GI_TYPE_TAG_BOOLEAN:
        element_size = sizeof(gboolean);
        break;
    case GI_TYPE_TAG_UNICHAR:
        element_size = sizeof(gunichar);
        break;
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
    case GI_TYPE_TAG_VOID:
    default:
        gjs_throw(context,
                  "Unhandled GArray element-type %d", element_type);
        return NULL;
    }

    return g_array_sized_new(true, false, element_size, length);
}

char* gjs_argument_display_name(const char* arg_name,
                                GjsArgumentType arg_type) {
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
    default:
        g_assert_not_reached ();
    }
}

GJS_USE
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
                       JS::HandleValue value,
                       GITypeInfo     *arginfo,
                       const char     *arg_name,
                       GjsArgumentType arg_type)
{
    GjsAutoChar display_name = gjs_argument_display_name(arg_name, arg_type);

    gjs_throw(context, "Expected type %s for %s but got type '%s'",
              type_tag_to_human_string(arginfo), display_name.get(),
              JS::InformalValueTypeName(value));
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_to_explicit_array_internal(JSContext       *context,
                                     JS::HandleValue  value,
                                     GITypeInfo      *type_info,
                                     const char      *arg_name,
                                     GjsArgumentType  arg_type,
                                     GITransfer       transfer,
                                     bool             may_be_null,
                                     gpointer        *contents,
                                     gsize           *length_p)
{
    bool ret = false;
    GITypeInfo *param_info;
    bool found_length;

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to C array, transfer %d",
        arg_name, gjs_debug_value(value).c_str(), transfer);

    param_info = g_type_info_get_param_type(type_info, 0);

    if ((value.isNull() && !may_be_null) ||
        (!value.isString() && !value.isObjectOrNull())) {
        throw_invalid_argument(context, value, param_info, arg_name, arg_type);
        g_base_info_unref((GIBaseInfo*) param_info);
        return false;
    }

    if (value.isNull()) {
        *contents = NULL;
        *length_p = 0;
    } else if (value.isString()) {
        /* Allow strings as int8/uint8/int16/uint16 arrays */
        JS::RootedString str(context, value.toString());
        if (!gjs_string_to_intarray(context, str, param_info, contents, length_p))
            goto out;
    } else {
        JS::RootedObject array_obj(context, &value.toObject());
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        GITypeTag element_type = g_type_info_get_tag(param_info);
        if (JS_IsUint8Array(array_obj) && (element_type == GI_TYPE_TAG_INT8 ||
                                           element_type == GI_TYPE_TAG_UINT8)) {
            GBytes* bytes = gjs_byte_array_get_bytes(array_obj);
            *contents = g_bytes_unref_to_data(bytes, length_p);
        } else if (JS_HasPropertyById(context, array_obj, atoms.length(),
                                      &found_length) &&
                   found_length) {
            guint32 length;

            if (!gjs_object_require_converted_property(
                    context, array_obj, nullptr, atoms.length(), &length)) {
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
    }

    ret = true;
 out:
    g_base_info_unref((GIBaseInfo*) param_info);

    return ret;
}

GJS_USE
static bool
is_gdk_atom(GIBaseInfo *info)
{
    return (strcmp("Atom", g_base_info_get_name(info)) == 0 &&
            strcmp("Gdk", g_base_info_get_namespace(info)) == 0);
}

static void
intern_gdk_atom(const char *name,
                GArgument  *ret)
{
    GjsAutoFunctionInfo atom_intern_fun =
        g_irepository_find_by_name(nullptr, "Gdk", "atom_intern");

    GIArgument atom_intern_args[2];

    /* Can only store char * in GIArgument. First argument to gdk_atom_intern
     * is const char *, string isn't modified. */
    atom_intern_args[0].v_string = const_cast<char *>(name);

    atom_intern_args[1].v_boolean = false;

    g_function_info_invoke(atom_intern_fun,
                           atom_intern_args, 2,
                           nullptr, 0,
                           ret,
                           nullptr);
}

static bool value_to_interface_gi_argument(JSContext* cx, JS::HandleValue value,
                                           GIBaseInfo* interface_info,
                                           GIInfoType interface_type,
                                           GITransfer transfer,
                                           bool expect_object, GIArgument* arg,
                                           bool* report_type_mismatch) {
    g_assert(report_type_mismatch);
    GType gtype = G_TYPE_NONE;

    if (interface_type == GI_INFO_TYPE_STRUCT ||
        interface_type == GI_INFO_TYPE_ENUM ||
        interface_type == GI_INFO_TYPE_FLAGS ||
        interface_type == GI_INFO_TYPE_OBJECT ||
        interface_type == GI_INFO_TYPE_INTERFACE ||
        interface_type == GI_INFO_TYPE_UNION ||
        interface_type == GI_INFO_TYPE_BOXED) {
        // These are subtypes of GIRegisteredTypeInfo for which the cast is safe
        gtype = g_registered_type_info_get_g_type(interface_info);
    } else if (interface_type == GI_INFO_TYPE_VALUE) {
        // Special case for GValues
        gtype = G_TYPE_VALUE;
    }

    if (gtype != G_TYPE_NONE)
        gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "gtype of INTERFACE is %s",
                          g_type_name(gtype));

    if (gtype == G_TYPE_VALUE) {
        GValue gvalue = G_VALUE_INIT;

        if (!gjs_value_to_g_value(cx, value, &gvalue)) {
            arg->v_pointer = nullptr;
            return false;
        }

        arg->v_pointer = g_boxed_copy(G_TYPE_VALUE, &gvalue);
        g_value_unset(&gvalue);
        return true;

    } else if (is_gdk_atom(interface_info)) {
        if (!value.isNull() && !value.isString()) {
            *report_type_mismatch = true;
            return false;
        } else if (value.isNull()) {
            intern_gdk_atom("NONE", arg);
            return true;
        }

        JS::RootedString str(cx, value.toString());
        JS::UniqueChars name(JS_EncodeStringToUTF8(cx, str));
        if (!name)
            return false;

        intern_gdk_atom(name.get(), arg);
        return true;

    } else if (expect_object != value.isObjectOrNull()) {
        *report_type_mismatch = true;
        return false;

    } else if (value.isNull()) {
        arg->v_pointer = nullptr;
        return true;

    } else if (value.isObject()) {
        JS::RootedObject obj(cx, &value.toObject());
        if (interface_type == GI_INFO_TYPE_STRUCT &&
            g_struct_info_is_gtype_struct(interface_info)) {
            GType actual_gtype;
            if (!gjs_gtype_get_actual_gtype(cx, obj, &actual_gtype))
                return false;

            if (actual_gtype == G_TYPE_NONE) {
                *report_type_mismatch = true;
                return false;
            }

            // We use peek here to simplify reference counting (we just ignore
            // transfer annotation, as GType classes are never really freed)
            // We know that the GType class is referenced at least once when
            // the JS constructor is initialized.
            void* klass;
            if (g_type_is_a(actual_gtype, G_TYPE_INTERFACE))
                klass = g_type_default_interface_peek(actual_gtype);
            else
                klass = g_type_class_peek(actual_gtype);

            arg->v_pointer = klass;
            return true;

        } else if ((interface_type == GI_INFO_TYPE_STRUCT ||
                    interface_type == GI_INFO_TYPE_BOXED) &&
                   !g_type_is_a(gtype, G_TYPE_CLOSURE)) {
            // Handle Struct/Union first since we don't necessarily need a GType
            // for them. We special case Closures later, so skip them here.
            if (g_type_is_a(gtype, G_TYPE_BYTES) && JS_IsUint8Array(obj)) {
                arg->v_pointer = gjs_byte_array_get_bytes(obj);
                return true;
            }
            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                return ErrorBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer);
            }
            return BoxedBase::transfer_to_gi_argument(
                cx, obj, arg, GI_DIRECTION_IN, transfer, gtype, interface_info);

        } else if (interface_type == GI_INFO_TYPE_UNION) {
            return UnionBase::transfer_to_gi_argument(
                cx, obj, arg, GI_DIRECTION_IN, transfer, gtype, interface_info);

        } else if (gtype != G_TYPE_NONE) {
            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                return ObjectBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);

            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if (!gjs_typecheck_param(cx, obj, gtype, true)) {
                    arg->v_pointer = nullptr;
                    return false;
                }
                arg->v_pointer = gjs_g_param_from_param(cx, obj);
                if (transfer != GI_TRANSFER_NOTHING)
                    g_param_spec_ref(G_PARAM_SPEC(arg->v_pointer));
                return true;

            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                    GClosure* closure = gjs_closure_new_marshaled(
                        cx, JS_GetObjectFunction(obj), "boxed");
                    g_closure_ref(closure);
                    g_closure_sink(closure);
                    arg->v_pointer = closure;
                    return true;
                }

                // Should have been caught above as STRUCT/BOXED/UNION
                gjs_throw(
                    cx,
                    "Boxed type %s registered for unexpected interface_type %d",
                    g_type_name(gtype), interface_type);
                return false;

            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                return FundamentalBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);

            } else if (G_TYPE_IS_INTERFACE(gtype)) {
                // Could be a GObject interface that's missing a prerequisite,
                // or could be a fundamental
                if (ObjectBase::typecheck(cx, obj, nullptr, gtype,
                                          GjsTypecheckNoThrow())) {
                    return ObjectBase::transfer_to_gi_argument(
                        cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);
                }

                // If this typecheck fails, then it's neither an object nor a
                // fundamental
                return FundamentalBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);
            }

            gjs_throw(cx, "Unhandled GType %s unpacking GIArgument from Object",
                      g_type_name(gtype));
            arg->v_pointer = nullptr;
            return false;
        }

        gjs_debug(GJS_DEBUG_GFUNCTION,
                  "conversion of JSObject value %s to type %s failed",
                  gjs_debug_value(value).c_str(),
                  g_base_info_get_name(interface_info));

        gjs_throw(cx,
                  "Unexpected unregistered type unpacking GIArgument from "
                  "Object");
        return false;

    } else if (value.isNumber()) {
        if (interface_type == GI_INFO_TYPE_ENUM) {
            int64_t value_int64;

            if (!JS::ToInt64(cx, value, &value_int64) ||
                !_gjs_enum_value_is_valid(cx, interface_info, value_int64))
                return false;

            arg->v_int = _gjs_enum_to_int(value_int64);
            return true;

        } else if (interface_type == GI_INFO_TYPE_FLAGS) {
            int64_t value_int64;

            if (!JS::ToInt64(cx, value, &value_int64) ||
                !_gjs_flags_value_is_valid(cx, gtype, value_int64))
                return false;

            arg->v_int = _gjs_enum_to_int(value_int64);
            return true;

        } else if (gtype == G_TYPE_NONE) {
            gjs_throw(cx,
                      "Unexpected unregistered type unpacking GIArgument from "
                      "Number");
            return false;
        }

        gjs_throw(cx, "Unhandled GType %s unpacking GIArgument from Number",
                  g_type_name(gtype));
        return false;
    }

    gjs_debug(GJS_DEBUG_GFUNCTION,
              "JSObject type '%s' is neither null nor an object",
              JS::InformalValueTypeName(value));
    *report_type_mismatch = true;
    return false;
}

bool
gjs_value_to_g_argument(JSContext      *context,
                        JS::HandleValue value,
                        GITypeInfo     *type_info,
                        const char     *arg_name,
                        GjsArgumentType arg_type,
                        GITransfer      transfer,
                        bool            may_be_null,
                        GArgument      *arg)
{
    GITypeTag type_tag = g_type_info_get_tag(type_info);

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type %s", arg_name,
        gjs_debug_value(value).c_str(), g_type_tag_to_string(type_tag));

    bool nullable_type = false;
    bool wrong = false;  // return false
    bool out_of_range = false;
    bool report_type_mismatch = false;  // wrong=true, and still need to
                                        // gjs_throw a type problem

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        nullable_type = true;
        arg->v_pointer = NULL; /* just so it isn't uninitialized */
        break;

    case GI_TYPE_TAG_INT8: {
        gint32 i;
        if (!JS::ToInt32(context, value, &i))
            wrong = true;
        if (i > G_MAXINT8 || i < G_MININT8)
            out_of_range = true;
        arg->v_int8 = (gint8)i;
        break;
    }
    case GI_TYPE_TAG_UINT8: {
        guint32 i;
        if (!JS::ToUint32(context, value, &i))
            wrong = true;
        if (i > G_MAXUINT8)
            out_of_range = true;
        arg->v_uint8 = (guint8)i;
        break;
    }
    case GI_TYPE_TAG_INT16: {
        gint32 i;
        if (!JS::ToInt32(context, value, &i))
            wrong = true;
        if (i > G_MAXINT16 || i < G_MININT16)
            out_of_range = true;
        arg->v_int16 = (gint16)i;
        break;
    }

    case GI_TYPE_TAG_UINT16: {
        guint32 i;
        if (!JS::ToUint32(context, value, &i))
            wrong = true;
        if (i > G_MAXUINT16)
            out_of_range = true;
        arg->v_uint16 = (guint16)i;
        break;
    }

    case GI_TYPE_TAG_INT32:
        if (!JS::ToInt32(context, value, &arg->v_int))
            wrong = true;
        break;

    case GI_TYPE_TAG_UINT32: {
        gdouble i;
        if (!JS::ToNumber(context, value, &i))
            wrong = true;
        if (i > G_MAXUINT32 || i < 0)
            out_of_range = true;
        arg->v_uint32 = CLAMP(i, 0, G_MAXUINT32);
        break;
    }

    case GI_TYPE_TAG_INT64: {
        double v;
        if (!JS::ToNumber(context, value, &v))
            wrong = true;
        if (v > G_MAXINT64 || v < G_MININT64)
            out_of_range = true;
        arg->v_int64 = v;
    }
        break;

    case GI_TYPE_TAG_UINT64: {
        double v;
        if (!JS::ToNumber(context, value, &v))
            wrong = true;
        if (v < 0)
            out_of_range = true;
        /* XXX we fail with values close to G_MAXUINT64 */
        arg->v_uint64 = MAX(v, 0);
    }
        break;

    case GI_TYPE_TAG_BOOLEAN:
        arg->v_boolean = JS::ToBoolean(value);
        break;

    case GI_TYPE_TAG_FLOAT: {
        double v;
        if (!JS::ToNumber(context, value, &v))
            wrong = true;
        if (v > G_MAXFLOAT || v < - G_MAXFLOAT)
            out_of_range = true;
        arg->v_float = (gfloat)v;
    }
        break;

    case GI_TYPE_TAG_DOUBLE:
        if (!JS::ToNumber(context, value, &arg->v_double))
            wrong = true;
        break;

    case GI_TYPE_TAG_UNICHAR:
        if (value.isString()) {
            if (!gjs_unichar_from_string(context, value, &arg->v_uint32))
                wrong = true;
        } else {
            wrong = true;
            report_type_mismatch = true;
        }
        break;

    case GI_TYPE_TAG_GTYPE:
        if (value.isObjectOrNull()) {
            GType gtype;
            JS::RootedObject obj(context, value.toObjectOrNull());
            if (!gjs_gtype_get_actual_gtype(context, obj, &gtype)) {
                wrong = true;
                break;
            }
            if (gtype == G_TYPE_INVALID)
                wrong = true;
            arg->v_ssize = gtype;
        } else {
            wrong = true;
            report_type_mismatch = true;
        }
        break;

    case GI_TYPE_TAG_FILENAME:
        nullable_type = true;
        if (value.isNull()) {
            arg->v_pointer = NULL;
        } else if (value.isString()) {
            GjsAutoChar filename_str;
            if (gjs_string_to_filename(context, value, &filename_str))
                arg->v_pointer = filename_str.release();
            else
                wrong = true;
        } else {
            wrong = true;
            report_type_mismatch = true;
        }
        break;
    case GI_TYPE_TAG_UTF8:
        nullable_type = true;
        if (value.isNull()) {
            arg->v_pointer = NULL;
        } else if (value.isString()) {
            JS::RootedString str(context, value.toString());
            JS::UniqueChars utf8_str(JS_EncodeStringToUTF8(context, str));
            if (utf8_str)
                arg->v_pointer = g_strdup(utf8_str.get());
            else
                wrong = true;
        } else {
            wrong = true;
            report_type_mismatch = true;
        }
        break;

    case GI_TYPE_TAG_ERROR:
        nullable_type = true;
        if (value.isNull()) {
            arg->v_pointer = NULL;
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());
            if (!ErrorBase::transfer_to_gi_argument(context, obj, arg,
                                                    GI_DIRECTION_IN, transfer))
                wrong = true;
        } else {
            wrong = true;
            report_type_mismatch = true;
        }
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            bool expect_object;

            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            g_assert(interface_info);

            GIInfoType interface_type = g_base_info_get_type(interface_info);
            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS) {
                nullable_type = false;
                expect_object = false;
            } else {
                nullable_type = true;
                expect_object = true;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_foreign(interface_info)) {
                return gjs_struct_foreign_convert_to_g_argument(
                    context, value, interface_info, arg_name, arg_type,
                    transfer, may_be_null, arg);
            }

            if (!value_to_interface_gi_argument(
                    context, value, interface_info, interface_type, transfer,
                    expect_object, arg, &report_type_mismatch))
                wrong = true;
        }
        break;

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST: {
        /* nullable_type=false; while a list can be NULL in C, that
         * means empty array in JavaScript, it doesn't mean null in
         * JavaScript.
         */
        if (value.isObject()) {
            bool found_length;
            const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
            JS::RootedObject array_obj(context, &value.toObject());

            if (JS_HasPropertyById(context, array_obj, atoms.length(),
                                   &found_length) &&
                found_length) {
                guint32 length;

                if (!gjs_object_require_converted_property(
                        context, array_obj, nullptr, atoms.length(), &length)) {
                    wrong = true;
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
                        wrong = true;
                    }

                    if (type_tag == GI_TYPE_TAG_GLIST) {
                        arg->v_pointer = list;
                    } else {
                        arg->v_pointer = slist;
                    }

                    g_base_info_unref((GIBaseInfo*) param_info);
                }
                break;
            }
        }

        /* At this point we should have broken out already if the value was an
         * object and had a length property */
        wrong = true;
        report_type_mismatch = true;
        break;
    }

    case GI_TYPE_TAG_GHASH:
        if (value.isNull()) {
            arg->v_pointer = NULL;
            if (!may_be_null) {
                wrong = true;
                report_type_mismatch = true;
            }
        } else if (!value.isObject()) {
            wrong = true;
            report_type_mismatch = true;
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
                wrong = true;
            } else {
#if __GNUC__ >= 8
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
#endif
                /* The compiler isn't smart enough to figure out that ghash
                 * will always be initialized if gjs_object_to_g_hash()
                 * returns true.
                 */
                arg->v_pointer = ghash;
#if __GNUC__ >= 8
_Pragma("GCC diagnostic pop")
#endif
            }

            g_base_info_unref((GIBaseInfo*) key_param_info);
            g_base_info_unref((GIBaseInfo*) val_param_info);
        }
        break;

    case GI_TYPE_TAG_ARRAY: {
        gpointer data;
        gsize length;
        GIArrayType array_type = g_type_info_get_array_type(type_info);

        /* First, let's handle the case where we're passed an instance
         * of Uint8Array and it needs to be marshalled to GByteArray.
         */
        if (value.isObject()) {
            JS::RootedObject bytearray_obj(context, value.toObjectOrNull());
            if (JS_IsUint8Array(bytearray_obj) &&
                array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                arg->v_pointer = gjs_byte_array_get_byte_array(bytearray_obj);
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
            wrong = true;
            break;
        }

        GITypeInfo *param_info = g_type_info_get_param_type(type_info, 0);
        if (array_type == GI_ARRAY_TYPE_C) {
            arg->v_pointer = data;
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GArray *array = gjs_g_array_new_for_type(context, length, param_info);

            if (!array)
                wrong = true;
            else {
                if (data)
                    g_array_append_vals(array, data, length);
                arg->v_pointer = array;
            }

            g_free(data);
        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            GByteArray *byte_array = g_byte_array_sized_new(length);

            if (data)
                g_byte_array_append(byte_array,
                                    static_cast<const uint8_t*>(data), length);
            arg->v_pointer = byte_array;

            g_free(data);
        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GPtrArray *array = g_ptr_array_sized_new(length);

            g_ptr_array_set_size(array, length);
            if (data)
                memcpy(array->pdata, data, sizeof(void*) * length);
            arg->v_pointer = array;

            g_free(data);
        }
        g_base_info_unref((GIBaseInfo*) param_info);
        break;
    }
    default:
        g_warning("Unhandled type %s for JavaScript to GArgument conversion",
                  g_type_tag_to_string(type_tag));
        wrong = true;
        report_type_mismatch = true;
        break;
    }

    if (G_UNLIKELY(wrong)) {
        if (report_type_mismatch) {
            throw_invalid_argument(context, value, type_info, arg_name, arg_type);
        }
        return false;
    } else if (G_UNLIKELY(out_of_range)) {
        GjsAutoChar display_name =
            gjs_argument_display_name(arg_name, arg_type);
        gjs_throw(context, "value is out of range for %s (type %s)",
                  display_name.get(), g_type_tag_to_string(type_tag));
        return false;
    } else if (nullable_type &&
               arg->v_pointer == NULL &&
               !may_be_null) {
        GjsAutoChar display_name =
            gjs_argument_display_name(arg_name, arg_type);
        gjs_throw(context, "%s (type %s) may not be null", display_name.get(),
                  g_type_tag_to_string(type_tag));
        return false;
    } else {
        return true;
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
void gjs_gi_argument_init_default(GITypeInfo* type_info, GIArgument* arg) {
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
        break;

    case GI_TYPE_TAG_BOOLEAN:
        arg->v_boolean = false;
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

            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS)
                arg->v_int = 0;
            else if (interface_type == GI_INFO_TYPE_VALUE)
                /* Better to use a non-NULL value holding NULL? */
                arg->v_pointer = NULL;
            else
                arg->v_pointer = NULL;

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

bool
gjs_value_to_arg(JSContext      *context,
                 JS::HandleValue value,
                 GIArgInfo      *arg_info,
                 GIArgument     *arg)
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

bool
gjs_value_to_explicit_array (JSContext      *context,
                             JS::HandleValue value,
                             GIArgInfo      *arg_info,
                             GIArgument     *arg,
                             size_t         *length_p)
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

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_g_list (JSContext             *context,
                       JS::MutableHandleValue value_p,
                       GITypeTag              list_tag,
                       GITypeInfo            *param_info,
                       GList                 *list,
                       GSList                *slist)
{
    unsigned int i;
    GArgument arg;
    JS::AutoValueVector elems(context);

    i = 0;
    if (list_tag == GI_TYPE_TAG_GLIST) {
        for ( ; list != NULL; list = list->next) {
            arg.v_pointer = list->data;
            if (!elems.growBy(1)) {
                JS_ReportOutOfMemory(context);
                return false;
            }

            if (!gjs_value_from_g_argument(context, elems[i], param_info, &arg,
                                           true))
                return false;
            ++i;
        }
    } else {
        for ( ; slist != NULL; slist = slist->next) {
            arg.v_pointer = slist->data;
            if (!elems.growBy(1)) {
                JS_ReportOutOfMemory(context);
                return false;
            }

            if (!gjs_value_from_g_argument(context, elems[i], param_info, &arg,
                                           true))
                return false;
            ++i;
        }
    }

    JS::RootedObject obj(context, JS_NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_carray_internal (JSContext             *context,
                                JS::MutableHandleValue value_p,
                                GIArrayType            array_type,
                                GITypeInfo            *param_info,
                                guint                  length,
                                gpointer               array)
{
    GArgument arg;
    GITypeTag element_type;
    guint i;

    element_type = g_type_info_get_tag(param_info);

    if (is_gvalue_flat_array(param_info, element_type))
        return gjs_array_from_flat_gvalue_array(context, array, length, value_p);

    /* Special case array(guint8) */
    if (element_type == GI_TYPE_TAG_UINT8) {
        JSObject* obj = gjs_byte_array_from_data(context, length, array);
        if (!obj)
            return false;
        value_p.setObject(*obj);
        return true;
    }

    /* Special case array(unichar) to be a string in JS */
    if (element_type == GI_TYPE_TAG_UNICHAR)
        return gjs_string_from_ucs4(context, (gunichar *) array, length, value_p);

    // a null array pointer takes precedence over whatever `length` says
    if (!array) {
        JSObject* jsarray = JS_NewArrayObject(context, 0);
        if (!jsarray)
            return false;
        value_p.setObject(*jsarray);
        return true;
    }

    JS::AutoValueVector elems(context);
    if (!elems.resize(length)) {
        JS_ReportOutOfMemory(context);
        return false;
    }

#define ITERATE(type) \
    for (i = 0; i < length; i++) { \
        arg.v_##type = *(((g##type*)array) + i);                         \
        if (!gjs_value_from_g_argument(context, elems[i], param_info,    \
                                       &arg, true))                      \
            return false; \
    }

    switch (element_type) {
        /* Special cases handled above */
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UNICHAR:
            g_assert_not_reached();
        case GI_TYPE_TAG_BOOLEAN:
            ITERATE(boolean);
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
        case GI_TYPE_TAG_INTERFACE: {
            GIBaseInfo *interface_info = g_type_info_get_interface (param_info);
            GIInfoType info_type = g_base_info_get_type (interface_info);

            if (array_type != GI_ARRAY_TYPE_PTR_ARRAY &&
                (info_type == GI_INFO_TYPE_STRUCT ||
                 info_type == GI_INFO_TYPE_UNION) &&
                !g_type_info_is_pointer(param_info)) {
                size_t struct_size;

                if (info_type == GI_INFO_TYPE_UNION)
                    struct_size = g_union_info_get_size(interface_info);
                else
                    struct_size = g_struct_info_get_size(interface_info);

                for (i = 0; i < length; i++) {
                    arg.v_pointer = static_cast<char *>(array) + (struct_size * i);

                    if (!gjs_value_from_g_argument(context, elems[i], param_info,
                                                   &arg, true))
                        return false;
                }

                g_base_info_unref(interface_info);
                break;
            }

            g_base_info_unref(interface_info);
        }
        /* fallthrough */
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
          ITERATE(pointer);
          break;
        case GI_TYPE_TAG_VOID:
        default:
          gjs_throw(context, "Unknown Array element-type %d", element_type);
          return false;
    }

#undef ITERATE

    JS::RootedObject obj(context, JS_NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_fixed_size_array (JSContext             *context,
                                 JS::MutableHandleValue value_p,
                                 GITypeInfo            *type_info,
                                 gpointer               array)
{
    gint length;
    GITypeInfo *param_info;
    bool res;

    length = g_type_info_get_array_fixed_size(type_info);

    g_assert (length != -1);

    param_info = g_type_info_get_param_type(type_info, 0);

    res = gjs_array_from_carray_internal(context, value_p,
                                         g_type_info_get_array_type(type_info),
                                         param_info, length, array);

    g_base_info_unref((GIBaseInfo*)param_info);

    return res;
}

bool
gjs_value_from_explicit_array(JSContext             *context,
                              JS::MutableHandleValue value_p,
                              GITypeInfo            *type_info,
                              GIArgument            *arg,
                              int                    length)
{
    GITypeInfo *param_info;
    bool res;

    param_info = g_type_info_get_param_type(type_info, 0);

    res = gjs_array_from_carray_internal(context, value_p,
                                         g_type_info_get_array_type(type_info),
                                         param_info, length, arg->v_pointer);

    g_base_info_unref((GIBaseInfo*)param_info);

    return res;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_boxed_array (JSContext             *context,
                            JS::MutableHandleValue value_p,
                            GIArrayType            array_type,
                            GITypeInfo            *param_info,
                            GArgument             *arg)
{
    GArray *array;
    GPtrArray *ptr_array;
    gpointer data = NULL;
    gsize length = 0;

    if (arg->v_pointer == NULL) {
        value_p.setNull();
        return true;
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
    case GI_ARRAY_TYPE_C: /* already checked in gjs_value_from_g_argument() */
    default:
        g_assert_not_reached();
    }

    return gjs_array_from_carray_internal(context, value_p, array_type,
                                          param_info, length, data);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_array_from_zero_terminated_c_array (JSContext             *context,
                                        JS::MutableHandleValue value_p,
                                        GITypeInfo            *param_info,
                                        gpointer               c_array)
{
    GArgument arg;
    GITypeTag element_type;
    guint i;

    element_type = g_type_info_get_tag(param_info);

    /* Special case array(guint8) */
    if (element_type == GI_TYPE_TAG_UINT8) {
        size_t len = strlen(static_cast<char*>(c_array));
        JSObject* obj = gjs_byte_array_from_data(context, len, c_array);
        if (!obj)
            return false;
        value_p.setObject(*obj);
        return true;
    }

    /* Special case array(gunichar) to JS string */
    if (element_type == GI_TYPE_TAG_UNICHAR)
        return gjs_string_from_ucs4(context, (gunichar *) c_array, -1, value_p);

    JS::AutoValueVector elems(context);

#define ITERATE(type) \
    do { \
        g##type *array = (g##type *) c_array; \
        for (i = 0; array[i]; i++) { \
            arg.v_##type = array[i]; \
            if (!elems.growBy(1)) {                                     \
                JS_ReportOutOfMemory(context);                          \
                return false;                                           \
            }                                                           \
            if (!gjs_value_from_g_argument(context, elems[i],           \
                                           param_info, &arg, true))     \
                return false; \
        } \
    } while(0);

    switch (element_type) {
        /* Special cases handled above. */
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UNICHAR:
            g_assert_not_reached();
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
        /* Boolean zero-terminated array makes no sense, because FALSE is also
         * zero */
        case GI_TYPE_TAG_BOOLEAN:
            gjs_throw(context, "Boolean zero-terminated array not supported");
            return false;
        case GI_TYPE_TAG_VOID:
        default:
          gjs_throw(context, "Unknown element-type %d", element_type);
          return false;
    }

#undef ITERATE

    JS::RootedObject obj(context, JS_NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_object_from_g_hash (JSContext             *context,
                        JS::MutableHandleValue value_p,
                        GITypeInfo            *key_param_info,
                        GITypeInfo            *val_param_info,
                        GHashTable            *hash)
{
    GHashTableIter iter;
    GArgument keyarg, valarg;

    // a NULL hash table becomes a null JS value
    if (hash==NULL) {
        value_p.setNull();
        return true;
    }

    JS::RootedObject obj(context, JS_NewPlainObject(context));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    JS::RootedValue keyjs(context), valjs(context);
    JS::RootedString keystr(context);

    g_hash_table_iter_init(&iter, hash);
    while (g_hash_table_iter_next
           (&iter, &keyarg.v_pointer, &valarg.v_pointer)) {
        if (!gjs_value_from_g_argument(context, &keyjs,
                                       key_param_info, &keyarg,
                                       true))
            return false;

        keystr = JS::ToString(context, keyjs);
        if (!keystr)
            return false;

        JS::UniqueChars keyutf8(JS_EncodeStringToUTF8(context, keystr));
        if (!keyutf8)
            return false;

        if (!gjs_value_from_g_argument(context, &valjs,
                                       val_param_info, &valarg,
                                       true))
            return false;

        if (!JS_DefineProperty(context, obj, keyutf8.get(), valjs,
                               JSPROP_ENUMERATE))
            return false;
    }

    return true;
}

static const int64_t MAX_SAFE_INT64 =
    int64_t(1) << std::numeric_limits<double>::digits;

bool
gjs_value_from_g_argument (JSContext             *context,
                           JS::MutableHandleValue value_p,
                           GITypeInfo            *type_info,
                           GArgument             *arg,
                           bool                   copy_structs)
{
    GITypeTag type_tag;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GArgument %s to JS::Value",
                      g_type_tag_to_string(type_tag));

    value_p.setNull();

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        value_p.setUndefined(); /* or .setNull() ? */
        break;

    case GI_TYPE_TAG_BOOLEAN:
        value_p.setBoolean(!!arg->v_int);
        break;

    case GI_TYPE_TAG_INT32:
        value_p.setInt32(arg->v_int);
        break;

    case GI_TYPE_TAG_UINT32:
        value_p.setNumber(arg->v_uint);
        break;

    case GI_TYPE_TAG_INT64:
        if (arg->v_int64 == G_MININT64 ||
            std::abs(arg->v_int64) > MAX_SAFE_INT64)
            g_warning("Value %" G_GINT64_FORMAT " cannot be safely stored in "
                      "a JS Number and may be rounded", arg->v_int64);
        value_p.setNumber(static_cast<double>(arg->v_int64));
        break;

    case GI_TYPE_TAG_UINT64:
        if (arg->v_uint64 > MAX_SAFE_INT64)
            g_warning("Value %" G_GUINT64_FORMAT " cannot be safely stored in "
                      "a JS Number and may be rounded", arg->v_uint64);
        value_p.setNumber(static_cast<double>(arg->v_uint64));
        break;

    case GI_TYPE_TAG_UINT16:
        value_p.setInt32(arg->v_uint16);
        break;

    case GI_TYPE_TAG_INT16:
        value_p.setInt32(arg->v_int16);
        break;

    case GI_TYPE_TAG_UINT8:
        value_p.setInt32(arg->v_uint8);
        break;

    case GI_TYPE_TAG_INT8:
        value_p.setInt32(arg->v_int8);
        break;

    case GI_TYPE_TAG_FLOAT:
        value_p.setNumber(arg->v_float);
        break;

    case GI_TYPE_TAG_DOUBLE:
        value_p.setNumber(arg->v_double);
        break;

    case GI_TYPE_TAG_GTYPE:
    {
        GType gtype = arg->v_ssize;
        if (gtype == 0)
            return true;  /* value_p is set to JS null */

        JS::RootedObject obj(context, gjs_gtype_create_gtype_wrapper(context, gtype));
        if (!obj)
            return false;

        value_p.setObject(*obj);
        return true;
    }
        break;

    case GI_TYPE_TAG_UNICHAR:
        {
            char utf8[7];
            gint bytes;

            /* Preserve the bidirectional mapping between 0 and "" */
            if (arg->v_uint32 == 0) {
                value_p.set(JS_GetEmptyStringValue(context));
                return true;
            } else if (!g_unichar_validate (arg->v_uint32)) {
                gjs_throw(context,
                          "Invalid unicode codepoint %" G_GUINT32_FORMAT,
                          arg->v_uint32);
                return false;
            } else {
                bytes = g_unichar_to_utf8 (arg->v_uint32, utf8);
                return gjs_string_from_utf8_n(context, utf8, bytes, value_p);
            }
        }

    case GI_TYPE_TAG_FILENAME:
        if (arg->v_pointer)
            return gjs_string_from_filename(context, (const char *) arg->v_pointer, -1, value_p);
        else {
            /* For NULL we'll return JS::NullValue(), which is already set
             * in *value_p
             */
            return true;
        }
    case GI_TYPE_TAG_UTF8:
        if (arg->v_pointer) {
            return gjs_string_from_utf8(context, reinterpret_cast<const char *>(arg->v_pointer), value_p);
        } else {
            /* For NULL we'll return JS::NullValue(), which is already set
             * in *value_p
             */
            return true;
        }

    case GI_TYPE_TAG_ERROR:
        {
            if (arg->v_pointer) {
                JSObject* obj = ErrorInstance::object_for_c_ptr(
                    context, static_cast<GError*>(arg->v_pointer));
                if (obj) {
                    value_p.setObject(*obj);
                    return true;
                }

                return false;
            }
            return true;
        }

    case GI_TYPE_TAG_INTERFACE:
        {
            JS::RootedValue value(context);
            GIBaseInfo* interface_info;
            GIInfoType interface_type;
            GType gtype;

            interface_info = g_type_info_get_interface(type_info);
            g_assert(interface_info != NULL);

            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_UNRESOLVED) {
                gjs_throw(context,
                          "Unable to resolve arg type '%s'",
                          g_base_info_get_name(interface_info));
                goto out;
            }

            /* Enum/Flags are aren't pointer types, unlike the other interface subtypes */
            if (interface_type == GI_INFO_TYPE_ENUM) {
                int64_t value_int64 =
                    _gjs_enum_from_int(interface_info, arg->v_int);

                if (_gjs_enum_value_is_valid(context, (GIEnumInfo *)interface_info, value_int64)) {
                    value = JS::NumberValue(value_int64);
                }

                goto out;
            } else if (interface_type == GI_INFO_TYPE_FLAGS) {
                int64_t value_int64 =
                    _gjs_enum_from_int(interface_info, arg->v_int);

                gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface_info);
                if (_gjs_flags_value_is_valid(context, gtype, value_int64)) {
                    value = JS::NumberValue(value_int64);
                }

                goto out;
            } else if (interface_type == GI_INFO_TYPE_STRUCT &&
                       g_struct_info_is_foreign((GIStructInfo*)interface_info)) {
                bool ret;
                ret = gjs_struct_foreign_convert_from_g_argument(context, value_p, interface_info, arg);
                g_base_info_unref(interface_info);
                return ret;
            }

            /* Everything else is a pointer type, NULL is the easy case */
            if (arg->v_pointer == NULL) {
                value = JS::NullValue();
                goto out;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_gtype_struct((GIStructInfo*)interface_info)) {
                bool ret;

                /* XXX: here we make the implicit assumption that GTypeClass is the same
                   as GTypeInterface. This is true for the GType field, which is what we
                   use, but not for the rest of the structure!
                */
                gtype = G_TYPE_FROM_CLASS(arg->v_pointer);

                if (g_type_is_a(gtype, G_TYPE_INTERFACE))
                    ret = gjs_lookup_interface_constructor(context, gtype, value_p);
                else
                    ret = gjs_lookup_object_constructor(context, gtype, value_p);

                g_base_info_unref(interface_info);
                return ret;
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
                    value = JS::UndefinedValue(); /* Make sure error is flagged */

                goto out;
            }
            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                JSObject* obj = ErrorInstance::object_for_c_ptr(
                    context, static_cast<GError*>(arg->v_pointer));
                if (obj)
                    value = JS::ObjectValue(*obj);
                else
                    value = JS::UndefinedValue();

                goto out;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) {
                if (is_gdk_atom(interface_info)) {
                    GIFunctionInfo *atom_name_fun = g_struct_info_find_method(interface_info, "name");
                    GIArgument atom_name_ret;

                    g_function_info_invoke(atom_name_fun,
                            arg, 1,
                            nullptr, 0,
                            &atom_name_ret,
                            nullptr);

                    g_base_info_unref(atom_name_fun);
                    g_base_info_unref(interface_info);

                    if (strcmp("NONE", atom_name_ret.v_string) == 0) {
                        g_free(atom_name_ret.v_string);
                        value = JS::NullValue();

                        return true;
                    }

                    bool atom_name_ok = gjs_string_from_utf8(context, atom_name_ret.v_string, value_p);
                    g_free(atom_name_ret.v_string);

                    return atom_name_ok;
                }

                JSObject *obj;

                if (copy_structs || g_type_is_a(gtype, G_TYPE_VARIANT))
                    obj = BoxedInstance::new_for_c_struct(
                        context, interface_info, arg->v_pointer);
                else
                    obj = BoxedInstance::new_for_c_struct(
                        context, interface_info, arg->v_pointer,
                        BoxedInstance::NoCopy());

                if (obj)
                    value = JS::ObjectValue(*obj);

                goto out;
            } else if (interface_type == GI_INFO_TYPE_UNION) {
                JSObject *obj;
                obj = gjs_union_from_c_union(context, (GIUnionInfo *)interface_info, arg->v_pointer);
                if (obj)
                        value = JS::ObjectValue(*obj);

                goto out;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                // arg->v_pointer == nullptr is already handled above
                JSObject* obj = ObjectInstance::wrapper_from_gobject(
                    context, G_OBJECT(arg->v_pointer));
                if (obj)
                    value = JS::ObjectValue(*obj);
            } else if (g_type_is_a(gtype, G_TYPE_BOXED) ||
                       g_type_is_a(gtype, G_TYPE_ENUM) ||
                       g_type_is_a(gtype, G_TYPE_FLAGS)) {
                /* Should have been handled above */
                gjs_throw(context,
                          "Type %s registered for unexpected interface_type %d",
                          g_type_name(gtype),
                          interface_type);
                return false;
            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                JSObject *obj;
                obj = gjs_param_from_g_param(context, G_PARAM_SPEC(arg->v_pointer));
                if (obj)
                    value = JS::ObjectValue(*obj);
            } else if (gtype == G_TYPE_NONE) {
                gjs_throw(context, "Unexpected unregistered type packing GArgument into JS::Value");
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype) || G_TYPE_IS_INTERFACE(gtype)) {
                JSObject* obj = FundamentalInstance::object_for_c_ptr(
                    context, arg->v_pointer);
                if (obj)
                    value = JS::ObjectValue(*obj);
            } else {
                gjs_throw(context, "Unhandled GType %s packing GArgument into JS::Value",
                          g_type_name(gtype));
            }

         out:
            g_base_info_unref( (GIBaseInfo*) interface_info);

            if (value.isUndefined())
                return false;

            value_p.set(value);
        }
        break;

    case GI_TYPE_TAG_ARRAY:
        if (arg->v_pointer == NULL) {
            /* OK, but no conversion to do */
        } else if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {

            if (g_type_info_is_zero_terminated(type_info)) {
                GITypeInfo *param_info;
                bool result;

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
                g_assert(((void) "Use gjs_value_from_explicit_array() for "
                          "arrays with length param",
                          g_type_info_get_array_length(type_info) == -1));
                return gjs_array_from_fixed_size_array(context, value_p, type_info, arg->v_pointer);
            }
        } else if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_BYTE_ARRAY) {
            auto* byte_array = static_cast<GByteArray*>(arg->v_pointer);
            JSObject* array =
                gjs_byte_array_from_byte_array(context, byte_array);
            if (!array) {
                gjs_throw(context,
                          "Couldn't convert GByteArray to a Uint8Array");
                return false;
            }
            value_p.setObject(*array);
        } else {
            /* this assumes the array type is one of GArray, GPtrArray or
             * GByteArray */
            GITypeInfo *param_info;
            bool result;

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
            bool result;

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
            bool result;

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
        return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_g_arg_release_internal(JSContext  *context,
                                         GITransfer  transfer,
                                         GITypeInfo *type_info,
                                         GITypeTag   type_tag,
                                         GArgument  *arg);

typedef struct {
    JSContext *context;
    GITypeInfo *key_param_info, *val_param_info;
    GITransfer transfer;
    bool failed;
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
        c->failed = true;

    GITypeTag val_type = g_type_info_get_tag(c->val_param_info);
    if (val_type == GI_TYPE_TAG_INT64 ||
        val_type == GI_TYPE_TAG_UINT64 ||
        val_type == GI_TYPE_TAG_FLOAT ||
        val_type == GI_TYPE_TAG_DOUBLE) {
        g_free(val_arg.v_pointer);
    } else if (!gjs_g_arg_release_internal(c->context, c->transfer,
                                           c->val_param_info, val_type,
                                           &val_arg)) {
        c->failed = true;
    }
    return true;
}

/* We need to handle GI_TRANSFER_NOTHING differently for out parameters
 * (free nothing) and for in parameters (free any temporaries we've
 * allocated
 */
#define TRANSFER_IN_NOTHING (GI_TRANSFER_EVERYTHING + 1)

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_g_arg_release_internal(JSContext  *context,
                           GITransfer  transfer,
                           GITypeInfo *type_info,
                           GITypeTag   type_tag,
                           GArgument  *arg)
{
    bool failed;

    g_assert(transfer != GI_TRANSFER_NOTHING);

    failed = false;

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
                    failed = true;
                }
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                if (transfer != TRANSFER_IN_NOTHING) {
                    auto* priv =
                        FundamentalPrototype::for_gtype(context, gtype);
                    priv->call_unref_function(arg->v_pointer);
                }
            } else {
                gjs_throw(context, "Unhandled GType %s releasing GArgument",
                          g_type_name(gtype));
                failed = true;
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
                    failed = true;
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
                        return false;
                    }

                    for (i = 0; i < len; i++) {
                        GValue *v = ((GValue*)arg->v_pointer) + i;
                        g_value_unset(v);
                    }
                }

                g_free(arg->v_pointer);
                g_base_info_unref(param_info);
                return true;
            }

            switch (element_type) {
            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
                if (transfer == GI_TRANSFER_CONTAINER)
                    g_free(arg->v_pointer);
                else
                    g_strfreev ((gchar **) arg->v_pointer);
                break;

            case GI_TYPE_TAG_BOOLEAN:
            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_UINT64:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_INT64:
            case GI_TYPE_TAG_FLOAT:
            case GI_TYPE_TAG_DOUBLE:
            case GI_TYPE_TAG_UNICHAR:
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
                                failed = true;
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
                                failed = true;
                            }
                        }
                    }
                }
                g_free (arg->v_pointer);
                break;

            case GI_TYPE_TAG_VOID:
            default:
                gjs_throw(context,
                          "Releasing a C array with explicit length, that was nested"
                          "inside another container. This is not supported (and will leak)");
                failed = true;
            }

            g_base_info_unref((GIBaseInfo*) param_info);
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeInfo *param_info;
            GITypeTag element_type;

            param_info = g_type_info_get_param_type(type_info, 0);
            element_type = g_type_info_get_tag(param_info);

            switch (element_type) {
            case GI_TYPE_TAG_BOOLEAN:
            case GI_TYPE_TAG_UNICHAR:
            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_UINT64:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_INT64:
            case GI_TYPE_TAG_FLOAT:
            case GI_TYPE_TAG_DOUBLE:
            case GI_TYPE_TAG_GTYPE:
                g_array_free((GArray*) arg->v_pointer, true);
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
                    g_array_free((GArray*) arg->v_pointer, true);
                } else if (type_needs_out_release (param_info, element_type)) {
                    GArray *array = (GArray *) arg->v_pointer;
                    guint i;

                    for (i = 0; i < array->len; i++) {
                        GArgument arg_iter;

                        arg_iter.v_pointer = g_array_index (array, gpointer, i);
                        failed = !gjs_g_arg_release_internal(
                            context, transfer, param_info, element_type,
                            &arg_iter);
                    }

                    g_array_free (array, true);
                }

                break;

            case GI_TYPE_TAG_VOID:
            default:
                gjs_throw(context,
                          "Don't know how to release GArray element-type %d",
                          element_type);
                failed = true;
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
                    GArgument arg_iter;

                    arg_iter.v_pointer = g_ptr_array_index (array, i);
                    failed = !gjs_g_argument_release(context, transfer,
                                                     param_info, &arg_iter);
                }
            }

            g_ptr_array_free(array, true);

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
                    failed = true;
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
                    false
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
        return false;
    }

    return !failed;
}

bool
gjs_g_argument_release(JSContext  *context,
                       GITransfer  transfer,
                       GITypeInfo *type_info,
                       GArgument  *arg)
{
    GITypeTag type_tag;

    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument %s out param or return value",
                      g_type_tag_to_string(type_tag));

    return gjs_g_arg_release_internal(context, transfer, type_info, type_tag, arg);
}

bool
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
        return true;

    type_tag = g_type_info_get_tag( (GITypeInfo*) type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GArgument %s in param",
                      g_type_tag_to_string(type_tag));

    if (type_needs_release (type_info, type_tag))
        return gjs_g_arg_release_internal(context, (GITransfer) TRANSFER_IN_NOTHING,
                                          type_info, type_tag, arg);

    return true;
}

bool gjs_g_argument_release_in_array(JSContext* context, GITransfer transfer,
                                     GITypeInfo* type_info, unsigned length,
                                     GIArgument* arg) {
    GITypeInfo *param_type;
    gpointer *array;
    GArgument elem;
    guint i;
    bool ret = true;
    GITypeTag type_tag;

    if (transfer != GI_TRANSFER_NOTHING)
        return true;

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
                ret = false;
                break;
            }
        }
    }

    g_base_info_unref(param_type);
    g_free(array);

    return ret;
}

bool gjs_g_argument_release_out_array(JSContext* context, GITransfer transfer,
                                      GITypeInfo* type_info, unsigned length,
                                      GIArgument* arg) {
    GITypeInfo *param_type;
    gpointer *array;
    GArgument elem;
    guint i;
    bool ret = true;
    GITypeTag type_tag;

    if (transfer == GI_TRANSFER_NOTHING)
        return true;

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
                ret = false;
            }
        }
    }

    g_base_info_unref(param_type);
    g_free(array);

    return ret;
}
