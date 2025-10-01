/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2020 Canonical, Ltd.

#include <config.h>

#include <inttypes.h>
#include <stdint.h>
#include <string.h>  // for strcmp, strlen, memcpy

#include <algorithm>  // for none_of
#include <array>
#include <functional>  // for mem_fn
#include <string>
#include <utility>  // for move
#include <vector>

#include <girepository/girepository.h>
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
#include <mozilla/Maybe.h>
#include <mozilla/Span.h>
#include <mozilla/Unused.h>

#include "gi/arg-inl.h"
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/foreign.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/info.h"
#include "gi/interface.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/struct.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/enum-utils.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

using mozilla::Maybe, mozilla::Nothing, mozilla::Some;

// This file contains marshalling functions for GIArgument. It is divided into
// three sections, with some helper functions coming first.
// 1. "To" marshallers - JS::Value or other JS data structure to GIArgument or
//    pointer, used for in arguments
// 2. "From" marshallers - GIArgument or pointer to JS::Value or other JS data
//    structure, used for out arguments and return values
// 3. "Release" marshallers - used when cleaning up GIArguments after a C
//    function call

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_g_arg_release_internal(JSContext*, GITransfer,
                                       const GI::TypeInfo, GITypeTag,
                                       GjsArgumentType, GjsArgumentFlags,
                                       GIArgument*);
static void throw_invalid_argument(JSContext*, JS::HandleValue,
                                   const GI::TypeInfo, const char*,
                                   GjsArgumentType);

bool _gjs_flags_value_is_valid(JSContext* cx, GType gtype, int64_t value) {
    /* Do proper value check for flags with GType's */
    if (gtype != G_TYPE_NONE) {
        Gjs::AutoTypeClass<GFlagsClass> gflags_class{gtype};
        uint32_t tmpval = static_cast<uint32_t>(value);

        /* check all bits are valid bits for the flag and is a 32 bit flag*/
        if ((tmpval &= gflags_class->mask) != value) { /* Not a guint32 with invalid mask values*/
            gjs_throw(cx, "0x%" PRIx64 " is not a valid value for flags %s",
                      value, g_type_name(gtype));
            return false;
        }
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool _gjs_enum_value_is_valid(JSContext* cx, const GI::EnumInfo info,
                                     int64_t value) {
    GI::EnumInfo::ValuesIterator values = info.values();
    if (std::none_of(values.begin(), values.end(),
                     [value](GI::AutoValueInfo info) {
                         return info.value() == value;
                     })) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for enumeration %s",
                  value, info.name());
        return false;
    }

    return true;
}

/* Check if an argument of the given needs to be released if we created it
 * from a JS value to pass it into a function and aren't transferring ownership.
 */
[[nodiscard]]
static bool type_needs_release(const GI::TypeInfo& type_info, GITypeTag tag) {
    switch (tag) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_UTF8:
            return true;

        case GI_TYPE_TAG_INTERFACE: {
            GI::AutoBaseInfo interface_info{type_info.interface()};
            if (auto reg_info =
                    interface_info.as<GI::InfoTag::REGISTERED_TYPE>()) {
                GType gtype = reg_info->gtype();

                if (g_type_is_a(gtype, G_TYPE_CLOSURE))
                    return true;
                else if (g_type_is_a(gtype, G_TYPE_VALUE))
                    return true;
            }
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
[[nodiscard]]
static bool type_needs_out_release(const GI::TypeInfo& type_info,
                                   GITypeTag tag) {
    switch (tag) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_UTF8:
            return true;

        case GI_TYPE_TAG_INTERFACE: {
            GI::AutoBaseInfo interface_info{type_info.interface()};
            if (interface_info.is_object())
                return true;
            if (interface_info.is_struct() || interface_info.is_union())
                return type_info.is_pointer();

            return false;
        }

        default:
            return false;
    }
}

///// "TO" MARSHALLERS /////////////////////////////////////////////////////////
// These marshaller functions are responsible for converting JS values to the
// required GIArgument type, for the in parameters of a C function call.

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_to_g_list(
    JSContext* cx, JS::HandleValue value, const GI::TypeInfo type_info,
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

    GI::AutoTypeInfo element_type{type_info.element_type()};

    GITypeTag element_tag = element_type.tag();
    g_assert(!GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "use basic_array_to_linked_list() instead");

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release(element_type, element_tag)) {
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
        if (!gjs_value_to_gi_argument(cx, elem, element_type,
                                      GJS_ARGUMENT_LIST_ELEMENT, transfer,
                                      &elem_arg)) {
            return false;
        }

        void* hash_pointer = element_type.hash_pointer_from_argument(&elem_arg);

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

template <typename IntTag>
GJS_JSAPI_RETURN_CONVENTION static bool hashtable_int_key(JSContext* cx,
                                                          JS::HandleValue value,
                                                          void** pointer_out) {
    using IntType = Gjs::Tag::RealT<IntTag>;
    static_assert(std::is_integral_v<IntType>, "Need an integer");
    bool out_of_range = false;

    Gjs::Tag::JSValueContainingT<IntTag> i;
    if (!Gjs::js_value_to_c_checked<IntType>(cx, value, &i, &out_of_range))
        return false;

    if (out_of_range) {
        gjs_throw(cx, "value is out of range for hash table key of type %s",
                  Gjs::static_type_name<IntTag>());
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
                      gi_type_tag_to_string(type_tag));

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
        Gjs::AutoChar cstr;
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
    // FIXME: The above four could be supported, but are currently not. The ones
    // below cannot be key types in a regular JS object; we would need to allow
    // marshalling Map objects into GHashTables to support those, as well as
    // refactoring this function to take GITypeInfo* and splitting out the
    // marshalling for basic types into a different function.
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
                  gi_type_tag_to_string(type_tag));
        unsupported = true;
        break;
    }

    if (G_UNLIKELY(unsupported)) {
        gjs_throw(cx, "Type %s not supported for hash table keys",
                  gi_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

template <typename TAG>
[[nodiscard]] static Gjs::Tag::RealT<TAG>* heap_value_new_from_arg(
    GIArgument* val_arg) {
    auto* heap_val = g_new(Gjs::Tag::RealT<TAG>, 1);
    *heap_val = gjs_arg_get<TAG>(val_arg);

    return heap_val;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_object_to_g_hash(JSContext* context, JS::HandleObject props,
                                 const GI::TypeInfo type_info,
                                 GITransfer transfer, GHashTable** hash_p) {
    size_t id_ix, id_len;

    g_assert(props && "Property bag cannot be null");

    GI::AutoTypeInfo key_type{type_info.key_type()};
    GI::AutoTypeInfo value_type{type_info.value_type()};
    GITypeTag key_tag = key_type.tag();
    GITypeTag val_type = value_type.tag();

    g_assert(
        (!GI_TYPE_TAG_IS_BASIC(key_tag) || !GI_TYPE_TAG_IS_BASIC(val_type)) &&
        "use gjs_value_to_basic_ghash_gi_argument() instead");

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (type_needs_release(key_type, key_tag) ||
            type_needs_release(value_type, val_type)) {
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

    Gjs::AutoPointer<GHashTable, GHashTable, g_hash_table_destroy> result{
        create_hash_table_for_key_type(key_tag)};

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
            !gjs_value_to_gi_argument(context, val_js, value_type, nullptr,
                                      GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                      GjsArgumentFlags::MAY_BE_NULL, &val_arg))
            return false;

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
            val_ptr = value_type.hash_pointer_from_argument(&val_arg);
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

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION static bool js_value_to_c_strict(
    JSContext* cx, JS::HandleValue value, Gjs::Tag::RealT<TAG>* out) {
    if constexpr (Gjs::type_has_js_getter<TAG,
                                          Gjs::HolderMode::ContainingType>())
        return Gjs::js_value_to_c<TAG>(cx, value, out);

    Gjs::Tag::JSValueContainingT<TAG> v;
    bool ret = Gjs::js_value_to_c<TAG>(cx, value, &v);
    *out = v;

    return ret;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_to_auto_array(
    JSContext* cx, JS::Value array_value, size_t length, void** arr_p) {
    using RealT = Gjs::Tag::RealT<T>;

    JS::RootedObject array(cx, array_value.toObjectOrNull());
    JS::RootedValue elem(cx);

    // Add one so we're always zero terminated
    Gjs::SmartPointer<RealT> result{array_allocate<RealT>(length + 1)};

    for (size_t i = 0; i < length; ++i) {
        elem = JS::UndefinedValue();

        if (!JS_GetElement(cx, array, i, &elem)) {
            gjs_throw(cx, "Missing array element %" G_GSIZE_FORMAT, i);
            return false;
        }

        if (!js_value_to_c_strict<T>(cx, elem, &result[i])) {
            gjs_throw(cx, "Invalid element in %s array",
                      Gjs::static_type_name<T>());
            return false;
        }
    }

    *arr_p = result.release();

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

            *arr_p = Gjs::js_chars_to_glib(std::move(result)).release();
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
                      gi_type_tag_to_string(element_type));
            return false;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool array_to_basic_c_array(JSContext* cx, JS::HandleValue v_array,
                                   size_t length, GITypeTag element_tag,
                                   void** array_out) {
    // Always one extra element, to cater for null terminated arrays
    Gjs::AutoPointer<void*> array{array_allocate<void*>(length + 1)};

    JS::RootedObject array_obj{cx, &v_array.toObject()};
    JS::RootedValue elem{cx};
    for (size_t ix = 0; ix < length; ix++) {
        GIArgument arg;
        gjs_arg_unset(&arg);

        elem.setUndefined();
        if (!JS_GetElement(cx, array_obj, ix, &elem)) {
            gjs_throw(cx, "Missing array element %zu", ix);
            return false;
        }

        if (!gjs_value_to_basic_gi_argument(cx, elem, element_tag, &arg,
                                            nullptr, GJS_ARGUMENT_ARRAY_ELEMENT,
                                            GjsArgumentFlags::NONE)) {
            gjs_throw(cx, "Invalid element in array");
            return false;
        }

        array[ix] = gjs_arg_get<void*>(&arg);
    }

    *array_out = array.release();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_ptrarray(JSContext* context, JS::Value array_value,
                                  unsigned length, GITransfer transfer,
                                  const GI::TypeInfo param_info, void** arr_p) {
    unsigned int i;
    JS::RootedObject array_obj(context, array_value.toObjectOrNull());
    JS::RootedValue elem(context);

    /* Always one extra element, to cater for null terminated arrays */
    Gjs::AutoPointer<void*> array{array_allocate<void*>(length + 1)};

    for (i = 0; i < length; i++) {
        GIArgument arg;
        gjs_arg_unset(&arg);

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
                                    unsigned length,
                                    const GI::TypeInfo param_info,
                                    size_t param_size, void** arr_p) {
    g_assert((param_size > 0) &&
             "Only flat arrays of elements of known size are supported");

    Gjs::AutoPointer<uint8_t> flat_array{g_new0(uint8_t, param_size * length)};

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

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_basic_array(JSContext* cx, JS::HandleValue v_array,
                                     size_t length,
                                     GITypeTag element_storage_type,
                                     void** array_out) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_storage_type));

    switch (element_storage_type) {
        case GI_TYPE_TAG_UTF8:
            return gjs_array_to_strv(cx, v_array, length, array_out);
        case GI_TYPE_TAG_FILENAME:
            return array_to_basic_c_array(cx, v_array, length,
                                          element_storage_type, array_out);
        case GI_TYPE_TAG_BOOLEAN:
            return gjs_array_to_auto_array<Gjs::Tag::GBoolean>(
                cx, v_array, length, array_out);
        case GI_TYPE_TAG_UNICHAR:
            return gjs_array_to_auto_array<char32_t>(cx, v_array, length,
                                                     array_out);
        case GI_TYPE_TAG_UINT8:
            return gjs_array_to_auto_array<uint8_t>(cx, v_array, length,
                                                    array_out);
        case GI_TYPE_TAG_INT8:
            return gjs_array_to_auto_array<int8_t>(cx, v_array, length,
                                                   array_out);
        case GI_TYPE_TAG_UINT16:
            return gjs_array_to_auto_array<uint16_t>(cx, v_array, length,
                                                     array_out);
        case GI_TYPE_TAG_INT16:
            return gjs_array_to_auto_array<int16_t>(cx, v_array, length,
                                                    array_out);
        case GI_TYPE_TAG_UINT32:
            return gjs_array_to_auto_array<uint32_t>(cx, v_array, length,
                                                     array_out);
        case GI_TYPE_TAG_INT32:
            return gjs_array_to_auto_array<int32_t>(cx, v_array, length,
                                                    array_out);
        case GI_TYPE_TAG_INT64:
            return gjs_array_to_auto_array<int64_t>(cx, v_array, length,
                                                    array_out);
        case GI_TYPE_TAG_UINT64:
            return gjs_array_to_auto_array<uint64_t>(cx, v_array, length,
                                                     array_out);
        case GI_TYPE_TAG_FLOAT:
            return gjs_array_to_auto_array<float>(cx, v_array, length,
                                                  array_out);
        case GI_TYPE_TAG_DOUBLE:
            return gjs_array_to_auto_array<double>(cx, v_array, length,
                                                   array_out);
        case GI_TYPE_TAG_GTYPE:
            return gjs_array_to_auto_array<Gjs::Tag::GType>(cx, v_array, length,
                                                            array_out);
        case GI_TYPE_TAG_VOID:
            gjs_throw(cx, "Unhandled array element type %d",
                      element_storage_type);
            return false;
        default:
            g_assert_not_reached();
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_to_array(JSContext* context, JS::HandleValue array_value,
                               size_t length, GITransfer transfer,
                               const GI::TypeInfo param_info, void** arr_p) {
    GITypeTag element_type = param_info.storage_type();

    if (GI_TYPE_TAG_IS_BASIC(element_type)) {
        return gjs_array_to_basic_array(context, array_value, length,
                                        element_type, arr_p);
    }

    switch (element_type) {
    case GI_TYPE_TAG_INTERFACE:
        if (!param_info.is_pointer()) {
            GI::AutoBaseInfo interface_info{param_info.interface()};
            if (auto reg_info =
                    interface_info.as<GI::InfoTag::REGISTERED_TYPE>();
                reg_info && reg_info->is_g_value()) {
                // Special case for GValue "flat arrays", this could also
                // using the generic case, but if we do so we're leaking
                // atm.
                return gjs_array_to_auto_array<GValue>(context, array_value,
                                                       length, arr_p);
            }

            size_t element_size =
                gjs_type_get_element_size(param_info.tag(), param_info);
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
        return gjs_array_to_ptrarray(context,
                                     array_value,
                                     length,
                                     transfer == GI_TRANSFER_CONTAINER ? GI_TRANSFER_NOTHING : transfer,
                                     param_info,
                                     arr_p);
    default:
        // Basic types already handled in gjs_array_to_basic_array()
        gjs_throw(context, "Unhandled array element type %d", element_type);
        return false;
    }
}

static inline size_t basic_type_element_size(GITypeTag element_tag) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag));

    switch (element_tag) {
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
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            return sizeof(char*);
        default:
            g_return_val_if_reached(0);
    }
}

static inline void set_arg_from_carray_element(GIArgument* arg,
                                               GITypeTag element_tag,
                                               void* value) {
    switch (element_tag) {
        case GI_TYPE_TAG_BOOLEAN:
            gjs_arg_set<gboolean>(arg, *static_cast<gboolean*>(value));
            break;
        case GI_TYPE_TAG_INT8:
            gjs_arg_set<int8_t>(arg, *static_cast<int8_t*>(value));
            break;
        case GI_TYPE_TAG_UINT8:
            gjs_arg_set<uint8_t>(arg, *static_cast<uint8_t*>(value));
            break;
        case GI_TYPE_TAG_INT16:
            gjs_arg_set<int16_t>(arg, *static_cast<int16_t*>(value));
            break;
        case GI_TYPE_TAG_UINT16:
            gjs_arg_set<uint16_t>(arg, *static_cast<uint16_t*>(value));
            break;
        case GI_TYPE_TAG_INT32:
            gjs_arg_set<int32_t>(arg, *static_cast<int32_t*>(value));
            break;
        case GI_TYPE_TAG_UINT32:
            gjs_arg_set<uint32_t>(arg, *static_cast<uint32_t*>(value));
            break;
        case GI_TYPE_TAG_INT64:
            gjs_arg_set<int64_t>(arg, *static_cast<int64_t*>(value));
            break;
        case GI_TYPE_TAG_UINT64:
            gjs_arg_set<uint64_t>(arg, *static_cast<uint64_t*>(value));
            break;
        case GI_TYPE_TAG_FLOAT:
            gjs_arg_set<float>(arg, *static_cast<float*>(value));
            break;
        case GI_TYPE_TAG_DOUBLE:
            gjs_arg_set<double>(arg, *static_cast<double*>(value));
            break;
        case GI_TYPE_TAG_INTERFACE:
            gjs_arg_set(arg, value);
            break;
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_UNICHAR:
            // non interface tag support handled elsewhere
            g_assert_not_reached();
    }
}

size_t gjs_type_get_element_size(GITypeTag element_type,
                                 const GI::TypeInfo type_info) {
    if (type_info.is_pointer() && element_type != GI_TYPE_TAG_ARRAY)
        return sizeof(void*);

    switch (element_type) {
    case GI_TYPE_TAG_GLIST:
        return sizeof(GSList);
    case GI_TYPE_TAG_GSLIST:
        return sizeof(GList);
    case GI_TYPE_TAG_ERROR:
        return sizeof(GError);
    case GI_TYPE_TAG_INTERFACE: {
        GI::AutoBaseInfo interface_info{type_info.interface()};
        if (interface_info.is_enum_or_flags())
            return sizeof(unsigned);
        if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>())
            return struct_info->size();
        if (auto union_info = interface_info.as<GI::InfoTag::UNION>())
            return union_info->size();
        return 0;
    }

    case GI_TYPE_TAG_GHASH:
        return sizeof(void*);

    case GI_TYPE_TAG_ARRAY:
        if (type_info.array_type() == GI_ARRAY_TYPE_C) {
            if (!type_info.array_length_index())
                return sizeof(void*);

            GI::AutoTypeInfo element_type{type_info.element_type()};
            return gjs_type_get_element_size(element_type.tag(), element_type);
        }

        return sizeof(void*);

    case GI_TYPE_TAG_VOID:
        break;

    default:
        return basic_type_element_size(element_type);
    }

    g_return_val_if_reached(0);
}

static GArray* garray_new_for_storage_type(unsigned length,
                                           GITypeTag storage_type,
                                           const GI::TypeInfo type_info) {
    size_t element_size = gjs_type_get_element_size(storage_type, type_info);
    return g_array_sized_new(true, false, element_size, length);
}

static GArray* garray_new_for_basic_type(unsigned length, GITypeTag tag) {
    size_t element_size = basic_type_element_size(tag);
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

static void throw_invalid_argument(JSContext* context, JS::HandleValue value,
                                   const GI::TypeInfo arginfo,
                                   const char* arg_name,
                                   GjsArgumentType arg_type) {
    Gjs::AutoChar display_name{gjs_argument_display_name(arg_name, arg_type)};

    gjs_throw(context, "Expected type %s for %s but got type '%s'",
              arginfo.display_string(), display_name.get(),
              JS::InformalValueTypeName(value));
}

GJS_JSAPI_RETURN_CONVENTION
static bool throw_invalid_argument_tag(JSContext* cx, JS::HandleValue value,
                                       GITypeTag type_tag, const char* arg_name,
                                       GjsArgumentType arg_type) {
    Gjs::AutoChar display_name{gjs_argument_display_name(arg_name, arg_type)};

    gjs_throw(cx, "Expected type %s for %s but got type '%s'",
              gi_type_tag_to_string(type_tag), display_name.get(),
              JS::InformalValueTypeName(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool throw_invalid_interface_argument(JSContext* cx,
                                             JS::HandleValue value,
                                             const GI::BaseInfo interface_info,
                                             const char* arg_name,
                                             GjsArgumentType arg_type) {
    Gjs::AutoChar display_name{gjs_argument_display_name(arg_name, arg_type)};

    gjs_throw(cx, "Expected type %s for %s but got type '%s'",
              interface_info.type_string(), display_name.get(),
              JS::InformalValueTypeName(value));
    return false;
}

bool gjs_array_to_basic_explicit_array(
    JSContext* cx, JS::HandleValue value, GITypeTag element_tag,
    const char* arg_name, GjsArgumentType arg_type, GjsArgumentFlags flags,
    void** contents_out, size_t* length_out) {
    g_assert(contents_out && length_out && "forgot out parameter");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting argument '%s' JS value %s to C array",
                      arg_name, gjs_debug_value(value).c_str());

    if ((value.isNull() && !(flags & GjsArgumentFlags::MAY_BE_NULL)) ||
        (!value.isString() && !value.isObjectOrNull())) {
        return throw_invalid_argument_tag(cx, value, element_tag, arg_name,
                                          arg_type);
    }

    if (value.isNull()) {
        *contents_out = nullptr;
        *length_out = 0;
        return true;
    }

    if (value.isString()) {
        // Allow strings as int8/uint8/int16/uint16 arrays
        JS::RootedString str{cx, value.toString()};
        return gjs_string_to_intarray(cx, str, element_tag, contents_out,
                                      length_out);
    }

    JS::RootedObject array_obj{cx, &value.toObject()};
    if (JS_IsUint8Array(array_obj) &&
        (element_tag == GI_TYPE_TAG_INT8 || element_tag == GI_TYPE_TAG_UINT8)) {
        GBytes* bytes = gjs_byte_array_get_bytes(array_obj);
        *contents_out = g_bytes_unref_to_data(bytes, length_out);
        return true;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    bool found_length;
    if (!JS_HasPropertyById(cx, array_obj, atoms.length(), &found_length))
        return false;
    if (found_length) {
        uint32_t length;

        if (!gjs_object_require_converted_property(cx, array_obj, nullptr,
                                                   atoms.length(), &length)) {
            return false;
        }

        // For basic types, type tag == storage type
        if (!gjs_array_to_basic_array(cx, value, length, element_tag,
                                      contents_out))
            return false;

        *length_out = length;
        return true;
    }

    return throw_invalid_argument_tag(cx, value, element_tag, arg_name,
                                      arg_type);
}

bool gjs_array_to_explicit_array(JSContext* context, JS::HandleValue value,
                                 const GI::TypeInfo type_info,
                                 const char* arg_name, GjsArgumentType arg_type,
                                 GITransfer transfer, GjsArgumentFlags flags,
                                 void** contents, size_t* length_p) {
    GI::AutoTypeInfo element_type{type_info.element_type()};
    GITypeTag element_tag = element_type.tag();

    if (GI_TYPE_TAG_IS_BASIC(element_tag)) {
        return gjs_array_to_basic_explicit_array(context, value, element_tag,
                                                 arg_name, arg_type, flags,
                                                 contents, length_p);
    }

    bool found_length;

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to C array, transfer %d",
        arg_name, gjs_debug_value(value).c_str(), transfer);

    if ((value.isNull() && !(flags & GjsArgumentFlags::MAY_BE_NULL)) ||
        (!value.isString() && !value.isObjectOrNull())) {
        throw_invalid_argument(context, value, element_type, arg_name,
                               arg_type);
        return false;
    }

    if (value.isNull()) {
        *contents = NULL;
        *length_p = 0;
    } else if (value.isString()) {
        /* Allow strings as int8/uint8/int16/uint16 arrays */
        JS::RootedString str(context, value.toString());
        if (!gjs_string_to_intarray(context, str, element_tag, contents, length_p))
            return false;
    } else {
        JS::RootedObject array_obj(context, &value.toObject());
        if (JS_IsUint8Array(array_obj) && (element_tag == GI_TYPE_TAG_INT8 ||
                                           element_tag == GI_TYPE_TAG_UINT8)) {
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
                if (!gjs_array_to_array(context, value, length, transfer,
                                        element_type, contents))
                    return false;

                *length_p = length;
            }
        } else {
            throw_invalid_argument(context, value, element_type, arg_name,
                                   arg_type);
            return false;
        }
    }

    return true;
}

static void intern_gdk_atom(const char* name, GIArgument* ret) {
    GI::Repository repo;
    GI::AutoFunctionInfo atom_intern_fun{
        repo.find_by_name<GI::InfoTag::FUNCTION>("Gdk", "atom_intern").value()};

    std::vector<GIArgument> atom_intern_args{2};

    /* Can only store char * in GIArgument. First argument to gdk_atom_intern
     * is const char *, string isn't modified. */
    gjs_arg_set(&atom_intern_args[0], name);
    gjs_arg_set(&atom_intern_args[1], false);

    mozilla::Unused << atom_intern_fun.invoke(atom_intern_args, {}, ret);
}

static bool value_to_gdk_atom_gi_argument_internal(JSContext* cx,
                                                   JS::HandleValue value,
                                                   GIArgument* arg,
                                                   const char* arg_name,
                                                   GjsArgumentType arg_type) {
    if (!value.isNull() && !value.isString()) {
        Gjs::AutoChar display_name{
            gjs_argument_display_name(arg_name, arg_type)};
        gjs_throw(cx, "Expected type String or null for %s but got type '%s'",
                  display_name.get(), JS::InformalValueTypeName(value));
        return false;
    }

    if (value.isNull()) {
        intern_gdk_atom("NONE", arg);
        return true;
    }

    JS::RootedString str{cx, value.toString()};
    JS::UniqueChars name{JS_EncodeStringToUTF8(cx, str)};
    if (!name)
        return false;

    intern_gdk_atom(name.get(), arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool value_to_interface_gi_argument_internal(
    JSContext* cx, JS::HandleValue value, const GI::BaseInfo interface_info,
    GITransfer transfer, GIArgument* arg, const char* arg_name,
    GjsArgumentType arg_type, GjsArgumentFlags flags) {
    auto reg_type = interface_info.as<GI::InfoTag::REGISTERED_TYPE>();
    if (reg_type && reg_type->is_gdk_atom()) {
        return value_to_gdk_atom_gi_argument_internal(cx, value, arg, arg_name,
                                                      arg_type);
    }

    GType gtype = reg_type.map(std::mem_fn(&GI::RegisteredTypeInfo::gtype))
                      .valueOr(G_TYPE_NONE);

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
            gjs_arg_unset(arg);
            return false;
        }

        gjs_arg_set(arg, g_boxed_copy(G_TYPE_VALUE, &gvalue));
        return true;

    } else if (interface_info.is_enum_or_flags() == value.isObjectOrNull()) {
        // for enum/flags, object/null are invalid. for everything else,
        // everything except object/null is invalid.
        return throw_invalid_interface_argument(cx, value, interface_info,
                                                arg_name, arg_type);

    } else if (value.isNull()) {
        gjs_arg_set(arg, nullptr);
        return true;

    } else if (value.isObject()) {
        JS::RootedObject obj(cx, &value.toObject());
        auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
        if (struct_info && struct_info->is_gtype_struct()) {
            GType actual_gtype;
            if (!gjs_gtype_get_actual_gtype(cx, obj, &actual_gtype))
                return false;

            if (actual_gtype == G_TYPE_NONE) {
                return throw_invalid_interface_argument(
                    cx, value, interface_info, arg_name, arg_type);
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
        if (struct_info && gtype == G_TYPE_NONE && !struct_info->is_foreign()) {
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

        if (struct_info && !g_type_is_a(gtype, G_TYPE_CLOSURE)) {
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
                if (!StructBase::typecheck(cx, obj, interface_info)) {
                    gjs_arg_unset(arg);
                    return false;
                }
                return StructBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);
            }
        }

        if (interface_info.is_union()) {
            if (!UnionBase::typecheck(cx, obj, interface_info)) {
                gjs_arg_unset(arg);
                return false;
            }
            return UnionBase::transfer_to_gi_argument(
                cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);
        }

        if (gtype != G_TYPE_NONE) {
            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                return ObjectBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);

            } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if (!gjs_typecheck_param(cx, obj, gtype, true)) {
                    gjs_arg_unset(arg);
                    return false;
                }
                gjs_arg_set(arg, gjs_g_param_from_param(cx, obj));
                if (transfer != GI_TRANSFER_NOTHING)
                    g_param_spec_ref(gjs_arg_get<GParamSpec*>(arg));
                return true;

            } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                if (g_type_is_a(gtype, G_TYPE_CLOSURE)) {
                    if (StructBase::typecheck(cx, obj, interface_info,
                                              GjsTypecheckNoThrow{}) &&
                        StructBase::typecheck(cx, obj, gtype,
                                              GjsTypecheckNoThrow{})) {
                        return StructBase::transfer_to_gi_argument(
                            cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);
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
                    "Boxed type %s registered for unexpected interface_type %s",
                    g_type_name(gtype), interface_info.type_string());
                return false;

            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                return FundamentalBase::transfer_to_gi_argument(
                    cx, obj, arg, GI_DIRECTION_IN, transfer, gtype);

            } else if (G_TYPE_IS_INTERFACE(gtype)) {
                // Could be a GObject interface that's missing a prerequisite,
                // or could be a fundamental
                if (ObjectBase::typecheck(cx, obj, gtype,
                                          GjsTypecheckNoThrow{})) {
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
            gjs_arg_unset(arg);
            return false;
        }

        gjs_debug(GJS_DEBUG_GFUNCTION,
                  "conversion of JSObject value %s to type %s failed",
                  gjs_debug_value(value).c_str(), interface_info.name());

        gjs_throw(cx,
                  "Unexpected unregistered type unpacking GIArgument from "
                  "Object");
        return false;

    } else if (value.isNumber()) {
        if (auto enum_info = interface_info.as<GI::InfoTag::ENUM>()) {
            int64_t value_int64;

            if (!JS::ToInt64(cx, value, &value_int64))
                return false;

            if (interface_info.is_flags()) {
                if (!_gjs_flags_value_is_valid(cx, gtype, value_int64))
                    return false;
            } else {
                if (!_gjs_enum_value_is_valid(cx, enum_info.value(),
                                              value_int64))
                    return false;
            }

            gjs_arg_set<Gjs::Tag::Enum>(arg,
                                        enum_info->enum_to_int(value_int64));
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
    return throw_invalid_interface_argument(cx, value, interface_info, arg_name,
                                            arg_type);
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION inline static bool gjs_arg_set_from_js_value(
    JSContext* cx, JS::HandleValue value, GIArgument* arg, const char* arg_name,
    GjsArgumentType arg_type) {
    bool out_of_range = false;

    if (!gjs_arg_set_from_js_value<TAG>(cx, value, arg, &out_of_range)) {
        if (out_of_range) {
            Gjs::AutoChar display_name{
                gjs_argument_display_name(arg_name, arg_type)};
            gjs_throw(cx, "value %s is out of range for %s (type %s)",
                      std::to_string(gjs_arg_get<TAG>(arg)).c_str(),
                      display_name.get(), Gjs::static_type_name<TAG>());
        }

        return false;
    }

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION, "%s set to value %s (type %s)",
        Gjs::AutoChar{gjs_argument_display_name(arg_name, arg_type)}.get(),
        std::to_string(gjs_arg_get<TAG>(arg)).c_str(),
        Gjs::static_type_name<TAG>());

    return true;
}

static bool check_nullable_argument(JSContext* cx, const char* arg_name,
                                    GjsArgumentType arg_type,
                                    GITypeTag type_tag, GjsArgumentFlags flags,
                                    GIArgument* arg) {
    if (!(flags & GjsArgumentFlags::MAY_BE_NULL) && !gjs_arg_get<void*>(arg)) {
        Gjs::AutoChar display_name{
            gjs_argument_display_name(arg_name, arg_type)};
        gjs_throw(cx, "%s (type %s) may not be null", display_name.get(),
                  gi_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

bool gjs_value_to_basic_gi_argument(JSContext* cx, JS::HandleValue value,
                                    GITypeTag type_tag, GIArgument* arg,
                                    const char* arg_name,
                                    GjsArgumentType arg_type,
                                    GjsArgumentFlags flags) {
    g_assert(GI_TYPE_TAG_IS_BASIC(type_tag) &&
             "use gjs_value_to_gi_argument() for non-basic types");

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type %s", arg_name,
        gjs_debug_value(value).c_str(), gi_type_tag_to_string(type_tag));

    switch (type_tag) {
        case GI_TYPE_TAG_VOID:
            // don't know how to handle non-pointer VOID
            return throw_invalid_argument_tag(cx, value, type_tag, arg_name,
                                              arg_type);
        case GI_TYPE_TAG_INT8:
            return gjs_arg_set_from_js_value<int8_t>(cx, value, arg, arg_name,
                                                     arg_type);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_set_from_js_value<uint8_t>(cx, value, arg, arg_name,
                                                      arg_type);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_set_from_js_value<int16_t>(cx, value, arg, arg_name,
                                                      arg_type);

        case GI_TYPE_TAG_UINT16:
            return gjs_arg_set_from_js_value<uint16_t>(cx, value, arg, arg_name,
                                                       arg_type);

        case GI_TYPE_TAG_INT32:
            return gjs_arg_set_from_js_value<int32_t>(cx, value, arg, arg_name,
                                                      arg_type);

        case GI_TYPE_TAG_UINT32:
            return gjs_arg_set_from_js_value<uint32_t>(cx, value, arg, arg_name,
                                                       arg_type);

        case GI_TYPE_TAG_INT64:
            return gjs_arg_set_from_js_value<int64_t>(cx, value, arg, arg_name,
                                                      arg_type);

        case GI_TYPE_TAG_UINT64:
            return gjs_arg_set_from_js_value<uint64_t>(cx, value, arg, arg_name,
                                                       arg_type);

        case GI_TYPE_TAG_BOOLEAN:
            gjs_arg_set(arg, JS::ToBoolean(value));
            return true;

        case GI_TYPE_TAG_FLOAT:
            return gjs_arg_set_from_js_value<float>(cx, value, arg, arg_name,
                                                    arg_type);

        case GI_TYPE_TAG_DOUBLE:
            return gjs_arg_set_from_js_value<double>(cx, value, arg, arg_name,
                                                     arg_type);

        case GI_TYPE_TAG_UNICHAR:
            if (value.isString()) {
                return gjs_unichar_from_string(cx, value,
                                               &gjs_arg_member<char32_t>(arg));
            }

            return throw_invalid_argument_tag(cx, value, type_tag, arg_name,
                                              arg_type);

        case GI_TYPE_TAG_GTYPE:
            if (value.isObjectOrNull()) {
                GType gtype;
                JS::RootedObject obj{cx, value.toObjectOrNull()};
                if (!gjs_gtype_get_actual_gtype(cx, obj, &gtype))
                    return false;

                if (gtype == G_TYPE_INVALID)
                    return false;
                gjs_arg_set<Gjs::Tag::GType>(arg, gtype);
                return true;
            }

            return throw_invalid_argument_tag(cx, value, type_tag, arg_name,
                                              arg_type);

        case GI_TYPE_TAG_FILENAME:
            if (value.isNull()) {
                gjs_arg_set(arg, nullptr);
            } else if (value.isString()) {
                Gjs::AutoChar filename_str;
                if (!gjs_string_to_filename(cx, value, &filename_str))
                    return false;

                gjs_arg_set(arg, filename_str.release());
            } else {
                return throw_invalid_argument_tag(cx, value, type_tag, arg_name,
                                                  arg_type);
            }

            return check_nullable_argument(cx, arg_name, arg_type, type_tag,
                                           flags, arg);

        case GI_TYPE_TAG_UTF8:
            if (value.isNull()) {
                gjs_arg_set(arg, nullptr);
            } else if (value.isString()) {
                JS::RootedString str{cx, value.toString()};
                JS::UniqueChars utf8_str{JS_EncodeStringToUTF8(cx, str)};
                if (!utf8_str)
                    return false;

                gjs_arg_set(
                    arg, Gjs::js_chars_to_glib(std::move(utf8_str)).release());
            } else {
                return throw_invalid_argument_tag(cx, value, type_tag, arg_name,
                                                  arg_type);
            }

            return check_nullable_argument(cx, arg_name, arg_type, type_tag,
                                           flags, arg);

        default:
            g_return_val_if_reached(false);  // non-basic type
    }
}

bool gjs_value_to_gerror_gi_argument(JSContext* cx, JS::HandleValue value,
                                     GITransfer transfer, GIArgument* arg,
                                     const char* arg_name,
                                     GjsArgumentType arg_type,
                                     GjsArgumentFlags flags) {
    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type error",
        arg_name, gjs_debug_value(value).c_str());

    if (value.isNull()) {
        gjs_arg_set(arg, nullptr);
    } else if (value.isObject()) {
        JS::RootedObject obj(cx, &value.toObject());
        if (!ErrorBase::transfer_to_gi_argument(cx, obj, arg, GI_DIRECTION_IN,
                                                transfer))
            return false;
    } else {
        return throw_invalid_argument_tag(cx, value, GI_TYPE_TAG_ERROR,
                                          arg_name, arg_type);
    }

    return check_nullable_argument(cx, arg_name, arg_type, GI_TYPE_TAG_ERROR,
                                   flags, arg);
}

bool gjs_value_to_gdk_atom_gi_argument(JSContext* cx, JS::HandleValue value,
                                       GIArgument* arg, const char* arg_name,
                                       GjsArgumentType arg_type) {
    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type interface",
        arg_name, gjs_debug_value(value).c_str());

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "gtype of INTERFACE is GdkAtom");

    return value_to_gdk_atom_gi_argument_internal(cx, value, arg, arg_name,
                                                  arg_type);
}

// Convert a JS value to GIArgument, specifically for arguments with type tag
// GI_TYPE_TAG_INTERFACE.
bool gjs_value_to_interface_gi_argument(JSContext* cx, JS::HandleValue value,
                                        const GI::BaseInfo interface_info,
                                        GITransfer transfer, GIArgument* arg,
                                        const char* arg_name,
                                        GjsArgumentType arg_type,
                                        GjsArgumentFlags flags) {
    if (auto reg_info = interface_info.as<GI::InfoTag::REGISTERED_TYPE>();
        reg_info && reg_info->is_gdk_atom()) {
        return gjs_value_to_gdk_atom_gi_argument(cx, value, arg, arg_name,
                                                 arg_type);
    }

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type interface",
        arg_name, gjs_debug_value(value).c_str());

    if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
        struct_info && struct_info->is_foreign()) {
        return gjs_struct_foreign_convert_to_gi_argument(
            cx, value, *struct_info, arg_name, arg_type, transfer, flags, arg);
    }

    if (!value_to_interface_gi_argument_internal(cx, value, interface_info,
                                                 transfer, arg, arg_name,
                                                 arg_type, flags))
        return false;

    if (interface_info.is_enum_or_flags())
        return true;

    return check_nullable_argument(cx, arg_name, arg_type,
                                   GI_TYPE_TAG_INTERFACE, flags, arg);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool basic_array_to_linked_list(
    JSContext* cx, JS::HandleValue value, GITypeTag element_tag,
    const char* arg_name, GjsArgumentType arg_type, T** list_p) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "use gjs_array_to_g_list() for lists containing non-basic types");

    constexpr GITypeTag list_tag =
        std::is_same_v<T, GList> ? GI_TYPE_TAG_GLIST : GI_TYPE_TAG_GSLIST;

    // While a list can be NULL in C, that means empty array in JavaScript, it
    // doesn't mean null in JavaScript.
    if (!value.isObject())
        return false;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedObject array_obj(cx, &value.toObject());

    bool found_length;
    if (!JS_HasPropertyById(cx, array_obj, atoms.length(), &found_length))
        return false;
    if (!found_length) {
        return throw_invalid_argument_tag(cx, value, list_tag, arg_name,
                                          arg_type);
    }

    uint32_t length;
    if (!gjs_object_require_converted_property(cx, array_obj, nullptr,
                                               atoms.length(), &length)) {
        return throw_invalid_argument_tag(cx, value, list_tag, arg_name,
                                          arg_type);
    }

    JS::RootedObject array{cx, value.toObjectOrNull()};
    JS::RootedValue elem{cx};
    T* list = nullptr;

    for (size_t i = 0; i < length; ++i) {
        GIArgument elem_arg = {0};

        elem = JS::UndefinedValue();
        if (!JS_GetElement(cx, array, i, &elem)) {
            gjs_throw(cx, "Missing array element %zu", i);
            return false;
        }

        if (!gjs_value_to_basic_gi_argument(cx, elem, element_tag, &elem_arg,
                                            arg_name, GJS_ARGUMENT_LIST_ELEMENT,
                                            GjsArgumentFlags::NONE)) {
            return false;
        }

        void* hash_pointer =
            gi_type_tag_hash_pointer_from_argument(element_tag, &elem_arg);

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

bool gjs_value_to_basic_glist_gi_argument(JSContext* cx, JS::HandleValue value,
                                          GITypeTag element_tag,
                                          GIArgument* arg, const char* arg_name,
                                          GjsArgumentType arg_type) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "use gjs_array_to_g_list() for lists containing non-basic types");

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type glist",
        arg_name, gjs_debug_value(value).c_str());

    return basic_array_to_linked_list(cx, value, element_tag, arg_name,
                                      arg_type, &gjs_arg_member<GList*>(arg));
}

bool gjs_value_to_basic_gslist_gi_argument(JSContext* cx, JS::HandleValue value,
                                           GITypeTag element_tag,
                                           GIArgument* arg,
                                           const char* arg_name,
                                           GjsArgumentType arg_type) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "use gjs_array_to_g_list() for lists containing non-basic types");

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type gslist",
        arg_name, gjs_debug_value(value).c_str());

    return basic_array_to_linked_list(cx, value, element_tag, arg_name,
                                      arg_type, &gjs_arg_member<GSList*>(arg));
}

bool gjs_value_to_basic_ghash_gi_argument(
    JSContext* cx, JS::HandleValue value, GITypeTag key_tag,
    GITypeTag value_tag, GITransfer transfer, GIArgument* arg,
    const char* arg_name, GjsArgumentType arg_type, GjsArgumentFlags flags) {
    g_assert(GI_TYPE_TAG_IS_BASIC(key_tag) &&
             "use gjs_object_to_g_hash() for hashes with non-basic key types");
    g_assert(
        GI_TYPE_TAG_IS_BASIC(value_tag) &&
        "use gjs_object_to_g_hash() for hashes with non-basic value types");

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type ghash",
        arg_name, gjs_debug_value(value).c_str());

    if (value.isNull()) {
        if (!(flags & GjsArgumentFlags::MAY_BE_NULL)) {
            return throw_invalid_argument_tag(cx, value, GI_TYPE_TAG_GHASH,
                                              arg_name, arg_type);
        }
        gjs_arg_set(arg, nullptr);
        return true;
    }

    if (!value.isObject()) {
        return throw_invalid_argument_tag(cx, value, GI_TYPE_TAG_GHASH,
                                          arg_name, arg_type);
    }

    if (transfer == GI_TRANSFER_CONTAINER) {
        if (Gjs::basic_type_needs_release(key_tag) ||
            Gjs::basic_type_needs_release(value_tag)) {
            // See comment in gjs_value_to_g_hash()
            gjs_throw(cx, "Container transfer for in parameters not supported");
            return false;
        }

        transfer = GI_TRANSFER_NOTHING;
    }

    JS::RootedObject props{cx, &value.toObject()};
    JS::Rooted<JS::IdVector> ids{cx, cx};
    if (!JS_Enumerate(cx, props, &ids))
        return false;

    Gjs::AutoPointer<GHashTable, GHashTable, g_hash_table_destroy> result{
        create_hash_table_for_key_type(key_tag)};

    JS::RootedValue v_key{cx}, v_val{cx};
    JS::RootedId cur_id{cx};
    for (size_t id_ix = 0, id_len = ids.length(); id_ix < id_len; ++id_ix) {
        cur_id = ids[id_ix];
        void* key_ptr;
        void* val_ptr;
        GIArgument val_arg = {0};

        if (!JS_IdToValue(cx, cur_id, &v_key) ||
            // Type check key type.
            !value_to_ghashtable_key(cx, v_key, key_tag, &key_ptr) ||
            !JS_GetPropertyById(cx, props, cur_id, &v_val) ||
            // Type check and convert value to a C type
            !gjs_value_to_basic_gi_argument(cx, v_val, value_tag, &val_arg,
                                            nullptr, GJS_ARGUMENT_HASH_ELEMENT,
                                            GjsArgumentFlags::MAY_BE_NULL))
            return false;

        // Use heap-allocated values for types that don't fit in a pointer
        if (value_tag == GI_TYPE_TAG_INT64) {
            val_ptr = heap_value_new_from_arg<int64_t>(&val_arg);
        } else if (value_tag == GI_TYPE_TAG_UINT64) {
            val_ptr = heap_value_new_from_arg<uint64_t>(&val_arg);
        } else if (value_tag == GI_TYPE_TAG_FLOAT) {
            val_ptr = heap_value_new_from_arg<float>(&val_arg);
        } else if (value_tag == GI_TYPE_TAG_DOUBLE) {
            val_ptr = heap_value_new_from_arg<double>(&val_arg);
        } else {
            // Other types are simply stuffed inside the pointer
            val_ptr =
                gi_type_tag_hash_pointer_from_argument(value_tag, &val_arg);
        }

        g_hash_table_insert(result, key_ptr, val_ptr);
    }

    gjs_arg_set(arg, result.release());
    return true;
}

bool gjs_value_to_basic_array_gi_argument(JSContext* cx, JS::HandleValue value,
                                          GITypeTag element_tag,
                                          GIArrayType array_type,
                                          GIArgument* arg, const char* arg_name,
                                          GjsArgumentType arg_type,
                                          GjsArgumentFlags flags) {
    Gjs::AutoPointer<void> data;
    size_t length;
    if (!gjs_array_to_basic_explicit_array(cx, value, element_tag, arg_name,
                                           arg_type, flags, data.out(),
                                           &length)) {
        return false;
    }

    if (array_type == GI_ARRAY_TYPE_C) {
        gjs_arg_set(arg, data.release());
    } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
        GArray* array = garray_new_for_basic_type(length, element_tag);

        if (data)
            g_array_append_vals(array, data, length);
        gjs_arg_set(arg, array);
    } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
        return gjs_value_to_byte_array_gi_argument(cx, value, arg, arg_name,
                                                   flags);
    } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
        GPtrArray* array = g_ptr_array_sized_new(length);

        g_ptr_array_set_size(array, length);
        if (data)
            memcpy(array->pdata, data, sizeof(void*) * length);
        gjs_arg_set(arg, array);
    }
    return true;
}

bool gjs_value_to_byte_array_gi_argument(JSContext* cx, JS::HandleValue value,
                                         GIArgument* arg, const char* arg_name,
                                         GjsArgumentFlags flags) {
    // First, let's handle the case where we're passed an instance of
    // Uint8Array and it needs to be marshalled to GByteArray.
    if (value.isObject()) {
        JSObject* bytearray_obj = &value.toObject();
        if (JS_IsUint8Array(bytearray_obj)) {
            gjs_arg_set(arg, gjs_byte_array_get_byte_array(bytearray_obj));
            return true;
        }
    }

    Gjs::AutoPointer<void> data;
    size_t length;
    if (!gjs_array_to_basic_explicit_array(cx, value, GI_TYPE_TAG_UINT8,
                                           arg_name, GJS_ARGUMENT_ARGUMENT,
                                           flags, data.out(), &length)) {
        return false;
    }

    GByteArray* byte_array = g_byte_array_sized_new(length);

    if (data)
        g_byte_array_append(byte_array, data.as<const uint8_t>(), length);
    gjs_arg_set(arg, byte_array);
    return true;
}

bool gjs_value_to_gi_argument(JSContext* context, JS::HandleValue value,
                              const GI::TypeInfo type_info,
                              const char* arg_name, GjsArgumentType arg_type,
                              GITransfer transfer, GjsArgumentFlags flags,
                              GIArgument* arg) {
    GITypeTag type_tag = type_info.tag();

    if (type_info.is_basic()) {
        return gjs_value_to_basic_gi_argument(context, value, type_tag, arg,
                                              arg_name, arg_type, flags);
    }

    if (type_tag == GI_TYPE_TAG_ERROR) {
        return gjs_value_to_gerror_gi_argument(context, value, transfer, arg,
                                               arg_name, arg_type, flags);
    }

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GI::AutoBaseInfo interface_info{type_info.interface()};
        return gjs_value_to_interface_gi_argument(context, value,
                                                  interface_info, transfer, arg,
                                                  arg_name, arg_type, flags);
    }

    if (type_tag == GI_TYPE_TAG_GLIST || type_tag == GI_TYPE_TAG_GSLIST) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            if (type_tag == GI_TYPE_TAG_GLIST)
                return gjs_value_to_basic_glist_gi_argument(
                    context, value, element_type.tag(), arg, arg_name,
                    arg_type);
            return gjs_value_to_basic_gslist_gi_argument(
                context, value, element_type.tag(), arg, arg_name, arg_type);
        }
        // else, fall through to generic marshaller

    } else if (type_tag == GI_TYPE_TAG_GHASH) {
        GI::AutoTypeInfo key_type{type_info.key_type()};
        GI::AutoTypeInfo value_type{type_info.value_type()};
        if (key_type.is_basic() && value_type.is_basic()) {
            return gjs_value_to_basic_ghash_gi_argument(
                context, value, key_type.tag(), value_type.tag(), transfer, arg,
                arg_name, arg_type, flags);
        }
        // else, fall through to generic marshaller

    } else if (type_tag == GI_TYPE_TAG_ARRAY) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            return gjs_value_to_basic_array_gi_argument(
                context, value, element_type.tag(), type_info.array_type(), arg,
                arg_name, arg_type, flags);
        }
        // else, fall through to generic marshaller
    }

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION,
        "Converting argument '%s' JS value %s to GIArgument type %s", arg_name,
        gjs_debug_value(value).c_str(), gi_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        g_assert(type_info.is_pointer() &&
                 "non-pointers should be handled by "
                 "gjs_value_to_basic_gi_argument()");
        // void pointer; cannot marshal. Pass null to C if argument is nullable.
        gjs_arg_unset(arg);
        return check_nullable_argument(context, arg_name, arg_type, type_tag,
                                       flags, arg);
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
        Gjs::AutoPointer<void> data;
        size_t length;
        GIArrayType array_type = type_info.array_type();

        if (!gjs_array_to_explicit_array(context, value, type_info, arg_name,
                                         arg_type, transfer, flags, data.out(),
                                         &length)) {
            return false;
        }

        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (array_type == GI_ARRAY_TYPE_C) {
            gjs_arg_set(arg, data.release());
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GArray* array = garray_new_for_storage_type(
                length, element_type.storage_type(), element_type);

            if (data)
                g_array_append_vals(array, data, length);
            gjs_arg_set(arg, array);
        } else if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            // handled in gjs_value_to_basic_array_gi_argument()
            g_assert_not_reached();
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
        // basic types handled in gjs_value_to_basic_gi_argument(), ERROR
        // handled in gjs_value_to_gerror_gi_argument(), and INTERFACE handled
        // in gjs_value_to_interface_gi_argument()
        g_warning("Unhandled type %s for JavaScript to GIArgument conversion",
                  gi_type_tag_to_string(type_tag));
        throw_invalid_argument(context, value, type_info, arg_name, arg_type);
        return false;
    }

    return true;
}

bool gjs_value_to_callback_out_arg(JSContext* cx, JS::HandleValue value,
                                   const GI::ArgInfo arg_info,
                                   GIArgument* arg) {
    g_assert((arg_info.direction() == GI_DIRECTION_OUT ||
              arg_info.direction() == GI_DIRECTION_INOUT) &&
             "gjs_value_to_callback_out_arg does not handle in arguments.");

    GjsArgumentFlags flags = GjsArgumentFlags::NONE;
    GI::StackTypeInfo type_info;
    arg_info.load_type(&type_info);

    // If the argument is optional and we're passed nullptr,
    // ignore the GJS value.
    if (arg_info.is_optional() && !arg)
        return true;

    // Otherwise, throw an error to prevent a segfault.
    if (!arg) {
        gjs_throw(cx, "Return value %s is not optional but was passed NULL",
                  arg_info.name());
        return false;
    }

    if (arg_info.may_be_null())
        flags |= GjsArgumentFlags::MAY_BE_NULL;
    if (arg_info.caller_allocates())
        flags |= GjsArgumentFlags::CALLER_ALLOCATES;

    return gjs_value_to_gi_argument(
        cx, value, type_info, arg_info.name(),
        (arg_info.is_return_value() ? GJS_ARGUMENT_RETURN_VALUE
                                    : GJS_ARGUMENT_ARGUMENT),
        arg_info.ownership_transfer(), flags, arg);
}

///// "FROM" MARSHALLERS ///////////////////////////////////////////////////////
// These marshaller functions are responsible for converting C values returned
// from a C function call, usually stored in a GIArgument, back to JS values.

bool gjs_value_from_basic_gi_argument(JSContext* cx,
                                      JS::MutableHandleValue value_out,
                                      GITypeTag type_tag, GIArgument* arg) {
    g_assert(GI_TYPE_TAG_IS_BASIC(type_tag) &&
             "use gjs_value_from_gi_argument() for non-basic types");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument %s to JS::Value",
                      gi_type_tag_to_string(type_tag));

    switch (type_tag) {
        case GI_TYPE_TAG_VOID:
            // Pointers are handled in gjs_value_from_gi_argument(), and would
            // set null instead
            value_out.setUndefined();
            return true;

        case GI_TYPE_TAG_BOOLEAN:
            value_out.setBoolean(gjs_arg_get<bool>(arg));
            return true;

        case GI_TYPE_TAG_INT32:
            value_out.setInt32(gjs_arg_get<int32_t>(arg));
            return true;

        case GI_TYPE_TAG_UINT32:
            value_out.setNumber(gjs_arg_get<uint32_t>(arg));
            return true;

        case GI_TYPE_TAG_INT64:
            value_out.setNumber(gjs_arg_get_maybe_rounded<int64_t>(arg));
            return true;

        case GI_TYPE_TAG_UINT64:
            value_out.setNumber(gjs_arg_get_maybe_rounded<uint64_t>(arg));
            return true;

        case GI_TYPE_TAG_UINT16:
            value_out.setInt32(gjs_arg_get<uint16_t>(arg));
            return true;

        case GI_TYPE_TAG_INT16:
            value_out.setInt32(gjs_arg_get<int16_t>(arg));
            return true;

        case GI_TYPE_TAG_UINT8:
            value_out.setInt32(gjs_arg_get<uint8_t>(arg));
            return true;

        case GI_TYPE_TAG_INT8:
            value_out.setInt32(gjs_arg_get<int8_t>(arg));
            return true;

        case GI_TYPE_TAG_FLOAT:
            value_out.setNumber(JS::CanonicalizeNaN(gjs_arg_get<float>(arg)));
            return true;

        case GI_TYPE_TAG_DOUBLE:
            value_out.setNumber(JS::CanonicalizeNaN(gjs_arg_get<double>(arg)));
            return true;

        case GI_TYPE_TAG_GTYPE: {
            GType gtype = gjs_arg_get<Gjs::Tag::GType>(arg);
            if (gtype == 0) {
                value_out.setNull();
                return true;
            }

            JSObject* obj = gjs_gtype_create_gtype_wrapper(cx, gtype);
            if (!obj)
                return false;

            value_out.setObject(*obj);
            return true;
        }

        case GI_TYPE_TAG_UNICHAR: {
            char32_t value = gjs_arg_get<char32_t>(arg);

            // Preserve the bidirectional mapping between 0 and ""
            if (value == 0) {
                value_out.set(JS_GetEmptyStringValue(cx));
                return true;
            } else if (!g_unichar_validate(value)) {
                gjs_throw(cx, "Invalid unicode codepoint %" G_GUINT32_FORMAT,
                          value);
                return false;
            }

            char utf8[7];
            int bytes = g_unichar_to_utf8(value, utf8);
            return gjs_string_from_utf8_n(cx, utf8, bytes, value_out);
        }

        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UTF8: {
            const char* str = gjs_arg_get<const char*>(arg);
            if (!str) {
                value_out.setNull();
                return true;
            }

            if (type_tag == GI_TYPE_TAG_FILENAME)
                return gjs_string_from_filename(cx, str, -1, value_out);

            if (!g_utf8_validate(str, -1, nullptr)) {
                gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                                 "String from C value is invalid UTF-8 and "
                                 "cannot be safely stored");
                return false;
            }
            return gjs_string_from_utf8(cx, str, value_out);
        }

        default:
            // this function handles only basic types
            g_return_val_if_reached(false);
    }
}

bool gjs_array_from_strv(JSContext* cx, JS::MutableHandleValue value_out,
                         const char** strv) {
    // We treat a NULL strv as an empty array, since this function should always
    // set an array value when returning true. Another alternative would be
    // value_out.setNull(), but clients would need to always check for both an
    // empty array and null if that was the case.
    JS::RootedValueVector elems{cx};
    for (size_t i = 0; strv && strv[i]; i++) {
        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_string_from_utf8(cx, strv[i], elems[i]))
            return false;
    }

    JSObject* obj = JS::NewArrayObject(cx, elems);
    if (!obj)
        return false;

    value_out.setObject(*obj);
    return true;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_array_from_g_list(
    JSContext* cx, JS::MutableHandleValue value_p, const GI::TypeInfo type_info,
    GITransfer transfer, T* list) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    JS::RootedValueVector elems(cx);
    GI::AutoTypeInfo element_type{type_info.element_type()};

    GIArgument arg;
    for (size_t i = 0; list; list = list->next, ++i) {
        element_type.argument_from_hash_pointer(list->data, &arg);

        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_value_from_gi_argument(cx, elems[i], element_type,
                                        GJS_ARGUMENT_LIST_ELEMENT, transfer,
                                        &arg))
            return false;
    }

    JSObject* obj = JS::NewArrayObject(cx, elems);
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION static bool fill_vector_from_basic_c_array(
    JSContext* cx, JS::MutableHandleValueVector elems, GITypeTag element_tag,
    GIArgument* arg, void* array, size_t length) {
    using T = Gjs::Tag::RealT<TAG>;
    for (size_t i = 0; i < length; i++) {
        gjs_arg_set<TAG>(arg, *(static_cast<T*>(array) + i));

        if (!gjs_value_from_basic_gi_argument(cx, elems[i], element_tag, arg))
            return false;
    }

    return true;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool fill_vector_from_carray(
    JSContext* cx, JS::RootedValueVector& elems,  // NOLINT(runtime/references)
    const GI::TypeInfo element_type, GIArgument* arg, void* array,
    size_t length, GITransfer transfer = GI_TRANSFER_EVERYTHING) {
    for (size_t i = 0; i < length; i++) {
        gjs_arg_set<T>(arg, *(static_cast<Gjs::Tag::RealT<T>*>(array) + i));

        if (!gjs_value_from_gi_argument(cx, elems[i], element_type,
                                        GJS_ARGUMENT_ARRAY_ELEMENT, transfer,
                                        arg))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_basic_c_array_internal(
    JSContext* cx, JS::MutableHandleValue value_out, GITypeTag element_tag,
    size_t length, void* contents) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag));

    // Special case array(uint8)
    if (element_tag == GI_TYPE_TAG_UINT8) {
        JSObject* u8array = gjs_byte_array_from_data_copy(cx, length, contents);
        if (!u8array)
            return false;
        value_out.setObject(*u8array);
        return true;
    }

    // Special case array(unichar) to be a string in JS
    if (element_tag == GI_TYPE_TAG_UNICHAR) {
        return gjs_string_from_ucs4(cx, static_cast<gunichar*>(contents),
                                    length, value_out);
    }

    // a null array pointer takes precedence over whatever `length` says
    if (!contents) {
        JSObject* array = JS::NewArrayObject(cx, 0);
        if (!array)
            return false;
        value_out.setObject(*array);
        return true;
    }

    JS::RootedValueVector elems{cx};
    if (!elems.resize(length)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    GIArgument arg;
    switch (element_tag) {
        // Special cases handled above
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UNICHAR:
            g_assert_not_reached();

        case GI_TYPE_TAG_BOOLEAN:
            if (!fill_vector_from_basic_c_array<Gjs::Tag::GBoolean>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_INT8:
            if (!fill_vector_from_basic_c_array<int8_t>(cx, &elems, element_tag,
                                                        &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT16:
            if (!fill_vector_from_basic_c_array<uint16_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_INT16:
            if (!fill_vector_from_basic_c_array<int16_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT32:
            if (!fill_vector_from_basic_c_array<uint32_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_INT32:
            if (!fill_vector_from_basic_c_array<int32_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_UINT64:
            if (!fill_vector_from_basic_c_array<uint64_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_INT64:
            if (!fill_vector_from_basic_c_array<int64_t>(
                    cx, &elems, element_tag, &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_FLOAT:
            if (!fill_vector_from_basic_c_array<float>(cx, &elems, element_tag,
                                                       &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_DOUBLE:
            if (!fill_vector_from_basic_c_array<double>(cx, &elems, element_tag,
                                                        &arg, contents, length))
                return false;
            break;

        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            if (!fill_vector_from_basic_c_array<void*>(cx, &elems, element_tag,
                                                       &arg, contents, length))
                return false;
            break;
        case GI_TYPE_TAG_VOID:
            gjs_throw(cx, "Unknown Array element-type %d", element_tag);
            return false;
        default:
            g_assert_not_reached();
    }

    JSObject* array = JS::NewArrayObject(cx, elems);
    if (!array)
        return false;

    value_out.setObject(*array);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_carray_internal(JSContext* context,
                                           JS::MutableHandleValue value_p,
                                           GIArrayType array_type,
                                           const GI::TypeInfo element_type,
                                           GITransfer transfer, size_t length,
                                           void* array) {
    GITypeTag element_tag = element_type.tag();
    if (GI_TYPE_TAG_IS_BASIC(element_tag)) {
        return gjs_array_from_basic_c_array_internal(
            context, value_p, element_tag, length, array);
    }

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
    switch (element_tag) {
        case GI_TYPE_TAG_INTERFACE: {
            GI::AutoBaseInfo interface_info{element_type.interface()};
            GITypeTag storage_element_type = element_type.storage_type();

            if (array_type != GI_ARRAY_TYPE_PTR_ARRAY &&
                (interface_info.is_struct() || interface_info.is_union() ||
                 interface_info.is_enum_or_flags()) &&
                !element_type.is_pointer()) {
                size_t element_size;

                if (auto union_info = interface_info.as<GI::InfoTag::UNION>()) {
                    element_size = union_info->size();
                } else if (auto struct_info =
                               interface_info.as<GI::InfoTag::STRUCT>()) {
                    element_size = struct_info->size();
                } else {
                    auto storage =
                        interface_info.as<GI::InfoTag::ENUM>()->storage_type();
                    element_size = basic_type_element_size(storage);
                }

                for (size_t i = 0; i < length; i++) {
                    auto value = static_cast<char*>(array) + (element_size * i);
                    // use the storage tag instead of element tag to handle
                    // enums and flags
                    set_arg_from_carray_element(&arg, storage_element_type,
                                                value);

                    if (!gjs_value_from_gi_argument(
                            context, elems[i], element_type,
                            GJS_ARGUMENT_ARRAY_ELEMENT, transfer, &arg))
                        return false;
                }

                break;
            }
        }
        /* fallthrough */
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            if (!fill_vector_from_carray<void*>(context, elems, element_type,
                                                &arg, array, length, transfer))
                return false;
            break;
        default:
            // Basic types handled above
            gjs_throw(context, "Unknown Array element-type %s",
                      element_type.display_string());
            return false;
    }

    JSObject* obj = JS::NewArrayObject(context, elems);
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_fixed_size_array(JSContext* cx,
                                            JS::MutableHandleValue value_p,
                                            const GI::TypeInfo type_info,
                                            GITransfer transfer, void* array) {
    Maybe<size_t> length = type_info.array_fixed_size();
    g_assert(length);

    return gjs_array_from_carray_internal(cx, value_p, type_info.array_type(),
                                          type_info.element_type(), transfer,
                                          *length, array);
}

bool gjs_value_from_explicit_array(JSContext* cx,
                                   JS::MutableHandleValue value_p,
                                   const GI::TypeInfo type_info,
                                   GITransfer transfer, GIArgument* arg,
                                   size_t length) {
    return gjs_array_from_carray_internal(cx, value_p, type_info.array_type(),
                                          type_info.element_type(), transfer,
                                          length, gjs_arg_get<void*>(arg));
}

bool gjs_value_from_basic_explicit_array(JSContext* cx,
                                         JS::MutableHandleValue value_out,
                                         GITypeTag element_tag, GIArgument* arg,
                                         size_t length) {
    return gjs_array_from_basic_c_array_internal(
        cx, value_out, element_tag, length, gjs_arg_get<void*>(arg));
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_boxed_array(JSContext* cx,
                                       JS::MutableHandleValue value_p,
                                       GIArrayType array_type,
                                       const GI::TypeInfo element_type,
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

    return gjs_array_from_carray_internal(cx, value_p, array_type, element_type,
                                          transfer, length, data);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_g_value_array(JSContext* cx, JS::MutableHandleValue value_p,
                                  const GI::TypeInfo element_type,
                                  GITransfer transfer, const GValue* gvalue) {
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
        auto* array = Gjs::gvalue_get<GArray*>(gvalue);
        data = array->data;
        length = array->len;
    } else if (g_type_is_a(value_gtype, G_TYPE_PTR_ARRAY)) {
        array_type = GI_ARRAY_TYPE_PTR_ARRAY;
        auto* ptr_array = Gjs::gvalue_get<GPtrArray*>(gvalue);
        data = ptr_array->pdata;
        length = ptr_array->len;
    } else {
        g_assert_not_reached();
        gjs_throw(cx, "%s is not an array type", g_type_name(value_gtype));
        return false;
    }

    return gjs_array_from_carray_internal(cx, value_p, array_type, element_type,
                                          transfer, length, data);
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION static bool
fill_vector_from_basic_zero_terminated_c_array(
    JSContext* cx, JS::MutableHandleValueVector elems, GITypeTag element_tag,
    GIArgument* arg, void* c_array) {
    using T = Gjs::Tag::RealT<TAG>;
    T* array = static_cast<T*>(c_array);

    for (size_t ix = 0; array[ix]; ix++) {
        gjs_arg_set<TAG>(arg, array[ix]);

        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_value_from_basic_gi_argument(cx, elems[ix], element_tag, arg))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION static bool
fill_vector_from_zero_terminated_pointer_carray(
    JSContext* cx, JS::RootedValueVector& elems,  // NOLINT(runtime/references)
    const GI::TypeInfo param_info, GIArgument* arg, void* c_array,
    GITransfer transfer = GI_TRANSFER_EVERYTHING) {
    void** array = static_cast<void**>(c_array);

    for (size_t i = 0;; i++) {
        if (!array[i])
            break;

        gjs_arg_set(arg, array[i]);

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

GJS_JSAPI_RETURN_CONVENTION static bool fill_vector_from_zero_terminated_non_pointer_carray(
    JSContext* cx, JS::RootedValueVector& elems,  // NOLINT(runtime/references)
    const GI::TypeInfo param_info, GIArgument* arg, size_t element_size,
    void* c_array, GITransfer transfer = GI_TRANSFER_EVERYTHING) {
    uint8_t* element_start = reinterpret_cast<uint8_t*>(c_array);

    for (size_t i = 0;; i++) {
        if (*element_start == 0 &&
            memcmp(element_start, element_start + 1, element_size - 1) == 0)
            break;

        gjs_arg_set(arg, element_start);
        element_start += element_size;

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

bool gjs_array_from_basic_zero_terminated_array(
    JSContext* cx, JS::MutableHandleValue value_out, GITypeTag element_tag,
    void* c_array) {
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "Use gjs_array_from_zero_terminated_c_array for non-basic types");

    if (!c_array) {
        // OK, but no conversion to do
        value_out.setNull();
        return true;
    }

    // Special case array(uint8_t)
    if (element_tag == GI_TYPE_TAG_UINT8) {
        size_t length = strlen(static_cast<char*>(c_array));
        JSObject* byte_array =
            gjs_byte_array_from_data_copy(cx, length, c_array);
        if (!byte_array)
            return false;

        value_out.setObject(*byte_array);
        return true;
    }

    // Special case array(gunichar) to JS string
    if (element_tag == GI_TYPE_TAG_UNICHAR) {
        return gjs_string_from_ucs4(cx, static_cast<gunichar*>(c_array), -1,
                                    value_out);
    }

    JS::RootedValueVector elems{cx};

    GIArgument arg;
    switch (element_tag) {
        case GI_TYPE_TAG_INT8:
            if (!fill_vector_from_basic_zero_terminated_c_array<int8_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT16:
            if (!fill_vector_from_basic_zero_terminated_c_array<uint16_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT16:
            if (!fill_vector_from_basic_zero_terminated_c_array<int16_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT32:
            if (!fill_vector_from_basic_zero_terminated_c_array<uint32_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT32:
            if (!fill_vector_from_basic_zero_terminated_c_array<int32_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_UINT64:
            if (!fill_vector_from_basic_zero_terminated_c_array<uint64_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_INT64:
            if (!fill_vector_from_basic_zero_terminated_c_array<int64_t>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_FLOAT:
            if (!fill_vector_from_basic_zero_terminated_c_array<float>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_DOUBLE:
            if (!fill_vector_from_basic_zero_terminated_c_array<double>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            if (!fill_vector_from_basic_zero_terminated_c_array<void*>(
                    cx, &elems, element_tag, &arg, c_array))
                return false;
            break;
        // Boolean zero-terminated array makes no sense, because false is also
        // zero
        case GI_TYPE_TAG_BOOLEAN:
            gjs_throw(cx, "Boolean zero-terminated array not supported");
            return false;
        case GI_TYPE_TAG_VOID:
            gjs_throw(cx, "Unknown element-type 'void'");
            return false;
        default:
            // UINT8 and UNICHAR are special cases handled above
            g_assert_not_reached();
    }

    JSObject* array = JS::NewArrayObject(cx, elems);
    if (!array)
        return false;

    value_out.setObject(*array);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_array_from_zero_terminated_c_array(
    JSContext* context, JS::MutableHandleValue value_p,
    const GI::TypeInfo element_type, GITransfer transfer, void* c_array) {
    GITypeTag element_tag = element_type.tag();

    if (element_type.is_basic()) {
        return gjs_array_from_basic_zero_terminated_array(context, value_p,
                                                          element_tag, c_array);
    }

    JS::RootedValueVector elems(context);

    GIArgument arg;
    switch (element_tag) {
        case GI_TYPE_TAG_INTERFACE: {
            GI::AutoBaseInfo interface_info{element_type.interface()};
            auto reg_info = interface_info.as<GI::InfoTag::REGISTERED_TYPE>();
            bool element_is_pointer = element_type.is_pointer();
            bool is_struct = reg_info->is_struct();

            if (!element_is_pointer && is_struct) {
                auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
                size_t element_size = struct_info->size();

                if (!fill_vector_from_zero_terminated_non_pointer_carray(
                        context, elems, element_type, &arg, element_size,
                        c_array))
                    return false;
                break;
            }

            [[fallthrough]];
        }
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            if (!fill_vector_from_zero_terminated_pointer_carray(
                    context, elems, element_type, &arg, c_array, transfer))
                return false;
            break;
        default:
            // Handled in gjs_array_from_basic_zero_terminated_c_array()
            gjs_throw(context, "Unknown element-type %s",
                      element_type.display_string());
            return false;
    }

    JSObject* obj = JS::NewArrayObject(context, elems);
    if (!obj)
        return false;

    value_p.setObject(*obj);

    return true;
}

bool gjs_object_from_g_hash(JSContext* context, JS::MutableHandleValue value_p,
                            const GI::TypeInfo key_type,
                            const GI::TypeInfo val_type, GITransfer transfer,
                            GHashTable* hash) {
    GHashTableIter iter;

    g_assert((!GI_TYPE_TAG_IS_BASIC(key_type.tag()) ||
              !GI_TYPE_TAG_IS_BASIC(val_type.tag())) &&
             "use gjs_value_from_basic_ghash() instead");

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
        key_type.argument_from_hash_pointer(key_pointer, &keyarg);
        if (!gjs_value_from_gi_argument(context, &keyjs, key_type,
                                        GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                        &keyarg))
            return false;

        JS::RootedId key{context};
        if (!JS_ValueToId(context, keyjs, &key))
            return false;

        val_type.argument_from_hash_pointer(val_pointer, &valarg);
        if (!gjs_value_from_gi_argument(context, &valjs, val_type,
                                        GJS_ARGUMENT_HASH_ELEMENT, transfer,
                                        &valarg) ||
            !JS_DefinePropertyById(context, obj, key, valjs, JSPROP_ENUMERATE))
            return false;
    }

    return true;
}

bool gjs_value_from_basic_fixed_size_array_gi_argument(
    JSContext* cx, JS::MutableHandleValue value_out, GITypeTag element_tag,
    size_t fixed_size, GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument fixed array of %s to JS::Value",
                      gi_type_tag_to_string(element_tag));

    void* c_array = gjs_arg_get<void*>(arg);
    if (!c_array) {
        // OK, but no conversion to do
        value_out.setNull();
        return true;
    }

    return gjs_array_from_basic_c_array_internal(cx, value_out, element_tag,
                                                 fixed_size, c_array);
}

bool gjs_value_from_byte_array_gi_argument(JSContext* cx,
                                           JS::MutableHandleValue value_out,
                                           GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument byte array to JS::Value");

    auto* byte_array = gjs_arg_get<GByteArray*>(arg);
    if (!byte_array) {
        value_out.setNull();
        return true;
    }

    JSObject* u8array = gjs_byte_array_from_byte_array(cx, byte_array);
    if (!u8array)
        return false;

    value_out.setObject(*u8array);
    return true;
}

bool gjs_value_from_basic_garray_gi_argument(JSContext* cx,
                                             JS::MutableHandleValue value_out,
                                             GITypeTag element_tag,
                                             GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument GArray of %s to JS::Value",
                      gi_type_tag_to_string(element_tag));

    auto* garray = gjs_arg_get<GArray*>(arg);
    if (!garray) {
        value_out.setNull();
        return true;
    }

    return gjs_array_from_basic_c_array_internal(cx, value_out, element_tag,
                                                 garray->len, garray->data);
}

bool gjs_value_from_basic_gptrarray_gi_argument(
    JSContext* cx, JS::MutableHandleValue value_out, GITypeTag element_tag,
    GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument GPtrArray of %s to JS::Value",
                      gi_type_tag_to_string(element_tag));

    auto* ptr_array = gjs_arg_get<GPtrArray*>(arg);
    if (!ptr_array) {
        value_out.setNull();
        return true;
    }

    return gjs_array_from_basic_c_array_internal(
        cx, value_out, element_tag, ptr_array->len, ptr_array->pdata);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool array_from_basic_linked_list(
    JSContext* cx, JS::MutableHandleValue value_out, GITypeTag element_tag,
    T* list) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    g_assert(
        GI_TYPE_TAG_IS_BASIC(element_tag) &&
        "use gjs_array_from_g_list() for lists containing non-basic types");

    GIArgument arg;
    JS::RootedValueVector elems{cx};

    for (size_t i = 0; list; list = list->next, ++i) {
        // for basic types, type tag == storage type
        gi_type_tag_argument_from_hash_pointer(element_tag, list->data, &arg);

        if (!elems.growBy(1)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        if (!gjs_value_from_basic_gi_argument(cx, elems[i], element_tag, &arg))
            return false;
    }

    JS::RootedObject obj{cx, JS::NewArrayObject(cx, elems)};
    if (!obj)
        return false;

    value_out.setObject(*obj);

    return true;
}

bool gjs_array_from_basic_glist_gi_argument(JSContext* cx,
                                            JS::MutableHandleValue value_out,
                                            GITypeTag element_tag,
                                            GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GArgument glist to JS::Value");
    return array_from_basic_linked_list(cx, value_out, element_tag,
                                        gjs_arg_get<GList*>(arg));
}

bool gjs_array_from_basic_gslist_gi_argument(JSContext* cx,
                                             JS::MutableHandleValue value_out,
                                             GITypeTag element_tag,
                                             GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GArgument gslist to JS::Value");
    return array_from_basic_linked_list(cx, value_out, element_tag,
                                        gjs_arg_get<GSList*>(arg));
}

bool gjs_value_from_basic_ghash(JSContext* cx, JS::MutableHandleValue value_out,
                                GITypeTag key_tag, GITypeTag value_tag,
                                GHashTable* hash) {
    g_assert(
        GI_TYPE_TAG_IS_BASIC(key_tag) &&
        "use gjs_object_from_g_hash() for hashes with non-basic key types");
    g_assert(
        GI_TYPE_TAG_IS_BASIC(value_tag) &&
        "use gjs_object_from_g_hash() for hashes with non-basic value types");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument ghash to JS::Value");

    // a NULL hash table becomes a null JS value
    if (!hash) {
        value_out.setNull();
        return true;
    }

    JS::RootedObject obj{cx, JS_NewPlainObject(cx)};
    if (!obj)
        return false;

    JS::RootedValue v_key{cx}, v_val{cx};
    JS::RootedId key{cx};
    GIArgument key_arg, value_arg;
    GHashTableIter iter;
    void* key_pointer;
    void* val_pointer;
    g_hash_table_iter_init(&iter, hash);
    while (g_hash_table_iter_next(&iter, &key_pointer, &val_pointer)) {
        gi_type_tag_argument_from_hash_pointer(key_tag, key_pointer, &key_arg);
        gi_type_tag_argument_from_hash_pointer(value_tag, val_pointer,
                                               &value_arg);
        if (!gjs_value_from_basic_gi_argument(cx, &v_key, key_tag, &key_arg) ||
            !JS_ValueToId(cx, v_key, &key) ||
            !gjs_value_from_basic_gi_argument(cx, &v_val, value_tag,
                                              &value_arg) ||
            !JS_DefinePropertyById(cx, obj, key, v_val, JSPROP_ENUMERATE))
            return false;
    }

    value_out.setObject(*obj);
    return true;
}

bool gjs_value_from_gi_argument(JSContext* context,
                                JS::MutableHandleValue value_p,
                                const GI::TypeInfo type_info,
                                GjsArgumentType argument_type,
                                GITransfer transfer, GIArgument* arg) {
    GITypeTag type_tag = type_info.tag();
    if (type_info.is_basic()) {
        return gjs_value_from_basic_gi_argument(context, value_p, type_tag,
                                                arg);
    }

    if (type_tag == GI_TYPE_TAG_GLIST) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            return gjs_array_from_basic_glist_gi_argument(
                context, value_p, element_type.tag(), arg);
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_GSLIST) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            return gjs_array_from_basic_gslist_gi_argument(
                context, value_p, element_type.tag(), arg);
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_GHASH) {
        GI::AutoTypeInfo key_type{type_info.key_type()};
        GI::AutoTypeInfo value_type{type_info.value_type()};
        if (key_type.is_basic() && value_type.is_basic()) {
            return gjs_value_from_basic_ghash(context, value_p, key_type.tag(),
                                              value_type.tag(),
                                              gjs_arg_get<GHashTable*>(arg));
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_ARRAY) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            switch (type_info.array_type()) {
                case GI_ARRAY_TYPE_C: {
                    if (type_info.is_zero_terminated()) {
                        return gjs_array_from_basic_zero_terminated_array(
                            context, value_p, element_type.tag(),
                            gjs_arg_get<void*>(arg));
                    }

                    Maybe<size_t> fixed_size = type_info.array_fixed_size();
                    g_assert(fixed_size &&
                             "arrays with length param handled in "
                             "gjs_value_from_basic_explicit_array()");
                    return gjs_value_from_basic_fixed_size_array_gi_argument(
                        context, value_p, element_type.tag(), *fixed_size, arg);
                }

                case GI_ARRAY_TYPE_BYTE_ARRAY:
                    return gjs_value_from_byte_array_gi_argument(context,
                                                                 value_p, arg);

                case GI_ARRAY_TYPE_ARRAY:
                    return gjs_value_from_basic_garray_gi_argument(
                        context, value_p, element_type.tag(), arg);

                case GI_ARRAY_TYPE_PTR_ARRAY:
                    return gjs_value_from_basic_gptrarray_gi_argument(
                        context, value_p, element_type.tag(), arg);
            }
        }
        // else fall through to generic marshaller
    }

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Converting GIArgument %s to JS::Value",
                      gi_type_tag_to_string(type_tag));

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        g_assert(type_info.is_pointer() &&
                 "non-pointers should be handled by "
                 "gjs_value_from_basic_gi_argument()");
        // If the argument is a pointer, convert to null to match our
        // in handling.
        value_p.setNull();
        return true;

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
            GI::AutoBaseInfo interface_info{type_info.interface()};

            if (interface_info.is_unresolved()) {
                gjs_throw(context, "Unable to resolve arg type '%s'",
                          interface_info.name());
                return false;
            }

            // Enum/Flags are aren't pointer types, unlike the other interface
            // subtypes
            if (auto enum_info = interface_info.as<GI::InfoTag::ENUM>()) {
                int64_t value_int64 =
                    enum_info->enum_from_int(gjs_arg_get<Gjs::Tag::Enum>(arg));

                if (interface_info.is_flags()) {
                    GType gtype = enum_info->gtype();

                    if (gtype != G_TYPE_NONE) {
                        // Check to make sure 32 bit flag
                        if (static_cast<uint32_t>(value_int64) != value_int64) {
                            gjs_throw(context,
                                      "0x%" PRIx64
                                      " is not a valid value for flags %s",
                                      value_int64, g_type_name(gtype));
                            return false;
                        }

                        // Pass only valid values
                        Gjs::AutoTypeClass<GFlagsClass> gflags_class{gtype};
                        value_int64 &= gflags_class->mask;
                    }
                } else {
                    if (!_gjs_enum_value_is_valid(context, enum_info.value(),
                                                  value_int64))
                        return false;
                }

                value_p.setNumber(static_cast<double>(value_int64));
                return true;
            }

            if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
                struct_info && struct_info->is_foreign()) {
                return gjs_struct_foreign_convert_from_gi_argument(
                    context, value_p, struct_info.value(), arg);
            }

            /* Everything else is a pointer type, NULL is the easy case */
            if (!gjs_arg_get<void*>(arg)) {
                value_p.setNull();
                return true;
            }

            if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
                struct_info && struct_info->is_gtype_struct()) {
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

            GType gtype =
                interface_info.as<GI::InfoTag::REGISTERED_TYPE>()->gtype();
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

            if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>()) {
                if (struct_info->is_gdk_atom()) {
                    GI::AutoFunctionInfo atom_name_fun{
                        struct_info->method("name").value()};

                    GIArgument atom_name_ret;
                    Gjs::GErrorResult<> result =
                        atom_name_fun.invoke({{*arg}}, {}, &atom_name_ret);
                    if (result.isErr()) {
                        gjs_throw(context, "Failed to call gdk_atom_name(): %s",
                                  result.inspectErr()->message);
                        return false;
                    }

                    Gjs::AutoChar name{gjs_arg_get<char*>(&atom_name_ret)};
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
                    obj = StructInstance::new_for_c_struct(
                        context, struct_info.value(), gjs_arg_get<void*>(arg));
                else
                    obj = StructInstance::new_for_c_struct(
                        context, struct_info.value(), gjs_arg_get<void*>(arg),
                        Boxed::NoCopy{});

                if (!obj)
                    return false;

                value_p.setObject(*obj);
                return true;
            }

            if (auto union_info = interface_info.as<GI::InfoTag::UNION>()) {
                JSObject* obj = UnionInstance::new_for_c_union(
                    context, union_info.value(), gjs_arg_get<void*>(arg));
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
                          "Type %s registered for unexpected interface_type %s",
                          g_type_name(gtype), interface_info.type_string());
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

        if (type_info.array_type() == GI_ARRAY_TYPE_C) {
            if (type_info.is_zero_terminated()) {
                return gjs_array_from_zero_terminated_c_array(
                    context, value_p, type_info.element_type(), transfer,
                    gjs_arg_get<void*>(arg));
            } else {
                /* arrays with length are handled outside of this function */
                g_assert(!type_info.array_length_index() &&
                         "Use gjs_value_from_explicit_array() for arrays with "
                         "length param");
                return gjs_array_from_fixed_size_array(context, value_p,
                                                       type_info, transfer,
                                                       gjs_arg_get<void*>(arg));
            }
        } else if (type_info.array_type() == GI_ARRAY_TYPE_BYTE_ARRAY) {
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
            return gjs_array_from_boxed_array(
                context, value_p, type_info.array_type(),
                type_info.element_type(), transfer, arg);
        }
        break;

    case GI_TYPE_TAG_GLIST:
        return gjs_array_from_g_list(context, value_p, type_info, transfer,
                                     gjs_arg_get<GList*>(arg));
    case GI_TYPE_TAG_GSLIST:
        return gjs_array_from_g_list(context, value_p, type_info, transfer,
                                     gjs_arg_get<GSList*>(arg));

    case GI_TYPE_TAG_GHASH:
        return gjs_object_from_g_hash(context, value_p, type_info.key_type(),
                                      type_info.value_type(), transfer,
                                      gjs_arg_get<GHashTable*>(arg));

    default:
        // basic types handled in gjs_value_from_basic_gi_argument()
        g_warning("Unhandled type %s converting GIArgument to JavaScript",
                  gi_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

///// RELEASE MARSHALLERS //////////////////////////////////////////////////////
// These marshaller function are responsible for releasing the values stored in
// GIArgument after a C function call succeeds or fails.

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_g_arg_release_g_list(
    JSContext* cx, GITransfer transfer, const GI::TypeInfo type_info,
    GjsArgumentFlags flags, GIArgument* arg) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    Gjs::SmartPointer<T> list{gjs_arg_steal<T*>(arg)};

    if (transfer == GI_TRANSFER_CONTAINER)
        return true;

    GIArgument elem;
    GI::AutoTypeInfo element_type{type_info.element_type()};

    for (T* l = list; l; l = l->next) {
        gjs_arg_set(&elem, l->data);

        if (!gjs_g_arg_release_internal(
                cx, transfer, element_type, element_type.tag(),
                GJS_ARGUMENT_LIST_ELEMENT, flags, &elem))
            return false;
    }

    return true;
}

struct GHR_closure {
    JSContext *context;
    GI::AutoTypeInfo key_type, val_type;
    GITransfer transfer;
    GjsArgumentFlags flags;
    bool failed;
};

static gboolean
gjs_ghr_helper(gpointer key, gpointer val, gpointer user_data) {
    GHR_closure *c = (GHR_closure *) user_data;

    GITypeTag key_tag = c->key_type.tag();
    GITypeTag val_tag = c->val_type.tag();
    g_assert(
        (!GI_TYPE_TAG_IS_BASIC(key_tag) || !GI_TYPE_TAG_IS_BASIC(val_tag)) &&
        "use basic_ghash_release() instead");

    GIArgument key_arg, val_arg;
    gjs_arg_set(&key_arg, key);
    gjs_arg_set(&val_arg, val);
    if (!gjs_g_arg_release_internal(c->context, c->transfer, c->key_type,
                                    key_tag, GJS_ARGUMENT_HASH_ELEMENT,
                                    c->flags, &key_arg))
        c->failed = true;

    switch (val_tag) {
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
            g_clear_pointer(&gjs_arg_member<void*>(&val_arg), g_free);
            break;

        default:
            if (!gjs_g_arg_release_internal(
                    c->context, c->transfer, c->val_type, val_tag,
                    GJS_ARGUMENT_HASH_ELEMENT, c->flags, &val_arg))
                c->failed = true;
    }

    return true;
}

enum class ArrayReleaseType {
    EXPLICIT_LENGTH,
    ZERO_TERMINATED,
};

template <ArrayReleaseType release_type>
static inline void release_basic_array_internal(GITypeTag element_tag,
                                                Maybe<size_t> length,
                                                void** array) {
    if (!Gjs::basic_type_needs_release(element_tag))
        return;

    for (size_t ix = 0;; ix++) {
        if constexpr (release_type == ArrayReleaseType::ZERO_TERMINATED) {
            if (!array[ix])
                break;
        }
        if constexpr (release_type == ArrayReleaseType::EXPLICIT_LENGTH) {
            if (ix == *length)
                break;
        }

        g_free(array[ix]);
    }
}

template <ArrayReleaseType release_type = ArrayReleaseType::EXPLICIT_LENGTH>
static inline bool gjs_gi_argument_release_array_internal(
    JSContext* cx, GITransfer element_transfer, GjsArgumentFlags flags,
    const GI::TypeInfo element_type, Maybe<size_t> length, GIArgument* arg) {
    Gjs::AutoPointer<uint8_t, void, g_free> arg_array{
        gjs_arg_steal<uint8_t*>(arg)};

    if (!arg_array)
        return true;

    if (element_transfer != GI_TRANSFER_EVERYTHING)
        return true;

    if (element_type.is_basic()) {
        release_basic_array_internal<release_type>(element_type.tag(), length,
                                                   arg_array.as<void*>());
        return true;
    }

    if constexpr (release_type == ArrayReleaseType::EXPLICIT_LENGTH) {
        if (*length == 0)
            return true;
    }

    if (flags & GjsArgumentFlags::ARG_IN &&
        !type_needs_release(element_type, element_type.tag()))
        return true;

    if (flags & GjsArgumentFlags::ARG_OUT &&
        !type_needs_out_release(element_type, element_type.tag()))
        return true;

    GITypeTag type_tag = element_type.tag();
    size_t element_size = gjs_type_get_element_size(type_tag, element_type);
    if G_UNLIKELY (element_size == 0)
        return true;

    bool is_pointer = element_type.is_pointer();
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
        if (!gjs_g_arg_release_internal(cx, element_transfer, element_type,
                                        type_tag, GJS_ARGUMENT_ARRAY_ELEMENT,
                                        flags, &elem)) {
            return false;
        }

        if constexpr (release_type == ArrayReleaseType::EXPLICIT_LENGTH) {
            if (i == *length - 1)
                break;
        }
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

static void release_basic_type_internal(GITypeTag type_tag, GIArgument* arg) {
    if (is_string_type(type_tag))
        g_clear_pointer(&gjs_arg_member<char*>(arg), g_free);
}

template <typename T>
static void basic_linked_list_release(GITransfer transfer,
                                      GITypeTag element_tag, GIArgument* arg) {
    static_assert(std::is_same_v<T, GList> || std::is_same_v<T, GSList>);
    g_assert(GI_TYPE_TAG_IS_BASIC(element_tag) &&
             "use gjs_g_arg_release_g_list() for lists with non-basic types");

    Gjs::SmartPointer<T> list = gjs_arg_steal<T*>(arg);

    if (transfer == GI_TRANSFER_CONTAINER)
        return;

    GIArgument elem;
    for (T* l = list; l; l = l->next) {
        gjs_arg_set(&elem, l->data);
        release_basic_type_internal(element_tag, &elem);
    }
}

void gjs_gi_argument_release_basic_glist(GITransfer transfer,
                                         GITypeTag element_tag,
                                         GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Releasing GIArgument GList");
    basic_linked_list_release<GList>(transfer, element_tag, arg);
}

void gjs_gi_argument_release_basic_gslist(GITransfer transfer,
                                          GITypeTag element_tag,
                                          GIArgument* arg) {
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Releasing GIArgument GSList");
    basic_linked_list_release<GSList>(transfer, element_tag, arg);
}

void gjs_gi_argument_release_basic_ghash(GITransfer transfer, GITypeTag key_tag,
                                         GITypeTag value_tag, GIArgument* arg) {
    g_assert(GI_TYPE_TAG_IS_BASIC(key_tag) && GI_TYPE_TAG_IS_BASIC(value_tag));

    if (!gjs_arg_get<GHashTable*>(arg))
        return;

    Gjs::AutoPointer<GHashTable, GHashTable, g_hash_table_destroy> hash_table{
        gjs_arg_steal<GHashTable*>(arg)};
    if (transfer == GI_TRANSFER_CONTAINER) {
        g_hash_table_remove_all(hash_table);
    } else {
        std::array<GITypeTag, 2> data{key_tag, value_tag};
        g_hash_table_foreach_steal(
            hash_table,
            [](void* key, void* val, void* user_data) -> gboolean {
                auto* tags = static_cast<std::array<GITypeTag, 2>*>(user_data);
                GITypeTag key_tag = (*tags)[0], value_tag = (*tags)[1];
                GIArgument key_arg, val_arg;
                gjs_arg_set(&key_arg, key);
                gjs_arg_set(&val_arg, val);
                release_basic_type_internal(key_tag, &key_arg);

                switch (value_tag) {
                    case GI_TYPE_TAG_DOUBLE:
                    case GI_TYPE_TAG_FLOAT:
                    case GI_TYPE_TAG_INT64:
                    case GI_TYPE_TAG_UINT64:
                        g_clear_pointer(&gjs_arg_member<void*>(&val_arg),
                                        g_free);
                        break;

                    default:
                        release_basic_type_internal(value_tag, &val_arg);
                }

                return true;
            },
            &data);
    }
}

void gjs_gi_argument_release_basic_c_array(GITransfer transfer,
                                           GITypeTag element_tag,
                                           GIArgument* arg) {
    if (!gjs_arg_get<void*>(arg))
        return;

    if (is_string_type(element_tag) && transfer != GI_TRANSFER_CONTAINER)
        g_clear_pointer(&gjs_arg_member<GStrv>(arg), g_strfreev);
    else
        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
}

void gjs_gi_argument_release_basic_c_array(GITransfer transfer,
                                           GITypeTag element_tag, size_t length,
                                           GIArgument* arg) {
    if (!gjs_arg_get<void*>(arg))
        return;

    Gjs::AutoPointer<void*, void, g_free> array{gjs_arg_steal<void**>(arg)};

    if (!is_string_type(element_tag) || transfer == GI_TRANSFER_CONTAINER)
        return;

    for (size_t ix = 0; ix < length; ix++)
        g_free(array[ix]);
}

void gjs_gi_argument_release_basic_garray(GITransfer transfer,
                                          GITypeTag element_tag,
                                          GIArgument* arg) {
    if (!gjs_arg_get<void*>(arg))
        return;

    Gjs::AutoPointer<GArray, GArray, g_array_unref> array{
        gjs_arg_steal<GArray*>(arg)};

    if (transfer == GI_TRANSFER_CONTAINER || !is_string_type(element_tag))
        return;

    for (size_t ix = 0; ix < array->len; ix++)
        g_free(g_array_index(array, char*, ix));
}

void gjs_gi_argument_release_byte_array(GIArgument* arg) {
    if (!gjs_arg_get<void*>(arg))
        return;

    g_clear_pointer(&gjs_arg_member<GByteArray*>(arg), g_byte_array_unref);
}

void gjs_gi_argument_release_basic_gptrarray(GITransfer transfer,
                                             GITypeTag element_tag,
                                             GIArgument* arg) {
    if (!gjs_arg_get<void*>(arg))
        return;

    Gjs::AutoPointer<GPtrArray, GPtrArray, g_ptr_array_unref> array{
        gjs_arg_steal<GPtrArray*>(arg)};

    if (transfer == GI_TRANSFER_CONTAINER || !is_string_type(element_tag))
        return;

    g_ptr_array_foreach(
        array, [](void* ptr, void*) { g_free(ptr); }, nullptr);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_g_arg_release_internal(
    JSContext* context, GITransfer transfer, const GI::TypeInfo type_info,
    GITypeTag type_tag, [[maybe_unused]] GjsArgumentType argument_type,
    GjsArgumentFlags flags, GIArgument* arg) {
    g_assert(transfer != GI_TRANSFER_NOTHING ||
             flags != GjsArgumentFlags::NONE);

    if (type_info.is_basic()) {
        release_basic_type_internal(type_tag, arg);
        return true;
    }

    if (type_tag == GI_TYPE_TAG_GLIST) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            basic_linked_list_release<GList>(transfer, element_type.tag(), arg);
            return true;
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_GSLIST) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            basic_linked_list_release<GSList>(transfer, element_type.tag(),
                                              arg);
            return true;
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_GHASH) {
        GI::AutoTypeInfo key_type{type_info.key_type()};
        GI::AutoTypeInfo value_type{type_info.value_type()};
        if (key_type.is_basic() && value_type.is_basic()) {
            gjs_gi_argument_release_basic_ghash(transfer, key_type.tag(),
                                                value_type.tag(), arg);
            return true;
        }
        // else fall through to generic marshaller
    }

    if (type_tag == GI_TYPE_TAG_ARRAY) {
        GI::AutoTypeInfo element_type{type_info.element_type()};
        if (element_type.is_basic()) {
            switch (type_info.array_type()) {
                case GI_ARRAY_TYPE_C:
                    gjs_gi_argument_release_basic_c_array(
                        transfer, element_type.tag(), arg);
                    return true;
                case GI_ARRAY_TYPE_ARRAY:
                    gjs_gi_argument_release_basic_garray(
                        transfer, element_type.tag(), arg);
                    return true;
                case GI_ARRAY_TYPE_BYTE_ARRAY:
                    gjs_gi_argument_release_byte_array(arg);
                    return true;
                case GI_ARRAY_TYPE_PTR_ARRAY:
                    gjs_gi_argument_release_basic_gptrarray(
                        transfer, element_type.tag(), arg);
                    return true;
                default:
                    g_assert_not_reached();
            }
        }
        // else fall through to generic marshaller
    }

    switch (type_tag) {
    case GI_TYPE_TAG_VOID:
        g_assert(type_info.is_pointer() &&
                 "non-pointer should be handled by "
                 "release_basic_type_internal()");
        break;

    case GI_TYPE_TAG_ERROR:
        if (!is_transfer_in_nothing(transfer, flags))
            g_clear_error(&gjs_arg_member<GError*>(arg));
        break;

    case GI_TYPE_TAG_INTERFACE:
        {
            GI::AutoBaseInfo interface_info{type_info.interface()};

            if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>();
                struct_info && struct_info->is_foreign())
                return gjs_struct_foreign_release_gi_argument(
                    context, transfer, struct_info.value(), arg);

            if (interface_info.is_enum_or_flags())
                return true;  // enum and flags

            /* Anything else is a pointer */
            if (!gjs_arg_get<void*>(arg))
                return true;

            GType gtype =
                interface_info.as<GI::InfoTag::REGISTERED_TYPE>()->gtype();
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
                if (type_info.is_pointer())
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
        GIArrayType array_type = type_info.array_type();

        if (!gjs_arg_get<void*>(arg)) {
            /* OK */
        } else if (array_type == GI_ARRAY_TYPE_C) {
            GI::AutoTypeInfo element_type{type_info.element_type()};

            switch (element_type.tag()) {
            case GI_TYPE_TAG_INTERFACE:
                if (!element_type.is_pointer()) {
                    GI::AutoBaseInfo interface_info{element_type.interface()};
                    if (interface_info.is_struct() ||
                        interface_info.is_union()) {
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

                if (type_info.is_zero_terminated()) {
                    return gjs_gi_argument_release_array_internal<
                        ArrayReleaseType::ZERO_TERMINATED>(
                        context, element_transfer,
                        flags | GjsArgumentFlags::ARG_OUT, element_type, {},
                        arg);
                } else {
                    return gjs_gi_argument_release_array_internal<
                        ArrayReleaseType::EXPLICIT_LENGTH>(
                        context, element_transfer,
                        flags | GjsArgumentFlags::ARG_OUT, element_type,
                        type_info.array_fixed_size(), arg);
                }
            }

            default:
                // basic types handled above
                gjs_throw(context,
                          "Releasing a C array with explicit length, that was nested"
                          "inside another container. This is not supported (and will leak)");
                return false;
            }
        } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
            GI::AutoTypeInfo element_type = type_info.element_type();
            GITypeTag element_tag = element_type.tag();

            switch (element_tag) {
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_INTERFACE:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_GHASH:
            case GI_TYPE_TAG_ERROR: {
                Gjs::AutoPointer<GArray, GArray, g_array_unref> array{
                    gjs_arg_steal<GArray*>(arg)};

                if (transfer != GI_TRANSFER_CONTAINER &&
                    type_needs_out_release(element_type, element_tag)) {
                    guint i;

                    for (i = 0; i < array->len; i++) {
                        GIArgument arg_iter;

                        gjs_arg_set(&arg_iter,
                                    g_array_index(array, gpointer, i));
                        if (!gjs_g_arg_release_internal(
                                context, transfer, element_type, element_tag,
                                GJS_ARGUMENT_ARRAY_ELEMENT, flags, &arg_iter))
                            return false;
                    }
                }

                break;
            }

            default:
                // basic types handled above
                gjs_throw(context,
                          "Don't know how to release GArray element-type %d",
                          element_tag);
                return false;
            }

        } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
            GI::AutoTypeInfo element_type{type_info.element_type()};
            Gjs::AutoPointer<GPtrArray, GPtrArray, g_ptr_array_unref> array{
                gjs_arg_steal<GPtrArray*>(arg)};

            if (transfer != GI_TRANSFER_CONTAINER) {
                guint i;

                for (i = 0; i < array->len; i++) {
                    GIArgument arg_iter;

                    gjs_arg_set(&arg_iter, g_ptr_array_index(array, i));
                    if (!gjs_gi_argument_release(
                            context, transfer, element_type, flags, &arg_iter))
                        return false;
                }
            }
        } else {
            // GI_ARRAY_TYPE_BYTEARRAY handled above; other values unknown
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
            Gjs::AutoPointer<GHashTable, GHashTable, g_hash_table_destroy>
                hash_table{gjs_arg_steal<GHashTable*>(arg)};
            if (transfer == GI_TRANSFER_CONTAINER)
                g_hash_table_remove_all(hash_table);
            else {
                GHR_closure c = {context,
                                 type_info.key_type(),
                                 type_info.value_type(),
                                 transfer,
                                 flags,
                                 false};

                g_hash_table_foreach_steal(hash_table, gjs_ghr_helper, &c);

                if (c.failed)
                    return false;
            }
        }
        break;

    default:
        // basic types should have been handled in release_basic_type_internal()
        g_warning("Unhandled type %s releasing GIArgument",
                  gi_type_tag_to_string(type_tag));
        return false;
    }

    return true;
}

bool gjs_gi_argument_release(JSContext* cx, GITransfer transfer,
                             const GI::TypeInfo type_info,
                             GjsArgumentFlags flags, GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING &&
        !is_transfer_in_nothing(transfer, flags))
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument %s out param or return value",
                      type_info.type_string());

    return gjs_g_arg_release_internal(cx, transfer, type_info, type_info.tag(),
                                      GJS_ARGUMENT_ARGUMENT, flags, arg);
}

void gjs_gi_argument_release_basic(GITransfer transfer, GITypeTag type_tag,
                                   GjsArgumentFlags flags, GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING &&
        !is_transfer_in_nothing(transfer, flags))
        return;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument %s out param or return value",
                      gi_type_tag_to_string(type_tag));

    release_basic_type_internal(type_tag, arg);
}

bool gjs_gi_argument_release_in_arg(JSContext* cx, GITransfer transfer,
                                    const GI::TypeInfo type_info,
                                    GjsArgumentFlags flags, GIArgument* arg) {
    /* GI_TRANSFER_EVERYTHING: we don't own the argument anymore.
     * GI_TRANSFER_CONTAINER:
     * - non-containers: treated as GI_TRANSFER_EVERYTHING
     * - containers: See FIXME in gjs_array_to_g_list(); currently
     *   an error and we won't get here.
     */
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    GITypeTag tag = type_info.tag();

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Releasing GIArgument %s in param",
                      type_info.type_string());

    if (!type_needs_release(type_info, tag))
        return true;

    return gjs_g_arg_release_internal(cx, transfer, type_info, tag,
                                      GJS_ARGUMENT_ARGUMENT, flags, arg);
}

void gjs_gi_argument_release_basic_in_array(GITransfer transfer,
                                            GITypeTag element_tag,
                                            GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument basic C array in param");

    if (is_string_type(element_tag) && transfer != GI_TRANSFER_CONTAINER)
        g_clear_pointer(&gjs_arg_member<GStrv>(arg), g_strfreev);
    else
        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
}

void gjs_gi_argument_release_basic_in_array(GITransfer transfer,
                                            GITypeTag element_tag,
                                            size_t length, GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument basic C array in param");

    Gjs::AutoPointer<void*, void, g_free> array{gjs_arg_steal<void**>(arg)};

    if (!is_string_type(element_tag))
        return;

    for (size_t ix = 0; ix < length; ix++)
        g_free(array[ix]);
}

bool gjs_gi_argument_release_in_array(JSContext* cx, GITransfer transfer,
                                      const GI::TypeInfo type_info,
                                      size_t length, GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array in param");

    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::EXPLICIT_LENGTH>(
        cx, GI_TRANSFER_EVERYTHING, GjsArgumentFlags::ARG_IN,
        type_info.element_type(), Some(length), arg);
}

bool gjs_gi_argument_release_in_array(JSContext* cx, GITransfer transfer,
                                      const GI::TypeInfo type_info,
                                      GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array in param");

    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::ZERO_TERMINATED>(cx, GI_TRANSFER_EVERYTHING,
                                           GjsArgumentFlags::ARG_IN,
                                           type_info.element_type(), {}, arg);
}

void gjs_gi_argument_release_basic_out_array(GITransfer transfer,
                                             GITypeTag element_tag,
                                             GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    if (is_string_type(element_tag) && transfer != GI_TRANSFER_CONTAINER)
        g_clear_pointer(&gjs_arg_member<GStrv>(arg), g_strfreev);
    else
        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
}

void gjs_gi_argument_release_basic_out_array(GITransfer transfer,
                                             GITypeTag element_tag,
                                             size_t length, GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    Gjs::AutoPointer<void*, void, g_free> array{gjs_arg_steal<void**>(arg)};

    if (transfer == GI_TRANSFER_CONTAINER || !is_string_type(element_tag))
        return;

    for (size_t ix = 0; ix < length; ix++)
        g_free(array[ix]);
}

bool gjs_gi_argument_release_out_array(JSContext* cx, GITransfer transfer,
                                       const GI::TypeInfo type_info,
                                       size_t length, GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    GITransfer element_transfer = transfer == GI_TRANSFER_CONTAINER
                                      ? GI_TRANSFER_NOTHING
                                      : GI_TRANSFER_EVERYTHING;

    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::EXPLICIT_LENGTH>(
        cx, element_transfer, GjsArgumentFlags::ARG_OUT,
        type_info.element_type(), Some(length), arg);
}

bool gjs_gi_argument_release_out_array(JSContext* context, GITransfer transfer,
                                       const GI::TypeInfo type_info,
                                       GIArgument* arg) {
    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Releasing GIArgument array out param");

    GITransfer element_transfer = transfer == GI_TRANSFER_CONTAINER
                                      ? GI_TRANSFER_NOTHING
                                      : GI_TRANSFER_EVERYTHING;

    return gjs_gi_argument_release_array_internal<
        ArrayReleaseType::ZERO_TERMINATED>(context, element_transfer,
                                           GjsArgumentFlags::ARG_OUT,
                                           type_info.element_type(), {}, arg);
}
