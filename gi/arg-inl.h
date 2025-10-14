/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>
#include <string.h>  // for memset

#include <cstddef>  // for nullptr_t
#include <limits>
#include <string>  // for to_string
#include <type_traits>

#include <girepository/girepository.h>
#include <glib-object.h>  // for GType
#include <glib.h>         // for gboolean
// IWYU pragma: no_forward_declare _GBytes
// IWYU pragma: no_forward_declare _GHashTable
// IWYU pragma: no_forward_declare _GVariant

#include <js/RootingAPI.h>  // for Handle
#include <js/TypeDecls.h>  // for HandleValue

#include "gi/arg-types-inl.h"
#include "gi/js-value-inl.h"
#include "gi/utils-inl.h"
#include "gjs/macros.h"

// GIArgument accessor templates
//
// These are intended to make access to the GIArgument union more type-safe and
// reduce bugs that occur from assigning to one member and reading from another.
// (These bugs often work fine on one processor architecture but crash on
// another.)
//
// gjs_arg_member<T>(GIArgument*) - returns a reference to the appropriate union
//   member that would hold the type T. Rarely used, unless as a pointer to a
//   return location.
// gjs_arg_get<T>(GIArgument*) - returns the value of type T from the
//   appropriate union member.
// gjs_arg_set(GIArgument*, T) - sets the appropriate union member for type T.
// gjs_arg_unset<T>(GIArgument*) - sets the appropriate zero value in the
//   appropriate union member for type T.
// gjs_arg_steal<T>(GIArgument*) - sets the appropriate zero value in the
//   appropriate union member for type T and returns the replaced value.

template <auto GIArgument::*member>
[[nodiscard]] constexpr inline decltype(auto) gjs_arg_member(GIArgument* arg) {
    return (arg->*member);
}

template <typename TAG>
[[nodiscard]] constexpr inline decltype(auto) gjs_arg_member(GIArgument* arg) {
    if constexpr (std::is_same_v<TAG, Gjs::Tag::GBoolean>)
        return gjs_arg_member<&GIArgument::v_boolean>(arg);

    if constexpr (std::is_same_v<TAG, Gjs::Tag::GType>) {
        // GType is defined differently on 32-bit vs. 64-bit architectures.
        if constexpr (std::is_same_v<GType, gsize>)
            return gjs_arg_member<&GIArgument::v_size>(arg);
        else if constexpr (std::is_same_v<GType, gulong>)
            return gjs_arg_member<&GIArgument::v_ulong>(arg);
    }

    if constexpr (std::is_same_v<TAG, Gjs::Tag::Long>)
        return gjs_arg_member<&GIArgument::v_long>(arg);
    if constexpr (std::is_same_v<TAG, Gjs::Tag::UnsignedLong>)
        return gjs_arg_member<&GIArgument::v_ulong>(arg);

    if constexpr (std::is_same_v<TAG, Gjs::Tag::Enum>)
        return gjs_arg_member<&GIArgument::v_int>(arg);
    if constexpr (std::is_same_v<TAG, Gjs::Tag::UnsignedEnum>)
        return gjs_arg_member<&GIArgument::v_uint>(arg);

    if constexpr (std::is_same_v<TAG, bool>)
        return gjs_arg_member<&GIArgument::v_boolean>(arg);
    if constexpr (std::is_same_v<TAG, int8_t>)
        return gjs_arg_member<&GIArgument::v_int8>(arg);
    if constexpr (std::is_same_v<TAG, uint8_t>)
        return gjs_arg_member<&GIArgument::v_uint8>(arg);
    if constexpr (std::is_same_v<TAG, int16_t>)
        return gjs_arg_member<&GIArgument::v_int16>(arg);
    if constexpr (std::is_same_v<TAG, uint16_t>)
        return gjs_arg_member<&GIArgument::v_uint16>(arg);
    if constexpr (std::is_same_v<TAG, int32_t>)
        return gjs_arg_member<&GIArgument::v_int32>(arg);
    if constexpr (std::is_same_v<TAG, uint32_t>)
        return gjs_arg_member<&GIArgument::v_uint32>(arg);
    if constexpr (std::is_same_v<TAG, int64_t>)
        return gjs_arg_member<&GIArgument::v_int64>(arg);
    if constexpr (std::is_same_v<TAG, uint64_t>)
        return gjs_arg_member<&GIArgument::v_uint64>(arg);

    // gunichar is stored in v_uint32
    if constexpr (std::is_same_v<TAG, char32_t>)
        return gjs_arg_member<&GIArgument::v_uint32>(arg);

    if constexpr (std::is_same_v<TAG, float>)
        return gjs_arg_member<&GIArgument::v_float>(arg);

    if constexpr (std::is_same_v<TAG, double>)
        return gjs_arg_member<&GIArgument::v_double>(arg);

    if constexpr (std::is_same_v<TAG, char*>)
        return gjs_arg_member<&GIArgument::v_string>(arg);

    if constexpr (std::is_same_v<TAG, void*>)
        return gjs_arg_member<&GIArgument::v_pointer>(arg);

    if constexpr (std::is_same_v<TAG, std::nullptr_t>)
        return gjs_arg_member<&GIArgument::v_pointer>(arg);

    if constexpr (std::is_pointer<TAG>()) {
        using NonconstPtrT =
            std::add_pointer_t<std::remove_const_t<std::remove_pointer_t<TAG>>>;
        return reinterpret_cast<NonconstPtrT&>(
            gjs_arg_member<&GIArgument::v_pointer>(arg));
    }
}

template <typename TAG, typename = std::enable_if_t<
                            std::is_arithmetic_v<Gjs::Tag::RealT<TAG>>>>
constexpr inline void gjs_arg_set(GIArgument* arg, Gjs::Tag::RealT<TAG> v) {
    if constexpr (std::is_same_v<TAG, bool> ||
                  std::is_same_v<TAG, Gjs::Tag::GBoolean>)
        v = !!v;

    gjs_arg_member<TAG>(arg) = v;
}

// Specialization for types where TAG and RealT<TAG> are the same type, to allow
// inferring template parameter
template <typename T,
          typename = std::enable_if_t<std::is_same_v<Gjs::Tag::RealT<T>, T> &&
                                      std::is_arithmetic_v<T>>>
constexpr inline void gjs_arg_set(GIArgument* arg, T v) {
    gjs_arg_set<T>(arg, v);
}

// Specialization for non-function pointers, so that you don't have to repeat
// the pointer type explicitly for type deduction, and that takes care of
// GIArgument not having constness
template <typename T, typename = std::enable_if_t<!std::is_function_v<T>>>
constexpr inline void gjs_arg_set(GIArgument* arg, T* v) {
    using NonconstPtrT = std::add_pointer_t<std::remove_const_t<T>>;
    gjs_arg_member<NonconstPtrT>(arg) = const_cast<NonconstPtrT>(v);
}

// Overload for nullptr since it's not handled by TAG*
constexpr inline void gjs_arg_set(GIArgument* arg, std::nullptr_t) {
    gjs_arg_member<void*>(arg) = nullptr;
}

// Store function pointers as void*. It is a requirement of GLib that your
// compiler can do this
template <typename ReturnT, typename... Args>
constexpr inline void gjs_arg_set(GIArgument* arg, ReturnT (*v)(Args...)) {
    gjs_arg_member<void*>(arg) = reinterpret_cast<void*>(v);
}

// Specifying an integer-type tag and passing a void pointer, extracts a stuffed
// integer out of the pointer; otherwise just store the pointer in v_pointer
template <typename TAG = void*>
constexpr inline void gjs_arg_set(GIArgument* arg, void* v) {
    using T = Gjs::Tag::RealT<TAG>;
    if constexpr (std::is_integral_v<T>)
        gjs_arg_set<TAG>(arg, gjs_pointer_to_int<T>(v));
    else
        gjs_arg_member<void*>(arg) = v;
}

template <typename TAG>
[[nodiscard]] constexpr inline Gjs::Tag::RealT<TAG> gjs_arg_get(
    GIArgument* arg) {
    if constexpr (std::is_same_v<TAG, bool> ||
                  std::is_same_v<TAG, Gjs::Tag::GBoolean>)
        return Gjs::Tag::RealT<TAG>(!!gjs_arg_member<TAG>(arg));

    return gjs_arg_member<TAG>(arg);
}

template <typename TAG>
[[nodiscard]] constexpr inline void* gjs_arg_get_as_pointer(GIArgument* arg) {
    return gjs_int_to_pointer(gjs_arg_get<TAG>(arg));
}

constexpr inline void gjs_arg_unset(GIArgument* arg) {
    // Clear all bits of the out C value. No one member is guaranteed to span
    // the whole union on all architectures, so use memset() instead of
    // gjs_arg_set<T>(arg, 0) for some type T.
    memset(arg, 0, sizeof(GIArgument));
}

template <typename TAG>
[[nodiscard]] constexpr inline Gjs::Tag::RealT<TAG> gjs_arg_steal(
    GIArgument* arg) {
    auto val = gjs_arg_get<TAG>(arg);
    gjs_arg_unset(arg);
    return val;
}

// Implementation to store rounded (u)int64_t numbers into double

template <typename BigTag>
[[nodiscard]] inline constexpr std::enable_if_t<
    std::is_integral_v<Gjs::Tag::RealT<BigTag>> &&
        (std::numeric_limits<Gjs::Tag::RealT<BigTag>>::max() >
         std::numeric_limits<int32_t>::max()),
    double>
gjs_arg_get_maybe_rounded(GIArgument* arg) {
    using BigT = Gjs::Tag::RealT<BigTag>;
    BigT val = gjs_arg_get<BigTag>(arg);

    if (val < Gjs::min_safe_big_number<BigT>() ||
        val > Gjs::max_safe_big_number<BigT>()) {
        g_warning(
            "Value %s cannot be safely stored in a JS Number "
            "and may be rounded",
            std::to_string(val).c_str());
    }

    return static_cast<double>(val);
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION inline bool gjs_arg_set_from_js_value(
    JSContext* cx, JS::HandleValue value, GIArgument* arg, bool* out_of_range) {
    if constexpr (Gjs::type_has_js_getter<TAG>())
        return Gjs::js_value_to_c<TAG>(cx, value, &gjs_arg_member<TAG>(arg));

    Gjs::Tag::JSValuePackT<TAG> val{};

    using T = Gjs::Tag::RealT<TAG>;
    using HolderTag = Gjs::Tag::JSValuePackTag<TAG>;
    if (!Gjs::js_value_to_c_checked<T, HolderTag>(cx, value, &val,
                                                  out_of_range))
        return false;

    if (*out_of_range)
        return false;

    gjs_arg_set<TAG>(arg, val);

    return true;
}

// A helper function to retrieve array lengths from a GIArgument (letting the
// compiler generate good instructions in case of big endian machines)
[[nodiscard]] constexpr size_t gjs_gi_argument_get_array_length(
    GITypeTag tag, GIArgument* arg) {
    switch (tag) {
        case GI_TYPE_TAG_INT8:
            return gjs_arg_get<int8_t>(arg);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_get<uint8_t>(arg);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_get<int16_t>(arg);
        case GI_TYPE_TAG_UINT16:
            return gjs_arg_get<uint16_t>(arg);
        case GI_TYPE_TAG_INT32:
            return gjs_arg_get<int32_t>(arg);
        case GI_TYPE_TAG_UINT32:
            return gjs_arg_get<uint32_t>(arg);
        case GI_TYPE_TAG_INT64:
            return gjs_arg_get<int64_t>(arg);
        case GI_TYPE_TAG_UINT64:
            return gjs_arg_get<uint64_t>(arg);
        default:
            g_assert_not_reached();
    }
}

namespace Gjs {

[[nodiscard]] static inline bool basic_type_needs_release(GITypeTag tag) {
    g_assert(GI_TYPE_TAG_IS_BASIC(tag));
    return tag == GI_TYPE_TAG_FILENAME || tag == GI_TYPE_TAG_UTF8;
}

}  // namespace Gjs
