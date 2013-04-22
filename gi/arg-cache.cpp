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

#include <config.h>

#include <inttypes.h>
#include <string.h>

#include <girepository.h>
#include <glib.h>

#include "arg.h"
#include "arg-cache.h"
#include "boxed.h"
#include "foreign.h"
#include "function.h"
#include "gerror.h"
#include "gjs/byteArray.h"
#include "gjs/jsapi-wrapper.h"
#include "object.h"
#include "union.h"
#include "value.h"

static bool gjs_arg_cache_build_normal_in_arg(GjsArgumentCache *self,
                                              GITypeTag         tag);

/* The global entry point for any invocations of GDestroyNotify;
 * look up the callback through the user_data and then free it.
 */
static void
gjs_destroy_notify_callback(void *data)
{
    auto trampoline = static_cast<GjsCallbackTrampoline *>(data);

    g_assert(trampoline);
    gjs_callback_trampoline_unref(trampoline);
}

static unsigned long
gjs_g_argument_get_ulong(GITypeTag   tag,
                         GIArgument *arg)
{
    if (tag == GI_TYPE_TAG_INT8)
        return arg->v_int8;
    if (tag == GI_TYPE_TAG_UINT8)
        return arg->v_uint8;
    if (tag == GI_TYPE_TAG_INT16)
        return arg->v_int16;
    if (tag == GI_TYPE_TAG_UINT16)
        return arg->v_uint16;
    if (tag == GI_TYPE_TAG_INT32)
        return arg->v_int32;
    if (tag == GI_TYPE_TAG_UINT32)
        return arg->v_uint32;
    if (tag == GI_TYPE_TAG_INT64)
        return arg->v_int64;
    if (tag == GI_TYPE_TAG_UINT64)
        return arg->v_uint64;
    g_assert_not_reached();
}

static void
gjs_g_argument_set_ulong(GITypeTag     tag,
                         GIArgument   *arg,
                         unsigned long value)
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    /* In a little endian system, the first byte of an unsigned long value is
     * the same value, downcasted to uint8, and no code is needed. Also, we
     * ignore the sign, as we're just moving bits here. */
    arg->v_ulong = value;
#else
    switch(tag) {
    case GI_TYPE_TAG_INT8:
        arg->v_int8 = value;
        break;
    case GI_TYPE_TAG_UINT8:
        arg->v_uint8 = value;
        break;
    case GI_TYPE_TAG_INT16:
        arg->v_int16 = value;
        break;
    case GI_TYPE_TAG_UINT16:
        arg->v_uint16 = value;
        break;
    case GI_TYPE_TAG_INT32:
        arg->v_int32 = value;
        break;
    case GI_TYPE_TAG_UINT32:
        arg->v_uint32 = value;
        break;
    case GI_TYPE_TAG_INT64:
        arg->v_int64 = value;
        break;
    case GI_TYPE_TAG_UINT64:
        arg->v_uint64 = value;
        break;
    }
#endif
}

/*
 * Marshallers:
 *
 * Each argument, irrespective of the direction, is processed in three phases:
 * - before calling the C function [in]
 * - after calling it, when converting the return value and out arguments [out]
 * - at the end of the invocation, to release any allocated memory [release]
 *
 * The convention on the names is thus
 * gjs_marshal_[argument type]_[direction]_[phase].
 * Some types don't have direction (for example, caller_allocates is only out,
 * and callback is only in), in which case it is implied.
 */

static bool
gjs_marshal_skipped_in(JSContext            *cx,
                       GjsArgumentCache     *self,
                       GjsFunctionCallState *state,
                       GIArgument           *arg,
                       JS::HandleValue       value)
{
    return true;
}

static bool
gjs_marshal_generic_in_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    return gjs_value_to_g_argument(cx, value, &self->type_info, self->arg_name,
                                   self->is_return ? GJS_ARGUMENT_RETURN_VALUE :
                                                     GJS_ARGUMENT_ARGUMENT,
                                   self->transfer, self->nullable, arg);
}

static bool
gjs_marshal_generic_inout_in(JSContext            *cx,
                             GjsArgumentCache     *self,
                             GjsFunctionCallState *state,
                             GIArgument           *arg,
                             JS::HandleValue       value)
{
    if (!gjs_marshal_generic_in_in(cx, self, state, arg, value))
        return false;

    int ix = self->arg_pos;
    state->out_cvalues[ix] = state->inout_original_cvalues[ix] = *arg;
    arg->v_pointer = &(state->out_cvalues[ix]);
    return true;
}

static bool
gjs_marshal_explicit_array_in_in(JSContext            *cx,
                                 GjsArgumentCache     *self,
                                 GjsFunctionCallState *state,
                                 GArgument            *arg,
                                 JS::HandleValue       value)
{
    void *data;
    size_t length;

    if (!gjs_array_to_explicit_array(cx, value, &self->type_info,
                                     self->arg_name, GJS_ARGUMENT_ARGUMENT,
                                     self->transfer, self->nullable,
                                     &data, &length))
        return false;

    int length_pos = self->contents.array.length_pos;
    gjs_g_argument_set_ulong(self->contents.array.length_tag,
                             &state->in_cvalues[length_pos], length);
    arg->v_pointer = data;
    return false;
}

static bool
gjs_marshal_explicit_array_inout_in(JSContext            *cx,
                                    GjsArgumentCache     *self,
                                    GjsFunctionCallState *state,
                                    GIArgument           *arg,
                                    JS::HandleValue       value)
{
    if (!gjs_marshal_explicit_array_in_in(cx, self, state, arg, value))
        return false;

    int length_pos = self->contents.array.length_pos;
    int ix = self->arg_pos;

    if (!arg->v_pointer) {
        /* Special case where we were given JS null to also pass null for
         * length, and not a pointer to an integer that derefs to 0. */
        state->in_cvalues[length_pos].v_pointer = nullptr;
        state->out_cvalues[length_pos].v_int = 0;
        state->inout_original_cvalues[length_pos].v_int = 0;

        state->out_cvalues[ix].v_pointer =
            state->inout_original_cvalues[ix].v_pointer = nullptr;
    } else {
        state->out_cvalues[length_pos] =
            state->inout_original_cvalues[length_pos] =
            state->in_cvalues[length_pos];
        state->in_cvalues[length_pos].v_pointer = &state->out_cvalues[length_pos];

        state->out_cvalues[ix] = state->inout_original_cvalues[ix] = *arg;
        arg->v_pointer = &(state->out_cvalues[ix]);
    }

    return true;
}

static bool
gjs_marshal_callback_in(JSContext            *cx,
                        GjsArgumentCache     *self,
                        GjsFunctionCallState *state,
                        GIArgument           *arg,
                        JS::HandleValue       value)
{
    GjsCallbackTrampoline *trampoline;
    ffi_closure *closure;

    if (value.isNull() && self->nullable) {
        closure = nullptr;
        trampoline = nullptr;
    } else {
        if (!(JS_TypeOfValue(cx, value) == JSTYPE_FUNCTION)) {
            gjs_throw(cx, "Expected function for callback argument %s, got %s",
                      self->arg_name, gjs_get_type_name(value));
            return false;
        }

        GICallableInfo *callable_info = g_type_info_get_interface(&self->type_info);
        trampoline = gjs_callback_trampoline_new(cx, value, callable_info,
                                                 self->contents.callback.scope,
                                                 /* FIXME: is_object_method ? obj : nullptr */
                                                 nullptr, false);
        closure = trampoline->closure;
        g_base_info_unref(callable_info);
    }

    int destroy_pos = self->contents.callback.destroy_pos;
    if (destroy_pos >= 0) {
        state->in_cvalues[destroy_pos].v_pointer =
            trampoline ? reinterpret_cast<void *>(gjs_destroy_notify_callback)
                       : nullptr;
    }
    int closure_pos = self->contents.callback.closure_pos;
    if (closure_pos >= 0) {
        state->in_cvalues[closure_pos].v_pointer = trampoline;
    }

    if (trampoline && self->contents.callback.scope != GI_SCOPE_TYPE_CALL) {
        /* Add an extra reference that will be cleared when collecting async
         * calls, or when GDestroyNotify is called */
        gjs_callback_trampoline_ref(trampoline);
    }
    arg->v_pointer = closure;

    return true;
}

static bool
gjs_marshal_generic_out_in(JSContext            *cx,
                           GjsArgumentCache     *self,
                           GjsFunctionCallState *state,
                           GIArgument           *arg,
                           JS::HandleValue       value)
{
    arg->v_pointer = &state->out_cvalues[self->arg_pos];
    return true;
}

static bool
gjs_marshal_caller_allocates_in(JSContext            *cx,
                                GjsArgumentCache     *self,
                                GjsFunctionCallState *state,
                                GIArgument           *arg,
                                JS::HandleValue       value)
{
    void *blob = g_slice_alloc0(self->contents.caller_allocates_size);
    arg->v_pointer = blob;
    state->out_cvalues[self->arg_pos].v_pointer = blob;

    return true;
}

static bool
gjs_marshal_skipped_out(JSContext             *cx,
                        GjsArgumentCache      *self,
                        GjsFunctionCallState  *state,
                        GIArgument            *arg,
                        JS::MutableHandleValue value)
{
    return true;
}

static bool
gjs_marshal_generic_out_out(JSContext             *cx,
                            GjsArgumentCache      *self,
                            GjsFunctionCallState  *state,
                            GIArgument            *arg,
                            JS::MutableHandleValue value)
{
    return gjs_value_from_g_argument(cx, value, &self->type_info, arg, true);
}

static bool
gjs_marshal_explicit_array_out_out(JSContext             *cx,
                                   GjsArgumentCache      *self,
                                   GjsFunctionCallState  *state,
                                   GIArgument            *arg,
                                   JS::MutableHandleValue value)
{
    int length_pos = self->contents.array.length_pos;
    GIArgument *length_arg = &(state->out_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    return gjs_value_from_explicit_array(cx, value, &self->type_info,
                                         arg, length);
}

static bool
gjs_marshal_skipped_release(JSContext            *cx,
                            GjsArgumentCache     *self,
                            GjsFunctionCallState *state,
                            GIArgument           *in_arg,
                            GIArgument           *out_arg)
{
    return true;
}

static bool
gjs_marshal_generic_in_release(JSContext            *cx,
                               GjsArgumentCache     *self,
                               GjsFunctionCallState *state,
                               GIArgument           *in_arg,
                               GIArgument           *out_arg)
{
    return gjs_g_argument_release_in_arg(cx, self->transfer, &self->type_info,
                                         in_arg);
}

static bool
gjs_marshal_generic_out_release(JSContext            *cx,
                                GjsArgumentCache     *self,
                                GjsFunctionCallState *state,
                                GIArgument           *in_arg,
                                GIArgument           *out_arg)
{
    return gjs_g_argument_release(cx, self->transfer, &self->type_info, out_arg);
}

static bool
gjs_marshal_generic_inout_release(JSContext            *cx,
                                  GjsArgumentCache     *self,
                                  GjsFunctionCallState *state,
                                  GIArgument           *in_arg,
                                  GIArgument           *out_arg)
{
    /* For inout, transfer refers to what we get back from the function; for
     * the temporary C value we allocated, clearly we're responsible for
     * freeing it. */

    GIArgument *original_out_arg = &(state->inout_original_cvalues[self->arg_pos]);
    if (!gjs_g_argument_release_in_arg(cx, GI_TRANSFER_NOTHING,
                                       &self->type_info, original_out_arg))
        return false;

    return gjs_marshal_generic_out_release(cx, self, state, in_arg, out_arg);
}

static bool
gjs_marshal_explicit_array_out_release(JSContext            *cx,
                                       GjsArgumentCache     *self,
                                       GjsFunctionCallState *state,
                                       GIArgument           *in_arg,
                                       GIArgument           *out_arg)
{
    int length_pos = self->contents.array.length_pos;
    GIArgument *length_arg = &(state->out_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

static bool
gjs_marshal_explicit_array_in_release(JSContext            *cx,
                                      GjsArgumentCache     *self,
                                      GjsFunctionCallState *state,
                                      GIArgument           *in_arg,
                                      GIArgument           *out_arg)
{
    int length_pos = self->contents.array.length_pos;
    GIArgument *length_arg = &(state->in_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    return gjs_g_argument_release_in_array(cx, self->transfer,
                                           &self->type_info, length, in_arg);
}

static bool
gjs_marshal_explicit_array_inout_release(JSContext            *cx,
                                         GjsArgumentCache     *self,
                                         GjsFunctionCallState *state,
                                         GIArgument           *in_arg,
                                         GIArgument           *out_arg)
{
    int length_pos = self->contents.array.length_pos;
    GIArgument *length_arg = &(state->in_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    /* For inout, transfer refers to what we get back from the function; for
     * the temporary C value we allocated, clearly we're responsible for
     * freeing it. */

    GIArgument *original_out_arg = &(state->inout_original_cvalues[self->arg_pos]);
    if (original_out_arg->v_pointer != out_arg->v_pointer &&
        !gjs_g_argument_release_in_array(cx, GI_TRANSFER_NOTHING,
                                         &self->type_info, length,
                                         original_out_arg))
        return false;

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

static bool
gjs_marshal_caller_allocates_release(JSContext            *cx,
                                     GjsArgumentCache     *self,
                                     GjsFunctionCallState *state,
                                     GIArgument           *in_arg,
                                     GIArgument           *out_arg)
{
    g_slice_free1(self->contents.caller_allocates_size, in_arg->v_pointer);
    return true;
}

static bool
gjs_marshal_callback_release(JSContext            *cx,
                             GjsArgumentCache     *self,
                             GjsFunctionCallState *state,
                             GIArgument           *in_arg,
                             GIArgument           *out_arg)
{
    auto closure = static_cast<ffi_closure *>(in_arg->v_pointer);

    if (!closure)
        return true;

    auto trampoline = static_cast<GjsCallbackTrampoline *>(closure->user_data);
    /* CallbackTrampolines are refcounted because for notified/async closures
     * it is possible to destroy it while in call, and therefore we cannot
     * check its scope at this point */
    gjs_callback_trampoline_unref(trampoline);
    in_arg->v_pointer = nullptr;

    return true;
}

static inline void
gjs_arg_cache_set_skip_all(GjsArgumentCache *self)
{
    self->marshal_in = gjs_marshal_skipped_in;
    self->marshal_out = gjs_marshal_skipped_out;
    self->release = gjs_marshal_skipped_release;
    self->skip_in = self->skip_out = true;
}

bool
gjs_arg_cache_build_return(GjsArgumentCache *self,
                           GjsArgumentCache *arguments,
                           GICallableInfo   *info,
                           bool&             inc_counter)
{
    GITypeInfo return_type;
    g_callable_info_load_return_type(info, &return_type);

    if (g_type_info_get_tag(&return_type) == GI_TYPE_TAG_VOID) {
        inc_counter = false;
        gjs_arg_cache_set_skip_all(self);
        return true;
    }

    inc_counter = true;
    self->arg_pos = -1;
    self->arg_name = "return value";
    g_callable_info_load_return_type(info, &self->type_info);
    self->transfer = g_callable_info_get_caller_owns(info);
    self->nullable = false;  /* We don't really care for return values */
    self->is_return = true;

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_ARRAY) {
        int length_pos = g_type_info_get_array_length(&return_type);
        if (length_pos >= 0) {
            gjs_arg_cache_set_skip_all(&arguments[length_pos]);

            /* Even if we skip the length argument most of the time, we need to
             * do some basic initialization here. */
            arguments[length_pos].arg_pos = length_pos;
            arguments[length_pos].marshal_in = gjs_marshal_generic_out_in;

            self->marshal_in = gjs_marshal_generic_out_in;
            self->marshal_out = gjs_marshal_explicit_array_out_out;
            self->release = gjs_marshal_explicit_array_out_release;

            self->contents.array.length_pos = length_pos;

            GIArgInfo length_arg;
            g_callable_info_load_arg(info, length_pos, &length_arg);
            GITypeInfo length_type;
            g_arg_info_load_type(&length_arg, &length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&length_type);

            return true;
        }
    }

    /* marshal_in is ignored for the return value, but skip_in is not
       (it is used in the failure release path) */
    self->skip_in = true;
    self->marshal_out = gjs_marshal_generic_out_out;
    self->release = gjs_marshal_generic_out_release;

    return true;
}

bool
gjs_arg_cache_build_arg(GjsArgumentCache *self,
                        GjsArgumentCache *arguments,
                        int               gi_index,
                        GIDirection       direction,
                        GIArgInfo        *arg_info,
                        GICallableInfo   *callable,
                        bool&             inc_counter)
{
    GITypeInfo type_info;
    g_arg_info_load_type(arg_info, &type_info);

    self->arg_pos = gi_index;
    self->arg_name = g_base_info_get_name(arg_info);
    g_arg_info_load_type(arg_info, &self->type_info);
    self->transfer = g_arg_info_get_ownership_transfer(arg_info);
    self->nullable = g_arg_info_may_be_null(arg_info);
    self->is_return = false;

    if (direction == GI_DIRECTION_IN)
        self->skip_out = true;
    else if (direction == GI_DIRECTION_OUT)
        self->skip_in = true;
    inc_counter = true;

    if (direction == GI_DIRECTION_OUT &&
        g_arg_info_is_caller_allocates(arg_info)) {
        GIInterfaceInfo *interface_info = g_type_info_get_interface(&type_info);
        g_assert(interface_info);

        GIInfoType interface_type = g_base_info_get_type(interface_info);

        size_t size;
        if (interface_type == GI_INFO_TYPE_STRUCT) {
            size = g_struct_info_get_size((GIStructInfo*)interface_info);
        } else if (interface_type == GI_INFO_TYPE_UNION) {
            size = g_union_info_get_size((GIUnionInfo*)interface_info);
        } else {
            /* Can't do caller allocates on anything else */

            g_base_info_unref((GIBaseInfo*)interface_info);
            return false;
        }

        g_base_info_unref((GIBaseInfo*)interface_info);

        self->marshal_in = gjs_marshal_caller_allocates_in;
        self->marshal_out = gjs_marshal_generic_out_out;
        self->release = gjs_marshal_caller_allocates_release;
        self->contents.caller_allocates_size = size;

        return true;
    }

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo *interface_info = g_type_info_get_interface(&type_info);
        GIInfoType interface_type = g_base_info_get_type(interface_info);
        if (interface_type == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                /* Can't do callbacks for out or inout */
                g_base_info_unref(interface_info);
                return false;
            }

            if (strcmp(g_base_info_get_name(interface_info), "DestroyNotify") == 0 &&
                strcmp(g_base_info_get_namespace(interface_info), "GLib") == 0) {
                /* Skip GDestroyNotify if they appear before the respective
                 * callback */
                gjs_arg_cache_set_skip_all(self);
                inc_counter = false;
            } else {
                self->marshal_in = gjs_marshal_callback_in;
                self->marshal_out = gjs_marshal_skipped_out;
                self->release = gjs_marshal_callback_release;

                int destroy_pos = g_arg_info_get_destroy(arg_info);
                int closure_pos = g_arg_info_get_closure(arg_info);

                if (destroy_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[destroy_pos]);

                if (closure_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[closure_pos]);

                if (destroy_pos >= 0 && closure_pos < 0) {
                    /* Function has a GDestroyNotify but no user_data, not
                     * supported */
                    g_base_info_unref(interface_info);
                    return false;
                }

                self->contents.callback.scope = g_arg_info_get_scope(arg_info);
                self->contents.callback.destroy_pos = destroy_pos;
                self->contents.callback.closure_pos = closure_pos;
            }

            g_base_info_unref(interface_info);
            return true;
        }

        g_base_info_unref(interface_info);
    }

    if (type_tag == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
        int length_pos = g_type_info_get_array_length(&type_info);

        if (length_pos >= 0) {
            gjs_arg_cache_set_skip_all(&arguments[length_pos]);

            if (direction == GI_DIRECTION_IN) {
                self->marshal_in = gjs_marshal_explicit_array_in_in;
                self->marshal_out = gjs_marshal_skipped_out;
                self->release = gjs_marshal_explicit_array_in_release;
            } else if (direction == GI_DIRECTION_INOUT) {
                self->marshal_in = gjs_marshal_explicit_array_inout_in;
                self->marshal_out = gjs_marshal_explicit_array_out_out;
                self->release = gjs_marshal_explicit_array_inout_release;
            } else {
                /* Even if we skip the length argument most of time,
                 * we need to do some basic initialization here. */
                arguments[length_pos].arg_pos = length_pos;
                arguments[length_pos].marshal_in = gjs_marshal_generic_out_in;

                self->marshal_in = gjs_marshal_generic_out_in;
                self->marshal_out = gjs_marshal_explicit_array_out_out;
                self->release = gjs_marshal_explicit_array_out_release;
            }

            self->contents.array.length_pos = length_pos;

            GIArgInfo length_arg;
            g_callable_info_load_arg(callable, length_pos, &length_arg);
            GITypeInfo length_type;
            g_arg_info_load_type(&length_arg, &length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&length_type);

            if (length_pos < gi_index) {
                /* we already collected length_pos, remove it */
                inc_counter = false;
            }

            return true;
        }
    }

    if (direction == GI_DIRECTION_IN) {
        gjs_arg_cache_build_normal_in_arg(self, type_tag);
        self->marshal_out = gjs_marshal_skipped_out;
    } else if (direction == GI_DIRECTION_INOUT) {
        self->marshal_in = gjs_marshal_generic_inout_in;
        self->marshal_out = gjs_marshal_generic_out_out;
        self->release = gjs_marshal_generic_inout_release;
    } else {
        self->marshal_in = gjs_marshal_generic_out_in;
        self->marshal_out = gjs_marshal_generic_out_out;
        self->release = gjs_marshal_generic_out_release;
    }

    return true;
}

static bool
report_primitive_type_mismatch(JSContext        *cx,
                               GjsArgumentCache *self,
                               JS::HandleValue   value,
                               JSType            expected)
{
    static const char *typenames[JSTYPE_LIMIT] = {
        "undefined", "object", "function", "string", "number", "boolean",
        "null", "symbol"
    };

    gjs_throw(cx, "Expected type %s for argument '%s' but got type %s",
              typenames[expected], self->arg_name,
              gjs_get_type_name(value));
    return false;
}

static bool
report_object_primitive_type_mismatch(JSContext        *cx,
                                      GjsArgumentCache *self,
                                      JS::Value         value,
                                      GType             expected)
{
    gjs_throw(cx, "Expected an object of type %s for argument '%s' but got type %s",
              g_type_name(expected), self->arg_name,
              gjs_get_type_name(value));
    return false;
}

static bool
report_out_of_range(JSContext        *cx,
                    GjsArgumentCache *self,
                    GITypeTag         tag)
{
    gjs_throw(cx, "Argument %s: value is out of range for %s",
              self->arg_name, g_type_tag_to_string(tag));
    return false;
}

static bool
report_invalid_null(JSContext        *cx,
                    GjsArgumentCache *self)
{
    gjs_throw(cx, "Argument %s may not be null", self->arg_name);
    return false;
}

static bool
gjs_marshal_null_in_in(JSContext            *cx,
                       GjsArgumentCache     *self,
                       GjsFunctionCallState *state,
                       GIArgument           *arg,
                       JS::HandleValue       value)
{
    arg->v_pointer = nullptr;
    return true;
}

static bool
gjs_marshal_boolean_in_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    arg->v_boolean = JS::ToBoolean(value);
    return true;
}

/* Type tags are alternated, signed / unsigned */
static int32_t min_max_ints[5][2] = {
    { G_MININT8,  G_MAXINT8 },
    { 0,          G_MAXUINT8 },
    { G_MININT16, G_MAXINT16 },
    { 0,          G_MAXUINT16 },
    { G_MININT32, G_MAXINT32 }
};

static inline bool
value_in_range(int32_t   number,
               GITypeTag tag)
{
    return (number >= min_max_ints[tag - GI_TYPE_TAG_INT8][0] &&
            number <= min_max_ints[tag - GI_TYPE_TAG_INT8][1]);
}

static bool
gjs_marshal_integer_in_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    GITypeTag tag = self->contents.number.number_tag;

    if (self->contents.number.is_unsigned) {
        uint32_t number;
        if (!JS::ToUint32(cx, value, &number))
            return false;

        if (!value_in_range(number, tag))
            return report_out_of_range(cx, self, tag);

        gjs_g_argument_set_ulong(tag, arg, number);
    } else {
        int32_t number;
        if (!JS::ToInt32(cx, value, &number))
            return false;

        if (!value_in_range(number, tag))
            return report_out_of_range(cx, self, tag);

        gjs_g_argument_set_ulong(tag, arg, number);
    }

    return true;
}

static bool
gjs_marshal_number_in_in(JSContext            *cx,
                         GjsArgumentCache     *self,
                         GjsFunctionCallState *state,
                         GIArgument           *arg,
                         JS::HandleValue       value)
{
    double v;
    if (!JS::ToNumber(cx, value, &v))
        return false;

    GITypeTag tag = self->contents.number.number_tag;
    if (tag == GI_TYPE_TAG_DOUBLE) {
        arg->v_double = v;
    } else if (tag == GI_TYPE_TAG_FLOAT) {
        if (v < -G_MAXFLOAT || v > G_MAXFLOAT)
            return report_out_of_range(cx, self, GI_TYPE_TAG_FLOAT);
        arg->v_float = v;
    } else if (tag == GI_TYPE_TAG_INT64) {
        if (v < G_MININT64 || v > G_MAXINT64)
            return report_out_of_range(cx, self, GI_TYPE_TAG_INT64);
        arg->v_int64 = v;
    } else if (tag == GI_TYPE_TAG_UINT64) {
        if (v < 0 || v > G_MAXUINT64)
            return report_out_of_range(cx, self, GI_TYPE_TAG_UINT64);
        arg->v_uint64 = v;
    } else if (tag == GI_TYPE_TAG_UINT32) {
        if (v < 0 || v > G_MAXUINT32)
            return report_out_of_range(cx, self, GI_TYPE_TAG_UINT32);
        arg->v_uint32 = v;
    } else {
        g_assert_not_reached();
    }

    return true;
}

static bool
gjs_marshal_unichar_in_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    if (!value.isString())
        return report_primitive_type_mismatch(cx, self, value, JSTYPE_STRING);

    return gjs_unichar_from_string(cx, value, &arg->v_uint32);
}

static bool
gjs_marshal_gtype_in_in(JSContext            *cx,
                        GjsArgumentCache     *self,
                        GjsFunctionCallState *state,
                        GIArgument           *arg,
                        JS::HandleValue       value)
{
    if (!value.isObjectOrNull())
        return report_primitive_type_mismatch(cx, self, value, JSTYPE_OBJECT);
    if (value.isNull())
        return report_invalid_null(cx, self);

    JS::RootedObject gtype_obj(cx, &value.toObject());
    arg->v_ssize = gjs_gtype_get_actual_gtype(cx, gtype_obj);
    return arg->v_ssize != G_TYPE_INVALID;
}

static bool
gjs_marshal_string_in_in(JSContext            *cx,
                         GjsArgumentCache     *self,
                         GjsFunctionCallState *state,
                         GIArgument           *arg,
                         JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    if (!value.isString())
        return report_primitive_type_mismatch(cx, self, value, JSTYPE_STRING);

    bool ok;
    if (self->contents.string_is_filename) {
        GjsAutoChar str;
        ok = gjs_string_to_filename(cx, value, &str);
        arg->v_pointer = str.release();
    } else {
        GjsAutoJSChar str;
        ok = gjs_string_to_utf8(cx, value, &str);
        arg->v_pointer = str.copy();
    }

    return ok;
}

static bool
gjs_marshal_string_in_release(JSContext            *cx,
                              GjsArgumentCache     *self,
                              GjsFunctionCallState *state,
                              GIArgument           *in_arg,
                              GIArgument           *out_arg)
{
    g_free(in_arg->v_pointer);
    return true;
}

static bool
gjs_marshal_enum_in_in(JSContext            *cx,
                       GjsArgumentCache     *self,
                       GjsFunctionCallState *state,
                       GIArgument           *arg,
                       JS::HandleValue       value)
{
    int64_t number;
    if (!JS::ToInt64(cx, value, &number))
        return false;

    if (number > self->contents.enum_type.enum_max ||
        number < self->contents.enum_type.enum_min) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for enum argument %s",
                  number, self->arg_name);
        return false;
    }

    if (self->contents.enum_type.enum_max <= G_MAXINT32)
        arg->v_int = number;
    else if (self->contents.enum_type.enum_max <= G_MAXUINT32)
        arg->v_uint = number;
    else
        arg->v_int64 = number;

    return true;
}

static bool
gjs_marshal_flags_in_in(JSContext            *cx,
                        GjsArgumentCache     *self,
                        GjsFunctionCallState *state,
                        GIArgument           *arg,
                        JS::HandleValue       value)
{
    int64_t number;
    if (!JS::ToInt64(cx, value, &number))
        return false;

    if ((uint64_t(number) & self->contents.flags_mask) != uint64_t(number)) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for flags argument %s",
                  number, self->arg_name);
        return false;
    }

    if (self->contents.flags_mask <= G_MAXUINT32)
        arg->v_uint = number;
    else
        arg->v_uint64 = number;

    return true;
}

static bool
gjs_marshal_foreign_in_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    GIStructInfo *foreign_info = g_type_info_get_interface(&self->type_info);
    self->contents.tmp_foreign_info = foreign_info;
    return gjs_struct_foreign_convert_to_g_argument(cx, value, foreign_info,
                                                    self->arg_name,
                                                    GJS_ARGUMENT_ARGUMENT,
                                                    self->transfer,
                                                    self->nullable, arg);
}

static bool
gjs_marshal_foreign_in_release(JSContext            *cx,
                               GjsArgumentCache     *self,
                               GjsFunctionCallState *state,
                               GIArgument           *in_arg,
                               GIArgument           *out_arg)
{
    bool ok = true;

    if (self->transfer == GI_TRANSFER_NOTHING)
        ok = gjs_struct_foreign_release_g_argument(cx, self->transfer,
                                                   self->contents.tmp_foreign_info,
                                                   in_arg);

    g_base_info_unref(self->contents.tmp_foreign_info);
    return ok;
}

static bool
gjs_marshal_gvalue_in_in(JSContext            *cx,
                         GjsArgumentCache     *self,
                         GjsFunctionCallState *state,
                         GIArgument           *arg,
                         JS::HandleValue       value)
{
    GValue gvalue = G_VALUE_INIT;

    if (!gjs_value_to_g_value(cx, value, &gvalue))
        return false;

    arg->v_pointer = g_boxed_copy(G_TYPE_VALUE, &gvalue);

    g_value_unset(&gvalue);
    return true;
}

static bool
gjs_marshal_boxed_in_in(JSContext            *cx,
                        GjsArgumentCache     *self,
                        GjsFunctionCallState *state,
                        GIArgument           *arg,
                        JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    GType gtype = self->contents.object.gtype;

    if (!value.isObject())
        return report_object_primitive_type_mismatch(cx, self,
                                                     value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    if (gtype == G_TYPE_ERROR) {
        if (!gjs_typecheck_gerror(cx, object, true))
            return false;

        arg->v_pointer = gjs_gerror_from_error(cx, object);
    } else {
        if (!gjs_typecheck_boxed(cx, object, self->contents.object.info,
                                 gtype, true))
            return false;

        arg->v_pointer = gjs_c_struct_from_boxed(cx, object);
    }

    if (self->transfer != GI_TRANSFER_NOTHING) {
        g_assert(gtype != G_TYPE_NONE);

        if (gtype == G_TYPE_VARIANT)
            g_variant_ref(static_cast<GVariant *>(arg->v_pointer));
        else
            arg->v_pointer = g_boxed_copy(gtype, arg->v_pointer);
    }

    return true;
}

/* Unions include ClutterEvent and GdkEvent, which occur fairly often in an
 * interactive application, so they're worth a special case in a different
 * virtual function. */
static bool
gjs_marshal_union_in_in(JSContext            *cx,
                        GjsArgumentCache     *self,
                        GjsFunctionCallState *state,
                        GIArgument           *arg,
                        JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_object_primitive_type_mismatch(cx, self,
                                                     value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    if (!gjs_typecheck_union(cx, object, self->contents.object.info,
                             gtype, true))
        return false;

    arg->v_pointer = gjs_c_union_from_union(cx, object);

    if (self->transfer != GI_TRANSFER_NOTHING)
        arg->v_pointer = g_boxed_copy(gtype, arg->v_pointer);

    return true;
}

static bool
gjs_marshal_gclosure_in_in(JSContext            *cx,
                           GjsArgumentCache     *self,
                           GjsFunctionCallState *state,
                           GIArgument           *arg,
                           JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    if (!value.isObject())
        return report_primitive_type_mismatch(cx, self, value, JSTYPE_FUNCTION);

    JS::RootedObject object(cx, &value.toObject());
    GClosure *closure = gjs_closure_new_marshaled(cx, object, "boxed");
    arg->v_pointer = closure;
    g_closure_ref(closure);
    g_closure_sink(closure);

    return true;
}

static bool
gjs_marshal_gbytes_in_in(JSContext            *cx,
                         GjsArgumentCache     *self,
                         GjsFunctionCallState *state,
                         GIArgument           *arg,
                         JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    if (!value.isObject())
        return report_object_primitive_type_mismatch(cx, self,
                                                     value, G_TYPE_BYTES);

    JS::RootedObject object(cx, &value.toObject());
    if (gjs_typecheck_bytearray(cx, object, false)) {
        arg->v_pointer = gjs_byte_array_get_bytes(cx, object);
    } else {
        if (!gjs_typecheck_boxed(cx, object, self->contents.object.info,
                                 G_TYPE_BYTES, true))
            return false;

        arg->v_pointer = gjs_c_struct_from_boxed(cx, object);

        /* The bytearray path is taking an extra ref irrespective of transfer
         * ownership, so we need to do the same here. */
        g_bytes_ref(static_cast<GBytes *>(arg->v_pointer));
    }

    return true;
}

static bool
gjs_marshal_object_in_in(JSContext            *cx,
                         GjsArgumentCache     *self,
                         GjsFunctionCallState *state,
                         GIArgument           *arg,
                         JS::HandleValue       value)
{
    if (value.isNull()) {
        if (!self->nullable)
            return report_invalid_null(cx, self);

        arg->v_pointer = nullptr;
        return true;
    }

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_object_primitive_type_mismatch(cx, self,
                                                     value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    if (!gjs_typecheck_object(cx, object, gtype, true))
        return false;

    arg->v_pointer = gjs_g_object_from_object(cx, object);

    if (self->transfer != GI_TRANSFER_NOTHING)
        g_object_ref(arg->v_pointer);

    return true;
}

static bool
gjs_marshal_boxed_in_release(JSContext            *cx,
                             GjsArgumentCache     *self,
                             GjsFunctionCallState *state,
                             GIArgument           *in_arg,
                             GIArgument           *out_arg)
{
    GType gtype = self->contents.object.gtype;
    g_assert(g_type_is_a(gtype, G_TYPE_BOXED));

    g_boxed_free(gtype, in_arg->v_pointer);
    return true;
}

static void
gjs_arg_cache_build_enum_bounds(GjsArgumentCache *self,
                                GIEnumInfo       *enum_info)
{
    int64_t min = G_MAXINT64;
    int64_t max = G_MININT64;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GIValueInfo *value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);

        if (value > max)
            max = value;
        if (value < min)
            min = value;

        g_base_info_unref(value_info);
    }

    self->contents.enum_type.enum_min = min;
    self->contents.enum_type.enum_max = max;
}

static void
gjs_arg_cache_build_flags_mask(GjsArgumentCache *self,
                               GIEnumInfo       *enum_info)
{
    uint64_t mask = 0;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GIValueInfo *value_info = g_enum_info_get_value(enum_info, i);
        uint64_t value = uint64_t(g_value_info_get_value(value_info));
        mask |= value;

        g_base_info_unref(value_info);
    }

    self->contents.flags_mask = mask;
}

static bool
gjs_arg_cache_build_interface_in_arg(GjsArgumentCache *self)
{
    GIBaseInfo *interface_info = g_type_info_get_interface(&self->type_info);
    GIInfoType interface_type = g_base_info_get_type(interface_info);
    bool ok = true;

    /* We do some transfer magic later, so let's ensure we don't mess up.
     * Should not happen in practice. */
    if (G_UNLIKELY (self->transfer == GI_TRANSFER_CONTAINER))
        return false;

    switch (interface_type) {
    case GI_INFO_TYPE_ENUM:
        gjs_arg_cache_build_enum_bounds(self, interface_info);
        self->marshal_in = gjs_marshal_enum_in_in;
        break;

    case GI_INFO_TYPE_FLAGS:
        gjs_arg_cache_build_flags_mask(self, interface_info);
        self->marshal_in = gjs_marshal_flags_in_in;
        break;

    case GI_INFO_TYPE_STRUCT:
        if (g_struct_info_is_foreign(interface_info)) {
            self->marshal_in = gjs_marshal_foreign_in_in;
            self->release = gjs_marshal_foreign_in_release;
            break;
        } else {
            /* fall through */
        }
    case GI_INFO_TYPE_BOXED:
    case GI_INFO_TYPE_OBJECT:
    case GI_INFO_TYPE_INTERFACE:
    case GI_INFO_TYPE_UNION:
    {
        GType gtype = g_registered_type_info_get_g_type(interface_info);
        self->contents.object.gtype = gtype;
        self->contents.object.info = interface_info;
        g_base_info_ref(self->contents.object.info);

        /* Transfer handling is a bit complex here, because
           some of our _in marshallers know not to copy stuff if we don't
           need to.
        */

        if (gtype == G_TYPE_VALUE) {
            self->marshal_in = gjs_marshal_gvalue_in_in;
            if (self->transfer == GI_TRANSFER_NOTHING)
                self->release = gjs_marshal_boxed_in_release;
        } else if (gtype == G_TYPE_CLOSURE) {
            self->marshal_in = gjs_marshal_gclosure_in_in;
            if (self->transfer == GI_TRANSFER_NOTHING)
                self->release = gjs_marshal_boxed_in_release;
        } else if (gtype == G_TYPE_BYTES) {
            self->marshal_in = gjs_marshal_gbytes_in_in;
            if (self->transfer == GI_TRANSFER_NOTHING)
                self->release = gjs_marshal_boxed_in_release;
        } else if (g_type_is_a(gtype, G_TYPE_OBJECT) ||
                   g_type_is_a(gtype, G_TYPE_INTERFACE)) {
            self->marshal_in = gjs_marshal_object_in_in;
            /* This is a smart marshaller, no release needed */
        } else if (interface_type == GI_INFO_TYPE_UNION) {
            if (gtype != G_TYPE_NONE) {
                self->marshal_in = gjs_marshal_union_in_in;
                /* This is a smart marshaller, no release needed */
            } else {
                /* Can't handle unions without a GType */
                ok = false;
            }
        } else { /* generic boxed type */
            if (gtype == G_TYPE_NONE &&
                self->transfer != GI_TRANSFER_NOTHING) {
                /* Can't transfer ownership of a structure type not registered
                 * as a boxed */
                ok = false;
            } else {
                self->marshal_in = gjs_marshal_boxed_in_in;
                /* This is a smart marshaller, no release needed */
            }
        }
    }
        break;

    case GI_INFO_TYPE_INVALID:
    case GI_INFO_TYPE_FUNCTION:
    case GI_INFO_TYPE_CALLBACK:
    case GI_INFO_TYPE_CONSTANT:
    case GI_INFO_TYPE_INVALID_0:
    case GI_INFO_TYPE_VALUE:
    case GI_INFO_TYPE_SIGNAL:
    case GI_INFO_TYPE_VFUNC:
    case GI_INFO_TYPE_PROPERTY:
    case GI_INFO_TYPE_FIELD:
    case GI_INFO_TYPE_ARG:
    case GI_INFO_TYPE_TYPE:
    case GI_INFO_TYPE_UNRESOLVED:
    default:
        /* Don't know how to handle this interface type (should not happen in
         * practice, for typelibs emitted by g-ir-compiler) */
        ok = false;
    }

    g_base_info_unref(interface_info);
    return ok;
}

static bool
gjs_arg_cache_build_normal_in_arg(GjsArgumentCache *self,
                                  GITypeTag         tag)
{
    /* "Normal" in arguments are those arguments that don't require special
     * processing, and don't touch other arguments.
     * Main categories are:
     * - void*
     * - small numbers (fit in 32bit)
     * - big numbers (need a double)
     * - strings
     * - enums/flags (different from numbers in the way they're exposed in GI)
     * - objects (GObjects, boxed, unions, etc.)
     * - hashes
     * - sequences (null-terminated arrays, lists, etc.)
     */

    self->release = gjs_marshal_skipped_release;

    switch (tag) {
    case GI_TYPE_TAG_VOID:
        self->marshal_in = gjs_marshal_null_in_in;
        break;

    case GI_TYPE_TAG_BOOLEAN:
        self->marshal_in = gjs_marshal_boolean_in_in;
        break;

    case GI_TYPE_TAG_INT8:
    case GI_TYPE_TAG_INT16:
    case GI_TYPE_TAG_INT32:
        self->marshal_in = gjs_marshal_integer_in_in;
        self->contents.number.number_tag = tag;
        self->contents.number.is_unsigned = false;
        break;

    case GI_TYPE_TAG_UINT8:
    case GI_TYPE_TAG_UINT16:
        self->marshal_in = gjs_marshal_integer_in_in;
        self->contents.number.number_tag = tag;
        self->contents.number.is_unsigned = true;
        break;

    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_INT64:
    case GI_TYPE_TAG_UINT64:
    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
        self->marshal_in = gjs_marshal_number_in_in;
        self->contents.number.number_tag = tag;
        break;

    case GI_TYPE_TAG_UNICHAR:
        self->marshal_in = gjs_marshal_unichar_in_in;
        break;

    case GI_TYPE_TAG_GTYPE:
        self->marshal_in = gjs_marshal_gtype_in_in;
        break;

    case GI_TYPE_TAG_FILENAME:
        self->marshal_in = gjs_marshal_string_in_in;
        if (self->transfer == GI_TRANSFER_NOTHING)
            self->release = gjs_marshal_string_in_release;
        self->contents.string_is_filename = true;
        break;

    case GI_TYPE_TAG_UTF8:
        self->marshal_in = gjs_marshal_string_in_in;
        if (self->transfer == GI_TRANSFER_NOTHING)
            self->release = gjs_marshal_string_in_release;
        self->contents.string_is_filename = false;
        break;

    case GI_TYPE_TAG_INTERFACE:
        return gjs_arg_cache_build_interface_in_arg(self);

    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
    default:
        /* FIXME */
        /* Falling back to the generic marshaller */
        self->marshal_in = gjs_marshal_generic_in_in;
        self->release = gjs_marshal_generic_in_release;
    }

    return true;
}
