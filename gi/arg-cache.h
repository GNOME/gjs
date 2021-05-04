/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>

#ifndef GI_ARG_CACHE_H_
#define GI_ARG_CACHE_H_

#include <config.h>

#include <stddef.h>
#include <stdint.h>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>  // for g_assert

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/arg.h"
#include "gjs/enum-utils.h"
#include "gjs/macros.h"

class GjsFunctionCallState;
struct GjsArgumentCache;

enum NotIntrospectableReason : uint8_t {
    CALLBACK_OUT,
    DESTROY_NOTIFY_NO_CALLBACK,
    DESTROY_NOTIFY_NO_USER_DATA,
    INTERFACE_TRANSFER_CONTAINER,
    OUT_CALLER_ALLOCATES_NON_STRUCT,
    UNREGISTERED_BOXED_WITH_TRANSFER,
    UNREGISTERED_UNION,
    UNSUPPORTED_TYPE,
    LAST_REASON
};

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
    GITransfer transfer : 2;
    GjsArgumentFlags flags : 5;

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
        struct {
            GType gtype;
            GIBaseInfo* info;
        } object;

        // foreign structures
        GIStructInfo* tmp_foreign_info;

        // enum / flags
        struct {
            uint32_t enum_min;
            uint32_t enum_max;
        } enum_type;
        unsigned flags_mask;

        // out caller allocates (FIXME: should be in object)
        size_t caller_allocates_size;

        // not introspectable
        NotIntrospectableReason reason;
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
        flags = static_cast<GjsArgumentFlags>(GjsArgumentFlags::NONE | GjsArgumentFlags::SKIP_OUT);
    }

    void set_return_value() {
        arg_pos = RETURN_VALUE;
        arg_name = "return value";
        flags =
            GjsArgumentFlags::NONE;  // We don't really care for return values
    }
    [[nodiscard]] bool is_return_value() { return arg_pos == RETURN_VALUE; }

    constexpr bool skip_in() const {
        return (flags & GjsArgumentFlags::SKIP_IN);
    }

    constexpr bool skip_out() const {
        return (flags & GjsArgumentFlags::SKIP_OUT);
    }
};

// This is a trick to print out the sizes of the structs at compile time, in
// an error message:
// template <int s> struct Measure;
// Measure<sizeof(GjsArgumentCache)> arg_cache_size;

#if defined(__x86_64__) && defined(__clang__) && !defined (_MSC_VER)
// This isn't meant to be comprehensive, but should trip on at least one CI job
// if sizeof(GjsArgumentCache) is increased.
// Note that this check is not applicable for clang-cl builds, as Windows is
// an LLP64 system
static_assert(sizeof(GjsArgumentCache) <= 112,
              "Think very hard before increasing the size of GjsArgumentCache. "
              "One is allocated for every argument to every introspected "
              "function.");
#endif  // x86-64 clang

void gjs_arg_cache_build_arg(GjsArgumentCache* self,
                             GjsArgumentCache* arguments, uint8_t gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out);

void gjs_arg_cache_build_return(GjsArgumentCache* self,
                                GjsArgumentCache* arguments,
                                GICallableInfo* callable,
                                bool* inc_counter_out);

void gjs_arg_cache_build_instance(GjsArgumentCache* self,
                                  GICallableInfo* callable);

[[nodiscard]] size_t gjs_g_argument_get_array_length(GITypeTag tag,
                                                     GIArgument* arg);

#endif  // GI_ARG_CACHE_H_
