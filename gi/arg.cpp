/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2020 Canonical, Ltd.

#include <config.h>

#include <stdint.h>
#include <string.h>  // for strcmp, strlen, memcpy

#include <string>
#include <type_traits>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CharacterEncoding.h>
#include <js/Conversions.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/Exception.h>
#include <js/GCVector.h>            // for RootedVector, MutableWrappedPtrOp...
#include <js/Id.h>
#include <js/PropertyAndElement.h>  // for JS_GetElement, JS_HasPropertyById
#include <js/PropertyDescriptor.h>  // for JSPROP_ENUMERATE
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/experimental/TypedData.h>
#include <jsapi.h>  // for InformalValueTypeName, IdVector

#include "gi/arg-inl.h"
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/foreign.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/interface.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/enum-utils.h"
#include "gjs/macros.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"
#include "util/misc.h"

GJS_JSAPI_RETURN_CONVENTION static bool gjs_g_arg_release_internal(
    JSContext*, GITransfer, GITypeInfo*, GITypeTag, GjsArgumentType,
    GjsArgumentFlags, GIArgument*);
static void throw_invalid_argument(JSContext* cx, JS::HandleValue value,
                                   GITypeInfo* arginfo, const char* arg_name,
                                   GjsArgumentType arg_type);

bool _gjs_flags_value_is_valid(JSContext* context, GType gtype, int64_t value) {
    /* Do proper value check for flags with GType's */
    if (gtype != G_TYPE_NONE) {
        GjsAutoTypeClass<GFlagsClass> gflags_class(gtype);
        uint32_t tmpval = static_cast<uint32_t>(value);

        /* check all bits are valid bits for the flag and is a 32 bit flag*/
        if ((tmpval &= gflags_class->mask) != value) { /* Not a guint32 with invalid mask values*/
            gjs_throw(context,
                    "0x%" G_GINT64_MODIFIER "x is not a valid value for flags %s",
                    value, g_type_name(gtype));
            return false;
        }
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
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t enum_value = g_value_info_get_value(value_info);

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

[[nodiscard]] static bool _gjs_enum_uses_signed_type(GIEnumInfo* enum_info) {
    switch (g_enum_info_get_storage_type(enum_info)) {
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_INT64:
            return true;
        default:
            return false;
    }
}

// This is hacky - g_function_info_invoke() and g_field_info_get/set_field()
// expect the enum value in gjs_arg_member<int>(arg) and depend on all flags and
// enumerations being passed on the stack in a 32-bit field. See FIXME comment
// in g_field_info_get_field(). The same assumption of enums cast to 32-bit
// signed integers is found in g_value_set_enum()/g_value_set_flags().
[[nodiscard]] int64_t _gjs_enum_from_int(GIEnumInfo* enum_info, int int_value) {
    if (_gjs_enum_uses_signed_type (enum_info))
        return int64_t(int_value);
    else
        return int64_t(uint32_t(int_value));
}

/* Here for symmetry, but result is the same for the two cases */
[[nodiscard]] static int _gjs_enum_to_int(int64_t value) {
    return static_cast<int>(value);
}

/* Check if an argument of the given needs to be released if we created it
 * from a JS value to pass it into a function and aren't transferring ownership.
 */
[[nodiscard]] static bool type_needs_release(GITypeInfo* type_info,
                                             GITypeTag type_tag) {
    switch (type_tag) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_UTF8:
            return true;

        case GI_TYPE_TAG_INTERFACE: {
            GType gtype;

            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            g_assert(interface_info != nullptr);

            switch (interface_info.type()) {
                case GI_INFO_TYPE_STRUCT:
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                case GI_INFO_TYPE_OBJECT:
                case GI_INFO_TYPE_INTERFACE:
                case GI_INFO_TYPE_UNION:
                case GI_INFO_TYPE_BOXED:
                    // These are subtypes of GIRegisteredTypeInfo for which the
                    // cast is safe
                    gtype = g_registered_type_info_get_g_type(interface_info);
                    break;
                default:
                    gtype = G_TYPE_NONE;
            }

            if (g_type_is_a(gtype, G_TYPE_CLOSURE))
                return true;
            else if (g_type_is_a(gtype, G_TYPE_VALUE))
                return true;
            else
                return false;
        }

        default:
            return false;
    }
}

[[nodiscard]] static inline bool is_string_type(GITypeTag tag) {
    return tag == GI_TYPE_TAG_FILENAME || tag == GI_TYPE_TAG_UTF8;
}

/* Check if an argument of the given needs to be released if we obtained it
 * from out argument (or the return value), and we're transferring ownership
 */
[[nodiscard]] static bool type_needs_out_release(GITypeInfo* type_info,
                                                 GITypeTag type_tag) {
    switch (type_tag) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_UTF8:
            return true;

        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);

            switch (interface_info.type()) {
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                    return false;

                case GI_INFO_TYPE_STRUCT:
                case GI_INFO_TYPE_UNION:
                    return g_type_info_is_pointer(type_info);

                case GI_INFO_TYPE_OBJECT:
                    return true;

                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_to_g_list(
    JSContext* cx, const JS::HandleValue& value, GITypeInfo* type_info,
    GITransfer transfer, const char* arg_name, GjsArgumentType arg_type,
    T** list_p) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);

    // While a list can be NULL in C, that means empty array in JavaScript, it
    // doesn't mean null in JavaScript.
    bool is_array;
    if (!JS::IsArrayObject(cx, value, &is_array))
        return false;
    if (!is_array) {
        throw_invalid_argument(cx, value, type_info, arg_name, arg_type);
        return false;
    }

    JS::RootedObject array_obj(cx, &value.toObject());

    uint32_t length;
    if (!JS::GetArrayLength(cx, array_obj, &length)) {
        throw_invalid_argument(cx, value, type_info, arg_name, arg_type);
        return false;
    }

    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);
    g_assert(param_info);

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release (param_info, g_type_info_get_tag(param_info))) {
            /* FIXME: to make this work, we'd have to keep a list of temporary
             * GIArguments for the function call so we could free them after
             * the surrounding container had been freed by the callee.
             */
            gjs_throw(cx, "Container transfer for in parameters not supported");
            return false;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    JS::RootedObject array(cx, value.toObjectOrNull());
    JS::RootedValue elem(cx);
    T* list = nullptr;

    for (size_t i = 0; i < length; ++i) {
        elem = JS::UndefinedValue();
        if (!JS_GetElement(cx, array, i, &elem)) {
            gjs_throw(cx, "Missing array element %zu", i);
            return false;
        }

        /* FIXME we don't know if the list elements can be NULL.
         * gobject-introspection needs to tell us this.
         * Always say they can't for now.
         */
        GIArgument elem_arg;
        if (!gjs_value_to_gi_argument(cx, elem, param_info,
                                      GJS_ARGUMENT_LIST_ELEMENT, transfer,
                                      &elem_arg)) {
            return false;
        }

        void* hash_pointer =
            g_type_info_hash_pointer_from_argument(param_info, &elem_arg);

        if constexpr (std::is_same_v<T, GList>)
            list = g_list_prepend(list, hash_pointer);
        else if constexpr (std::is_same_v<T, GSList>)
            list = g_slist_prepend(list, hash_pointer);
    }

    if constexpr (std::is_same_v<T, GList>)
        list = g_list_reverse(list);
    else if constexpr (std::is_same_v<T, GSList>)
        list = g_slist_reverse(list);

    *list_p = list;

    return true;
}

[[nodiscard]] static GHashTable* create_hash_table_for_key_type(
    GITypeTag key_type) {
    /* Don't use key/value destructor functions here, because we can't
     * construct correct ones in general if the value type is complex.
     * Rely on the type-aware gi_argument_release functions. */
    if (is_string_type(key_type))
        return g_hash_table_new(g_str_hash, g_str_equal);
    return g_hash_table_new(NULL, NULL);
}

template <typename IntType>
GJS_JSAPI_RETURN_CONVENTION static bool hashtable_int_key(
    JSContext* cx, const JS::HandleValue& value, void** pointer_out) {
    static_assert(std::is_integral_v<IntType>, "Need an integer");
    bool out_of_range = false;

    Gjs::JsValueHolder::Strict<IntType> i;
    if (!Gjs::js_value_to_c_checked<IntType>(cx, value, &i, &out_of_range))
        return false;

    if (out_of_range) {
        gjs_throw(cx, "value is out of range for hash table key of type %s",
                  Gjs::static_type_name<IntType>());
    }

    *pointer_out = gjs_int_to_pointer<IntType>(i);

    return true;
}

/* Converts a JS::Value to a GHashTable key, stuffing it into @pointer_out if
 * possible, otherwise giving the location of an allocated key in @pointer_out.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool value_to_ghashtable_key(JSContext* cx, JS::HandleValue value,
                                    GITypeTag type_tag, void** pointer_out) {
    bool unsupported = false;

    g_assert((value.isString() || value.isInt32()) &&
             "keys from JS_Enumerate must be non-symbol property keys");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting JS::Value to GHashTable key %s",
                      g_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_BOOLEAN:
        /* This doesn't seem particularly useful, but it's easy */
        *pointer_out = gjs_int_to_pointer(JS::ToBoolean(value));
        break;

    case GI_TYPE_TAG_UNICHAR:
        if (value.isInt32()) {
            *pointer_out = gjs_int_to_pointer(value.toInt32());
        } else {
            uint32_t ch;
            if (!gjs_unichar_from_string(cx, value, &ch))
                return false;
            *pointer_out = gjs_int_to_pointer(ch);
        }
        break;

    case GI_TYPE_TAG_INT8:
        if (!hashtable_int_key<int8_t>(cx, value, pointer_out))
            return false;
        break;

    case GI_TYPE_TAG_INT16:
        if (!hashtable_int_key<int16_t>(cx, value, pointer_out))
            return false;
        break;

    case GI_TYPE_TAG_INT32:
        if (!hashtable_int_key<int32_t>(cx, value, pointer_out))
            return false;
        break;

    case GI_TYPE_TAG_UINT8:
        if (!hashtable_int_key<uint8_t>(cx, value, pointer_out))
            return false;
        break;

    case GI_TYPE_TAG_UINT16:
        if (!hashtable_int_key<uint16_t>(cx, value, pointer_out))
            return false;
        break;

    case GI_TYPE_TAG_UINT32:
        if (!hashtable_int_key<uint32_t>(cx, value, pointer_out))
            return false;
        break;

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

    return true;
}

template <typename T>
[[nodiscard]] static T* heap_value_new_from_arg(GIArgument* val_arg) {
    T* heap_val = g_new(T, 1);
    *heap_val = gjs_arg_get<T>(val_arg);

    return heap_val;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_object_to_g_hash(JSContext* context, JS::HandleObject props,
                                 GITypeInfo* type_info, GITransfer transfer,
                                 GHashTable** hash_p) {
    size_t id_ix, id_len;

    g_assert(props && "Property bag cannot be null");

    GjsAutoTypeInfo key_param_info = g_type_info_get_param_type(type_info, 0);
    GjsAutoTypeInfo val_param_info = g_type_info_get_param_type(type_info, 1);

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release (key_param_info, g_type_info_get_tag(key_param_info)) ||
            type_needs_release (val_param_info, g_type_info_get_tag(val_param_info))) {
            /* FIXME: to make this work, we'd have to keep a list of temporary
             * GIArguments for the function call so we could free them after
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

    GITypeTag key_tag = g_type_info_get_tag(key_param_info);
    GjsAutoPointer<GHashTable, GHashTable, g_hash_table_destroy> result =
        create_hash_table_for_key_type(key_tag);

    JS::RootedValue key_js(context), val_js(context);
    JS::RootedId cur_id(context);
    for (id_ix = 0, id_len = ids.length(); id_ix < id_len; ++id_ix) {
        cur_id = ids[id_ix];
        gpointer key_ptr, val_ptr;
        GIArgument val_arg = { 0 };

        if (!JS_IdToValue(context, cur_id, &key_js) ||
            // Type check key type.
            !value_to_ghashtable_key(context, key_js, key_tag, &key_ptr) ||
            !JS_GetPropertyById(context, props, cur_id, &val_js) ||
            // Type check and convert value to a C type
            !gjs_value_to_gi_argument(context, val_js, val_param_info, nullptr,
                                      GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                      GjsArgumentFlags::MAY_BE_NULL, &val_arg))
            return false;

        GITypeTag val_type = g_type_info_get_tag(val_param_info);
        /* Use heap-allocated values for types that don't fit in a pointer */
        if (val_type == GI_TYPE_TAG_INT64) {
            val_ptr = heap_value_new_from_arg<int64_t>(&val_arg);
        } else if (val_type == GI_TYPE_TAG_UINT64) {
            val_ptr = heap_value_new_from_arg<uint64_t>(&val_arg);
        } else if (val_type == GI_TYPE_TAG_FLOAT) {
            val_ptr = heap_value_new_from_arg<float>(&val_arg);
        } else if (val_type == GI_TYPE_TAG_DOUBLE) {
            val_ptr = heap_value_new_from_arg<double>(&val_arg);
        } else {
            // Other types are simply stuffed inside the pointer
            val_ptr = g_type_info_hash_pointer_from_argument(val_param_info,
                                                             &val_arg);
        }

#if __GNUC__ >= 8  // clang-format off
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
#endif
        // The compiler isn't smart enough to figure out that key_ptr will
        // always be initialized if value_to_ghashtable_key() returns true.
        g_hash_table_insert(result, key_ptr, val_ptr);
#if __GNUC__ >= 8
_Pragma("GCC diagnostic pop")
#endif  // clang-format on
    }

    *hash_p = result.release();
    return true;
}

template <typename T>
[[nodiscard]] constexpr T* array_allocate(size_t length) {
    if constexpr (std::is_same_v<T, char*>)
        return g_new0(char*, length);

    T* array = g_new(T, length);
    array[length - 1] = {0};
    return array;
}

template <GITypeTag TAG, typename T>
GJS_JSAPI_RETURN_CONVENTION static bool js_value_to_c_strict(
    JSContext* cx, const JS::HandleValue& value, T* out) {
    using ValueHolderT = Gjs::JsValueHolder::Strict<T, TAG>;
    if constexpr (Gjs::type_has_js_getter<T, ValueHolderT>())
        return Gjs::js_value_to_c<TAG>(cx, value, out);

    ValueHolderT v;
    bool ret = Gjs::js_value_to_c<TAG>(cx, value, &v);
    *out = v;

    return ret;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_to_auto_array(
    JSContext* cx, JS::Value array_value, size_t length, void** arr_p) {
    JS::RootedObject array(cx, array_value.toObjectOrNull());
    JS::RootedValue elem(cx);

    // Add one so we're always zero terminated
    GjsSmartPointer<T> result = array_allocate<T>(length + 1);

    for (size_t i = 0; i < length; ++i) {
        elem = JS::UndefinedValue();

        if (!JS_GetElement(cx, array, i, &elem)) {
            gjs_throw(cx, "Missing array element %" G_GSIZE_FORMAT, i);
            return false;
        }

        if (!js_value_to_c_strict<TAG>(cx, elem, &result[i])) {
            gjs_throw(cx, "Invalid element in %s array",
                      Gjs::static_type_name<T, TAG>());
            return false;
        }
    }

    *arr_p = result.release();

    return true;
}

bool
gjs_array_from_strv(JSContext             *context,
                    JS::MutableHandleValue value_p,
                    const char           **strv)
{
    guint i;
    JS::RootedValueVector elems(context);

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

    JS::RootedObject obj(context, JS::NewArrayObject(context, elems));
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
    return gjs_array_to_auto_array<char*>(context, array_value, length, arr_p);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_string_to_intarray(JSContext* context, JS::HandleString str,
                                   GITypeTag element_type, void** arr_p,
                                   size_t* length) {
    char16_t *result16;

    switch (element_type) {
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8: {
            JS::UniqueChars result;
            if (!gjs_string_to_utf8_n(context, str, &result, length))
                return false;

            *arr_p = _gjs_memdup2(result.get(), *length);
            return true;
        }

        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16: {
            if (!gjs_string_get_char16_data(context, str, &result16, length))
                return false;
            *arr_p = result16;
            return true;
        }

        case GI_TYPE_TAG_UNICHAR: {
            gunichar* result_ucs4;
            if (!gjs_string_to_ucs4(context, str, &result_ucs4, length))
                return false;
            *arr_p = result_ucs4;
            return true;
        }

        default:
            /* can't convert a string to this type */
            gjs_throw(context, "Cannot convert string to array of '%s'",
                      g_type_tag_to_string(element_type));
            return false;
    }
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
    GjsAutoPointer<void*> array = array_allocate<void*>(length + 1);

    for (i = 0; i < length; i++) {
        GIArgument arg;
        gjs_arg_unset<void*>(&arg);

        elem = JS::UndefinedValue();
        if (!JS_GetElement(context, array_obj, i, &elem)) {
            gjs_throw(context,
                      "Missing array element %u",
                      i);
            return false;
        }

        if (!gjs_value_to_gi_argument(context, elem, param_info,
                                      GJS_ARGUMENT_ARRAY_ELEMENT, transfer,
                                      &arg)) {
            gjs_throw(context,
                      "Invalid element in array");
            return false;
        }

        array[i] = gjs_arg_get<void*>(&arg);
    }

    *arr_p = array.release();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_flat_array(JSContext* cx, JS::HandleValue array_value,
                                    unsigned length, GITypeInfo* param_info,
                                    size_t param_size, void** arr_p) {
    g_assert((param_size > 0) &&
             "Only flat arrays of elements of known size are supported");

    GjsAutoPointer<uint8_t> flat_array = g_new0(uint8_t, param_size * length);

    JS::RootedObject array(cx, &array_value.toObject());
    JS::RootedValue elem(cx);
    for (unsigned i = 0; i < length; i++) {
        elem = JS::UndefinedValue();

        if (!JS_GetElement(cx, array, i, &elem)) {
            gjs_throw(cx, "Missing array element %u", i);
            return false;
        }

        GIArgument arg;
        if (!gjs_value_to_gi_argument(cx, elem, param_info,
                                      GJS_ARGUMENT_ARRAY_ELEMENT,
                                      GI_TRANSFER_NOTHING, &arg))
            return false;

        memcpy(&flat_array[param_size * i], gjs_arg_get<void*>(&arg),
               param_size);
    }

    *arr_p = flat_array.release();
    return true;
}

[[nodiscard]] static bool is_gvalue(GIBaseInfo* info) {
    switch (g_base_info_get_type(info)) {
        case GI_INFO_TYPE_STRUCT:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_BOXED: {
            GType gtype = g_registered_type_info_get_g_type(info);
            return g_type_is_a(gtype, G_TYPE_VALUE);
        }

        default:
            return false;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_array(JSContext* context, JS::HandleValue array_value,
                               size_t length, GITransfer transfer,
                               GITypeInfo* param_info, void** arr_p) {
    GITypeTag element_type = g_type_info_get_storage_type(param_info);

    switch (element_type) {
    case GI_TYPE_TAG_UTF8:
        return gjs_array_to_strv (context, array_value, length, arr_p);
    case GI_TYPE_TAG_BOOLEAN:
        return gjs_array_to_auto_array<gboolean, GI_TYPE_TAG_BOOLEAN>(
            context, array_value, length, arr_p);
    case GI_TYPE_TAG_UNICHAR:
        return gjs_array_to_auto_array<char32_t>(context, array_value, length,
                                                 arr_p);
    case GI_TYPE_TAG_UINT8:
        return gjs_array_to_auto_array<uint8_t>(context, array_value, length,
                                                arr_p);
    case GI_TYPE_TAG_INT8:
        return gjs_array_to_auto_array<int8_t>(context, array_value, length,
                                               arr_p);
    case GI_TYPE_TAG_UINT16:
        return gjs_array_to_auto_array<uint16_t>(context, array_value, length,
                                                 arr_p);
    case GI_TYPE_TAG_INT16:
        return gjs_array_to_auto_array<int16_t>(context, array_value, length,
                                                arr_p);
    case GI_TYPE_TAG_UINT32:
        return gjs_array_to_auto_array<uint32_t>(context, array_value, length,
                                                 arr_p);
    case GI_TYPE_TAG_INT32:
        return gjs_array_to_auto_array<int32_t>(context, array_value, length,
                                                arr_p);
    case GI_TYPE_TAG_INT64:
        return gjs_array_to_auto_array<int64_t>(context, array_value, length,
                                                arr_p);
    case GI_TYPE_TAG_UINT64:
        return gjs_array_to_auto_array<uint64_t>(context, array_value, length,
                                                 arr_p);
    case GI_TYPE_TAG_FLOAT:
        return gjs_array_to_auto_array<float>(context, array_value, length,
                                              arr_p);
    case GI_TYPE_TAG_DOUBLE:
        return gjs_array_to_auto_array<double>(context, array_value, length,
                                               arr_p);
    case GI_TYPE_TAG_GTYPE:
        return gjs_array_to_auto_array<GType, GI_TYPE_TAG_GTYPE>(
            context, array_value, length, arr_p);

    /* Everything else is a pointer type */
    case GI_TYPE_TAG_INTERFACE:
        if (!g_type_info_is_pointer(param_info)) {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(param_info);
            if (is_gvalue(interface_info)) {
                // Special case for GValue "flat arrays", this could also
                // using the generic case, but if we do so we're leaking atm.
                return gjs_array_to_auto_array<GValue>(context, array_value,
                                                       length, arr_p);
            }

            size_t element_size = gjs_type_get_element_size(
                g_type_info_get_tag(param_info), param_info);

            if (element_size) {
                return gjs_array_to_flat_array(context, array_value, length,
                                               param_info, element_size, arr_p);
            }
        }
        [[fallthrough]];
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

size_t gjs_type_get_element_size(GITypeTag element_type,
                                 GITypeInfo* type_info) {
    if (g_type_info_is_pointer(type_info) &&
        element_type != GI_TYPE_TAG_ARRAY)
        return sizeof(void*);

    switch (element_type) {
    case GI_TYPE_TAG_BOOLEAN:
        return sizeof(gboolean);
    case GI_TYPE_TAG_INT8:
        return sizeof(int8_t);
    case GI_TYPE_TAG_UINT8:
        return sizeof(uint8_t);
    case GI_TYPE_TAG_INT16:
        return sizeof(int16_t);
    case GI_TYPE_TAG_UINT16:
        return sizeof(uint16_t);
    case GI_TYPE_TAG_INT32:
        return sizeof(int32_t);
    case GI_TYPE_TAG_UINT32:
        return sizeof(uint32_t);
    case GI_TYPE_TAG_INT64:
        return sizeof(int64_t);
    case GI_TYPE_TAG_UINT64:
        return sizeof(uint64_t);
    case GI_TYPE_TAG_FLOAT:
        return sizeof(float);
    case GI_TYPE_TAG_DOUBLE:
        return sizeof(double);
    case GI_TYPE_TAG_GTYPE:
        return sizeof(GType);
    case GI_TYPE_TAG_UNICHAR:
        return sizeof(char32_t);
    case GI_TYPE_TAG_GLIST:
        return sizeof(GSList);
    case GI_TYPE_TAG_GSLIST:
        return sizeof(GList);
    case GI_TYPE_TAG_ERROR:
        return sizeof(GError);
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
        return sizeof(char*);
    case GI_TYPE_TAG_INTERFACE: {
        GjsAutoBaseInfo interface_info = g_type_info_get_interface(type_info);

        switch (interface_info.type()) {
            case GI_INFO_TYPE_ENUM:
            case GI_INFO_TYPE_FLAGS:
                return sizeof(unsigned int);

            case GI_INFO_TYPE_STRUCT:
                return g_struct_info_get_size(interface_info);
            case GI_INFO_TYPE_UNION:
                return g_union_info_get_size(interface_info);
            default:
                return 0;
        }
    }

    case GI_TYPE_TAG_GHASH:
        return sizeof(void*);

    case GI_TYPE_TAG_ARRAY:
        if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            int length = g_type_info_get_array_length(type_info);
            if (length < 0)
                return sizeof(void*);

            GjsAutoTypeInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            GITypeTag param_tag = g_type_info_get_tag(param_info);
            return gjs_type_get_element_size(param_tag, param_info);
        }

        return sizeof(void*);

    case GI_TYPE_TAG_VOID:
        break;
    }

    g_return_val_if_reached(0);
}

enum class ArrayReleaseType {
    EXPLICIT_LENGTH,
    ZERO_TERMINATED,
};

template <ArrayReleaseType release_type = ArrayReleaseType::EXPLICIT_LENGTH>
static inline bool gjs_gi_argument_release_array_internal(
    JSContext* cx, GITransfer element_transfer, GjsArgumentFlags flags,
    GITypeInfo* param_type, unsigned length, GIArgument* arg) {
    GjsAutoPointer<uint8_t, void, g_free> arg_array =
        gjs_arg_steal<uint8_t*>(arg);

    if (!arg_array)
        return true;

    if (element_transfer != GI_TRANSFER_EVERYTHING)
        return true;

    if constexpr (release_type == ArrayReleaseType::EXPLICIT_LENGTH) {
        if (length == 0)
            return true;
    }

    GITypeTag type_tag = g_type_info_get_tag(param_type);

    if (flags & GjsArgumentFlags::ARG_IN &&
        !type_needs_release(param_type, type_tag))
        return true;

    if (flags & GjsArgumentFlags::ARG_OUT &&
        !type_needs_out_release(param_type, type_tag))
        return true;

    size_t element_size = gjs_type_get_element_size(type_tag, param_type);
    if G_UNLIKELY (element_size == 0)
        return true;

    bool is_pointer = g_type_info_is_pointer(param_type);
    for (size_t i = 0;; i++) {
        GIArgument elem;
        auto* element_start = &arg_array[i * element_size];
        auto* pointer =
            is_pointer ? *reinterpret_cast<uint8_t**>(element_start) : nullptr;

        if constexpr (release_type == ArrayReleaseType::ZERO_TERMINATED) {
            if (is_pointer) {
                if (!pointer)
                    break;
            } else if (*element_start == 0 &&
                       memcmp(element_start, element_start + 1,
                              element_size - 1) == 0) {
                break;
            }
        }

        gjs_arg_set(&elem, is_pointer ? pointer : element_start);
        JS::AutoSaveExceptionState saved_exc(cx);
        if (!gjs_g_arg_release_internal(cx, element_transfer, param_type,
                                        type_tag, GJS_ARGUMENT_ARRAY_ELEMENT,
                                        flags, &elem)) {
            return false;
        }

        if constexpr (release_type == ArrayReleaseType::EXPLICIT_LENGTH) {
            if (i == length - 1)
                break;
        }
    }

    return true;
}

static GArray* garray_new_for_storage_type(unsigned length,
                                           GITypeTag storage_type,
                                           GITypeInfo* type_info) {
    size_t element_size = gjs_type_get_element_size(storage_type, type_info);
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

[[nodiscard]] static const char* type_tag_to_human_string(
    GITypeInfo* type_info) {
    GITypeTag tag;

    tag = g_type_info_get_tag(type_info);

    if (tag == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface = g_type_info_get_interface(type_info);
        return g_info_type_to_string(interface.type());
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
bool gjs_array_to_explicit_array(JSContext* context, JS::HandleValue value,
                                 GITypeInfo* type_info, const char* arg_name,
                                 GjsArgumentType arg_type, GITransfer transfer,
                                 GjsArgumentFlags flags, void** contents,
                                 size_t* length_p) {
    bool found_length;

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to C array, transfer %d",
        arg_name, gjs_debug_value(value).c_str(), transfer);

    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);

    if ((value.isNull() && !(flags & GjsArgumentFlags::MAY_BE_NULL)) ||
        (!value.isString() && !value.isObjectOrNull())) {
        throw_invalid_argument(context, value, param_info, arg_name, arg_type);
        return false;
    }

    if (value.isNull()) {
        *contents = NULL;
        *length_p = 0;
    } else if (value.isString()) {
        /* Allow strings as int8/uint8/int16/uint16 arrays */
        JS::RootedString str(context, value.toString());
        GITypeTag element_tag = g_type_info_get_tag(param_info);
        if (!gjs_string_to_intarray(context, str, element_tag, contents, length_p))
            return false;
    } else {
        JS::RootedObject array_obj(context, &value.toObject());
        GITypeTag element_type = g_type_info_get_tag(param_info);
        if (JS_IsUint8Array(array_obj) && (element_type == GI_TYPE_TAG_INT8 ||
                                           element_type == GI_TYPE_TAG_UINT8)) {
            GBytes* bytes = gjs_byte_array_get_bytes(array_obj);
            *contents = g_bytes_unref_to_data(bytes, length_p);
            return true;
        }

        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        if (!JS_HasPropertyById(context, array_obj, atoms.length(),
                                &found_length))
            return false;
        if (found_length) {
            guint32 length;

            if (!gjs_object_require_converted_property(
                    context, array_obj, nullptr, atoms.length(), &length)) {
                return false;
            } else {
                if (!gjs_array_to_array(context,
                                        value,
                                        length,
                                        transfer,
                                        param_info,
                                        contents))
                    return false;

                *length_p = length;
            }
        } else {
            throw_invalid_argument(context, value, param_info, arg_name, arg_type);
            return false;
        }
    }

    return true;
}

namespace arg {
[[nodiscard]] static bool is_gdk_atom(GIBaseInfo* info) {
    return (strcmp("Atom", g_base_info_get_name(info)) == 0 &&
            strcmp("Gdk", g_base_info_get_namespace(info)) == 0);
}
}  // namespace arg

static void intern_gdk_atom(const char* name, GIArgument* ret) {
    GjsAutoFunctionInfo atom_intern_fun =
        g_irepository_find_by_name(nullptr, "Gdk", "atom_intern");

    GIArgument atom_intern_args[2];

    /* Can only store char * in GIArgument. First argument to gdk_atom_intern
     * is const char *, string isn't modified. */
    gjs_arg_set(&atom_intern_args[0], name);
    gjs_arg_set(&atom_intern_args[1], false);

    g_function_info_invoke(atom_intern_fun,
                           atom_intern_args, 2,
                           nullptr, 0,
                           ret,
                           nullptr);
}

static bool value_to_interface_gi_argument(
    JSContext* cx, JS::HandleValue value, GIBaseInfo* interface_info,
    GIInfoType interface_type, GITransfer transfer, bool expect_object,
    GIArgument* arg, GjsArgumentType arg_type, GjsArgumentFlags flags,
    bool* report_type_mismatch) {
    g_assert(report_type_mismatch);
    GType gtype;

    switch (interface_type) {
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_ENUM:
        case GI_INFO_TYPE_FLAGS:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_STRUCT:
        case GI_INFO_TYPE_UNION:
            // These are subtypes of GIRegisteredTypeInfo for which the cast is
            // safe
            gtype = g_registered_type_info_get_g_type(interface_info);
            break;

        default:
            gtype = G_TYPE_NONE;
    }

    if (gtype != G_TYPE_NONE)
        gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "gtype of INTERFACE is %s",
                          g_type_name(gtype));

    if (gtype == G_TYPE_VALUE) {
        if (flags & GjsArgumentFlags::CALLER_ALLOCATES) {
            if (!gjs_value_to_g_value_no_copy(cx, value,
                                              gjs_arg_get<GValue*>(arg)))
                return false;

            return true;
        }

        Gjs::AutoGValue gvalue;
        if (!gjs_value_to_g_value(cx, value, &gvalue)) {
            gjs_arg_unset<void*>(arg);
            return false;
        }

        gjs_arg_set(arg, g_boxed_copy(G_TYPE_VALUE, &gvalue));
        return true;

    } else if (arg::is_gdk_atom(interface_info)) {
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
        gjs_arg_set(arg, nullptr);
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

            gjs_arg_set(arg, klass);
            return true;
        }

        GType arg_gtype = gtype;
        if (interface_type == GI_INFO_TYPE_STRUCT && gtype == G_TYPE_NONE &&
            !g_struct_info_is_foreign(interface_info)) {
            GType actual_gtype = G_TYPE_NONE;
            // In case we have no known type from gi we should try to be
            // more dynamic and try to get the type from JS, to handle the
            // case in which we're handling a gpointer such as GTypeInstance
            // FIXME(3v1n0): would be nice to know if GI would give this info
            if (!gjs_gtype_get_actual_gtype(cx, obj, &actual_gtype))
                return false;

            if (G_TYPE_IS_INSTANTIATABLE(actual_gtype))
                gtype = actual_gtype;
        }

        if ((interface_type == GI_INFO_TYPE_STRUCT ||
             interface_type == GI_INFO_TYPE_BOXED) &&
            !g_type_is_a(gtype, G_TYPE_CLOSURE)) {
            // Handle Struct/Union first since we don't necessarily need a GType
            // for them. We special case Closures later, so skip them here.
            if (g_type_is_a(gtype, G_TYPE_BYTES) && JS_IsUint8Array(obj)) {
                gjs_arg_set(arg, gjs_byte_array_get_bytes(obj));
                return true;
            }
            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                return ErrorBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer);
            }
            if (arg_gtype != G_TYPE_NONE || gtype == G_TYPE_NONE ||
                g_type_is_a(gtype, G_TYPE_BOXED) ||
                g_type_is_a(gtype, G_TYPE_VALUE) ||
                g_type_is_a(gtype, G_TYPE_VARIANT)) {
                return BoxedBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype,
                    interface_info);
            }
        }

        if (interface_type == GI_INFO_TYPE_UNION) {
            return UnionBase::transfer_to_gi_argument(
                cx, obj, arg, GI_DIRECTION_IN, transfer, gtype, interface_info);
        }

        if (gtype != G_TYPE_NONE) {
            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                return ObjectBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);

            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if (!gjs_typecheck_param(cx, obj, gtype, true)) {
                    gjs_arg_unset<void*>(arg);
                    return false;
                }
                gjs_arg_set(arg, gjs_g_param_from_param(cx, obj));
                if (transfer != GI_TRANSFER_NOTHING)
                    g_param_spec_ref(gjs_arg_get<GParamSpec*>(arg));
                return true;

            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                    if (BoxedBase::typecheck(cx, obj, interface_info, gtype,
                                             GjsTypecheckNoThrow())) {
                        return BoxedBase::transfer_to_gi_argument(
                            cx, obj, arg, GI_DIRECTION_IN, transfer, gtype,
                            interface_info);
                    }

                    GClosure* closure =
                        Gjs::Closure::create_marshaled(cx, obj, "boxed");
                    // GI doesn't know about floating GClosure references. We
                    // guess that if this is a return value going from JS::Value
                    // to GIArgument, it's intended to be passed to a C API that
                    // will consume the floating reference.
                    if (arg_type != GJS_ARGUMENT_RETURN_VALUE) {
                        g_closure_ref(closure);
                        g_closure_sink(closure);
                    }
                    gjs_arg_set(arg, closure);
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
            gjs_arg_unset<void*>(arg);
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

            gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(
                arg, _gjs_enum_to_int(value_int64));
            return true;

        } else if (interface_type == GI_INFO_TYPE_FLAGS) {
            int64_t value_int64;

            if (!JS::ToInt64(cx, value, &value_int64) ||
                !_gjs_flags_value_is_valid(cx, gtype, value_int64))
                return false;

            gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(
                arg, _gjs_enum_to_int(value_int64));
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

template <typename T>
GJS_JSAPI_RETURN_CONVENTION inline static bool gjs_arg_set_from_js_value(
    JSContext* cx, const JS::HandleValue& value, GIArgument* arg,
    const char* arg_name, GjsArgumentType arg_type) {
    bool out_of_range = false;

    if (!gjs_arg_set_from_js_value<T>(cx, value, arg, &out_of_range)) {
        if (out_of_range) {
            GjsAutoChar display_name =
                gjs_argument_display_name(arg_name, arg_type);
            gjs_throw(cx, "value %s is out of range for %s (type %s)",
                      std::to_string(gjs_arg_get<T>(arg)).c_str(),
                      display_name.get(), Gjs::static_type_name<T>());
        }

        return false;
    }

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION, "%s set to value %s (type %s)",
        GjsAutoChar(gjs_argument_display_name(arg_name, arg_type)).get(),
        std::to_string(gjs_arg_get<T>(arg)).c_str(),
        Gjs::static_type_name<T>());

    return true;
}

static bool check_nullable_argument(JSContext* cx, const char* arg_name,
                                    GjsArgumentType arg_type,
                                    GITypeTag type_tag, GjsArgumentFlags flags,
                                    GIArgument* arg) {
    if (!(flags & GjsArgumentFlags::MAY_BE_NULL) && !gjs_arg_get<void*>(arg)) {
        GjsAutoChar display_name =
            gjs_argument_display_name(arg_name, arg_type);
        gjs_throw(cx, "%s (type %s) may not be null", display_name.get(),
                  g_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

bool gjs_value_to_gi_argument(JSContext* context, JS::HandleValue value,
                              GITypeInfo* type_info, const char* arg_name,
                              GjsArgumentType arg_type, GITransfer transfer,
                              GjsArgumentFlags flags, GIArgument* arg) {
    GITypeTag type_tag = g_type_info_get_tag(type_info);

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type %s", arg_name,
        gjs_debug_value(value).c_str(), g_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        // just so it isn't uninitialized
        gjs_arg_unset<void*>(arg);
        return check_nullable_argument(context, arg_name, arg_type, type_tag,
                                       flags, arg);

    case GI_TYPE_TAG_INT8:
        return gjs_arg_set_from_js_value<int8_t>(context, value, arg, arg_name,
                                                 arg_type);
    case GI_TYPE_TAG_UINT8:
        return gjs_arg_set_from_js_value<uint8_t>(context, value, arg, arg_name,
                                                  arg_type);
    case GI_TYPE_TAG_INT16:
        return gjs_arg_set_from_js_value<int16_t>(context, value, arg, arg_name,
                                                  arg_type);

    case GI_TYPE_TAG_UINT16:
        return gjs_arg_set_from_js_value<uint16_t>(context, value, arg,
                                                   arg_name, arg_type);

    case GI_TYPE_TAG_INT32:
        return gjs_arg_set_from_js_value<int32_t>(context, value, arg, arg_name,
                                                  arg_type);

    case GI_TYPE_TAG_UINT32:
        return gjs_arg_set_from_js_value<uint32_t>(context, value, arg,
                                                   arg_name, arg_type);

    case GI_TYPE_TAG_INT64:
        return gjs_arg_set_from_js_value<int64_t>(context, value, arg, arg_name,
                                                  arg_type);

    case GI_TYPE_TAG_UINT64:
        return gjs_arg_set_from_js_value<uint64_t>(context, value, arg,
                                                   arg_name, arg_type);

    case GI_TYPE_TAG_BOOLEAN:
        gjs_arg_set(arg, JS::ToBoolean(value));
        return true;

    case GI_TYPE_TAG_FLOAT:
        return gjs_arg_set_from_js_value<float>(context, value, arg, arg_name,
                                                arg_type);

    case GI_TYPE_TAG_DOUBLE:
        return gjs_arg_set_from_js_value<double>(context, value, arg, arg_name,
                                                 arg_type);

    case GI_TYPE_TAG_UNICHAR:
        if (value.isString()) {
            if (!gjs_unichar_from_string(context, value,
                                         &gjs_arg_member<char32_t>(arg)))
                return false;
        } else {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        }
        break;

    case GI_TYPE_TAG_GTYPE:
        if (value.isObjectOrNull()) {
            GType gtype;
            JS::RootedObject obj(context, value.toObjectOrNull());
            if (!gjs_gtype_get_actual_gtype(context, obj, &gtype))
                return false;

            if (gtype == G_TYPE_INVALID)
                return false;
            gjs_arg_set<GType, GI_TYPE_TAG_GTYPE>(arg, gtype);
        } else {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        }
        break;

    case GI_TYPE_TAG_FILENAME:
        if (value.isNull()) {
            gjs_arg_set(arg, nullptr);
        } else if (value.isString()) {
            GjsAutoChar filename_str;
            if (!gjs_string_to_filename(context, value, &filename_str))
                return false;

            gjs_arg_set(arg, filename_str.release());
        } else {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        }

        return check_nullable_argument(context, arg_name, arg_type, type_tag,
                                       flags, arg);

    case GI_TYPE_TAG_UTF8:
        if (value.isNull()) {
            gjs_arg_set(arg, nullptr);
        } else if (value.isString()) {
            JS::RootedString str(context, value.toString());
            JS::UniqueChars utf8_str(JS_EncodeStringToUTF8(context, str));
            if (!utf8_str)
                return false;

            gjs_arg_set(arg, g_strdup(utf8_str.get()));
        } else {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        }

        return check_nullable_argument(context, arg_name, arg_type, type_tag,
                                       flags, arg);

    case GI_TYPE_TAG_ERROR:
        if (value.isNull()) {
            gjs_arg_set(arg, nullptr);
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());
            if (!ErrorBase::transfer_to_gi_argument(context, obj, arg,
                                                    GI_DIRECTION_IN, transfer))
                return false;
        } else {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        }

        return check_nullable_argument(context, arg_name, arg_type, type_tag,
                                       flags, arg);

    case GI_TYPE_TAG_INTERFACE:
        {
        bool expect_object = true;

        GjsAutoBaseInfo interface_info = g_type_info_get_interface(type_info);
        g_assert(interface_info);

        GIInfoType interface_type = interface_info.type();
        if (interface_type == GI_INFO_TYPE_ENUM ||
            interface_type == GI_INFO_TYPE_FLAGS ||
            arg::is_gdk_atom(interface_info))
            expect_object = false;

        if (interface_type == GI_INFO_TYPE_STRUCT &&
            g_struct_info_is_foreign(interface_info)) {
            return gjs_struct_foreign_convert_to_gi_argument(
                context, value, interface_info, arg_name, arg_type, transfer,
                flags, arg);
        }

        bool report_type_mismatch = false;
        if (!value_to_interface_gi_argument(
                context, value, interface_info, interface_type, transfer,
                expect_object, arg, arg_type, flags, &report_type_mismatch)) {
            if (report_type_mismatch)
                throw_invalid_argument(context, value, type_info, arg_name,
                                       arg_type);

            return false;
        }

        if (expect_object)
            return check_nullable_argument(context, arg_name, arg_type,
                                           type_tag, flags, arg);
        }
        break;

    case GI_TYPE_TAG_GLIST:
        return gjs_array_to_g_list(context, value, type_info, transfer,
                                   arg_name, arg_type,
                                   &gjs_arg_member<GList*>(arg));
    case GI_TYPE_TAG_GSLIST:
        return gjs_array_to_g_list(context, value, type_info, transfer,
                                   arg_name, arg_type,
                                   &gjs_arg_member<GSList*>(arg));

    case GI_TYPE_TAG_GHASH:
        if (value.isNull()) {
            gjs_arg_set(arg, nullptr);
            if (!(flags & GjsArgumentFlags::MAY_BE_NULL)) {
                throw_invalid_argument(context, value, type_info, arg_name,
                                       arg_type);
                return false;
            }
        } else if (!value.isObject()) {
            throw_invalid_argument(context, value, type_info, arg_name,
                                   arg_type);
            return false;
        } else {
            GHashTable *ghash;
            JS::RootedObject props(context, &value.toObject());
            if (!gjs_object_to_g_hash(context, props, type_info, transfer,
                                      &ghash)) {
                return false;
            }

            gjs_arg_set(arg, ghash);
        }
        break;

    case GI_TYPE_TAG_ARRAY: {
        GjsAutoPointer<void> data;
        size_t length;
        GIArrayType array_type = g_type_info_get_array_type(type_info);

        /* First, let's handle the case where we're passed an instance
         * of Uint8Array and it needs to be marshalled to GByteArray.
         */
        if (value.isObject()) {
            JSObject* bytearray_obj = &value.toObject();
            if (JS_IsUint8Array(bytearray_obj) &&
                array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                gjs_arg_set(arg, gjs_byte_array_get_byte_array(bytearray_obj));
                break;
            } else {
                /* Fall through, !handled */
            }
        }

        if (!gjs_array_to_explicit_array(context, value, type_info, arg_name,
                                         arg_type, transfer, flags, data.out(),
                                         &length)) {
            return false;
        }

        GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);
        if (array_type == GI_ARRAY_TYPE_C) {
            gjs_arg_set(arg, data.release());
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeTag storage_type = g_type_info_get_storage_type(param_info);
            GArray* array =
                garray_new_for_storage_type(length, storage_type, param_info);

            if (data)
                g_array_append_vals(array, data, length);
            gjs_arg_set(arg, array);
        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            GByteArray *byte_array = g_byte_array_sized_new(length);

            if (data)
                g_byte_array_append(byte_array, data.as<const uint8_t>(),
                                    length);
            gjs_arg_set(arg, byte_array);
        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GPtrArray *array = g_ptr_array_sized_new(length);

            g_ptr_array_set_size(array, length);
            if (data)
                memcpy(array->pdata, data, sizeof(void*) * length);
            gjs_arg_set(arg, array);
        }
        break;
    }
    default:
        g_warning("Unhandled type %s for JavaScript to GIArgument conversion",
                  g_type_tag_to_string(type_tag));
        throw_invalid_argument(context, value, type_info, arg_name, arg_type);
        return false;
    }

    return true;
}

/* If a callback function with a return value throws, we still have
 * to return something to C. This function defines what that something
 * is. It basically boils down to memset(arg, 0, sizeof(*arg)), but
 * gives as a bit more future flexibility and also will work if
 * libffi passes us a buffer that only has room for the appropriate
 * branch of GIArgument. (Currently it appears that the return buffer
 * has a fixed size large enough for the union of all types.)
 */
void gjs_gi_argument_init_default(GITypeInfo* type_info, GIArgument* arg) {
    GITypeTag type_tag = g_type_info_get_tag(type_info);

    switch (type_tag) {
        case GI_TYPE_TAG_VOID:
            // just so it isn't uninitialized
            gjs_arg_unset<void*>(arg);
            break;
        case GI_TYPE_TAG_INT8:
            gjs_arg_unset<int8_t>(arg);
            break;
        case GI_TYPE_TAG_UINT8:
            gjs_arg_unset<uint8_t>(arg);
            break;
        case GI_TYPE_TAG_INT16:
            gjs_arg_unset<int16_t>(arg);
            break;
        case GI_TYPE_TAG_UINT16:
            gjs_arg_unset<uint16_t>(arg);
            break;
        case GI_TYPE_TAG_INT32:
            gjs_arg_unset<int32_t>(arg);
            break;
        case GI_TYPE_TAG_UINT32:
            gjs_arg_unset<uint32_t>(arg);
            break;
        case GI_TYPE_TAG_UNICHAR:
            gjs_arg_unset<char32_t>(arg);
            break;
        case GI_TYPE_TAG_INT64:
            gjs_arg_unset<int64_t>(arg);
            break;
        case GI_TYPE_TAG_UINT64:
            gjs_arg_unset<uint64_t>(arg);
            break;
        case GI_TYPE_TAG_BOOLEAN:
            gjs_arg_unset<bool>(arg);
            break;
        case GI_TYPE_TAG_FLOAT:
            gjs_arg_unset<float>(arg);
            break;
        case GI_TYPE_TAG_DOUBLE:
            gjs_arg_unset<double>(arg);
            break;
        case GI_TYPE_TAG_GTYPE:
            gjs_arg_unset<GType, GI_TYPE_TAG_GTYPE>(arg);
            break;
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_ERROR:
            gjs_arg_unset<void*>(arg);
            break;
        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            g_assert(interface_info != nullptr);

            GIInfoType interface_type = interface_info.type();

            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS)
                gjs_arg_unset<int, GI_TYPE_TAG_INTERFACE>(arg);
            else
                gjs_arg_unset<void*>(arg);
        } break;
        case GI_TYPE_TAG_GHASH:
            // Possibly better to return an empty hash table?
            gjs_arg_unset<GHashTable*>(arg);
            break;
        case GI_TYPE_TAG_ARRAY:
            gjs_arg_unset<void*>(arg);
            break;
        default:
            g_warning("Unhandled type %s for default GIArgument initialization",
                      g_type_tag_to_string(type_tag));
            break;
    }
}

bool gjs_value_to_callback_out_arg(JSContext* context, JS::HandleValue value,
                                   GIArgInfo* arg_info, GIArgument* arg) {
    GIDirection direction [[maybe_unused]] = g_arg_info_get_direction(arg_info);
    g_assert(
        (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT) &&
        "gjs_value_to_callback_out_arg does not handle in arguments.");

    GjsArgumentFlags flags = GjsArgumentFlags::NONE;
    GITypeInfo type_info;

    g_arg_info_load_type(arg_info, &type_info);

    // If the argument is optional and we're passed nullptr,
    // ignore the GJS value.
    if (g_arg_info_is_optional(arg_info) && !arg)
        return true;

    // Otherwise, throw an error to prevent a segfault.
    if (!arg) {
        gjs_throw(context,
                  "Return value %s is not optional but was passed NULL",
                  g_base_info_get_name(arg_info));
        return false;
    }

    if (g_arg_info_may_be_null(arg_info))
        flags |= GjsArgumentFlags::MAY_BE_NULL;
    if (g_arg_info_is_caller_allocates(arg_info))
        flags |= GjsArgumentFlags::CALLER_ALLOCATES;

    return gjs_value_to_gi_argument(
        context, value, &type_info, g_base_info_get_name(arg_info),
        (g_arg_info_is_return_value(arg_info) ? GJS_ARGUMENT_RETURN_VALUE
                                              : GJS_ARGUMENT_ARGUMENT),
        g_arg_info_get_ownership_transfer(arg_info), flags, arg);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_from_g_list(
    JSContext* cx, JS::MutableHandleValue value_p, GITypeInfo* type_info,
    GITransfer transfer, T* list) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    JS::RootedValueVector elems(cx);
    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);

    g_assert(param_info);

    GIArgument arg;
    for (size_t i = 0; list; list = list->next, ++i) {
        g_type_info_argument_from_hash_pointer(param_info, list->data, &arg);

        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_value_from_gi_argument(cx, elems[i], param_info,
                                        GJS_ARGUMENT_LIST_ELEMENT, transfer,
                                        &arg))
            return false;
    }

    JS::RootedObject obj(cx, JS::NewArrayObject(cx, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_g_arg_release_g_list(
    JSContext* cx, GITransfer transfer, GITypeInfo* type_info,
    GjsArgumentFlags flags, GIArgument* arg) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    GjsSmartPointer<T> list = gjs_arg_steal<T*>(arg);

    if (transfer == GI_TRANSFER_CONTAINER)
        return true;

    GIArgument elem;
    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);
    g_assert(param_info);
    GITypeTag type_tag = g_type_info_get_tag(param_info);

    for (T* l = list; l; l = l->next) {
        gjs_arg_set(&elem, l->data);

        if (!gjs_g_arg_release_internal(cx, transfer, param_info, type_tag,
                                        GJS_ARGUMENT_LIST_ELEMENT, flags,
                                        &elem)) {
            return false;
        }
    }

    return true;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_JSAPI_RETURN_CONVENTION static bool fill_vector_from_carray(
    JSContext* cx, JS::RootedValueVector& elems,  // NOLINT(runtime/references)
    GITypeInfo* param_info, GIArgument* arg, void* array, size_t length,
    GITransfer transfer = GI_TRANSFER_EVERYTHING) {
    for (size_t i = 0; i < length; i++) {
        gjs_arg_set<T, TAG>(arg, *(static_cast<T*>(array) + i));

        if (!gjs_value_from_gi_argument(cx, elems[i], param_info,
                                        GJS_ARGUMENT_ARRAY_ELEMENT, transfer,
                                        arg))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_carray_internal(
    JSContext* context, JS::MutableHandleValue value_p, GIArrayType array_type,
    GITypeInfo* param_info, GITransfer transfer, guint length, void* array) {
    GITypeTag element_type;
    guint i;

    element_type = g_type_info_get_tag(param_info);

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
        JSObject* jsarray = JS::NewArrayObject(context, 0);
        if (!jsarray)
            return false;
        value_p.setObject(*jsarray);
        return true;
    }

    JS::RootedValueVector elems(context);
    if (!elems.resize(length)) {
        JS_ReportOutOfMemory(context);
        return false;
    }

    GIArgument arg;
    switch (element_type) {
        /* Special cases handled above */
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UNICHAR:
            g_assert_not_reached();
        case GI_TYPE_TAG_BOOLEAN:
            if (!fill_vector_from_carray<gboolean, GI_TYPE_TAG_BOOLEAN>(
                    context, elems, param_info, &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_INT8:
            if (!fill_vector_from_carray<int8_t>(context, elems, param_info,
                                                 &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT16:
            if (!fill_vector_from_carray<uint16_t>(context, elems, param_info,
                                                   &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_INT16:
            if (!fill_vector_from_carray<int16_t>(context, elems, param_info,
                                                  &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT32:
            if (!fill_vector_from_carray<uint32_t>(context, elems, param_info,
                                                   &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_INT32:
            if (!fill_vector_from_carray<int32_t>(context, elems, param_info,
                                                  &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT64:
            if (!fill_vector_from_carray<uint64_t>(context, elems, param_info,
                                                   &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_INT64:
            if (!fill_vector_from_carray<int64_t>(context, elems, param_info,
                                                  &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_FLOAT:
            if (!fill_vector_from_carray<float>(context, elems, param_info,
                                                &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_DOUBLE:
            if (!fill_vector_from_carray<double>(context, elems, param_info,
                                                 &arg, array, length))
                return false;
            break;
        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(param_info);
            GIInfoType info_type = interface_info.type();

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
                    gjs_arg_set(&arg,
                                static_cast<char*>(array) + (struct_size * i));

                    if (!gjs_value_from_gi_argument(
                            context, elems[i], param_info,
                            GJS_ARGUMENT_ARRAY_ELEMENT, transfer, &arg))
                        return false;
                }

                break;
            }
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
            if (!fill_vector_from_carray<void*>(context, elems, param_info,
                                                &arg, array, length, transfer))
                return false;
            break;
        case GI_TYPE_TAG_VOID:
        default:
          gjs_throw(context, "Unknown Array element-type %d", element_type);
          return false;
    }

    JS::RootedObject obj(context, JS::NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_fixed_size_array(JSContext* context,
                                            JS::MutableHandleValue value_p,
                                            GITypeInfo* type_info,
                                            GITransfer transfer, void* array) {
    gint length;

    length = g_type_info_get_array_fixed_size(type_info);

    g_assert (length != -1);

    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);

    return gjs_array_from_carray_internal(context, value_p,
                                          g_type_info_get_array_type(type_info),
                                          param_info, transfer, length, array);
}

bool gjs_value_from_explicit_array(JSContext* context,
                                   JS::MutableHandleValue value_p,
                                   GITypeInfo* type_info, GITransfer transfer,
                                   GIArgument* arg, int length) {
    GjsAutoTypeInfo param_info = g_type_info_get_param_type(type_info, 0);

    return gjs_array_from_carray_internal(
        context, value_p, g_type_info_get_array_type(type_info), param_info,
        transfer, length, gjs_arg_get<void*>(arg));
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_boxed_array(JSContext* context,
                                       JS::MutableHandleValue value_p,
                                       GIArrayType array_type,
                                       GITypeInfo* param_info,
                                       GITransfer transfer, GIArgument* arg) {
    GArray *array;
    GPtrArray *ptr_array;
    gpointer data = NULL;
    gsize length = 0;

    if (!gjs_arg_get<void*>(arg)) {
        value_p.setNull();
        return true;
    }

    switch(array_type) {
    case GI_ARRAY_TYPE_BYTE_ARRAY:
        /* GByteArray is just a typedef for GArray internally */
    case GI_ARRAY_TYPE_ARRAY:
        array = gjs_arg_get<GArray*>(arg);
        data = array->data;
        length = array->len;
        break;
    case GI_ARRAY_TYPE_PTR_ARRAY:
        ptr_array = gjs_arg_get<GPtrArray*>(arg);
        data = ptr_array->pdata;
        length = ptr_array->len;
        break;
    case GI_ARRAY_TYPE_C:  // already checked in gjs_value_from_gi_argument()
    default:
        g_assert_not_reached();
    }

    return gjs_array_from_carray_internal(context, value_p, array_type,
                                          param_info, transfer, length, data);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_g_value_array(JSContext* cx, JS::MutableHandleValue value_p,
                                  GITypeInfo* param_info, GITransfer transfer,
                                  const GValue* gvalue) {
    void* data = nullptr;
    size_t length = 0;
    GIArrayType array_type;
    GType value_gtype = G_VALUE_TYPE(gvalue);

    // GByteArray is just a typedef for GArray internally
    if (g_type_is_a(value_gtype, G_TYPE_BYTE_ARRAY) ||
        g_type_is_a(value_gtype, G_TYPE_ARRAY)) {
        array_type = g_type_is_a(value_gtype, G_TYPE_BYTE_ARRAY)
                         ? GI_ARRAY_TYPE_BYTE_ARRAY
                         : GI_ARRAY_TYPE_ARRAY;
        auto* array = reinterpret_cast<GArray*>(g_value_get_boxed(gvalue));
        data = array->data;
        length = array->len;
    } else if (g_type_is_a(value_gtype, G_TYPE_PTR_ARRAY)) {
        array_type = GI_ARRAY_TYPE_PTR_ARRAY;
        auto* ptr_array =
            reinterpret_cast<GPtrArray*>(g_value_get_boxed(gvalue));
        data = ptr_array->pdata;
        length = ptr_array->len;
    } else {
        g_assert_not_reached();
        gjs_throw(cx, "%s is not an array type", g_type_name(value_gtype));
        return false;
    }

    return gjs_array_from_carray_internal(cx, value_p, array_type, param_info,
                                          transfer, length, data);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool fill_vector_from_zero_terminated_carray(
    JSContext* cx, JS::RootedValueVector& elems,  // NOLINT(runtime/references)
    GITypeInfo* param_info, GIArgument* arg, void* c_array,
    GITransfer transfer = GI_TRANSFER_EVERYTHING) {
    T* array = static_cast<T*>(c_array);

    for (size_t i = 0;; i++) {
        if constexpr (std::is_scalar_v<T>) {
            if (!array[i])
                    break;

            gjs_arg_set(arg, array[i]);
        } else {
            uint8_t* element_start = reinterpret_cast<uint8_t*>(&array[i]);
            if (*element_start == 0 &&
                // cppcheck-suppress pointerSize
                memcmp(element_start, element_start + 1, sizeof(T) - 1) == 0)
                    break;

            gjs_arg_set(arg, element_start);
        }

        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_value_from_gi_argument(cx, elems[i], param_info,
                                        GJS_ARGUMENT_ARRAY_ELEMENT, transfer,
                                        arg))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_zero_terminated_c_array(
    JSContext* context, JS::MutableHandleValue value_p, GITypeInfo* param_info,
    GITransfer transfer, void* c_array) {
    GITypeTag element_type;

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

    JS::RootedValueVector elems(context);

    GIArgument arg;
    switch (element_type) {
        /* Special cases handled above. */
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UNICHAR:
            g_assert_not_reached();
        case GI_TYPE_TAG_INT8:
            if (!fill_vector_from_zero_terminated_carray<int8_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT16:
            if (!fill_vector_from_zero_terminated_carray<uint16_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT16:
            if (!fill_vector_from_zero_terminated_carray<int16_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT32:
            if (!fill_vector_from_zero_terminated_carray<uint32_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT32:
            if (!fill_vector_from_zero_terminated_carray<int32_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT64:
            if (!fill_vector_from_zero_terminated_carray<uint64_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT64:
            if (!fill_vector_from_zero_terminated_carray<int64_t>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_FLOAT:
            if (!fill_vector_from_zero_terminated_carray<float>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_DOUBLE:
            if (!fill_vector_from_zero_terminated_carray<double>(
                    context, elems, param_info, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(param_info);

            if (!g_type_info_is_pointer(param_info) &&
                is_gvalue(interface_info)) {
                if (!fill_vector_from_zero_terminated_carray<GValue>(
                        context, elems, param_info, &arg, c_array))
                    return false;
                break;
            }

            if (!g_type_info_is_pointer(param_info)) {
                gjs_throw(context,
                          "Flat C array of %s.%s not supported (see "
                          "https://gitlab.gnome.org/GNOME/gjs/-/issues/603)",
                          interface_info.ns(), interface_info.name());
                return false;
            }

            [[fallthrough]];
        }
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            if (!fill_vector_from_zero_terminated_carray<void*>(
                    context, elems, param_info, &arg, c_array, transfer))
                return false;
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

    JS::RootedObject obj(context, JS::NewArrayObject(context, elems));
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

bool gjs_object_from_g_hash(JSContext* context, JS::MutableHandleValue value_p,
                            GITypeInfo* key_param_info,
                            GITypeInfo* val_param_info, GITransfer transfer,
                            GHashTable* hash) {
    GHashTableIter iter;

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
    void* key_pointer;
    void* val_pointer;
    GIArgument keyarg, valarg;
    while (g_hash_table_iter_next(&iter, &key_pointer, &val_pointer)) {
        g_type_info_argument_from_hash_pointer(key_param_info, key_pointer,
                                               &keyarg);
        if (!gjs_value_from_gi_argument(context, &keyjs, key_param_info,
                                        GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                        &keyarg))
            return false;

        keystr = JS::ToString(context, keyjs);
        if (!keystr)
            return false;

        JS::UniqueChars keyutf8(JS_EncodeStringToUTF8(context, keystr));
        if (!keyutf8)
            return false;

        g_type_info_argument_from_hash_pointer(val_param_info, val_pointer,
                                               &valarg);
        if (!gjs_value_from_gi_argument(context, &valjs, val_param_info,
                                        GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                        &valarg))
            return false;

        if (!JS_DefineProperty(context, obj, keyutf8.get(), valjs,
                               JSPROP_ENUMERATE))
            return false;
    }

    return true;
}

bool gjs_value_from_gi_argument(JSContext* context,
                                JS::MutableHandleValue value_p,
                                GITypeInfo* type_info,
                                GjsArgumentType argument_type,
                                GITransfer transfer, GIArgument* arg) {
    GITypeTag type_tag = g_type_info_get_tag(type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument %s to JS::Value",
                      g_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        // If the argument is a pointer, convert to null to match our
        // in handling.
        if (g_type_info_is_pointer(type_info))
            value_p.setNull();
        else
            value_p.setUndefined();
        break;

    case GI_TYPE_TAG_BOOLEAN:
        value_p.setBoolean(gjs_arg_get<bool>(arg));
        break;

    case GI_TYPE_TAG_INT32:
        value_p.setInt32(gjs_arg_get<int32_t>(arg));
        break;

    case GI_TYPE_TAG_UINT32:
        value_p.setNumber(gjs_arg_get<uint32_t>(arg));
        break;

    case GI_TYPE_TAG_INT64:
        value_p.setNumber(gjs_arg_get_maybe_rounded<int64_t>(arg));
        break;

    case GI_TYPE_TAG_UINT64:
        value_p.setNumber(gjs_arg_get_maybe_rounded<uint64_t>(arg));
        break;

    case GI_TYPE_TAG_UINT16:
        value_p.setInt32(gjs_arg_get<uint16_t>(arg));
        break;

    case GI_TYPE_TAG_INT16:
        value_p.setInt32(gjs_arg_get<int16_t>(arg));
        break;

    case GI_TYPE_TAG_UINT8:
        value_p.setInt32(gjs_arg_get<uint8_t>(arg));
        break;

    case GI_TYPE_TAG_INT8:
        value_p.setInt32(gjs_arg_get<int8_t>(arg));
        break;

    case GI_TYPE_TAG_FLOAT:
        value_p.setNumber(JS::CanonicalizeNaN(gjs_arg_get<float>(arg)));
        break;

    case GI_TYPE_TAG_DOUBLE:
        value_p.setNumber(JS::CanonicalizeNaN(gjs_arg_get<double>(arg)));
        break;

    case GI_TYPE_TAG_GTYPE:
    {
        GType gtype = gjs_arg_get<GType, GI_TYPE_TAG_GTYPE>(arg);
        if (gtype == 0) {
            value_p.setNull();
            return true;
        }

        JS::RootedObject obj(context, gjs_gtype_create_gtype_wrapper(context, gtype));
        if (!obj)
            return false;

        value_p.setObject(*obj);
        return true;
    }
        break;

    case GI_TYPE_TAG_UNICHAR: {
        char32_t value = gjs_arg_get<char32_t>(arg);

        // Preserve the bidirectional mapping between 0 and ""
        if (value == 0) {
            value_p.set(JS_GetEmptyStringValue(context));
            return true;
        } else if (!g_unichar_validate(value)) {
            gjs_throw(context, "Invalid unicode codepoint %" G_GUINT32_FORMAT,
                      value);
            return false;
        }

        char utf8[7];
        int bytes = g_unichar_to_utf8(value, utf8);
        return gjs_string_from_utf8_n(context, utf8, bytes, value_p);
    }

    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_UTF8: {
        const char* str = gjs_arg_get<const char*>(arg);
        if (!str) {
            value_p.setNull();
            return true;
        }

        if (type_tag == GI_TYPE_TAG_FILENAME)
            return gjs_string_from_filename(context, str, -1, value_p);

        return gjs_string_from_utf8(context, str, value_p);
    }

    case GI_TYPE_TAG_ERROR: {
        GError* ptr = gjs_arg_get<GError*>(arg);
        if (!ptr) {
            value_p.setNull();
            return true;
        }

        JSObject* obj = ErrorInstance::object_for_c_ptr(context, ptr);
        if (!obj)
            return false;

        value_p.setObject(*obj);
        return true;
    }

    case GI_TYPE_TAG_INTERFACE:
        {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            g_assert(interface_info);

            GIInfoType interface_type = interface_info.type();

            if (interface_type == GI_INFO_TYPE_UNRESOLVED) {
                gjs_throw(context,
                          "Unable to resolve arg type '%s'",
                          g_base_info_get_name(interface_info));
                return false;
            }

            /* Enum/Flags are aren't pointer types, unlike the other interface subtypes */
            if (interface_type == GI_INFO_TYPE_ENUM) {
                int64_t value_int64 = _gjs_enum_from_int(
                    interface_info,
                    gjs_arg_get<int, GI_TYPE_TAG_INTERFACE>(arg));

                if (!_gjs_enum_value_is_valid(context, interface_info,
                                              value_int64))
                    return false;

                value_p.setNumber(static_cast<double>(value_int64));
                return true;
            }

            if (interface_type == GI_INFO_TYPE_FLAGS) {
                int64_t value_int64 = _gjs_enum_from_int(
                    interface_info,
                    gjs_arg_get<int, GI_TYPE_TAG_INTERFACE>(arg));

                GType gtype = g_registered_type_info_get_g_type(
                    interface_info.as<GIRegisteredTypeInfo>());

                if (gtype != G_TYPE_NONE) {
                    /* check make sure 32 bit flag */
                   if (static_cast<uint32_t>(value_int64) != value_int64) { /* Not a guint32 */
                        gjs_throw(context,
                                "0x%" G_GINT64_MODIFIER "x is not a valid value for flags %s",
                                value_int64, g_type_name(gtype));
                        return false;
                    }

                    /* Pass only valid values*/
                    GjsAutoTypeClass<GFlagsClass> gflags_class(gtype);
                    value_int64 &= gflags_class->mask;
                }

                value_p.setNumber(static_cast<double>(value_int64));
                return true;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_foreign(interface_info.as<GIStructInfo>())) {
                return gjs_struct_foreign_convert_from_gi_argument(
                    context, value_p, interface_info, arg);
            }

            /* Everything else is a pointer type, NULL is the easy case */
            if (!gjs_arg_get<void*>(arg)) {
                value_p.setNull();
                return true;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_gtype_struct(
                    interface_info.as<GIStructInfo>())) {
                /* XXX: here we make the implicit assumption that GTypeClass is the same
                   as GTypeInterface. This is true for the GType field, which is what we
                   use, but not for the rest of the structure!
                */
                GType gtype = G_TYPE_FROM_CLASS(gjs_arg_get<GTypeClass*>(arg));

                if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                    return gjs_lookup_interface_constructor(context, gtype,
                                                            value_p);
                }
                return gjs_lookup_object_constructor(context, gtype, value_p);
            }

            GType gtype = g_registered_type_info_get_g_type(
                interface_info.as<GIRegisteredTypeInfo>());
            if (G_TYPE_IS_INSTANTIATABLE(gtype) ||
                G_TYPE_IS_INTERFACE(gtype))
                gtype = G_TYPE_FROM_INSTANCE(gjs_arg_get<GTypeInstance*>(arg));

            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));


            /* Test GValue and GError before Struct, or it will be handled as the latter */
            if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                return gjs_value_from_g_value(context, value_p,
                                              gjs_arg_get<const GValue*>(arg));
            }

            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                JSObject* obj = ErrorInstance::object_for_c_ptr(
                    context, gjs_arg_get<GError*>(arg));
                if (!obj)
                    return false;
                value_p.setObject(*obj);
                return true;
            }

            if (interface_type == GI_INFO_TYPE_STRUCT || interface_type == GI_INFO_TYPE_BOXED) {
                if (arg::is_gdk_atom(interface_info)) {
                    GjsAutoFunctionInfo atom_name_fun =
                        g_struct_info_find_method(interface_info, "name");
                    GIArgument atom_name_ret;

                    g_function_info_invoke(atom_name_fun,
                            arg, 1,
                            nullptr, 0,
                            &atom_name_ret,
                            nullptr);

                    GjsAutoChar name = gjs_arg_get<char*>(&atom_name_ret);
                    if (g_strcmp0("NONE", name) == 0) {
                        value_p.setNull();
                        return true;
                    }

                    return gjs_string_from_utf8(context, name, value_p);
                }

                JSObject *obj;

                if (gtype == G_TYPE_VARIANT) {
                    transfer = GI_TRANSFER_EVERYTHING;
                } else if (transfer == GI_TRANSFER_CONTAINER) {
                    switch (argument_type) {
                        case GJS_ARGUMENT_ARRAY_ELEMENT:
                        case GJS_ARGUMENT_LIST_ELEMENT:
                        case GJS_ARGUMENT_HASH_ELEMENT:
                            transfer = GI_TRANSFER_EVERYTHING;
                        default:
                            break;
                    }
                }

                if (transfer == GI_TRANSFER_EVERYTHING)
                    obj = BoxedInstance::new_for_c_struct(
                        context, interface_info, gjs_arg_get<void*>(arg));
                else
                    obj = BoxedInstance::new_for_c_struct(
                        context, interface_info, gjs_arg_get<void*>(arg),
                        BoxedInstance::NoCopy());

                if (!obj)
                    return false;

                value_p.setObject(*obj);
                return true;
            }

            if (interface_type == GI_INFO_TYPE_UNION) {
                JSObject* obj = UnionInstance::new_for_c_union(
                    context, interface_info.as<GIUnionInfo>(),
                    gjs_arg_get<void*>(arg));
                if (!obj)
                    return false;

                value_p.setObject(*obj);
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                g_assert(gjs_arg_get<void*>(arg) &&
                         "Null arg is already handled above");
                return ObjectInstance::set_value_from_gobject(
                    context, gjs_arg_get<GObject*>(arg), value_p);
            }

            if (g_type_is_a(gtype, G_TYPE_BOXED) ||
                g_type_is_a(gtype, G_TYPE_ENUM) ||
                g_type_is_a(gtype, G_TYPE_FLAGS)) {
                /* Should have been handled above */
                gjs_throw(context,
                          "Type %s registered for unexpected interface_type %d",
                          g_type_name(gtype),
                          interface_type);
                return false;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                JSObject* obj = gjs_param_from_g_param(
                    context, G_PARAM_SPEC(gjs_arg_get<GParamSpec*>(arg)));
                if (!obj)
                    return false;
                value_p.setObject(*obj);
                return true;
            }

            if (gtype == G_TYPE_NONE) {
                gjs_throw(context,
                          "Unexpected unregistered type packing GIArgument "
                          "into JS::Value");
                return false;
            }

            if (G_TYPE_IS_INSTANTIATABLE(gtype) || G_TYPE_IS_INTERFACE(gtype)) {
                JSObject* obj = FundamentalInstance::object_for_c_ptr(
                    context, gjs_arg_get<void*>(arg));
                if (!obj)
                    return false;
                value_p.setObject(*obj);
                return true;
            }

            gjs_throw(context,
                      "Unhandled GType %s packing GIArgument into JS::Value",
                      g_type_name(gtype));
            return false;
        }

    case GI_TYPE_TAG_ARRAY:
        if (!gjs_arg_get<void*>(arg)) {
            value_p.setNull();
            return true;
        }

        if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            if (g_type_info_is_zero_terminated(type_info)) {
                GjsAutoTypeInfo param_info =
                    g_type_info_get_param_type(type_info, 0);
                g_assert(param_info != nullptr);

                return gjs_array_from_zero_terminated_c_array(
                    context, value_p, param_info, transfer,
                    gjs_arg_get<void*>(arg));
            } else {
                /* arrays with length are handled outside of this function */
                g_assert(((void) "Use gjs_value_from_explicit_array() for "
                          "arrays with length param",
                          g_type_info_get_array_length(type_info) == -1));
                return gjs_array_from_fixed_size_array(context, value_p,
                                                       type_info, transfer,
                                                       gjs_arg_get<void*>(arg));
            }
        } else if (g_type_info_get_array_type(type_info) ==
                   GI_ARRAY_TYPE_BYTE_ARRAY) {
            auto* byte_array = gjs_arg_get<GByteArray*>(arg);
            JSObject* array =
                gjs_byte_array_from_byte_array(context, byte_array);
            if (!array) {
                gjs_throw(context,
                          "Couldn't convert GByteArray to a Uint8Array");
                return false;
            }
            value_p.setObject(*array);
        } else {
            // this assumes the array type is GArray or GPtrArray
            GjsAutoTypeInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            g_assert(param_info != nullptr);

            return gjs_array_from_boxed_array(
                context, value_p, g_type_info_get_array_type(type_info),
                param_info, transfer, arg);
        }
        break;

    case GI_TYPE_TAG_GLIST:
        return gjs_array_from_g_list(context, value_p, type_info, transfer,
                                     gjs_arg_get<GList*>(arg));
    case GI_TYPE_TAG_GSLIST:
        return gjs_array_from_g_list(context, value_p, type_info, transfer,
                                     gjs_arg_get<GSList*>(arg));

    case GI_TYPE_TAG_GHASH:
        {
        GjsAutoTypeInfo key_param_info =
            g_type_info_get_param_type(type_info, 0);
        GjsAutoTypeInfo val_param_info =
            g_type_info_get_param_type(type_info, 1);
        g_assert(key_param_info != nullptr);
        g_assert(val_param_info != nullptr);

        return gjs_object_from_g_hash(context, value_p, key_param_info,
                                      val_param_info, transfer,
                                      gjs_arg_get<GHashTable*>(arg));
        }
        break;

    default:
        g_warning("Unhandled type %s converting GIArgument to JavaScript",
                  g_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

struct GHR_closure {
    JSContext *context;
    GjsAutoTypeInfo key_param_info, val_param_info;
    GITransfer transfer;
    GjsArgumentFlags flags;
    bool failed;
};

static gboolean
gjs_ghr_helper(gpointer key, gpointer val, gpointer user_data) {
    GHR_closure *c = (GHR_closure *) user_data;
    GIArgument key_arg, val_arg;
    gjs_arg_set(&key_arg, key);
    gjs_arg_set(&val_arg, val);
    if (!gjs_g_arg_release_internal(c->context, c->transfer, c->key_param_info,
                                    g_type_info_get_tag(c->key_param_info),
                                    GJS_ARGUMENT_HASH_ELEMENT, c->flags,
                                    &key_arg))
        c->failed = true;

    GITypeTag val_type = g_type_info_get_tag(c->val_param_info);

    switch (val_type) {
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
            g_clear_pointer(&gjs_arg_member<void*>(&val_arg), g_free);
            break;

        default:
            if (!gjs_g_arg_release_internal(
                    c->context, c->transfer, c->val_param_info, val_type,
                    GJS_ARGUMENT_HASH_ELEMENT, c->flags, &val_arg))
            c->failed = true;
    }

    return true;
}

/* We need to handle GI_TRANSFER_NOTHING differently for out parameters
 * (free nothing) and for in parameters (free any temporaries we've
 * allocated
 */
constexpr static bool is_transfer_in_nothing(GITransfer transfer,
                                             GjsArgumentFlags flags) {
    return (transfer == GI_TRANSFER_NOTHING) && (flags & GjsArgumentFlags::ARG_IN);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_g_arg_release_internal(
    JSContext* context, GITransfer transfer, GITypeInfo* type_info,
    GITypeTag type_tag, [[maybe_unused]] GjsArgumentType argument_type,
    GjsArgumentFlags flags, GIArgument* arg) {
    g_assert(transfer != GI_TRANSFER_NOTHING ||
             flags != GjsArgumentFlags::NONE);

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
        g_clear_pointer(&gjs_arg_member<char*>(arg), g_free);
        break;

    case GI_TYPE_TAG_ERROR:
        if (!is_transfer_in_nothing(transfer, flags))
            g_clear_error(&gjs_arg_member<GError*>(arg));
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            g_assert(interface_info);

            GIInfoType interface_type = interface_info.type();

            if (interface_type == GI_INFO_TYPE_STRUCT &&
                g_struct_info_is_foreign(interface_info.as<GIStructInfo>()))
                return gjs_struct_foreign_release_gi_argument(
                    context, transfer, interface_info, arg);

            if (interface_type == GI_INFO_TYPE_ENUM || interface_type == GI_INFO_TYPE_FLAGS)
                return true;

            /* Anything else is a pointer */
            if (!gjs_arg_get<void*>(arg))
                return true;

            GType gtype = g_registered_type_info_get_g_type(
                interface_info.as<GIRegisteredTypeInfo>());
            if (G_TYPE_IS_INSTANTIATABLE(gtype) ||
                G_TYPE_IS_INTERFACE(gtype))
                gtype = G_TYPE_FROM_INSTANCE(gjs_arg_get<GTypeInstance*>(arg));

            gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                              "gtype of INTERFACE is %s", g_type_name(gtype));

            // In gjs_value_from_gi_argument we handle Struct/Union types
            // without a registered GType, but here we are specifically handling
            // a GIArgument that *owns* its value, and that is nonsensical for
            // such types, so we don't have to worry about it.

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                if (!is_transfer_in_nothing(transfer, flags))
                    g_clear_object(&gjs_arg_member<GObject*>(arg));
            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if (!is_transfer_in_nothing(transfer, flags))
                    g_clear_pointer(&gjs_arg_member<GParamSpec*>(arg),
                                    g_param_spec_unref);
            } else if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                g_clear_pointer(&gjs_arg_member<GClosure*>(arg),
                                g_closure_unref);
            } else if (g_type_is_a(gtype, G_TYPE_VALUE)) {
                /* G_TYPE_VALUE is-a G_TYPE_BOXED, but we special case it */
                if (g_type_info_is_pointer (type_info))
                    g_boxed_free(gtype, gjs_arg_steal<void*>(arg));
                else
                    g_clear_pointer(&gjs_arg_member<GValue*>(arg),
                                    g_value_unset);
            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (!is_transfer_in_nothing(transfer, flags))
                    g_boxed_free(gtype, gjs_arg_steal<void*>(arg));
            } else if (g_type_is_a(gtype, G_TYPE_VARIANT)) {
                if (!is_transfer_in_nothing(transfer, flags))
                    g_clear_pointer(&gjs_arg_member<GVariant*>(arg),
                                    g_variant_unref);
            } else if (gtype == G_TYPE_NONE) {
                if (!is_transfer_in_nothing(transfer, flags)) {
                    gjs_throw(context,
                              "Don't know how to release GIArgument: not an "
                              "object or boxed type");
                    return false;
                }
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                if (!is_transfer_in_nothing(transfer, flags)) {
                    auto* priv =
                        FundamentalPrototype::for_gtype(context, gtype);
                    priv->call_unref_function(gjs_arg_steal<void*>(arg));
                }
            } else {
                gjs_throw(context, "Unhandled GType %s releasing GIArgument",
                          g_type_name(gtype));
                return false;
            }
            return true;
        }

    case GI_TYPE_TAG_ARRAY:
    {
        GIArrayType array_type = g_type_info_get_array_type(type_info);

        if (!gjs_arg_get<void*>(arg)) {
            /* OK */
        } else if (array_type == GI_ARRAY_TYPE_C) {
            GjsAutoTypeInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            GITypeTag element_type;

            element_type = g_type_info_get_tag(param_info);

            switch (element_type) {
            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
                if (transfer == GI_TRANSFER_CONTAINER)
                    g_clear_pointer(&gjs_arg_member<void*>(arg),
                                    g_free);
                else
                    g_clear_pointer(&gjs_arg_member<GStrv>(arg),
                                    g_strfreev);
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
                g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
                break;

            case GI_TYPE_TAG_INTERFACE:
                if (!g_type_info_is_pointer(param_info)) {
                    GjsAutoBaseInfo interface_info =
                        g_type_info_get_interface(param_info);
                    GIInfoType info_type = interface_info.type();
                    if (info_type == GI_INFO_TYPE_STRUCT ||
                        info_type == GI_INFO_TYPE_UNION) {
                        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
                        break;
                    }
                }
                [[fallthrough]];
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_GHASH:
            case GI_TYPE_TAG_ERROR: {
                GITransfer element_transfer = transfer;

                if (argument_type != GJS_ARGUMENT_ARGUMENT &&
                    transfer != GI_TRANSFER_EVERYTHING)
                    element_transfer = GI_TRANSFER_NOTHING;

                if (g_type_info_is_zero_terminated(type_info)) {
                    return gjs_gi_argument_release_array_internal<
                        ArrayReleaseType::ZERO_TERMINATED>(
                        context, element_transfer,
                        flags | GjsArgumentFlags::ARG_OUT, param_info, 0, arg);
                } else {
                    return gjs_gi_argument_release_array_internal<
                        ArrayReleaseType::EXPLICIT_LENGTH>(
                        context, element_transfer,
                        flags | GjsArgumentFlags::ARG_OUT, param_info,
                        g_type_info_get_array_fixed_size(type_info), arg);
                }
            }

            case GI_TYPE_TAG_VOID:
            default:
                gjs_throw(context,
                          "Releasing a C array with explicit length, that was nested"
                          "inside another container. This is not supported (and will leak)");
                return false;
            }
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GITypeTag element_type;

            GjsAutoTypeInfo param_info =
                g_type_info_get_param_type(type_info, 0);
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
                g_clear_pointer(&gjs_arg_member<GArray*>(arg), g_array_unref);
                break;

            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_INTERFACE:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_GHASH:
            case GI_TYPE_TAG_ERROR: {
                GjsAutoPointer<GArray, GArray, g_array_unref> array =
                    gjs_arg_steal<GArray*>(arg);

                if (transfer != GI_TRANSFER_CONTAINER &&
                    type_needs_out_release(param_info, element_type)) {
                    guint i;

                    for (i = 0; i < array->len; i++) {
                        GIArgument arg_iter;

                        gjs_arg_set(&arg_iter,
                                    g_array_index(array, gpointer, i));
                        if (!gjs_g_arg_release_internal(
                                context, transfer, param_info, element_type,
                                GJS_ARGUMENT_ARRAY_ELEMENT, flags, &arg_iter))
                            return false;
                    }
                }

                break;
            }

            case GI_TYPE_TAG_VOID:
            default:
                gjs_throw(context,
                          "Don't know how to release GArray element-type %d",
                          element_type);
                return false;
            }

        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            g_clear_pointer(&gjs_arg_member<GByteArray*>(arg),
                            g_byte_array_unref);
        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GjsAutoTypeInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            GjsAutoPointer<GPtrArray, GPtrArray, g_ptr_array_unref> array =
                gjs_arg_steal<GPtrArray*>(arg);

            if (transfer != GI_TRANSFER_CONTAINER) {
                guint i;

                for (i = 0; i < array->len; i++) {
                    GIArgument arg_iter;

                    gjs_arg_set(&arg_iter, g_ptr_array_index(array, i));
                    if (!gjs_gi_argument_release(context, transfer, param_info,
                                                 flags, &arg_iter))
                        return false;
                }
            }
        } else {
            g_assert_not_reached();
        }
        break;
    }

    case GI_TYPE_TAG_GLIST:
        return gjs_g_arg_release_g_list<GList>(context, transfer, type_info,
                                               flags, arg);

    case GI_TYPE_TAG_GSLIST:
        return gjs_g_arg_release_g_list<GSList>(context, transfer, type_info,
                                                flags, arg);

    case GI_TYPE_TAG_GHASH:
        if (gjs_arg_get<GHashTable*>(arg)) {
            GjsAutoPointer<GHashTable, GHashTable, g_hash_table_destroy>
                hash_table = gjs_arg_steal<GHashTable*>(arg);
            if (transfer == GI_TRANSFER_CONTAINER)
                g_hash_table_remove_all(hash_table);
            else {
                GHR_closure c = {context,  nullptr, nullptr,
                                 transfer, flags,   false};

                c.key_param_info = g_type_info_get_param_type(type_info, 0);
                g_assert(c.key_param_info != nullptr);
                c.val_param_info = g_type_info_get_param_type(type_info, 1);
                g_assert(c.val_param_info != nullptr);

                g_hash_table_foreach_steal(hash_table, gjs_ghr_helper, &c);

                if (c.failed)
                    return false;
            }
        }
        break;

    default:
        g_warning("Unhandled type %s releasing GIArgument",
                  g_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

bool gjs_gi_argument_release(JSContext* cx, GITransfer transfer,
                             GITypeInfo* type_info, GjsArgumentFlags flags,
                             GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING &&
        !is_transfer_in_nothing(transfer, flags))
        return true;

    GITypeTag type_tag = g_type_info_get_tag(type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument %s out param or return value",
                      g_type_tag_to_string(type_tag));

    return gjs_g_arg_release_internal(cx, transfer, type_info, type_tag,
                                      GJS_ARGUMENT_ARGUMENT, flags, arg);
}

bool gjs_gi_argument_release_in_arg(JSContext* cx, GITransfer transfer,
                                    GITypeInfo* type_info,
                                    GjsArgumentFlags flags, GIArgument* arg) {
    /* GI_TRANSFER_EVERYTHING: we don't own the argument anymore.
     * GI_TRANSFER_CONTAINER:
     * - non-containers: treated as GI_TRANSFER_EVERYTHING
     * - containers: See FIXME in gjs_array_to_g_list(); currently
     *   an error and we won't get here.
     */
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    GITypeTag type_tag = g_type_info_get_tag(type_info);

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Releasing GIArgument %s in param",
                      g_type_tag_to_string(type_tag));

    if (type_needs_release (type_info, type_tag))
        return gjs_g_arg_release_internal(cx, transfer, type_info, type_tag,
                                          GJS_ARGUMENT_ARGUMENT, flags, arg);

    return true;
}

bool gjs_gi_argument_release_in_array(JSContext* context, GITransfer transfer,
                                      GITypeInfo* type_info, unsigned length,
                                      GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array in param");

    GjsAutoTypeInfo param_type = g_type_info_get_param_type(type_info, 0);
    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::EXPLICIT_LENGTH>(context, GI_TRANSFER_EVERYTHING,
                                           GjsArgumentFlags::ARG_IN, param_type,
                                           length, arg);
}

bool gjs_gi_argument_release_in_array(JSContext* context, GITransfer transfer,
                                      GITypeInfo* type_info, GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array in param");

    GjsAutoTypeInfo param_type = g_type_info_get_param_type(type_info, 0);
    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::ZERO_TERMINATED>(context, GI_TRANSFER_EVERYTHING,
                                           GjsArgumentFlags::ARG_IN, param_type,
                                           0, arg);
}

bool gjs_gi_argument_release_out_array(JSContext* context, GITransfer transfer,
                                       GITypeInfo* type_info, unsigned length,
                                       GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    GITransfer element_transfer = transfer == GI_TRANSFER_CONTAINER
                                      ? GI_TRANSFER_NOTHING
                                      : GI_TRANSFER_EVERYTHING;

    GjsAutoTypeInfo param_type = g_type_info_get_param_type(type_info, 0);
    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::EXPLICIT_LENGTH>(context, element_transfer,
                                           GjsArgumentFlags::ARG_OUT,
                                           param_type, length, arg);
}

bool gjs_gi_argument_release_out_array(JSContext* context, GITransfer transfer,
                                       GITypeInfo* type_info, GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    GITransfer element_transfer = transfer == GI_TRANSFER_CONTAINER
                                      ? GI_TRANSFER_NOTHING
                                      : GI_TRANSFER_EVERYTHING;

    GjsAutoTypeInfo param_type = g_type_info_get_param_type(type_info, 0);
    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::ZERO_TERMINATED>(context, element_transfer,
                                           GjsArgumentFlags::ARG_OUT,
                                           param_type, 0, arg);
}
