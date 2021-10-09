/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <type_traits>

namespace GjsEnum {

template <typename T>
constexpr bool is_class() {
    if constexpr (std::is_enum_v<T>) {
        return !std::is_convertible_v<T, std::underlying_type_t<T>>;
    }
    return false;
}

template <class EnumType>
struct WrapperImpl {
    EnumType e;

    constexpr explicit WrapperImpl(EnumType const& en) : e(en) {}
    constexpr explicit WrapperImpl(std::underlying_type_t<EnumType> const& en)
        : e(static_cast<EnumType>(en)) {}
    constexpr explicit operator bool() const { return static_cast<bool>(e); }
    constexpr operator EnumType() const { return e; }
    constexpr operator std::underlying_type_t<EnumType>() const {
        return std::underlying_type_t<EnumType>(e);
    }
};


#if defined (__clang__) || defined (__GNUC__)
template <class EnumType>
using Wrapper =
    std::conditional_t<is_class<EnumType>(), WrapperImpl<EnumType>, void>;
#else
template <class EnumType>
using Wrapper =
    std::conditional_t<is_class<EnumType>(), std::underlying_type_t<EnumType>, void>;
#endif
}  // namespace GjsEnum

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), Wrapped> operator&(
    EnumType const& first, EnumType const& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) &
                                static_cast<Wrapped>(second));
}

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), Wrapped> operator|(
    EnumType const& first, EnumType const& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) |
                                static_cast<Wrapped>(second));
}

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), Wrapped> operator^(
    EnumType const& first, EnumType const& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) ^
                                static_cast<Wrapped>(second));
}

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), Wrapped&> operator|=(
    EnumType& first,  //  NOLINT(runtime/references)
    EnumType const& second) {
    first = static_cast<EnumType>(first | second);
    return reinterpret_cast<Wrapped&>(first);
}

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), Wrapped&> operator&=(
    EnumType& first,  //  NOLINT(runtime/references)
    EnumType const& second) {
    first = static_cast<EnumType>(first & second);
    return reinterpret_cast<Wrapped&>(first);
}

template <class EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr std::enable_if_t<GjsEnum::is_class<EnumType>(), EnumType> operator~(
    EnumType const& first) {
    return static_cast<EnumType>(~static_cast<Wrapped>(first));
}
