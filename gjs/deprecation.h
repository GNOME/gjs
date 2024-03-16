/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#ifndef GJS_DEPRECATION_H_
#define GJS_DEPRECATION_H_

#include <vector>

struct JSContext;

enum GjsDeprecationMessageId {
    None,
    ByteArrayInstanceToString,
    DeprecatedGObjectProperty,
    ModuleExportedLetOrConst,
    PlatformSpecificTypelib,
    LastValue,  // insert new elements before this one
};

void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            GjsDeprecationMessageId message);

void _gjs_warn_deprecated_once_per_callsite(
    JSContext* cx, GjsDeprecationMessageId id,
    const std::vector<const char*>& args);

#endif  // GJS_DEPRECATION_H_
