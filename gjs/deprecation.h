/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#ifndef GJS_DEPRECATION_H_
#define GJS_DEPRECATION_H_

struct JSContext;

enum GjsDeprecationMessageId {
    None,
    ByteArrayInstanceToString,
    DeprecatedGObjectProperty,
};

void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            GjsDeprecationMessageId message);

#endif  // GJS_DEPRECATION_H_
