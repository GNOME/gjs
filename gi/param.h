/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_PARAM_H_
#define GI_PARAM_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_param_class(JSContext       *context,
                            JS::HandleObject in_object);

GJS_JSAPI_RETURN_CONVENTION
GParamSpec *gjs_g_param_from_param (JSContext       *context,
                                    JS::HandleObject obj);

GJS_JSAPI_RETURN_CONVENTION
JSObject*   gjs_param_from_g_param     (JSContext  *context,
                                        GParamSpec *param);

[[nodiscard]] bool gjs_typecheck_param(JSContext* cx, JS::HandleObject obj,
                                       GType expected_type, bool throw_error);

#endif  // GI_PARAM_H_
