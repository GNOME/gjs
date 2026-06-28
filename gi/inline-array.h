/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Andre Nijman

#pragma once

#include <config.h>

#include <stddef.h>  // for size_t

namespace Gjs {

// Small-buffer-optimized array storage. The number of elements is almost always
// tiny, so up to N are kept inline in the (stack-only) owner and only larger
// counts fall back to the heap. The data pointer doubles as the heap pointer,
// so the object is only the size of N + 1 pointers.
//
// allocate() must be called exactly once, before any element is accessed.
template <typename T, size_t N>
class InlineArray {
    T m_inline[N];
    T* m_pointer = m_inline;

 public:
    InlineArray() = default;
    ~InlineArray() {
        if (m_pointer != m_inline)
            delete[] m_pointer;
    }

    InlineArray(const InlineArray&) = delete;
    InlineArray& operator=(const InlineArray&) = delete;

    void allocate(size_t size) {
        if (size > N)
            m_pointer = new T[size];
    }

    [[nodiscard]] constexpr T& operator[](size_t index) const {
        return m_pointer[index];
    }
    [[nodiscard]] constexpr T* get() const { return m_pointer; }
};

}  // namespace Gjs
