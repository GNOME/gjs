/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <girepository.h>
#include <glib-object.h>  // for GValue
#include <glib.h>         // for gboolean

namespace Gjs {

namespace Tag {
struct GBoolean {};
struct GType {};
}  // namespace Tag

template <typename TAG>
struct MarshallingInfo {};

template <>
struct MarshallingInfo<bool> {
    static constexpr const char* name = "bool";
};

template <>
struct MarshallingInfo<int8_t> {
    static constexpr const char* name = "int8";
};

template <>
struct MarshallingInfo<uint8_t> {
    static constexpr const char* name = "uint8";
};

template <>
struct MarshallingInfo<int16_t> {
    static constexpr const char* name = "int16";
};

template <>
struct MarshallingInfo<uint16_t> {
    static constexpr const char* name = "uint16";
};

template <>
struct MarshallingInfo<int32_t> {
    static constexpr const char* name = "int32";
};

template <>
struct MarshallingInfo<uint32_t> {
    static constexpr const char* name = "uint32";
};

template <>
struct MarshallingInfo<char32_t> {
    static constexpr const char* name = "char32";
};

template <>
struct MarshallingInfo<int64_t> {
    static constexpr const char* name = "int64";
};

template <>
struct MarshallingInfo<uint64_t> {
    static constexpr const char* name = "uint64";
};

template <>
struct MarshallingInfo<float> {
    static constexpr const char* name = "float";
};

template <>
struct MarshallingInfo<double> {
    static constexpr const char* name = "double";
};

template <>
struct MarshallingInfo<void*> {
    static constexpr const char* name = "pointer";
};

template <>
struct MarshallingInfo<Tag::GType> {
    static constexpr const char* name = "GType";
};

template <>
struct MarshallingInfo<Tag::GBoolean> {
    static constexpr const char* name = "boolean";
};

template <>
struct MarshallingInfo<GValue*> {
    static constexpr const char* name = "GValue";
};

template <>
struct MarshallingInfo<GValue> {
    static constexpr const char* name = "flat GValue";
};

template <>
struct MarshallingInfo<char*> {
    static constexpr const char* name = "string";
};

template <>
struct MarshallingInfo<const char*> {
    static constexpr const char* name = "constant string";
};

template <typename TAG>
constexpr inline const char* static_type_name() {
    return MarshallingInfo<TAG>::name;
}

}  // namespace Gjs
