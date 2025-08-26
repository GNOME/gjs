/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <cmath>  // for isnan
#include <limits>
#include <string>
#include <type_traits>
#include <utility>  // for move

#include <glib-object.h>
#include <glib.h>

#include <js/BigInt.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/Conversions.h>
#include <js/ErrorReport.h>  // for JSExnType
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>    // for CanonicalizeNaN

#include "gi/arg-types-inl.h"
#include "gi/gtype.h"
#include "gi/value.h"
#include "gjs/auto.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

namespace Gjs {

// There are two ways you can unpack a C value from a JSValue.
// ContainingType means storing the unpacked value in the most appropriate C
// type that can contain it. Implicit conversion may be performed and the value
// may need to be checked to make sure it is in range.
// PackType, on the other hand, means storing it in the C type that is exactly
// equivalent to how JSValue stores it, so no implicit conversion is performed
// unless the JSValue contains a pointer to a GC-thing, like BigInt.
enum HolderMode { ContainingType, PackType };

template <typename TAG, HolderMode MODE = HolderMode::PackType>
constexpr bool type_has_js_getter() {
    if constexpr (MODE == HolderMode::PackType) {
        return std::is_same_v<Tag::RealT<TAG>, Tag::JSValuePackT<TAG>>;
    } else {
        return std::is_same_v<Tag::RealT<TAG>, Tag::JSValueContainingT<TAG>>;
    }
}

/* Avoid implicit conversions */
template <typename TAG, typename UnpackT>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c(JSContext*,
                                                      JS::HandleValue,
                                                      UnpackT*) = delete;

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<signed char>(
    JSContext* cx, JS::HandleValue value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool
    js_value_to_c<signed short>  // NOLINT(runtime/int)
    (JSContext* cx, JS::HandleValue value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<int32_t>(
    JSContext* cx, JS::HandleValue value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<unsigned char>(
    JSContext* cx, JS::HandleValue value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<unsigned char>(
    JSContext* cx, JS::HandleValue value, uint32_t* out) {
    return JS::ToUint32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool
    js_value_to_c<unsigned short>  // NOLINT(runtime/int)
    (JSContext* cx, JS::HandleValue value, int32_t* out) {
    return JS::ToInt32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool
    js_value_to_c<unsigned short>  // NOLINT(runtime/int)
    (JSContext* cx, JS::HandleValue value, uint32_t* out) {
    return JS::ToUint32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<uint32_t>(
    JSContext* cx, JS::HandleValue value, uint32_t* out) {
    return JS::ToUint32(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<char32_t>(
    JSContext* cx, JS::HandleValue value, char32_t* out) {
    uint32_t tmp;
    bool retval = JS::ToUint32(cx, value, &tmp);
    *out = tmp;
    return retval;
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<int64_t>(
    JSContext* cx, JS::HandleValue value, int64_t* out) {
    if (value.isBigInt()) {
        *out = JS::ToBigInt64(value.toBigInt());
        return true;
    }
    return JS::ToInt64(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<uint64_t>(
    JSContext* cx, JS::HandleValue value, uint64_t* out) {
    if (value.isBigInt()) {
        *out = JS::ToBigUint64(value.toBigInt());
        return true;
    }
    return JS::ToUint64(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<uint32_t>(
    JSContext* cx, JS::HandleValue value, double* out) {
    return JS::ToNumber(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<float>(
    JSContext* cx, JS::HandleValue value, double* out) {
    return JS::ToNumber(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<double>(
    JSContext* cx, JS::HandleValue value, double* out) {
    return JS::ToNumber(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<Tag::GBoolean>(
    JSContext*, JS::HandleValue value, gboolean* out) {
    *out = !!JS::ToBoolean(value);
    return true;
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<Tag::GType>(
    JSContext* cx, JS::HandleValue value, GType* out) {
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
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<GValue>(
    JSContext* cx, JS::HandleValue value, GValue* out) {
    *out = G_VALUE_INIT;
    return gjs_value_to_g_value(cx, value, out);
}

template <>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c<char*>(
    JSContext* cx, JS::HandleValue value, char** out) {
    if (value.isNull()) {
        *out = nullptr;
        return true;
    }

    if (!value.isString())
        return false;

    JS::RootedString str{cx, value.toString()};
    JS::UniqueChars utf8 = JS_EncodeStringToUTF8(cx, str);

    *out = js_chars_to_glib(std::move(utf8)).release();
    return true;
}

template <typename BigT>
[[nodiscard]] inline constexpr BigT max_safe_big_number() {
    return (BigT(1) << std::numeric_limits<double>::digits) - 1;
}

template <typename BigT>
[[nodiscard]] inline constexpr BigT min_safe_big_number() {
    if constexpr (std::is_signed_v<BigT>)
        return -(max_safe_big_number<BigT>());

    return std::numeric_limits<BigT>::lowest();
}

template <typename WantedType, typename TAG,
          typename = std::enable_if_t<!std::is_same_v<Tag::RealT<TAG>, TAG>>,
          typename U>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c_checked(
    JSContext* cx, JS::HandleValue value, U* out, bool* out_of_range) {
    using T = Tag::RealT<TAG>;
    static_assert(std::numeric_limits<T>::max() >=
                          std::numeric_limits<WantedType>::max() &&
                      std::numeric_limits<T>::lowest() <=
                          std::numeric_limits<WantedType>::lowest(),
                  "Container can't contain wanted type");

    if constexpr (std::is_same_v<WantedType, uint64_t> ||
                  std::is_same_v<WantedType, int64_t>) {
        if (out_of_range) {
            JS::BigInt* bi = nullptr;
            *out_of_range = false;

            if (value.isBigInt()) {
                bi = value.toBigInt();
            } else if (value.isNumber()) {
                double number = value.toNumber();
                if (!std::isfinite(number)) {
                    *out = 0;
                    return true;
                }
                number = std::trunc(number);
                bi = JS::NumberToBigInt(cx, number);
                if (!bi)
                    return false;
            }

            if (bi) {
                *out_of_range = Gjs::bigint_is_out_of_range(bi, out);
                return true;
            }
        }
    }

    if constexpr (std::is_same_v<WantedType, T>)
        return js_value_to_c<TAG>(cx, value, out);

    // JS::ToIntNN() converts undefined, NaN, infinity to 0
    if constexpr (std::is_integral_v<WantedType>) {
        if (value.isUndefined() ||
            (value.isDouble() && !std::isfinite(value.toDouble()))) {
            *out = 0;
            return true;
        }
    }

    if constexpr (std::is_arithmetic_v<T>) {
        bool ret = js_value_to_c<TAG>(cx, value, out);
        if (out_of_range) {
            // Infinity and NaN preserved between floating point types
            if constexpr (std::is_floating_point_v<WantedType> &&
                          std::is_floating_point_v<T>) {
                if (!std::isfinite(*out)) {
                    *out_of_range = false;
                    return ret;
                }
            }

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
        // https://trac.cppcheck.net/ticket/10731
        // cppcheck-suppress missingReturn
    }
}

template <typename WantedType, typename T,
          typename = std::enable_if_t<std::is_same_v<Tag::RealT<T>, T>>>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c_checked(
    JSContext* cx, JS::HandleValue value, T* out, bool* out_of_range) {
    return js_value_to_c_checked<WantedType, T, void, T>(cx, value, out,
                                                         out_of_range);
}

template <typename WantedType, typename TAG, typename U>
GJS_JSAPI_RETURN_CONVENTION inline bool js_value_to_c_checked(
    JSContext* cx, JS::HandleValue value, TypeWrapper<U>* out,
    bool* out_of_range) {
    static_assert(std::is_integral_v<WantedType>);

    if constexpr (std::is_same_v<WantedType, U>) {
        WantedType wanted_out;
        if (!js_value_to_c_checked<WantedType, TAG>(cx, value, &wanted_out,
                                                    out_of_range))
            return false;

        *out = TypeWrapper<WantedType>{wanted_out};

        return true;
    }

    // Handle the cases resulting from TypeWrapper<long> and
    // TypeWrapper<int64_t> not being convertible on macOS
    if constexpr (!std::is_same_v<int64_t, long> &&    // NOLINT(runtime/int)
                  std::is_same_v<WantedType, long> &&  // NOLINT(runtime/int)
                  std::is_same_v<U, int64_t>) {
        return js_value_to_c_checked<int64_t, int64_t>(cx, value, out,
                                                       out_of_range);
    }

    if constexpr (!std::is_same_v<uint64_t,
                                  unsigned long> &&  // NOLINT(runtime/int)
                  std::is_same_v<WantedType,
                                 unsigned long> &&  // NOLINT(runtime/int)
                  std::is_same_v<U, uint64_t>) {
        return js_value_to_c_checked<uint64_t, uint64_t>(cx, value, out,
                                                         out_of_range);
        // https://trac.cppcheck.net/ticket/10731
        // cppcheck-suppress missingReturn
    }
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION inline bool c_value_to_js(
    JSContext* cx [[maybe_unused]], Tag::RealT<TAG> value,
    JS::MutableHandleValue js_value_p) {
    using T = Tag::RealT<TAG>;

    if constexpr (std::is_same_v<TAG, bool> ||
                  std::is_same_v<TAG, Tag::GBoolean>) {
        js_value_p.setBoolean(value);
        return true;
    } else if constexpr (std::is_arithmetic_v<T>) {
        if constexpr (std::is_same_v<T, int64_t> ||
                      std::is_same_v<T, uint64_t>) {
            if (value < Gjs::min_safe_big_number<T>() ||
                value > Gjs::max_safe_big_number<T>()) {
                js_value_p.setDouble(value);
                return true;
            }
        }
        if constexpr (std::is_floating_point_v<T>) {
            js_value_p.setDouble(JS::CanonicalizeNaN(double{value}));
            return true;
        }
        js_value_p.setNumber(value);
        return true;
    } else if constexpr (std::is_same_v<T,  // NOLINT(readability/braces)
                                        char*> ||
                         std::is_same_v<T, const char*>) {
        if (!value) {
            js_value_p.setNull();
            return true;
        }
        return gjs_string_from_utf8(cx, value, js_value_p);
    } else {
        static_assert(std::is_arithmetic_v<T>, "Unsupported type");
    }
}

// Specialization for types where TAG and RealT<TAG> are the same type, to allow
// inferring template parameter
template <typename T,
          typename = std::enable_if_t<std::is_same_v<Tag::RealT<T>, T>>>
GJS_JSAPI_RETURN_CONVENTION inline bool c_value_to_js(
    JSContext* cx, T value, JS::MutableHandleValue js_value_p) {
    return c_value_to_js<T>(cx, value, js_value_p);
}

template <typename TAG>
GJS_JSAPI_RETURN_CONVENTION inline bool c_value_to_js_checked(
    JSContext* cx [[maybe_unused]], Tag::RealT<TAG> value,
    JS::MutableHandleValue js_value_p) {
    using T = Tag::RealT<TAG>;
    if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
        if (value < Gjs::min_safe_big_number<T>() ||
            value > Gjs::max_safe_big_number<T>()) {
            g_warning(
                "Value %s cannot be safely stored in a JS Number "
                "and may be rounded",
                std::to_string(value).c_str());
        }
    }

    if constexpr (std::is_same_v<T, char*>) {
        if (value && !g_utf8_validate(value, -1, nullptr)) {
            gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                             "String from C value is invalid UTF-8 and cannot "
                             "be safely stored");
            return false;
        }
    }

    return c_value_to_js<TAG>(cx, value, js_value_p);
}

// Specialization for types where TAG and RealT<TAG> are the same type, to allow
// inferring template parameter
template <typename T,
          typename = std::enable_if_t<std::is_same_v<Tag::RealT<T>, T>>>
GJS_JSAPI_RETURN_CONVENTION inline bool c_value_to_js_checked(
    JSContext* cx, T value, JS::MutableHandleValue js_value_p) {
    return c_value_to_js_checked<T>(cx, value, js_value_p);
}

}  // namespace Gjs
