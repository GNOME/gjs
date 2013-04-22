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

#include <stddef.h>
#include <stdint.h>

#include <girepository.h>
#include <glib-object.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

struct GjsFunctionCallState;

typedef struct _GjsArgumentCache {
    bool (*marshal_in)(JSContext* cx, struct _GjsArgumentCache* cache,
                       GjsFunctionCallState* state, GIArgument* in_argument,
                       JS::HandleValue value);
    bool (*marshal_out)(JSContext* cx, struct _GjsArgumentCache* cache,
                        GjsFunctionCallState* state, GIArgument* out_argument,
                        JS::MutableHandleValue value);
    bool (*release)(JSContext* cx, struct _GjsArgumentCache* cache,
                    GjsFunctionCallState* state, GIArgument* in_argument,
                    GIArgument* out_argument);
    void (*free)(struct _GjsArgumentCache* cache);

    const char* arg_name;
    int arg_pos;
    GITypeInfo type_info;

    bool skip_in : 1;
    bool skip_out : 1;
    GITransfer transfer : 2;
    bool nullable : 1;
    bool is_return : 1;

    union {
        // for explicit array only
        struct {
            int length_pos;
            GITypeTag length_tag;
        } array;

        struct {
            GIScopeType scope;
            int closure_pos;
            int destroy_pos;
        } callback;

        struct {
            GITypeTag number_tag;
            bool is_unsigned : 1;
        } number;

        // boxed / union / GObject
        struct {
            GType gtype;
            GIBaseInfo* info;
        } object;

        // foreign structures
        GIStructInfo* tmp_foreign_info;

        // enum / flags
        struct {
            int64_t enum_min;
            int64_t enum_max;
        } enum_type;
        uint64_t flags_mask;

        // string / filename
        bool string_is_filename : 1;

        // out caller allocates (FIXME: should be in object)
        size_t caller_allocates_size;
    } contents;
} GjsArgumentCache;

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_arg(JSContext* cx, GjsArgumentCache* self,
                             GjsArgumentCache* arguments, int gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_return(JSContext* cx, GjsArgumentCache* self,
                                GjsArgumentCache* arguments,
                                GICallableInfo* callable,
                                bool* inc_counter_out);

#endif  // GI_ARG_CACHE_H_
