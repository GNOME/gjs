/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#ifndef GI_ARG_CACHE_H_
#define GI_ARG_CACHE_H_

#include <config.h>

#include <girepository.h>

#include <js/TypeDecls.h>

#include "gi/function.h"
#include "gjs/macros.h"

typedef struct _GjsArgumentCache {
    // For compatibility
    GjsParamType param_type;
} GjsArgumentCache;

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_arg(JSContext* cx, GjsArgumentCache* self,
                             GjsParamType* param_types, int gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_inout_arg(JSContext* cx, GjsArgumentCache* in_self,
                                   GjsArgumentCache* out_self,
                                   GjsParamType* param_types, int gi_index,
                                   GIArgInfo* arg, bool* inc_counter_out);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_return(JSContext* cx, GjsArgumentCache* self,
                                GjsParamType* param_types, GICallableInfo* info,
                                bool* inc_counter_out);

#endif  // GI_ARG_CACHE_H_
