/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <limits>
#include <type_traits>

#include <girepository/girepository.h>
#include <glib-object.h>  // for GValue
#include <glib.h>         // for gboolean

namespace Gjs {

template <typename T>
struct TypeWrapper {
    constexpr TypeWrapper() : m_value(0) {}
    explicit constexpr TypeWrapper(T v) : m_value(v) {}
    constexpr operator T() const { return m_value; }
    constexpr operator T() { return m_value; }

 private:
    T m_value;
};

template <typename T1, typename T2>
constexpr bool comparable_types() {
    return std::is_arithmetic_v<T1> == std::is_arithmetic_v<T2> &&
           std::is_integral_v<T1> == std::is_integral_v<T2> &&
           std::is_signed_v<T1> == std::is_signed_v<T2>;
}

template <typename T, typename Container>
constexpr bool type_fits() {
    if constexpr (comparable_types<T, Container>()) {
        return (std::numeric_limits<T>::max() <=
                    std::numeric_limits<Container>::max() &&
                std::numeric_limits<T>::lowest() >=
                    std::numeric_limits<Container>::lowest());
    }

    return false;
}

// These tags are used to disambiguate types such as gboolean and GType which
// are in fact typedefs of other generic types. Using the tag instead of the
// type directly allows performing proper specialization. See also arg-inl.h.
namespace Tag {
struct GBoolean {};
struct GType {};
struct Long {};
struct UnsignedLong {};
struct Enum {};
struct UnsignedEnum {};
}  // namespace Tag

template <typename TAG>
struct MarshallingInfo {};

template <>
struct MarshallingInfo<bool> {
    using real_type = bool;
    using containing_tag = bool;
    using jsvalue_pack_type = int32_t;
    static constexpr const char* name = "bool";
};

template <>
struct MarshallingInfo<int8_t> {
    using real_type = int8_t;
    using containing_tag = int32_t;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_INT8;
    static constexpr const char* name = "int8";
};

template <>
struct MarshallingInfo<uint8_t> {
    using real_type = uint8_t;
    using containing_tag = uint32_t;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_UINT8;
    static constexpr const char* name = "uint8";
};

template <>
struct MarshallingInfo<int16_t> {
    using real_type = int16_t;
    using containing_tag = int32_t;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_INT16;
    static constexpr const char* name = "int16";
};

template <>
struct MarshallingInfo<uint16_t> {
    using real_type = uint16_t;
    using containing_tag = uint32_t;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_UINT16;
    static constexpr const char* name = "uint16";
};

template <>
struct MarshallingInfo<int32_t> {
    using real_type = int32_t;
    using containing_tag = int32_t;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_INT32;
    static constexpr const char* name = "int32";
};

template <>
struct MarshallingInfo<uint32_t> {
    using real_type = uint32_t;
    using containing_tag = uint32_t;
    using jsvalue_pack_type = double;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_UINT32;
    static constexpr const char* name = "uint32";
};

template <>
struct MarshallingInfo<char32_t> {
    using real_type = char32_t;
    using containing_tag = char32_t;
    using jsvalue_pack_type = double;
    static constexpr const char* name = "char32";
};

template <>
struct MarshallingInfo<int64_t> {
    using real_type = int64_t;
    using containing_tag = int64_t;
    using jsvalue_pack_type = TypeWrapper<int64_t>;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_INT64;
    static constexpr const char* name = "int64";
};

template <>
struct MarshallingInfo<uint64_t> {
    using real_type = uint64_t;
    using containing_tag = uint64_t;
    using jsvalue_pack_type = TypeWrapper<uint64_t>;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_UINT64;
    static constexpr const char* name = "uint64";
};

template <>
struct MarshallingInfo<float> {
    using real_type = float;
    using containing_tag = double;
    using jsvalue_pack_type = double;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_FLOAT;
    static constexpr const char* name = "float";
};

template <>
struct MarshallingInfo<double> {
    using real_type = double;
    using containing_tag = double;
    using jsvalue_pack_type = double;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_DOUBLE;
    static constexpr const char* name = "double";
};

template <typename T>
struct MarshallingInfo<T*> {
    using real_type = T*;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_VOID;
    static constexpr const char* name = "pointer";
};

template <>
struct MarshallingInfo<Tag::GType> {
    using real_type = GType;
    using containing_tag = Tag::GType;
    using jsvalue_pack_type = TypeWrapper<uint64_t>;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_GTYPE;
    static constexpr const char* name = "GType";
};

template <>
struct MarshallingInfo<Tag::GBoolean> {
    using real_type = gboolean;
    using containing_tag = Tag::GBoolean;
    using jsvalue_pack_type = int32_t;
    static constexpr GITypeTag gi_tag = GI_TYPE_TAG_BOOLEAN;
    static constexpr const char* name = "boolean";
};

template <>
struct MarshallingInfo<GValue*> {
    using real_type = GValue*;
    static constexpr const char* name = "GValue";
};

template <>
struct MarshallingInfo<GValue> {
    using real_type = GValue;
    using containing_tag = GValue;
    static constexpr const char* name = "flat GValue";
};

template <>
struct MarshallingInfo<char*> {
    using real_type = char*;
    using containing_tag = char*;
    using jsvalue_pack_type = char*;
    static constexpr const char* name = "string";
};

template <>
struct MarshallingInfo<const char*> {
    using real_type = const char*;
    static constexpr const char* name = "constant string";
};

template <>
struct MarshallingInfo<Tag::Long> {
    static constexpr bool is_32 =
        type_fits<long, int32_t>();  // NOLINT(runtime/int)
    using real_type = long;  // NOLINT(runtime/int)
    using containing_tag = std::conditional_t<is_32, int32_t, int64_t>;
    using jsvalue_pack_type =
        std::conditional_t<is_32, int32_t, TypeWrapper<int64_t>>;
    static constexpr const char* name = "long";
};

template <>
struct MarshallingInfo<Tag::UnsignedLong> {
    static constexpr bool is_32 =
        type_fits<unsigned long, uint32_t>();  // NOLINT(runtime/int)
    using real_type = unsigned long;  // NOLINT(runtime/int)
    using containing_tag = std::conditional_t<is_32, uint32_t, uint64_t>;
    using jsvalue_pack_type =
        std::conditional_t<is_32, double, TypeWrapper<uint64_t>>;
    static constexpr const char* name = "unsigned long";
};

template <>
struct MarshallingInfo<Tag::Enum> {
    using real_type = int;
    using containing_tag = int32_t;
    using jsvalue_pack_type = int32_t;
};

template <>
struct MarshallingInfo<Tag::UnsignedEnum> {
    using real_type = unsigned int;
    using containing_tag = uint32_t;
    using jsvalue_pack_type = double;
};

template <>
struct MarshallingInfo<void> {
    using real_type = void;
};

namespace Tag {
template <typename TAG>
using RealT = typename MarshallingInfo<TAG>::real_type;

// There are two ways you can unpack a C value from a JSValue.
// The containing type is the most appropriate C type that can contain the
// unpacked value. Implicit conversion may be performed and the value may need
// to be checked to make sure it is in range.
// The JSValue pack type, on the other hand, is the C type that is exactly
// equivalent to how JSValue stores the value, so no implicit conversion is
// performed unless the JSValue contains a pointer to a GC-thing, like BigInt.
template <typename TAG>
using JSValueContainingT = RealT<typename MarshallingInfo<TAG>::containing_tag>;

template <typename TAG>
using JSValueContainingTag = typename MarshallingInfo<TAG>::containing_tag;

template <typename TAG>
using JSValuePackT = typename MarshallingInfo<TAG>::jsvalue_pack_type;

template <typename TAG>
using JSValuePackTag = std::conditional_t<
    std::is_same_v<JSValuePackT<TAG>, TypeWrapper<int64_t>>, int64_t,
    std::conditional_t<std::is_same_v<JSValuePackT<TAG>, TypeWrapper<uint64_t>>,
                       uint64_t, JSValuePackT<TAG>>>;
}  // namespace Tag

template <typename TAG>
constexpr inline const char* static_type_name() {
    return MarshallingInfo<TAG>::name;
}

}  // namespace Gjs
