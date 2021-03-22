/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_CLOSURE_H_
#define GI_CLOSURE_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

class JSTracer;
namespace JS {
class HandleValueArray;
}

[[nodiscard]] GClosure* gjs_closure_new(JSContext* cx, JSFunction* callable,
                                        const char* description,
                                        bool root_function);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_closure_invoke(GClosure* closure, JS::HandleObject this_obj,
                        const JS::HandleValueArray& args,
                        JS::MutableHandleValue retval);

[[nodiscard]] JSContext* gjs_closure_get_context(GClosure* closure);
[[nodiscard]] bool gjs_closure_is_valid(GClosure* closure);
[[nodiscard]] JSFunction* gjs_closure_get_callable(GClosure* closure);

void       gjs_closure_trace         (GClosure     *closure,
                                      JSTracer     *tracer);

#endif  // GI_CLOSURE_H_
