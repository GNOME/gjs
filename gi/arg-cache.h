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
#include <glib.h>  // for g_assert

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

struct GjsFunctionCallState;
struct GjsArgumentCache;

struct GjsArgumentMarshallers {
    bool (*in)(JSContext* cx, GjsArgumentCache* cache,
               GjsFunctionCallState* state, GIArgument* in_argument,
               JS::HandleValue value);
    bool (*out)(JSContext* cx, GjsArgumentCache* cache,
                GjsFunctionCallState* state, GIArgument* out_argument,
                JS::MutableHandleValue value);
    bool (*release)(JSContext* cx, GjsArgumentCache* cache,
                    GjsFunctionCallState* state, GIArgument* in_argument,
                    GIArgument* out_argument);
    void (*free)(GjsArgumentCache* cache);
};

struct GjsArgumentCache {
    const GjsArgumentMarshallers* marshallers;
    const char* arg_name;
    GITypeInfo type_info;

    uint8_t arg_pos;
    bool skip_in : 1;
    bool skip_out : 1;
    GITransfer transfer : 2;
    bool nullable : 1;
    bool is_unsigned : 1;  // number and enum only

    union {
        // for explicit array only
        struct {
            uint8_t length_pos;
            GITypeTag length_tag : 5;
        } array;

        struct {
            uint8_t closure_pos;
            uint8_t destroy_pos;
            GIScopeType scope : 2;
        } callback;

        struct {
            GITypeTag number_tag : 5;
        } number;

        // boxed / union / GObject
        GIRegisteredTypeInfo* info;

        // foreign structures
        GIStructInfo* tmp_foreign_info;

        // enum / flags
        struct {
            uint32_t enum_min;
            uint32_t enum_max;
        } enum_type;
        uint64_t flags_mask;

        // string / filename
        bool string_is_filename : 1;

        // out caller allocates (FIXME: should be in object)
        size_t caller_allocates_size;
    } contents;

    GJS_JSAPI_RETURN_CONVENTION
    bool handle_nullable(JSContext* cx, GIArgument* arg);

    // Introspected functions can have up to 253 arguments. 255 is a placeholder
    // for the return value and 254 for the instance parameter. The callback
    // closure or destroy notify parameter may have a value of 255 to indicate
    // that it is absent.
    static constexpr uint8_t MAX_ARGS = 253;
    static constexpr uint8_t INSTANCE_PARAM = 254;
    static constexpr uint8_t RETURN_VALUE = 255;
    static constexpr uint8_t ABSENT = 255;
    void set_arg_pos(int pos) {
        g_assert(pos <= MAX_ARGS && "No more than 253 arguments allowed");
        arg_pos = pos;
    }
    void set_array_length_pos(int pos) {
        g_assert(pos <= MAX_ARGS && "No more than 253 arguments allowed");
        contents.array.length_pos = pos;
    }
    void set_callback_destroy_pos(int pos) {
        g_assert(pos <= MAX_ARGS && "No more than 253 arguments allowed");
        contents.callback.destroy_pos = pos < 0 ? ABSENT : pos;
    }
    [[nodiscard]] bool has_callback_destroy() {
        return contents.callback.destroy_pos != ABSENT;
    }
    void set_callback_closure_pos(int pos) {
        g_assert(pos <= MAX_ARGS && "No more than 253 arguments allowed");
        contents.callback.closure_pos = pos < 0 ? ABSENT : pos;
    }
    [[nodiscard]] bool has_callback_closure() {
        return contents.callback.closure_pos != ABSENT;
    }

    void set_instance_parameter() {
        arg_pos = INSTANCE_PARAM;
        arg_name = "instance parameter";
        // Some calls accept null for the instance, but generally in an object
        // oriented language it's wrong to call a method on null
        nullable = false;
        skip_out = true;
    }

    void set_return_value() {
        arg_pos = RETURN_VALUE;
        arg_name = "return value";
        nullable = false;  // We don't really care for return values
    }
    [[nodiscard]] bool is_return_value() { return arg_pos == RETURN_VALUE; }
};

// This is a trick to print out the sizes of the structs at compile time, in
// an error message:
// template <int s> struct Measure;
// Measure<sizeof(GjsArgumentCache)> arg_cache_size;

#if defined(__x86_64__) && defined(__clang__)
// This isn't meant to be comprehensive, but should trip on at least one CI job
// if sizeof(GjsArgumentCache) is increased. */
static_assert(sizeof(GjsArgumentCache) <= 104,
              "Think very hard before increasing the size of GjsArgumentCache. "
              "One is allocated for every argument to every introspected "
              "function.");
#endif  // x86-64 clang

GJS_JSAPI_RETURN_CONVENTION
bool gjs_arg_cache_build_arg(JSContext* cx, GjsArgumentCache* self,
                             GjsArgumentCache* arguments, uint8_t gi_index,
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
