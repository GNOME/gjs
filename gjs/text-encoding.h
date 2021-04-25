/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh

#pragma once

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool bytearray_to_string(JSContext* cx, JS::HandleObject uint8array,
                         const char* encoding, JS::MutableHandleValue rval);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_text_encoding_stuff(JSContext* cx,
                                    JS::MutableHandleObject module);
