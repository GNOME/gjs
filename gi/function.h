/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <glib.h>

#include <jsapi.h>

#include <girepository.h>
#include <girffi.h>

G_BEGIN_DECLS

typedef struct {
    gint ref_count;
    JSRuntime *runtime;
    GICallableInfo *info;
    jsval js_function;
    ffi_cif cif;
    ffi_closure *closure;
    GIScopeType scope;
} GjsCallbackTrampoline;

GjsCallbackTrampoline* gjs_callback_trampoline_new(JSContext      *context,
                                                   jsval           function,
                                                   GICallableInfo *callable_info,
                                                   GIScopeType     scope);

void gjs_callback_trampoline_unref(GjsCallbackTrampoline *trampoline);
void gjs_callback_trampoline_ref(GjsCallbackTrampoline *trampoline);

JSObject* gjs_define_function   (JSContext      *context,
                                 JSObject       *in_object,
                                 GIFunctionInfo *info);
JSBool    gjs_invoke_c_function_uncached (JSContext      *context,
                                          GIFunctionInfo *info,
                                          JSObject       *obj,
                                          uintN           argc,
                                          jsval          *argv,
                                          jsval          *rval);

void     gjs_init_cinvoke_profiling ();

G_END_DECLS

#endif  /* __GJS_FUNCTION_H__ */
