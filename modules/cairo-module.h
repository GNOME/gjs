/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#ifndef MODULES_CAIRO_MODULE_H_
#define MODULES_CAIRO_MODULE_H_

#include <config.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_js_define_cairo_stuff(JSContext              *context,
                               JS::MutableHandleObject module);

#endif  // MODULES_CAIRO_MODULE_H_
