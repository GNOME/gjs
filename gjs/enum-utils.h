/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <concepts>
#include <type_traits>

namespace GjsEnum {

// COMPAT: Use std::is_scoped_enum_v in C++23
template <typename T>
concept Scoped =
    std::is_enum_v<T> && !std::convertible_to<T, std::underlying_type_t<T>>;

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
using Wrapper = WrapperImpl<EnumType>;
#else
template <class EnumType>
using Wrapper = std::underlying_type_t<EnumType>;
#endif
}  // namespace GjsEnum

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped operator&(const EnumType& first, const EnumType& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) &
                                static_cast<Wrapped>(second));
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped operator|(const EnumType& first, const EnumType& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) |
                                static_cast<Wrapped>(second));
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped operator|(const Wrapped& first, const EnumType& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) |
                                static_cast<Wrapped>(second));
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped operator^(const EnumType& first, const EnumType& second) {
    return static_cast<Wrapped>(static_cast<Wrapped>(first) ^
                                static_cast<Wrapped>(second));
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped& operator|=(EnumType& first,  //  NOLINT(runtime/references)
                              const EnumType& second) {
    first = static_cast<EnumType>(first | second);
    return reinterpret_cast<Wrapped&>(first);
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr Wrapped& operator&=(EnumType& first,  //  NOLINT(runtime/references)
                              const EnumType& second) {
    first = static_cast<EnumType>(first & second);
    return reinterpret_cast<Wrapped&>(first);
}

template <GjsEnum::Scoped EnumType, class Wrapped = GjsEnum::Wrapper<EnumType>>
constexpr EnumType operator~(const EnumType& first) {
    return static_cast<EnumType>(~static_cast<Wrapped>(first));
}
