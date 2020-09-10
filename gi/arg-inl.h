/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include <stdint.h>

#include <cstddef>  // for nullptr_t
#include <limits>
#include <string>  // for to_string
#include <type_traits>

#include <girepository.h>
#include <glib-object.h>  // for GType
#include <glib.h>         // for gboolean

#include "gi/utils-inl.h"

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

template <typename T>
[[nodiscard]] inline decltype(auto) gjs_arg_member(GIArgument* arg,
                                                   T GIArgument::*member) {
    return (arg->*member);
}

/* The tag is needed to disambiguate types such as gboolean and GType
 * which are in fact typedef's of other generic types.
 * Setting a tag for a type allows to perform proper specialization. */
template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
[[nodiscard]] inline decltype(auto) gjs_arg_member(GIArgument* arg) {
    if constexpr (TAG == GI_TYPE_TAG_VOID) {
        if constexpr (std::is_same_v<T, bool>)
            return gjs_arg_member(arg, &GIArgument::v_boolean);
        if constexpr (std::is_same_v<T, int8_t>)
            return gjs_arg_member(arg, &GIArgument::v_int8);
        if constexpr (std::is_same_v<T, uint8_t>)
            return gjs_arg_member(arg, &GIArgument::v_uint8);
        if constexpr (std::is_same_v<T, int16_t>)
            return gjs_arg_member(arg, &GIArgument::v_int16);
        if constexpr (std::is_same_v<T, uint16_t>)
            return gjs_arg_member(arg, &GIArgument::v_uint16);
        if constexpr (std::is_same_v<T, int32_t>)
            return gjs_arg_member(arg, &GIArgument::v_int32);
        if constexpr (std::is_same_v<T, uint32_t>)
            return gjs_arg_member(arg, &GIArgument::v_uint32);
        if constexpr (std::is_same_v<T, int64_t>)
            return gjs_arg_member(arg, &GIArgument::v_int64);
        if constexpr (std::is_same_v<T, uint64_t>)
            return gjs_arg_member(arg, &GIArgument::v_uint64);

        // gunichar is stored in v_uint32
        if constexpr (std::is_same_v<T, char32_t>)
            return gjs_arg_member(arg, &GIArgument::v_uint32);

        if constexpr (std::is_same_v<T, float>)
            return gjs_arg_member(arg, &GIArgument::v_float);

        if constexpr (std::is_same_v<T, double>)
            return gjs_arg_member(arg, &GIArgument::v_double);

        if constexpr (std::is_same_v<T, char*>)
            return gjs_arg_member(arg, &GIArgument::v_string);

        if constexpr (std::is_same_v<T, void*>)
            return gjs_arg_member(arg, &GIArgument::v_pointer);

        if constexpr (std::is_same_v<T, std::nullptr_t>)
            return gjs_arg_member(arg, &GIArgument::v_pointer);

        if constexpr (std::is_pointer<T>()) {
            using NonconstPtrT = std::add_pointer_t<
                std::remove_const_t<std::remove_pointer_t<T>>>;
            return reinterpret_cast<NonconstPtrT&>(
                gjs_arg_member(arg, &GIArgument::v_pointer));
        }
    }

    if constexpr (TAG == GI_TYPE_TAG_BOOLEAN && std::is_same_v<T, gboolean>)
        return gjs_arg_member(arg, &GIArgument::v_boolean);

    if constexpr (TAG == GI_TYPE_TAG_GTYPE && std::is_same_v<T, GType>) {
        // GType is defined differently on 32-bit vs. 64-bit architectures.
        if constexpr (std::is_same_v<GType, gsize>)
            return gjs_arg_member(arg, &GIArgument::v_size);
        else if constexpr (std::is_same_v<GType, gulong>)
            return gjs_arg_member(arg, &GIArgument::v_ulong);
    }

    if constexpr (TAG == GI_TYPE_TAG_INTERFACE && std::is_integral_v<T>) {
        if constexpr (std::is_signed_v<T>)
            return gjs_arg_member(arg, &GIArgument::v_int);
        else
            return gjs_arg_member(arg, &GIArgument::v_uint);
    }
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline void gjs_arg_set(GIArgument* arg, T v) {
    if constexpr (std::is_pointer_v<T>) {
        using NonconstPtrT =
            std::add_pointer_t<std::remove_const_t<std::remove_pointer_t<T>>>;
        gjs_arg_member<NonconstPtrT, TAG>(arg) = const_cast<NonconstPtrT>(v);
    } else {
        if constexpr (std::is_same_v<T, bool> || (std::is_same_v<T, gboolean> &&
                                                  TAG == GI_TYPE_TAG_BOOLEAN))
            v = !!v;

        gjs_arg_member<T, TAG>(arg) = v;
    }
}

// Store function pointers as void*. It is a requirement of GLib that your
// compiler can do this
template <typename ReturnT, typename... Args>
inline void gjs_arg_set(GIArgument* arg, ReturnT (*v)(Args...)) {
    gjs_arg_member<void*>(arg) = reinterpret_cast<void*>(v);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<std::is_integral_v<T>> gjs_arg_set(GIArgument* arg,
                                                           void *v) {
    gjs_arg_set<T, TAG>(arg, gjs_pointer_to_int<T>(v));
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
[[nodiscard]] inline T gjs_arg_get(GIArgument* arg) {
    if constexpr (std::is_same_v<T, bool> ||
                  (std::is_same_v<T, gboolean> && TAG == GI_TYPE_TAG_BOOLEAN))
        return !!gjs_arg_member<T, TAG>(arg);

    return gjs_arg_member<T, TAG>(arg);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
[[nodiscard]] inline void* gjs_arg_get_as_pointer(GIArgument* arg) {
    return gjs_int_to_pointer(gjs_arg_get<T, TAG>(arg));
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline void gjs_arg_unset(GIArgument* arg) {
    if constexpr (std::is_pointer_v<T>)
        gjs_arg_set<T, TAG>(arg, nullptr);
    else
        gjs_arg_set<T, TAG>(arg, static_cast<T>(0));
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
[[nodiscard]] inline T gjs_arg_steal(GIArgument* arg) {
    auto val = gjs_arg_get<T, TAG>(arg);
    gjs_arg_unset<T, TAG>(arg);
    return val;
}

// Implementation to store rounded (u)int64_t numbers into double

template <typename BigT>
[[nodiscard]] inline constexpr BigT max_safe_big_number() {
    return BigT(1) << std::numeric_limits<double>::digits;
}

template <typename BigT>
[[nodiscard]] inline constexpr BigT min_safe_big_number() {
    if constexpr (std::is_signed_v<BigT>)
        return -(max_safe_big_number<BigT>()) + 1;

    return std::numeric_limits<BigT>::lowest();
}

template <typename BigT>
[[nodiscard]] inline std::enable_if_t<std::is_integral_v<BigT> &&
                                          (std::numeric_limits<BigT>::max() >
                                           std::numeric_limits<int32_t>::max()),
                                      double>
gjs_arg_get_maybe_rounded(GIArgument* arg) {
    BigT val = gjs_arg_get<BigT>(arg);

    if (val < min_safe_big_number<BigT>() ||
        val > max_safe_big_number<BigT>()) {
        g_warning(
            "Value %s cannot be safely stored in a JS Number "
            "and may be rounded",
            std::to_string(val).c_str());
    }

    return static_cast<double>(val);
}
