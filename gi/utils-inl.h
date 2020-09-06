/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <stdint.h>

#include <type_traits>  // IWYU pragma: keep

template <typename T>
constexpr void* gjs_int_to_pointer(T v) {
    static_assert(std::is_integral_v<T>, "Need integer value");

    if constexpr (std::is_signed_v<T>)
        return reinterpret_cast<void*>(static_cast<intptr_t>(v));
    else
        return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
}

template <typename T>
constexpr T gjs_pointer_to_int(void* p) {
    static_assert(std::is_integral_v<T>, "Need integer value");

    if constexpr (std::is_signed_v<T>)
        return static_cast<T>(reinterpret_cast<intptr_t>(p));
    else
        return static_cast<T>(reinterpret_cast<uintptr_t>(p));
}

template <>
inline void* gjs_int_to_pointer<bool>(bool v) {
    return gjs_int_to_pointer<int8_t>(!!v);
}

template <>
inline bool gjs_pointer_to_int<bool>(void* p) {
    return !!gjs_pointer_to_int<int8_t>(p);
}
