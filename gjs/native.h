/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

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
