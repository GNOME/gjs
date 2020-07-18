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

struct GjsArgumentCache {
    bool (*marshal_in)(JSContext* cx, GjsArgumentCache* cache,
                       GjsFunctionCallState* state, GIArgument* in_argument,
                       JS::HandleValue value);
    bool (*marshal_out)(JSContext* cx, GjsArgumentCache* cache,
                        GjsFunctionCallState* state, GIArgument* out_argument,
                        JS::MutableHandleValue value);
    bool (*release)(JSContext* cx, GjsArgumentCache* cache,
                    GjsFunctionCallState* state, GIArgument* in_argument,
                    GIArgument* out_argument);
    void (*free)(GjsArgumentCache* cache);

    const char* arg_name;
    GITypeInfo type_info;

    int arg_pos;
    bool skip_in : 1;
    bool skip_out : 1;
    GITransfer transfer : 2;
    bool nullable : 1;
    bool is_return : 1;

    union {
        // for explicit array only
        struct {
            int length_pos;
            GITypeTag length_tag : 5;
        } array;

        struct {
            int closure_pos;
            int destroy_pos;
            GIScopeType scope : 2;
        } callback;

        struct {
            GITypeTag number_tag : 5;
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

    GJS_JSAPI_RETURN_CONVENTION
    bool handle_nullable(JSContext* cx, GIArgument* arg);
};

// This is a trick to print out the sizes of the structs at compile time, in
// an error message:
// template <int s> struct Measure;
// Measure<sizeof(GjsArgumentCache)> arg_cache_size;

#if defined(__x86_64__) && defined(__clang__)
// This isn't meant to be comprehensive, but should trip on at least one CI job
// if sizeof(GjsArgumentCache) is increased. */
static_assert(sizeof(GjsArgumentCache) <= 136,
              "Think very hard before increasing the size of GjsArgumentCache. "
              "One is allocated for every argument to every introspected "
              "function.");
#endif  // x86-64 clang

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

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_instance(JSContext* cx, GjsArgumentCache* self,
                                  GICallableInfo* callable);

#endif  // GI_ARG_CACHE_H_
