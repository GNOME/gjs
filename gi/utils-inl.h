/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include <stdint.h>

#include <type_traits>

template <typename T>
inline std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, void*>
gjs_int_to_pointer(T v) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(v));
}

template <typename T>
inline std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, void*>
gjs_int_to_pointer(T v) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
}

template <typename T>
inline std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, T>
gjs_pointer_to_int(void* p) {
    return static_cast<T>(reinterpret_cast<intptr_t>(p));
}

template <typename T>
inline std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, T>
gjs_pointer_to_int(void* p) {
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
