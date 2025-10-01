// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018-2020  Canonical, Ltd

#pragma once

#include <config.h>

#include <glib.h>

#include <mozilla/Result.h>
#include <mozilla/ResultVariant.h>  // IWYU pragma: keep (see Result::Impl)

#include "gjs/auto.h"

namespace mozilla {
namespace detail {
template <typename V, typename E, PackingStrategy Strategy>
class ResultImplementation;
}
}  // namespace mozilla

// Auto pointer type for GError, as well as a Result type that can be used as a
// type-safe return type for fallible GNOME-platform operations.
// To indicate success, return Ok{}, and to indicate failure, return Err(error)
// or Err(error.release()) if using AutoError.
// When calling a function that returns GErrorResult inside another function
// that returns GErrorResult, you can use the MOZ_TRY() macro as if it were the
// Rust ? operator, to bail out on error even if the GErrorResult's success
// types are different.

// COMPAT: We use Mozilla's Result type because std::expected does not appear in
// the standard library until C++23.

namespace Gjs {

struct AutoError : AutoPointer<GError, GError, g_error_free> {
    using BaseType::BaseType;
    using BaseType::operator=;

    constexpr BaseType::ConstPtr* operator&()  // NOLINT(runtime/operator)
        const {
        return out();
    }
    constexpr BaseType::Ptr* operator&() {  // NOLINT(runtime/operator)
        return out();
    }
};

template <>
struct SmartPointer<GError> : AutoError {
    using AutoError::AutoError;
    using AutoError::operator=;
    using AutoError::operator&;
};

template <typename T = mozilla::Ok>
using GErrorResult = mozilla::Result<T, AutoError>;

}  // namespace Gjs

namespace mozilla {
namespace detail {
// Custom packing for GErrorResult<>
template <>
class SelectResultImpl<Ok, Gjs::AutoError> {
 public:
    class Type {
        Gjs::AutoError m_value;

     public:
        explicit constexpr Type(Ok) : m_value() {}
        explicit constexpr Type(GError* error) : m_value(error) {}
        constexpr Type(Type&& other) : m_value(other.m_value.release()) {}
        Type& operator=(Type&& other) {
            m_value = other.m_value.release();
            return *this;
        }
        constexpr bool isOk() const { return !m_value; }
        constexpr const Ok inspect() const { return {}; }
        constexpr Ok unwrap() { return {}; }
        constexpr const GError* inspectErr() const { return m_value.get(); }
        Gjs::AutoError unwrapErr() { return m_value.release(); }
    };
};

// Packing for any other pointer. Unlike in SpiderMonkey, GLib-allocated
// pointers may not be aligned, so their bottom bit cannot be used for a flag.
template <typename T>
class SelectResultImpl<T*, Gjs::AutoError> {
 public:
    using Type = ResultImplementation<T*, Gjs::AutoError,
                                      PackingStrategy::PackedVariant>;
};

}  // namespace detail
}  // namespace mozilla
