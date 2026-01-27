/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <stdint.h>

#include <string>
#include <vector>

struct JSContext;

enum GjsDeprecationMessageId : uint8_t {
    None,
    ByteArrayInstanceToString,
    DeprecatedGObjectProperty,
    ModuleExportedLetOrConst,
    PlatformSpecificTypelib,
    Renamed,
    LastValue,  // insert new elements before this one
};

void gjs_warn_deprecated_once_per_callsite(JSContext*, GjsDeprecationMessageId,
                                           unsigned max_frames = 1);

void gjs_warn_deprecated_once_per_callsite(JSContext*, GjsDeprecationMessageId,
                                           const std::vector<std::string>& args,
                                           unsigned max_frames = 1);
