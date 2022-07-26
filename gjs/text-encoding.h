/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh

#pragma once

#include <config.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

enum class GjsStringTermination {
    ZERO_TERMINATED,
    EXPLICIT_LENGTH,
};

GJS_JSAPI_RETURN_CONVENTION
JSString* gjs_decode_from_uint8array(JSContext* cx, JS::HandleObject uint8array,
                                     const char* encoding,
                                     GjsStringTermination string_termination,
                                     bool fatal);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_encode_to_uint8array(JSContext* cx, JS::HandleString str,
                                   const char* encoding,
                                   GjsStringTermination string_termination);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_text_encoding_stuff(JSContext* cx,
                                    JS::MutableHandleObject module);
