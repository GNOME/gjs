/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include <stdint.h>

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

    return reinterpret_cast<T>(
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
    return gjs_g_argument_value(arg, &GIArgument::v_pointer);
}

template <>
GJS_USE inline decltype(auto) gjs_g_argument_value<void*>(GIArgument* arg) {
    return gjs_g_argument_value(arg, &GIArgument::v_pointer);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline void gjs_g_argument_value_set(GIArgument* arg, T v) {
    gjs_g_argument_value<T, TAG>(arg) = v;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
GJS_USE inline T gjs_g_argument_value_get(GIArgument* arg) {
    return gjs_g_argument_value<T, TAG>(arg);
}
