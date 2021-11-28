/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <girepository.h>
#include <glib-object.h>  // for GType
#include <glib.h>         // for gboolean

namespace Gjs {

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
constexpr inline const char* static_type_name() = delete;

template <>
constexpr inline const char* static_type_name<bool>() {
    return "bool";
}

template <>
constexpr inline const char* static_type_name<int8_t>() {
    return "int8";
}

template <>
constexpr inline const char* static_type_name<uint8_t>() {
    return "uint8";
}

template <>
constexpr inline const char* static_type_name<int16_t>() {
    return "int16";
}

template <>
constexpr inline const char* static_type_name<uint16_t>() {
    return "uint16";
}

template <>
constexpr inline const char* static_type_name<int32_t>() {
    return "int32";
}

template <>
constexpr inline const char* static_type_name<uint32_t>() {
    return "uint32";
}

template <>
constexpr inline const char* static_type_name<char32_t>() {
    return "char32_t";
}

template <>
constexpr inline const char* static_type_name<int64_t>() {
    return "int64";
}

template <>
constexpr inline const char* static_type_name<uint64_t>() {
    return "uint64";
}

template <>
constexpr inline const char* static_type_name<float>() {
    return "float";
}

template <>
constexpr inline const char* static_type_name<double>() {
    return "double";
}

template <>
constexpr inline const char* static_type_name<void*>() {
    return "pointer";
}

template <>
constexpr inline const char* static_type_name<GType, GI_TYPE_TAG_GTYPE>() {
    return "GType";
}

template <>
constexpr inline const char* static_type_name<gboolean, GI_TYPE_TAG_BOOLEAN>() {
    return "boolean";
}

template <>
constexpr inline const char* static_type_name<GValue>() {
    return "GValue";
}

template <>
inline const char* static_type_name<char*>() {
    return "string";
}

}  // namespace Gjs
