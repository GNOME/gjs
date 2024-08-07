/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#ifndef GJS_BYTEARRAY_H_
#define GJS_BYTEARRAY_H_

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_byte_array_stuff(JSContext              *context,
                                 JS::MutableHandleObject module);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_byte_array_from_data_copy(JSContext* cx, size_t nbytes,
                                        void* data);

GJS_JSAPI_RETURN_CONVENTION
JSObject *    gjs_byte_array_from_byte_array (JSContext  *context,
                                              GByteArray *array);

[[nodiscard]] GByteArray* gjs_byte_array_get_byte_array(JSObject* obj);
[[nodiscard]] GBytes* gjs_byte_array_get_bytes(JSObject* obj);

#endif  // GJS_BYTEARRAY_H_
