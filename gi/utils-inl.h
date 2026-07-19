/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <algorithm>
#include <concepts>  // for integral, signed_integral, unsigned_integral
#include <type_traits>
#include <utility>
#include <vector>

constexpr void* gjs_int_to_pointer(std::signed_integral auto v) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(v));
}

constexpr void* gjs_int_to_pointer(std::unsigned_integral auto v) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
}

template <std::integral T>
constexpr T gjs_pointer_to_int(void* p) {
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

namespace Gjs {

template <typename T>
inline bool remove_one_from_unsorted_vector(std::vector<T>* v, const T& value) {
    // This assumes that there's only a copy of the same value in the vector
    // so this needs to be ensured when populating it.
    // We use the swap and pop idiom to avoid moving all the values.
    auto it = std::ranges::find(*v, value);
    if (it != v->end()) {
        std::swap(*it, v->back());
        v->pop_back();
        // COMPAT: use std::ranges::contains in C++23
        g_assert(std::ranges::find(*v, value) == v->end());
        return true;
    }

    return false;
}

}  // namespace Gjs
