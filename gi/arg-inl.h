/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include <stdint.h>

#include <cstddef>  // for nullptr_t
#include <type_traits>

#include <girepository.h>
#include <glib-object.h>  // for GType
#include <glib.h>         // for gboolean

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

template <typename T>
GJS_USE inline decltype(auto) gjs_arg_member(GIArgument* arg,
                                             T GIArgument::*member) {
    return (arg->*member);
}

/* The tag is needed to disambiguate types such as gboolean and GType
 * which are in fact typedef's of other generic types.
 * Setting a tag for a type allows to perform proper specialization. */
template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_USE inline decltype(auto) gjs_arg_member(GIArgument* arg) {
    static_assert(!std::is_arithmetic<T>(), "Missing declaration for type");

    using NonconstPtrT =
        std::add_pointer_t<std::remove_const_t<std::remove_pointer_t<T>>>;
    return reinterpret_cast<NonconstPtrT&>(
        gjs_arg_member(arg, &GIArgument::v_pointer));
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<bool>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_boolean);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<gboolean, GI_TYPE_TAG_BOOLEAN>(
    GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_boolean);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<int8_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_int8);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<uint8_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint8);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<int16_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_int16);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<uint16_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint16);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<int32_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_int32);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<uint32_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint32);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<int64_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_int64);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<uint64_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint64);
}

// gunichar is stored in v_uint32
template <>
GJS_USE inline decltype(auto) gjs_arg_member<char32_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint32);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<GType, GI_TYPE_TAG_GTYPE>(
    GIArgument* arg) {
    // GType is defined differently on 32-bit vs. 64-bit architectures. From gtype.h:
    //
    // #if     GLIB_SIZEOF_SIZE_T != GLIB_SIZEOF_LONG || !defined __cplusplus
    // typedef gsize                           GType;
    // #else   /* for historic reasons, C++ links against gulong GTypes */
    // typedef gulong                          GType;
    // #endif
    if constexpr (std::is_same_v<GType, gsize>)
        return gjs_arg_member(arg, &GIArgument::v_size);
    else if constexpr (std::is_same_v<GType, gulong>)
        return gjs_arg_member(arg, &GIArgument::v_ulong);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<float>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_float);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<double>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_double);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<char*>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_string);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<void*>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_pointer);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<std::nullptr_t>(GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_pointer);
}

template <>
GJS_USE inline decltype(auto) gjs_arg_member<int, GI_TYPE_TAG_INTERFACE>(
    GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_int);
}

// Unsigned enums
template <>
GJS_USE inline decltype(auto) gjs_arg_member<unsigned, GI_TYPE_TAG_INTERFACE>(
    GIArgument* arg) {
    return gjs_arg_member(arg, &GIArgument::v_uint);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<!std::is_pointer_v<T>> gjs_arg_set(GIArgument* arg,
                                                           T v) {
    gjs_arg_member<T, TAG>(arg) = v;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<std::is_pointer_v<T>> gjs_arg_set(GIArgument* arg,
                                                          T v) {
    using NonconstPtrT =
        std::add_pointer_t<std::remove_const_t<std::remove_pointer_t<T>>>;
    gjs_arg_member<NonconstPtrT, TAG>(arg) = const_cast<NonconstPtrT>(v);
}

// Store function pointers as void*. It is a requirement of GLib that your
// compiler can do this
template <typename ReturnT, typename... Args>
inline void gjs_arg_set(GIArgument* arg, ReturnT (*v)(Args...)) {
    gjs_arg_member<void*>(arg) = reinterpret_cast<void*>(v);
}

template <>
inline void gjs_arg_set<bool>(GIArgument* arg, bool v) {
    gjs_arg_member<bool>(arg) = !!v;
}

template <>
inline void gjs_arg_set<gboolean, GI_TYPE_TAG_BOOLEAN>(GIArgument* arg,
                                                       gboolean v) {
    gjs_arg_member<bool>(arg) = !!v;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_USE inline T gjs_arg_get(GIArgument* arg) {
    return gjs_arg_member<T, TAG>(arg);
}

template <>
GJS_USE inline bool gjs_arg_get<bool>(GIArgument* arg) {
    return !!gjs_arg_member<bool>(arg);
}

template <>
GJS_USE inline gboolean gjs_arg_get<gboolean, GI_TYPE_TAG_BOOLEAN>(
    GIArgument* arg) {
    return !!gjs_arg_member<bool>(arg);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<!std::is_pointer_v<T>> gjs_arg_unset(GIArgument* arg) {
    gjs_arg_set<T, TAG>(arg, static_cast<T>(0));
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<std::is_pointer_v<T>> gjs_arg_unset(GIArgument* arg) {
    gjs_arg_set<T, TAG>(arg, nullptr);
}
