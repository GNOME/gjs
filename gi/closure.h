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

#ifndef GI_CLOSURE_H_
#define GI_CLOSURE_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

namespace JS {
class HandleValueArray;
}

GJS_USE
GClosure* gjs_closure_new(JSContext* cx, JSFunction* callable,
                          const char* description, bool root_function);

GJS_USE
bool gjs_closure_invoke(GClosure                   *closure,
                        JS::HandleObject            this_obj,
                        const JS::HandleValueArray& args,
                        JS::MutableHandleValue      retval,
                        bool                        return_exception);

GJS_USE
JSContext* gjs_closure_get_context   (GClosure     *closure);
GJS_USE
bool       gjs_closure_is_valid      (GClosure     *closure);
GJS_USE
JSFunction* gjs_closure_get_callable(GClosure* closure);

void       gjs_closure_trace         (GClosure     *closure,
                                      JSTracer     *tracer);

#endif  // GI_CLOSURE_H_
