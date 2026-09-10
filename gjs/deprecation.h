/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <format>
#include <iterator>  // for size
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>  // for forward

struct JSContext;

enum GjsDeprecationMessageId : uint8_t {
    None,
    ByteArrayInstanceToString,
    DeprecatedGObjectProperty,
    ModuleExportedLetOrConst,
    PlatformSpecificTypelib,
    Renamed,
    MakeProxyWrapperFunctionCall,
    LastValue,  // insert new elements before this one
};

namespace Gjs::detail {

inline constexpr const std::string_view messages[] = {
    // None:
    "(invalid message)",

    // ByteArrayInstanceToString:
    "Some code called array.toString() on a Uint8Array instance. Previously "
    "this would have interpreted the bytes of the array as a string, but that "
    "is nonstandard. In the future this will return the bytes as "
    "comma-separated digits. For the time being, the old behavior has been "
    "preserved, but please fix your code anyway to use TextDecoder.\n"
    "(Note that array.toString() may have been called implicitly.)",

    // DeprecatedGObjectProperty:
    "The GObject property {}.{} is deprecated.",

    // ModuleExportedLetOrConst:
    "Some code accessed the property '{}' on the module '{}'. That property "
    "was defined with 'let' or 'const' inside the module. This was previously "
    "supported, but is not correct according to the ES6 standard. Any symbols "
    "to be exported from a module must be defined with 'var'. The property "
    "access will work as previously for the time being, but please fix your "
    "code anyway.",

    // PlatformSpecificTypelib:
    "{} has been moved to a separate platform-specific library. Please update "
    "your code to use {} instead.",

    // Renamed:
    "{} has been renamed. Please update your code to use {} instead.",

    // MakeProxyWrapperFunctionCall:
    "Gio.DBusProxy.makeProxyWrapper for '{}' returns a class expression. Do "
    "not call it as a function without 'new'.",
};
static_assert(std::size(messages) == GjsDeprecationMessageId::LastValue);

void warn_deprecated_internal(JSContext*, GjsDeprecationMessageId,
                              const char* msg, unsigned max_frames);

}  // namespace Gjs::detail

/* Note, this can only be called from the JS thread because it uses the full
 * stack dump API and not the "safe" gjs_dumpstack() which can only print to
 * stdout or stderr. Do not use this function during GC, for example. */
template <GjsDeprecationMessageId ID, typename... Args>
void gjs_warn_deprecated_once_per_callsite(JSContext* cx, Args&&... args) {
    std::string msg =
        std::format(std::format_string<Args...>{Gjs::detail::messages[ID]},
                    std::forward<Args>(args)...);
    Gjs::detail::warn_deprecated_internal(cx, ID, msg.c_str(), 1);
}
