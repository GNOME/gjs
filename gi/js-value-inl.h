/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <stdint.h>
#include <limits>
#include <type_traits>

#include <girepository.h>
#include <js/Conversions.h>

#include "gi/gtype.h"
#include "gi/value.h"
#include "gjs/jsapi-util.h"
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

/* The tag is needed to disambiguate types such as gboolean and GType
 * which are in fact typedef's of other generic types.
 * Setting a tag for a type allows to perform proper specialization. */
template <typename T, GITypeTag TAG>
constexpr auto get_strict() {
    if constexpr (TAG != GI_TYPE_TAG_VOID) {
        if constexpr (std::is_same_v<T, GType> && TAG == GI_TYPE_TAG_GTYPE)
            return GType{};
        else if constexpr (std::is_same_v<T, gboolean> &&
                           TAG == GI_TYPE_TAG_BOOLEAN)
            return gboolean{};
        else
            return;
    } else {
        if constexpr (std::is_same_v<T, char32_t>)
            return char32_t{};
        else if constexpr (type_fits<T, int32_t>())
            return int32_t{};
        else if constexpr (type_fits<T, uint32_t>())
            return uint32_t{};
        else if constexpr (type_fits<T, int64_t>())
            return int64_t{};
        else if constexpr (type_fits<T, uint64_t>())
            return uint64_t{};
        else if constexpr (type_fits<T, double>())
            return double{};
        else
            return T{};
    }
}

template <typename T>
constexpr auto get_relaxed() {
    if constexpr (type_fits<T, int32_t>())
        return int32_t{};
    else if constexpr (type_fits<T, uint16_t>())
        return uint32_t{};
    else if constexpr (std::is_arithmetic_v<T>)
        return double{};
    else
        return T{};
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
using Strict = decltype(JsValueHolder::get_strict<T, TAG>());

template <typename T>
using Relaxed = decltype(JsValueHolder::get_relaxed<T>());

}  // namespace JsValueHolder


template <typename T, typename MODE = JsValueHolder::Relaxed<T>>
constexpr bool type_has_js_getter() {
    return std::is_same_v<T, MODE>;
}

/* Avoid implicit conversions */
template <GITypeTag TAG = GI_TYPE_TAG_VOID, typename T>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(JSContext*,
                                                      const JS::HandleValue&,
                                                      T*) = delete;

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, uint32_t* out) {
    return JS::ToUint32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, char32_t* out) {
    uint32_t tmp;
    bool retval = JS::ToUint32(cx, value, &tmp);
    *out = tmp;
    return retval;
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, int64_t* out) {
    return JS::ToInt64(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, uint64_t* out) {
    return JS::ToUint64(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, double* out) {
    return JS::ToNumber(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<GI_TYPE_TAG_BOOLEAN>(
    JSContext*, const JS::HandleValue& value, gboolean* out) {
    *out = !!JS::ToBoolean(value);
    return true;
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<GI_TYPE_TAG_GTYPE>(
    JSContext* cx, const JS::HandleValue& value, GType* out) {
    if (!value.isObject())
        return false;

    JS::RootedObject elem_obj(cx);
    elem_obj = &value.toObject();

    if (!gjs_gtype_get_actual_gtype(cx, elem_obj, out))
        return false;

    if (*out == G_TYPE_INVALID)
        return false;

    return true;
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, GValue* out) {
    *out = G_VALUE_INIT;
    return gjs_value_to_g_value(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(
    JSContext* cx, const JS::HandleValue& value, char** out) {
    JS::UniqueChars tmp_result = gjs_string_to_utf8(cx, value);

    if (!tmp_result)
        return false;

    *out = g_strdup(tmp_result.get());
    return true;
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

    if constexpr (std::is_integral_v<WantedType>) {
        if (value.isUndefined()) {
            *out = 0;
            return true;
        }
    }

    if constexpr (std::is_arithmetic_v<T>) {
        bool ret = js_value_to_c(cx, value, out);
        if (out_of_range) {
            *out_of_range =
                (*out >
                     static_cast<T>(std::numeric_limits<WantedType>::max()) ||
                 *out <
                     static_cast<T>(std::numeric_limits<WantedType>::lowest()));

            if constexpr (std::is_integral_v<WantedType> &&
                          std::is_floating_point_v<T>)
                *out_of_range |= std::isnan(*out);
        }
        return ret;
    }
}

}  // namespace Gjs
