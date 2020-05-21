/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

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
JSObject* gjs_byte_array_from_data(JSContext* cx, size_t nbytes, void* data);

GJS_JSAPI_RETURN_CONVENTION
JSObject *    gjs_byte_array_from_byte_array (JSContext  *context,
                                              GByteArray *array);

GJS_USE
GByteArray* gjs_byte_array_get_byte_array(JSObject* obj);

GJS_USE
GBytes* gjs_byte_array_get_bytes(JSObject* obj);

#endif  // GJS_BYTEARRAY_H_
