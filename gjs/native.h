/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef GJS_NATIVE_H_
#define GJS_NATIVE_H_

#include <config.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

typedef bool (* GjsDefineModuleFunc) (JSContext              *context,
                                      JS::MutableHandleObject module_out);

/* called on context init */
void   gjs_register_native_module (const char            *module_id,
                                   GjsDefineModuleFunc  func);

/* called by importer.c to to check for already loaded modules */
[[nodiscard]] bool gjs_is_registered_native_module(const char* name);

/* called by importer.cpp to load a statically linked native module */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_load_native_module(JSContext              *cx,
                            const char             *name,
                            JS::MutableHandleObject module_out);

#endif  // GJS_NATIVE_H_
