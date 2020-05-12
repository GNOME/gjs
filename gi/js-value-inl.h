/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <stdint.h>
#include <limits>
#include <type_traits>

#include <js/Conversions.h>

#include "gjs/macros.h"

namespace Gjs {

namespace JsValueHolder {

template <typename T1, typename T2>
constexpr bool comparable_types() {
    return std::is_arithmetic_v<T1> == std::is_arithmetic_v<T2> &&
           std::is_signed_v<T1> == std::is_signed_v<T2>;
}

template <typename T, typename Container>
constexpr bool type_fits() {
    if constexpr (comparable_types<T, Container>()) {
        return (std::is_integral_v<T> == std::is_integral_v<Container> &&
                std::numeric_limits<T>::max() <=
                    std::numeric_limits<Container>::max() &&
                std::numeric_limits<T>::lowest() >=
                    std::numeric_limits<Container>::lowest());
    }

    return false;
}

template <typename T>
constexpr auto get_strict() {
    if constexpr (type_fits<T, int32_t>())
        return int32_t{};
    else if constexpr (type_fits<T, uint32_t>())
        return uint32_t{};
    else
        return T{};
}

template <typename T>
using Strict = decltype(JsValueHolder::get_strict<T>());

}  // namespace JsValueHolder

/* Avoid implicit conversions */
template <typename T>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(JSContext*,
                                                      const JS::HandleValue&,
                                                      T*) = delete;

GJS_JSAPI_RETURN_CONVENTION
inline bool js_value_to_c(JSContext* cx, const JS::HandleValue& value,
                          int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

GJS_JSAPI_RETURN_CONVENTION
inline bool js_value_to_c(JSContext* cx, const JS::HandleValue& value,
                          uint32_t* out) {
    return JS::ToUint32(cx, value, out);
}

template <typename WantedType, typename T>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c_checked(
    JSContext* cx, const JS::HandleValue& value, T* out, bool* out_of_range) {
    static_assert(std::numeric_limits<T>::max() >=
                          std::numeric_limits<WantedType>::max() &&
                      std::numeric_limits<T>::lowest() <=
                          std::numeric_limits<WantedType>::lowest(),
                  "Container can't contain wanted type");

    if constexpr (std::is_same_v<WantedType, T>)
        return js_value_to_c(cx, value, out);

    if constexpr (std::is_arithmetic_v<T>) {
        bool ret = js_value_to_c(cx, value, out);
        if (out_of_range) {
            *out_of_range =
                (*out >
                     static_cast<T>(std::numeric_limits<WantedType>::max()) ||
                 *out <
                     static_cast<T>(std::numeric_limits<WantedType>::lowest()));
        }
        return ret;
    }
}

}  // namespace Gjs
