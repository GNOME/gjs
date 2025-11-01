/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#pragma once

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_byte_array_stuff(JSContext*, JS::MutableHandleObject module);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_byte_array_from_data_copy(JSContext*, size_t nbytes, void* data);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_byte_array_from_byte_array(JSContext*, GByteArray*);

[[nodiscard]] GByteArray* gjs_byte_array_get_byte_array(JSObject*);
[[nodiscard]] GBytes* gjs_byte_array_get_bytes(JSObject*);
