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

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <girepository.h>

#include "arg.h"
#include "arg-cache.h"
#include "function.h"

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
    /* In a little endian system, the first byte
       of an unsigned long value is the same value,
       downcasted to uint8, and no code is needed.
       Also, we ignore the sign, as we're just moving
       bits here.
    */
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
 * Each argument, irrespective of the direction, is processed
 * in three phases:
 * - before calling the C function [in]
 * - after calling it, when converting the return value
 *   and out arguments [out]
 * - at the end of the invocation, to release any
 *   allocated memory [release]
 *
 * The convention on the names is thus
 * gjs_marshal_[argument type]_[direction]_[phase].
 * Some types don't have direction (for example, caller_allocates
 * is only out, and callback is only in), in which case it is
 * implied.
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
gjs_marshal_normal_in_in(JSContext            *cx,
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
gjs_marshal_normal_inout_in(JSContext            *cx,
                            GjsArgumentCache     *self,
                            GjsFunctionCallState *state,
                            GIArgument           *arg,
                            JS::HandleValue       value)
{
    if (!gjs_marshal_normal_in_in(cx, self, state, arg, value))
        return false;

    state->out_arg_cvalues[self->arg_index] = state->inout_original_arg_cvalues[self->arg_index] = *arg;
    arg->v_pointer = &(state->out_arg_cvalues[self->arg_index]);
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

    gjs_g_argument_set_ulong(self->contents.array.length_tag,
                             &state->in_arg_cvalues[self->contents.array.length_arg],
                             length);
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

    int array_length_pos = self->contents.array.length_arg;

    if (!arg->v_pointer) {
        /* Special case where we were given JS null to
         * also pass null for length, and not a
         * pointer to an integer that derefs to 0.
         */
        state->in_arg_cvalues[array_length_pos].v_pointer = nullptr;
        state->out_arg_cvalues[array_length_pos].v_int = 0;
        state->inout_original_arg_cvalues[array_length_pos].v_int = 0;

        state->out_arg_cvalues[self->arg_index].v_pointer = state->inout_original_arg_cvalues[self->arg_index].v_pointer = nullptr;
    } else {
        state->out_arg_cvalues[array_length_pos] = state->inout_original_arg_cvalues[array_length_pos] = state->in_arg_cvalues[array_length_pos];
        state->in_arg_cvalues[array_length_pos].v_pointer = &state->out_arg_cvalues[array_length_pos];

        state->out_arg_cvalues[self->arg_index] = state->inout_original_arg_cvalues[self->arg_index] = *arg;
        arg->v_pointer = &(state->out_arg_cvalues[self->arg_index]);
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
    GICallableInfo *callable_info;
    GjsCallbackTrampoline *trampoline;
    ffi_closure *closure;
    int destroy_arg, closure_arg;

    if (value.isNull() && self->nullable) {
        closure = nullptr;
        trampoline = nullptr;
    } else {
        if (!(JS_TypeOfValue(cx, value) == JSTYPE_FUNCTION)) {
            gjs_throw(cx, "Expected function for callback argument %s, got %s",
                      self->arg_name, gjs_get_type_name(value));
            return false;
        }

        callable_info = (GICallableInfo*) g_type_info_get_interface(&self->type_info);
        trampoline = gjs_callback_trampoline_new(cx,
                                                 value,
                                                 callable_info,
                                                 self->contents.callback.scope,
                                                 /* FIXME: is_object_method ? obj : NULL */
                                                 nullptr,
                                                 false);
        closure = trampoline->closure;
        g_base_info_unref(callable_info);
    }

    destroy_arg = self->contents.callback.destroy;
    if (destroy_arg >= 0) {
        state->in_arg_cvalues[destroy_arg].v_pointer = trampoline ? reinterpret_cast<void *>(gjs_destroy_notify_callback) : nullptr;
    }
    closure_arg = self->contents.callback.closure;
    if (closure_arg >= 0) {
        state->in_arg_cvalues[closure_arg].v_pointer = trampoline;
    }

    if (trampoline && self->contents.callback.scope != GI_SCOPE_TYPE_CALL) {
        /* Add an extra reference that will be cleared when collecting
           async calls, or when GDestroyNotify is called */
        gjs_callback_trampoline_ref(trampoline);
    }
    arg->v_pointer = closure;

    return true;
}

static bool
gjs_marshal_normal_out_in(JSContext            *cx,
                          GjsArgumentCache     *self,
                          GjsFunctionCallState *state,
                          GIArgument           *arg,
                          JS::HandleValue       value)
{
    arg->v_pointer = &state->out_arg_cvalues[self->arg_index];
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
    state->out_arg_cvalues[self->arg_index].v_pointer = blob;

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
gjs_marshal_normal_out_out(JSContext             *cx,
                           GjsArgumentCache      *self,
                           GjsFunctionCallState  *state,
                           GIArgument            *arg,
                           JS::MutableHandleValue value)
{
    return gjs_value_from_g_argument(cx, value,
                                     &self->type_info,
                                     arg, TRUE);
}

static bool
gjs_marshal_explicit_array_out_out(JSContext             *cx,
                                   GjsArgumentCache      *self,
                                   GjsFunctionCallState  *state,
                                   GIArgument            *arg,
                                   JS::MutableHandleValue value)
{
    GIArgument *length_arg = &(state->out_arg_cvalues[self->contents.array.length_arg]);
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
gjs_marshal_normal_in_release(JSContext            *cx,
                              GjsArgumentCache     *self,
                              GjsFunctionCallState *state,
                              GIArgument           *in_arg,
                              GIArgument           *out_arg)
{
    return gjs_g_argument_release_in_arg(cx, self->transfer,
                                         &self->type_info, in_arg);
}

static bool
gjs_marshal_normal_out_release(JSContext            *cx,
                               GjsArgumentCache     *self,
                               GjsFunctionCallState *state,
                               GIArgument           *in_arg,
                               GIArgument           *out_arg)
{
    return gjs_g_argument_release(cx, self->transfer,
                                  &self->type_info, out_arg);
}

static bool
gjs_marshal_normal_inout_release(JSContext            *cx,
                                 GjsArgumentCache     *self,
                                 GjsFunctionCallState *state,
                                 GIArgument           *in_arg,
                                 GIArgument           *out_arg)
{
    /* For inout, transfer refers to what we get back from the function; for
     * the temporary C value we allocated, clearly we're responsible for
     * freeing it.
     */

    GIArgument *original_out_arg = &(state->inout_original_arg_cvalues[self->arg_index]);
    if (!gjs_g_argument_release_in_arg(cx, GI_TRANSFER_NOTHING,
                                       &self->type_info, original_out_arg))
        return false;

    return gjs_marshal_normal_out_release(cx, self, state, in_arg, out_arg);
}

static bool
gjs_marshal_explicit_array_out_release(JSContext            *cx,
                                       GjsArgumentCache     *self,
                                       GjsFunctionCallState *state,
                                       GIArgument           *in_arg,
                                       GIArgument           *out_arg)
{
    GIArgument *length_arg = &(state->out_arg_cvalues[self->contents.array.length_arg]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info,
                                            length, out_arg);
}

static bool
gjs_marshal_explicit_array_in_release(JSContext            *cx,
                                      GjsArgumentCache     *self,
                                      GjsFunctionCallState *state,
                                      GIArgument           *in_arg,
                                      GIArgument           *out_arg)
{
    GIArgument *length_arg = &(state->in_arg_cvalues[self->contents.array.length_arg]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    return gjs_g_argument_release_in_array(cx, self->transfer,
                                           &self->type_info,
                                           length, in_arg);
}

static bool
gjs_marshal_explicit_array_inout_release(JSContext            *cx,
                                         GjsArgumentCache     *self,
                                         GjsFunctionCallState *state,
                                         GIArgument           *in_arg,
                                         GIArgument           *out_arg)
{
    GIArgument *length_arg = &(state->in_arg_cvalues[self->contents.array.length_arg]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_ulong(length_tag, length_arg);

    /* For inout, transfer refers to what we get back from the function; for
     * the temporary C value we allocated, clearly we're responsible for
     * freeing it.
     */

    GIArgument *original_out_arg = &(state->inout_original_arg_cvalues[self->arg_index]);
    if (original_out_arg->v_pointer != out_arg->v_pointer &&
        !gjs_g_argument_release_in_array(cx, GI_TRANSFER_NOTHING,
                                         &self->type_info, length,
                                         original_out_arg))
        return false;

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info,
                                            length, out_arg);
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

    if (closure) {
        auto trampoline = static_cast<GjsCallbackTrampoline *>(closure->user_data);
        /* CallbackTrampolines are refcounted because for notified/async closures
           it is possible to destroy it while in call, and therefore we cannot check
           its scope at this point */
        gjs_callback_trampoline_unref(trampoline);
        in_arg->v_pointer = nullptr;
    }

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
    self->arg_index = -1;
    self->arg_name = "return value";
    g_callable_info_load_return_type(info, &self->type_info);
    self->transfer = g_callable_info_get_caller_owns(info);
    self->nullable = false;  /* We don't really care for return values */
    self->is_return = true;

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_ARRAY) {
        int array_length_pos = g_type_info_get_array_length(&return_type);
        if (array_length_pos >= 0) {
            gjs_arg_cache_set_skip_all(&arguments[array_length_pos]);

            /* Even if we skip the length argument most of the time, we need to
             * do some basic initialization here. */
            arguments[array_length_pos].arg_index = array_length_pos;
            arguments[array_length_pos].marshal_in = gjs_marshal_normal_out_in;

            self->marshal_in = gjs_marshal_normal_out_in;
            self->marshal_out = gjs_marshal_explicit_array_out_out;
            self->release = gjs_marshal_explicit_array_out_release;

            self->contents.array.length_arg = array_length_pos;

            GIArgInfo array_length_arg;
            g_callable_info_load_arg(info, array_length_pos, &array_length_arg);
            GITypeInfo array_length_type;
            g_arg_info_load_type(&array_length_arg, &array_length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&array_length_type);

            return true;
        }
    }

    /* marshal_in is ignored for the return value, but skip_in is not
       (it is used in the failure release path) */
    self->skip_in = true;
    self->marshal_out = gjs_marshal_normal_out_out;
    self->release = gjs_marshal_normal_out_release;

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

    self->arg_index = gi_index;
    self->arg_name = g_base_info_get_name((GIBaseInfo*) arg_info);
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
        self->marshal_out = gjs_marshal_normal_out_out;
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
                /* Skip GDestroyNotify if they appear before the respective callback */
                gjs_arg_cache_set_skip_all(self);
                inc_counter = false;
            } else {
                self->marshal_in = gjs_marshal_callback_in;
                self->marshal_out = gjs_marshal_skipped_out;
                self->release = gjs_marshal_callback_release;

                int destroy = g_arg_info_get_destroy(arg_info);
                int closure = g_arg_info_get_closure(arg_info);

                if (destroy >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[destroy]);

                if (closure >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[closure]);

                if (destroy >= 0 && closure < 0) {
                    /* Function has a GDestroyNotify but no user_data, not supported */
                    g_base_info_unref(interface_info);
                    return false;
                }

                self->contents.callback.scope = g_arg_info_get_scope(arg_info);
                self->contents.callback.destroy = destroy;
                self->contents.callback.closure = closure;
            }

            g_base_info_unref(interface_info);
            return true;
        }

        g_base_info_unref(interface_info);
    }

    if (type_tag == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
        int array_length_pos = g_type_info_get_array_length(&type_info);

        if (array_length_pos >= 0) {
            GIArgInfo array_length_arg;
            GITypeInfo array_length_type;

            gjs_arg_cache_set_skip_all(&arguments[array_length_pos]);

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
                arguments[array_length_pos].arg_index = array_length_pos;
                arguments[array_length_pos].marshal_in = gjs_marshal_normal_out_in;

                self->marshal_in = gjs_marshal_normal_out_in;
                self->marshal_out = gjs_marshal_explicit_array_out_out;
                self->release = gjs_marshal_explicit_array_out_release;
            }

            self->contents.array.length_arg = array_length_pos;

            g_callable_info_load_arg(callable, array_length_pos, &array_length_arg);
            g_arg_info_load_type(&array_length_arg, &array_length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&array_length_type);

            if (array_length_pos < gi_index) {
                /* we already collected array_length_pos, remove it */
                inc_counter = false;
            }

            return true;
        }
    }

    if (direction == GI_DIRECTION_IN) {
        self->marshal_in = gjs_marshal_normal_in_in;
        self->marshal_out = gjs_marshal_skipped_out;
        self->release = gjs_marshal_normal_in_release;
    } else if (direction == GI_DIRECTION_INOUT) {
        self->marshal_in = gjs_marshal_normal_inout_in;
        self->marshal_out = gjs_marshal_normal_out_out;
        self->release = gjs_marshal_normal_inout_release;
    } else {
        self->marshal_in = gjs_marshal_normal_out_in;
        self->marshal_out = gjs_marshal_normal_out_out;
        self->release = gjs_marshal_normal_out_release;
    }

    return true;
}
