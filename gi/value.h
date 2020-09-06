/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_VALUE_H_
#define GI_VALUE_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool       gjs_value_to_g_value         (JSContext      *context,
                                         JS::HandleValue value,
                                         GValue         *gvalue);
GJS_JSAPI_RETURN_CONVENTION
bool       gjs_value_to_g_value_no_copy (JSContext      *context,
                                         JS::HandleValue value,
                                         GValue         *gvalue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_g_value(JSContext             *context,
                            JS::MutableHandleValue value_p,
                            const GValue          *gvalue);

[[nodiscard]] GClosure* gjs_closure_new_marshaled(JSContext* cx,
                                                  JSFunction* callable,
                                                  const char* description);
[[nodiscard]] GClosure* gjs_closure_new_for_signal(JSContext* cx,
                                                   JSFunction* callable,
                                                   const char* description,
                                                   unsigned signal_id);

#endif  // GI_VALUE_H_
