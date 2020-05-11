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

template <typename T>
GJS_USE inline decltype(auto) gjs_g_argument_value(GIArgument* arg,
                                                   T GIArgument::*member) {
    return (arg->*member);
}

/* The tag is needed to disambiguate types such as gboolean and GType
 * which are in fact typedef's of other generic types.
 * Setting a tag for a type allows to perform proper specialization. */
template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_USE inline decltype(auto) gjs_g_argument_value(GIArgument* arg) {
    static_assert(!std::is_arithmetic<T>(), "Missing declaration for type");

    using NonconstPtrT =
        std::add_pointer_t<std::remove_const_t<std::remove_pointer_t<T>>>;
    return reinterpret_cast<NonconstPtrT&>(
        gjs_g_argument_value(arg, &GIArgument::v_pointer));
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<bool>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_boolean);
}

template <>
GJS_USE inline decltype(auto)
gjs_g_argument_value<gboolean, GI_TYPE_TAG_BOOLEAN>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_boolean);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<int8_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_int8);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<uint8_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_uint8);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<int16_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_int16);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<uint16_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_uint16);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<int32_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_int32);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<uint32_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_uint32);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<int64_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_int64);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<uint64_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_uint64);
}

// gunichar is stored in v_uint32
template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<char32_t>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_uint32);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<GType, GI_TYPE_TAG_GTYPE>(
    GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_ssize);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<float>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_float);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<double>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_double);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<char*>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_string);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<void*>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_pointer);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<std::nullptr_t>(
    GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_pointer);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline void gjs_g_argument_value_set(GIArgument* arg, T v) {
    gjs_g_argument_value<T, TAG>(arg) = v;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline void gjs_g_argument_value_set(GIArgument* arg, T* v) {
    using NonconstT = std::remove_const_t<T>;
    gjs_g_argument_value<NonconstT*, TAG>(arg) = const_cast<NonconstT*>(v);
}

// Store function pointers as void*. It is a requirement of GLib that your
// compiler can do this
template <typename ReturnT, typename... Args>
inline void gjs_g_argument_value_set(GIArgument* arg, ReturnT (*v)(Args...)) {
    gjs_g_argument_value<void*>(arg) = reinterpret_cast<void*>(v);
}

template <>
inline void gjs_g_argument_value_set<bool>(GIArgument* arg, bool v) {
    gjs_g_argument_value<bool>(arg) = !!v;
}

template <>
inline void gjs_g_argument_value_set<gboolean, GI_TYPE_TAG_BOOLEAN>(
    GIArgument* arg, gboolean v) {
    gjs_g_argument_value<bool>(arg) = !!v;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_USE inline T gjs_g_argument_value_get(GIArgument* arg) {
    return gjs_g_argument_value<T, TAG>(arg);
}

template <>
GJS_USE inline bool gjs_g_argument_value_get<bool>(GIArgument* arg) {
    return !!gjs_g_argument_value<bool>(arg);
}

template <>
GJS_USE inline gboolean gjs_g_argument_value_get<gboolean, GI_TYPE_TAG_BOOLEAN>(
    GIArgument* arg) {
    return !!gjs_g_argument_value<bool>(arg);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<!std::is_pointer<T>::value> gjs_g_argument_value_unset(
    GIArgument* arg) {
    gjs_g_argument_value_set<T, TAG>(arg, static_cast<T>(0));
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline std::enable_if_t<std::is_pointer<T>::value> gjs_g_argument_value_unset(
    GIArgument* arg) {
    gjs_g_argument_value_set<T, TAG>(arg, nullptr);
}
