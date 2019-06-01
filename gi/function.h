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

#ifndef __GJS_FUNCTION_H__
#define __GJS_FUNCTION_H__

#include <stdbool.h>
#include <glib.h>

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

#include <girepository.h>
#include <girffi.h>

G_BEGIN_DECLS

typedef enum {
    PARAM_NORMAL,
    PARAM_SKIPPED,
    PARAM_ARRAY,
    PARAM_CALLBACK,
    PARAM_UNKNOWN,
} GjsParamType;

struct GjsCallbackTrampoline {
    int ref_count;
    GICallableInfo *info;

    GClosure *js_function;

    ffi_cif cif;
    ffi_closure *closure;
    GIScopeType scope;
    bool is_vfunc;
    GjsParamType *param_types;
};

GJS_JSAPI_RETURN_CONVENTION
GjsCallbackTrampoline* gjs_callback_trampoline_new(
    JSContext* cx, JS::HandleFunction function, GICallableInfo* callable_info,
    GIScopeType scope, JS::HandleObject scope_object, bool is_vfunc);

void gjs_callback_trampoline_unref(GjsCallbackTrampoline *trampoline);
void gjs_callback_trampoline_ref(GjsCallbackTrampoline *trampoline);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_define_function(JSContext       *context,
                              JS::HandleObject in_object,
                              GType            gtype,
                              GICallableInfo  *info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_invoke_c_function_uncached(JSContext                  *context,
                                    GIFunctionInfo             *info,
                                    JS::HandleObject            obj,
                                    const JS::HandleValueArray& args,
                                    JS::MutableHandleValue      rval);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_invoke_constructor_from_c(JSContext                  *context,
                                   JS::HandleObject            constructor,
                                   JS::HandleObject            obj,
                                   const JS::HandleValueArray& args,
                                   GIArgument                 *rvalue);

G_END_DECLS

#endif  /* __GJS_FUNCTION_H__ */
