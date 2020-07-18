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
#include <stdint.h>
#include <string.h>

#include <ffi.h>
#include <girepository.h>
#include <glib.h>

#include <js/Conversions.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>        // for JS_TypeOfValue
#include <jsfriendapi.h>  // for JS_GetObjectFunction
#include <jspubtd.h>      // for JSTYPE_FUNCTION

#include "gi/arg-cache.h"
#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/foreign.h"
#include "gi/function.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gjs/byteArray.h"
#include "gjs/jsapi-util.h"

enum ExpectedType {
    OBJECT,
    FUNCTION,
    STRING,
    LAST,
};

static const char* expected_type_names[] = {"object", "function", "string"};
static_assert(G_N_ELEMENTS(expected_type_names) == ExpectedType::LAST,
              "Names must match the values in ExpectedType");

// The global entry point for any invocations of GDestroyNotify; look up the
// callback through the user_data and then free it.
static void gjs_destroy_notify_callback(void* data) {
    auto* trampoline = static_cast<GjsCallbackTrampoline*>(data);

    g_assert(trampoline);
    gjs_callback_trampoline_unref(trampoline);
}

// A helper function to retrieve array lengths from a GIArgument (letting the
// compiler generate good instructions in case of big endian machines)
GJS_USE
static size_t gjs_g_argument_get_array_length(GITypeTag tag, GIArgument* arg) {
    if (tag == GI_TYPE_TAG_INT8)
        return gjs_arg_get<int8_t>(arg);
    if (tag == GI_TYPE_TAG_UINT8)
        return gjs_arg_get<uint8_t>(arg);
    if (tag == GI_TYPE_TAG_INT16)
        return gjs_arg_get<int16_t>(arg);
    if (tag == GI_TYPE_TAG_UINT16)
        return gjs_arg_get<uint16_t>(arg);
    if (tag == GI_TYPE_TAG_INT32)
        return gjs_arg_get<int32_t>(arg);
    if (tag == GI_TYPE_TAG_UINT32)
        return gjs_arg_get<uint32_t>(arg);
    if (tag == GI_TYPE_TAG_INT64)
        return gjs_arg_get<int64_t>(arg);
    if (tag == GI_TYPE_TAG_UINT64)
        return gjs_arg_get<uint64_t>(arg);
    g_assert_not_reached();
}

static void gjs_g_argument_set_array_length(GITypeTag tag, GIArgument* arg,
                                            size_t value) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    // In a little endian system, the first bytes of an unsigned long value are
    // the same value, downcasted, and no code is needed. Also, we ignore the
    // sign, as we're just moving bits here.
    (void)tag;
    arg->v_ulong = value;
#else
    switch (tag) {
        case GI_TYPE_TAG_INT8:
            gjs_arg_set<int8_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT8:
            gjs_arg_set<uint8_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT16:
            gjs_arg_set<int16_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT16:
            gjs_arg_set<uint16_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT32:
            gjs_arg_set<int32_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT32:
            gjs_arg_set<uint32_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT64:
            gjs_arg_set<int64_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT64:
            gjs_arg_set<uint64_t>(arg, value);
            break;
    }
#endif
}

GJS_JSAPI_RETURN_CONVENTION
static bool throw_not_introspectable_argument(JSContext* cx,
                                              GICallableInfo* function,
                                              const char* arg_name) {
    gjs_throw(cx,
              "Function %s.%s cannot be called: argument '%s' is not "
              "introspectable.",
              g_base_info_get_namespace(function),
              g_base_info_get_name(function), arg_name);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool throw_not_introspectable_unboxed_type(JSContext* cx,
                                                  GICallableInfo* function,
                                                  const char* arg_name) {
    gjs_throw(cx,
              "Function %s.%s cannot be called: unexpected unregistered type "
              "for argument '%s'.",
              g_base_info_get_namespace(function),
              g_base_info_get_name(function), arg_name);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_typeof_mismatch(JSContext* cx, const char* arg_name,
                                   JS::HandleValue value,
                                   ExpectedType expected) {
    gjs_throw(cx, "Expected type %s for argument '%s' but got type %s",
              expected_type_names[expected], arg_name,
              JS::InformalValueTypeName(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_gtype_mismatch(JSContext* cx, const char* arg_name,
                                  JS::Value value, GType expected) {
    gjs_throw(
        cx, "Expected an object of type %s for argument '%s' but got type %s",
        g_type_name(expected), arg_name, JS::InformalValueTypeName(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_out_of_range(JSContext* cx, const char* arg_name,
                                GITypeTag tag) {
    gjs_throw(cx, "Argument %s: value is out of range for %s", arg_name,
              g_type_tag_to_string(tag));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_invalid_null(JSContext* cx, const char* arg_name) {
    gjs_throw(cx, "Argument %s may not be null", arg_name);
    return false;
}

// Marshallers:
//
// Each argument, irrespective of the direction, is processed in three phases:
// - before calling the C function [in]
// - after calling it, when converting the return value and out arguments [out]
// - at the end of the invocation, to release any allocated memory [release]
//
// The convention on the names is thus
// gjs_marshal_[argument type]_[direction]_[phase].
// Some types don't have direction (for example, caller_allocates is only out,
// and callback is only in), in which case it is implied.

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_skipped_in(JSContext*, GjsArgumentCache*,
                                   GjsFunctionCallState*, GIArgument*,
                                   JS::HandleValue) {
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    return gjs_value_to_g_argument(
        cx, value, &self->type_info, self->arg_name,
        self->is_return ? GJS_ARGUMENT_RETURN_VALUE : GJS_ARGUMENT_ARGUMENT,
        self->transfer, self->nullable, arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_inout_in(JSContext* cx, GjsArgumentCache* self,
                                         GjsFunctionCallState* state,
                                         GIArgument* arg,
                                         JS::HandleValue value) {
    if (!gjs_marshal_generic_in_in(cx, self, state, arg, value))
        return false;

    int ix = self->arg_pos;
    state->out_cvalues[ix] = state->inout_original_cvalues[ix] = *arg;
    gjs_arg_set(arg, &state->out_cvalues[ix]);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_in_in(JSContext* cx,
                                             GjsArgumentCache* self,
                                             GjsFunctionCallState* state,
                                             GArgument* arg,
                                             JS::HandleValue value) {
    void* data;
    size_t length;

    if (!gjs_array_to_explicit_array(
            cx, value, &self->type_info, self->arg_name, GJS_ARGUMENT_ARGUMENT,
            self->transfer, self->nullable, &data, &length))
        return false;

    int length_pos = self->contents.array.length_pos;
    gjs_g_argument_set_array_length(self->contents.array.length_tag,
                                    &state->in_cvalues[length_pos], length);
    gjs_arg_set(arg, data);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_inout_in(JSContext* cx,
                                                GjsArgumentCache* self,
                                                GjsFunctionCallState* state,
                                                GIArgument* arg,
                                                JS::HandleValue value) {
    if (!gjs_marshal_explicit_array_in_in(cx, self, state, arg, value))
        return false;

    int length_pos = self->contents.array.length_pos;
    int ix = self->arg_pos;

    if (!gjs_arg_get<void*>(arg)) {
        // Special case where we were given JS null to also pass null for
        // length, and not a pointer to an integer that derefs to 0.
        gjs_arg_unset<void*>(&state->in_cvalues[length_pos]);
        gjs_arg_unset<int>(&state->out_cvalues[length_pos]);
        gjs_arg_unset<int>(&state->inout_original_cvalues[length_pos]);

        gjs_arg_unset<void*>(&state->out_cvalues[ix]);
        gjs_arg_unset<void*>(&state->inout_original_cvalues[ix]);
    } else {
        state->out_cvalues[length_pos] =
            state->inout_original_cvalues[length_pos] =
                state->in_cvalues[length_pos];
        gjs_arg_set(&state->in_cvalues[length_pos],
                    &state->out_cvalues[length_pos]);

        state->out_cvalues[ix] = state->inout_original_cvalues[ix] = *arg;
        gjs_arg_set(arg, &state->out_cvalues[ix]);
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_callback_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState* state,
                                    GIArgument* arg, JS::HandleValue value) {
    GjsCallbackTrampoline* trampoline;
    ffi_closure* closure;

    if (value.isNull() && self->nullable) {
        closure = nullptr;
        trampoline = nullptr;
    } else {
        if (JS_TypeOfValue(cx, value) != JSTYPE_FUNCTION) {
            gjs_throw(cx, "Expected function for callback argument %s, got %s",
                      self->arg_name, JS::InformalValueTypeName(value));
            return false;
        }

        JS::RootedFunction func(cx, JS_GetObjectFunction(&value.toObject()));
        GjsAutoCallableInfo callable_info =
            g_type_info_get_interface(&self->type_info);
        trampoline = gjs_callback_trampoline_new(cx, func, callable_info,
                                                 self->contents.callback.scope,
                                                 state->instance_object, false);
        closure = trampoline->closure;
    }

    int destroy_pos = self->contents.callback.destroy_pos;
    if (destroy_pos >= 0) {
        gjs_arg_set(&state->in_cvalues[destroy_pos],
                    trampoline ? gjs_destroy_notify_callback : nullptr);
    }
    int closure_pos = self->contents.callback.closure_pos;
    if (closure_pos >= 0)
        gjs_arg_set(&state->in_cvalues[closure_pos], trampoline);

    if (trampoline && self->contents.callback.scope != GI_SCOPE_TYPE_CALL) {
        // Add an extra reference that will be cleared when collecting async
        // calls, or when GDestroyNotify is called
        gjs_callback_trampoline_ref(trampoline);
    }
    gjs_arg_set(arg, closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_out_in(JSContext*, GjsArgumentCache* self,
                                       GjsFunctionCallState* state,
                                       GIArgument* arg, JS::HandleValue) {
    // Default value in case a broken C function doesn't fill in the pointer
    gjs_arg_unset<void*>(&state->out_cvalues[self->arg_pos]);
    gjs_arg_set(arg,
                &gjs_arg_member<void*>(&state->out_cvalues[self->arg_pos]));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_caller_allocates_in(JSContext*, GjsArgumentCache* self,
                                            GjsFunctionCallState* state,
                                            GIArgument* arg, JS::HandleValue) {
    void* blob = g_slice_alloc0(self->contents.caller_allocates_size);
    gjs_arg_set(arg, blob);
    gjs_arg_set(&state->out_cvalues[self->arg_pos], blob);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_null_in_in(JSContext*, GjsArgumentCache*,
                                   GjsFunctionCallState*, GIArgument* arg,
                                   JS::HandleValue) {
    gjs_arg_unset<void*>(arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_boolean_in_in(JSContext*, GjsArgumentCache*,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    gjs_arg_set(arg, JS::ToBoolean(value));
    return true;
}

// Type tags are alternated, signed / unsigned
static int32_t min_max_ints[5][2] = {{G_MININT8, G_MAXINT8},
                                     {0, G_MAXUINT8},
                                     {G_MININT16, G_MAXINT16},
                                     {0, G_MAXUINT16},
                                     {G_MININT32, G_MAXINT32}};

GJS_USE
static inline bool value_in_range(int32_t number, GITypeTag tag) {
    return (number >= min_max_ints[tag - GI_TYPE_TAG_INT8][0] &&
            number <= min_max_ints[tag - GI_TYPE_TAG_INT8][1]);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_integer_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    GITypeTag tag = self->contents.number.number_tag;

    if (self->contents.number.is_unsigned) {
        uint32_t number;
        if (!JS::ToUint32(cx, value, &number))
            return false;

        if (!value_in_range(number, tag))
            return report_out_of_range(cx, self->arg_name, tag);

        gjs_g_argument_set_array_length(tag, arg, number);
    } else {
        int32_t number;
        if (!JS::ToInt32(cx, value, &number))
            return false;

        if (!value_in_range(number, tag))
            return report_out_of_range(cx, self->arg_name, tag);

        gjs_g_argument_set_array_length(tag, arg, number);
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_number_in_in(JSContext* cx, GjsArgumentCache* self,
                                     GjsFunctionCallState*, GIArgument* arg,
                                     JS::HandleValue value) {
    double v;
    if (!JS::ToNumber(cx, value, &v))
        return false;

    GITypeTag tag = self->contents.number.number_tag;
    if (tag == GI_TYPE_TAG_DOUBLE) {
        gjs_arg_set(arg, v);
    } else if (tag == GI_TYPE_TAG_FLOAT) {
        if (v < -G_MAXFLOAT || v > G_MAXFLOAT)
            return report_out_of_range(cx, self->arg_name, GI_TYPE_TAG_FLOAT);
        gjs_arg_set<float>(arg, v);
    } else if (tag == GI_TYPE_TAG_INT64) {
        if (v < G_MININT64 || v > G_MAXINT64)
            return report_out_of_range(cx, self->arg_name, GI_TYPE_TAG_INT64);
        gjs_arg_set<int64_t>(arg, v);
    } else if (tag == GI_TYPE_TAG_UINT64) {
        if (v < 0 || v > G_MAXUINT64)
            return report_out_of_range(cx, self->arg_name, GI_TYPE_TAG_UINT64);
        gjs_arg_set<uint64_t>(arg, v);
    } else if (tag == GI_TYPE_TAG_UINT32) {
        if (v < 0 || v > G_MAXUINT32)
            return report_out_of_range(cx, self->arg_name, GI_TYPE_TAG_UINT32);
        gjs_arg_set<uint32_t>(arg, v);
    } else {
        g_assert_not_reached();
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_unichar_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    if (!value.isString())
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::STRING);

    return gjs_unichar_from_string(cx, value, &gjs_arg_member<char32_t>(arg));
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gtype_in_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState*, GIArgument* arg,
                                    JS::HandleValue value) {
    if (value.isNull())
        return report_invalid_null(cx, self->arg_name);
    if (!value.isObject())
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject gtype_obj(cx, &value.toObject());
    return gjs_gtype_get_actual_gtype(
        cx, gtype_obj, &gjs_arg_member<GType, GI_TYPE_TAG_GTYPE>(arg));
}

// Common code for most types that are pointers on the C side
bool GjsArgumentCache::handle_nullable(JSContext* cx, GIArgument* arg) {
    if (!nullable)
        return report_invalid_null(cx, arg_name);
    gjs_arg_unset<void*>(arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_string_in_in(JSContext* cx, GjsArgumentCache* self,
                                     GjsFunctionCallState*, GIArgument* arg,
                                     JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    if (!value.isString())
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::STRING);

    if (self->contents.string_is_filename) {
        GjsAutoChar str;
        if (!gjs_string_to_filename(cx, value, &str))
            return false;
        gjs_arg_set(arg, str.release());
        return true;
    }

    JS::UniqueChars str = gjs_string_to_utf8(cx, value);
    if (!str)
        return false;
    gjs_arg_set(arg, g_strdup(str.get()));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_enum_in_in(JSContext* cx, GjsArgumentCache* self,
                                   GjsFunctionCallState*, GIArgument* arg,
                                   JS::HandleValue value) {
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
        gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(arg, number);
    else if (self->contents.enum_type.enum_max <= G_MAXUINT32)
        gjs_arg_set<unsigned, GI_TYPE_TAG_INTERFACE>(arg, number);
    else
        gjs_arg_set(arg, number);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_flags_in_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState*, GIArgument* arg,
                                    JS::HandleValue value) {
    int64_t number;
    if (!JS::ToInt64(cx, value, &number))
        return false;

    if ((uint64_t(number) & self->contents.flags_mask) != uint64_t(number)) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for flags argument %s",
                  number, self->arg_name);
        return false;
    }

    if (self->contents.flags_mask <= G_MAXUINT32)
        gjs_arg_set<unsigned, GI_TYPE_TAG_INTERFACE>(arg, number);
    else
        gjs_arg_set(arg, uint64_t(number));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_foreign_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    GIStructInfo* foreign_info = g_type_info_get_interface(&self->type_info);
    self->contents.tmp_foreign_info = foreign_info;
    return gjs_struct_foreign_convert_to_g_argument(
        cx, value, foreign_info, self->arg_name, GJS_ARGUMENT_ARGUMENT,
        self->transfer, self->nullable, arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gvalue_in_in(JSContext* cx, GjsArgumentCache*,
                                     GjsFunctionCallState*, GIArgument* arg,
                                     JS::HandleValue value) {
    GValue gvalue = G_VALUE_INIT;

    if (!gjs_value_to_g_value(cx, value, &gvalue))
        return false;

    gjs_arg_set(arg, g_boxed_copy(G_TYPE_VALUE, &gvalue));

    g_value_unset(&gvalue);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_boxed_in_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState*, GIArgument* arg,
                                    JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    GType gtype = self->contents.object.gtype;

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    if (gtype == G_TYPE_ERROR) {
        return ErrorBase::transfer_to_gi_argument(
            cx, object, arg, GI_DIRECTION_IN, self->transfer);
    }

    return BoxedBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                              self->transfer, gtype,
                                              self->contents.object.info);
}

// Unions include ClutterEvent and GdkEvent, which occur fairly often in an
// interactive application, so they're worth a special case in a different
// virtual function.
GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_union_in_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState*, GIArgument* arg,
                                    JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return UnionBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                              self->transfer, gtype,
                                              self->contents.object.info);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gclosure_in_in(JSContext* cx, GjsArgumentCache* self,
                                       GjsFunctionCallState*, GIArgument* arg,
                                       JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    if (!(JS_TypeOfValue(cx, value) == JSTYPE_FUNCTION))
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::FUNCTION);

    JS::RootedFunction func(cx, JS_GetObjectFunction(&value.toObject()));
    GClosure* closure = gjs_closure_new_marshaled(cx, func, "boxed");
    gjs_arg_set(arg, closure);
    g_closure_ref(closure);
    g_closure_sink(closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gbytes_in_in(JSContext* cx, GjsArgumentCache* self,
                                     GjsFunctionCallState*, GIArgument* arg,
                                     JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, G_TYPE_BYTES);

    JS::RootedObject object(cx, &value.toObject());
    if (JS_IsUint8Array(object)) {
        gjs_arg_set(arg, gjs_byte_array_get_bytes(object));
        return true;
    }

    // The bytearray path is taking an extra ref irrespective of transfer
    // ownership, so we need to do the same here.
    return BoxedBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, GI_TRANSFER_EVERYTHING, G_TYPE_BYTES,
        self->contents.object.info);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_object_in_in(JSContext* cx, GjsArgumentCache* self,
                                     GjsFunctionCallState*, GIArgument* arg,
                                     JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return ObjectBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                               self->transfer, gtype);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gtype_struct_instance_in(JSContext* cx,
                                                 GjsArgumentCache* self,
                                                 GjsFunctionCallState*,
                                                 GIArgument* arg,
                                                 JS::HandleValue value) {
    // Instance parameter is never nullable
    if (!value.isObject())
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject obj(cx, &value.toObject());
    GType actual_gtype;
    if (!gjs_gtype_get_actual_gtype(cx, obj, &actual_gtype))
        return false;

    if (actual_gtype == G_TYPE_NONE) {
        gjs_throw(cx, "Invalid GType class passed for instance parameter");
        return false;
    }

    // We use peek here to simplify reference counting (we just ignore transfer
    // annotation, as GType classes are never really freed.) We know that the
    // GType class is referenced at least once when the JS constructor is
    // initialized.
    if (g_type_is_a(actual_gtype, G_TYPE_INTERFACE))
        gjs_arg_set(arg, g_type_default_interface_peek(actual_gtype));
    else
        gjs_arg_set(arg, g_type_class_peek(actual_gtype));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_param_instance_in(JSContext* cx, GjsArgumentCache* self,
                                          GjsFunctionCallState*,
                                          GIArgument* arg,
                                          JS::HandleValue value) {
    // Instance parameter is never nullable
    if (!value.isObject())
        return report_typeof_mismatch(cx, self->arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject obj(cx, &value.toObject());
    if (!gjs_typecheck_param(cx, obj, G_TYPE_PARAM, true))
        return false;
    gjs_arg_set(arg, gjs_g_param_from_param(cx, obj));

    if (self->transfer == GI_TRANSFER_EVERYTHING)
        g_param_spec_ref(gjs_arg_get<GParamSpec*>(arg));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_skipped_out(JSContext*, GjsArgumentCache*,
                                    GjsFunctionCallState*, GIArgument*,
                                    JS::MutableHandleValue) {
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_out_out(JSContext* cx, GjsArgumentCache* self,
                                        GjsFunctionCallState*, GIArgument* arg,
                                        JS::MutableHandleValue value) {
    return gjs_value_from_g_argument(cx, value, &self->type_info, arg, true);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_out_out(JSContext* cx,
                                               GjsArgumentCache* self,
                                               GjsFunctionCallState* state,
                                               GIArgument* arg,
                                               JS::MutableHandleValue value) {
    int length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &(state->out_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    return gjs_value_from_explicit_array(cx, value, &self->type_info, arg,
                                         length);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_skipped_release(JSContext*, GjsArgumentCache*,
                                        GjsFunctionCallState*,
                                        GIArgument* in_arg G_GNUC_UNUSED,
                                        GIArgument* out_arg G_GNUC_UNUSED) {
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_in_release(JSContext* cx,
                                           GjsArgumentCache* self,
                                           GjsFunctionCallState* state,
                                           GIArgument* in_arg,
                                           GIArgument* out_arg G_GNUC_UNUSED) {
    GITransfer transfer =
        state->call_completed ? self->transfer : GI_TRANSFER_NOTHING;
    return gjs_g_argument_release_in_arg(cx, transfer, &self->type_info,
                                         in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_out_release(JSContext* cx,
                                            GjsArgumentCache* self,
                                            GjsFunctionCallState*,
                                            GIArgument* in_arg G_GNUC_UNUSED,
                                            GIArgument* out_arg) {
    return gjs_g_argument_release(cx, self->transfer, &self->type_info,
                                  out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_inout_release(JSContext* cx,
                                              GjsArgumentCache* self,
                                              GjsFunctionCallState* state,
                                              GIArgument* in_arg,
                                              GIArgument* out_arg) {
    // For inout, transfer refers to what we get back from the function; for
    // the temporary C value we allocated, clearly we're responsible for
    // freeing it.

    GIArgument* original_out_arg =
        &(state->inout_original_cvalues[self->arg_pos]);
    if (!gjs_g_argument_release_in_arg(cx, GI_TRANSFER_NOTHING,
                                       &self->type_info, original_out_arg))
        return false;

    return gjs_marshal_generic_out_release(cx, self, state, in_arg, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_out_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg G_GNUC_UNUSED, GIArgument* out_arg) {
    int length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &(state->out_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_in_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg, GIArgument* out_arg G_GNUC_UNUSED) {
    int length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &(state->in_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    GITransfer transfer =
        state->call_completed ? self->transfer : GI_TRANSFER_NOTHING;

    return gjs_g_argument_release_in_array(cx, transfer, &self->type_info,
                                           length, in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_inout_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg G_GNUC_UNUSED, GIArgument* out_arg) {
    int length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &(state->in_cvalues[length_pos]);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    // For inout, transfer refers to what we get back from the function; for
    // the temporary C value we allocated, clearly we're responsible for
    // freeing it.

    GIArgument* original_out_arg =
        &(state->inout_original_cvalues[self->arg_pos]);
    if (gjs_arg_get<void*>(original_out_arg) != gjs_arg_get<void*>(out_arg) &&
        !gjs_g_argument_release_in_array(cx, GI_TRANSFER_NOTHING,
                                         &self->type_info, length,
                                         original_out_arg))
        return false;

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_caller_allocates_release(
    JSContext*, GjsArgumentCache* self, GjsFunctionCallState*,
    GIArgument* in_arg, GIArgument* out_arg G_GNUC_UNUSED) {
    g_slice_free1(self->contents.caller_allocates_size,
                  gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_callback_release(JSContext*, GjsArgumentCache*,
                                         GjsFunctionCallState*,
                                         GIArgument* in_arg,
                                         GIArgument* out_arg G_GNUC_UNUSED) {
    auto* closure = gjs_arg_get<ffi_closure*>(in_arg);
    if (!closure)
        return true;

    auto trampoline = static_cast<GjsCallbackTrampoline*>(closure->user_data);
    // CallbackTrampolines are refcounted because for notified/async closures
    // it is possible to destroy it while in call, and therefore we cannot
    // check its scope at this point
    gjs_callback_trampoline_unref(trampoline);
    gjs_arg_unset<void*>(in_arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_string_in_release(JSContext*, GjsArgumentCache*,
                                          GjsFunctionCallState*,
                                          GIArgument* in_arg,
                                          GIArgument* out_arg G_GNUC_UNUSED) {
    g_free(gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_foreign_in_release(JSContext* cx,
                                           GjsArgumentCache* self,
                                           GjsFunctionCallState* state,
                                           GIArgument* in_arg,
                                           GIArgument* out_arg G_GNUC_UNUSED) {
    bool ok = true;

    GITransfer transfer =
        state->call_completed ? self->transfer : GI_TRANSFER_NOTHING;

    if (transfer == GI_TRANSFER_NOTHING)
        ok = gjs_struct_foreign_release_g_argument(
            cx, self->transfer, self->contents.tmp_foreign_info, in_arg);

    g_base_info_unref(self->contents.tmp_foreign_info);
    return ok;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_boxed_in_release(JSContext*, GjsArgumentCache* self,
                                         GjsFunctionCallState*,
                                         GIArgument* in_arg,
                                         GIArgument* out_arg G_GNUC_UNUSED) {
    GType gtype = self->contents.object.gtype;
    g_assert(g_type_is_a(gtype, G_TYPE_BOXED));

    if (!gjs_arg_get<void*>(in_arg))
        return true;

    g_boxed_free(gtype, gjs_arg_get<void*>(in_arg));
    return true;
}

static void gjs_arg_cache_interface_free(GjsArgumentCache* self) {
    g_clear_pointer(&self->contents.object.info, g_base_info_unref);
}

static inline void gjs_arg_cache_set_skip_all(GjsArgumentCache* self) {
    self->marshal_in = gjs_marshal_skipped_in;
    self->marshal_out = gjs_marshal_skipped_out;
    self->release = gjs_marshal_skipped_release;
    self->skip_in = self->skip_out = true;
}

bool gjs_arg_cache_build_return(JSContext*, GjsArgumentCache* self,
                                GjsArgumentCache* arguments,
                                GICallableInfo* callable,
                                bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    g_callable_info_load_return_type(callable, &self->type_info);

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_VOID) {
        *inc_counter_out = false;
        gjs_arg_cache_set_skip_all(self);
        return true;
    }

    *inc_counter_out = true;
    self->arg_pos = -1;
    self->arg_name = "return value";
    self->transfer = g_callable_info_get_caller_owns(callable);
    self->nullable = false;  // We don't really care for return values
    self->is_return = true;

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_ARRAY) {
        int length_pos = g_type_info_get_array_length(&self->type_info);
        if (length_pos >= 0) {
            gjs_arg_cache_set_skip_all(&arguments[length_pos]);

            // Even if we skip the length argument most of the time, we need to
            // do some basic initialization here.
            arguments[length_pos].arg_pos = length_pos;
            arguments[length_pos].marshal_in = gjs_marshal_generic_out_in;

            self->marshal_in = gjs_marshal_generic_out_in;
            self->marshal_out = gjs_marshal_explicit_array_out_out;
            self->release = gjs_marshal_explicit_array_out_release;

            self->contents.array.length_pos = length_pos;

            GIArgInfo length_arg;
            g_callable_info_load_arg(callable, length_pos, &length_arg);
            GITypeInfo length_type;
            g_arg_info_load_type(&length_arg, &length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&length_type);

            return true;
        }
    }

    // marshal_in is ignored for the return value, but skip_in is not (it is
    // used in the failure release path)
    self->skip_in = true;
    self->marshal_out = gjs_marshal_generic_out_out;
    self->release = gjs_marshal_generic_out_release;

    return true;
}

static void gjs_arg_cache_build_enum_bounds(GjsArgumentCache* self,
                                            GIEnumInfo* enum_info) {
    int64_t min = G_MAXINT64;
    int64_t max = G_MININT64;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);

        if (value > max)
            max = value;
        if (value < min)
            min = value;
    }

    self->contents.enum_type.enum_min = min;
    self->contents.enum_type.enum_max = max;
}

static void gjs_arg_cache_build_flags_mask(GjsArgumentCache* self,
                                           GIEnumInfo* enum_info) {
    uint64_t mask = 0;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        uint64_t value = uint64_t(g_value_info_get_value(value_info));
        mask |= value;
    }

    self->contents.flags_mask = mask;
}

GJS_USE
static inline bool is_gdk_atom(GIBaseInfo* info) {
    return strcmp("Atom", g_base_info_get_name(info)) == 0 &&
           strcmp("Gdk", g_base_info_get_namespace(info)) == 0;
}

static bool gjs_arg_cache_build_interface_in_arg(JSContext* cx,
                                                 GjsArgumentCache* self,
                                                 GICallableInfo* callable,
                                                 GIBaseInfo* interface_info) {
    GIInfoType interface_type = g_base_info_get_type(interface_info);

    // We do some transfer magic later, so let's ensure we don't mess up.
    // Should not happen in practice.
    if (G_UNLIKELY(self->transfer == GI_TRANSFER_CONTAINER))
        return throw_not_introspectable_argument(cx, callable, self->arg_name);

    switch (interface_type) {
        case GI_INFO_TYPE_ENUM:
            gjs_arg_cache_build_enum_bounds(self, interface_info);
            self->marshal_in = gjs_marshal_enum_in_in;
            return true;

        case GI_INFO_TYPE_FLAGS:
            gjs_arg_cache_build_flags_mask(self, interface_info);
            self->marshal_in = gjs_marshal_flags_in_in;
            return true;

        case GI_INFO_TYPE_STRUCT:
            if (g_struct_info_is_foreign(interface_info)) {
                self->marshal_in = gjs_marshal_foreign_in_in;
                self->release = gjs_marshal_foreign_in_release;
                return true;
            }
            // fall through
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_UNION: {
            GType gtype = g_registered_type_info_get_g_type(interface_info);
            self->contents.object.gtype = gtype;
            self->contents.object.info = g_base_info_ref(interface_info);
            self->free = gjs_arg_cache_interface_free;

            // Transfer handling is a bit complex here, because some of our _in
            // marshallers know not to copy stuff if we don't need to.

            if (gtype == G_TYPE_VALUE) {
                self->marshal_in = gjs_marshal_gvalue_in_in;
                if (self->transfer == GI_TRANSFER_NOTHING)
                    self->release = gjs_marshal_boxed_in_release;
                return true;
            }

            if (is_gdk_atom(interface_info)) {
                // Fall back to the generic marshaller
                self->marshal_in = gjs_marshal_generic_in_in;
                self->release = gjs_marshal_generic_in_release;
                return true;
            }

            if (gtype == G_TYPE_CLOSURE) {
                self->marshal_in = gjs_marshal_gclosure_in_in;
                if (self->transfer == GI_TRANSFER_NOTHING)
                    self->release = gjs_marshal_boxed_in_release;
                return true;
            }

            if (gtype == G_TYPE_BYTES) {
                self->marshal_in = gjs_marshal_gbytes_in_in;
                if (self->transfer == GI_TRANSFER_NOTHING)
                    self->release = gjs_marshal_boxed_in_release;
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT) ||
                g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                self->marshal_in = gjs_marshal_object_in_in;
                // This is a smart marshaller, no release needed
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                // Fall back to the generic marshaller
                self->marshal_in = gjs_marshal_generic_in_in;
                self->release = gjs_marshal_generic_in_release;
                return true;
            }

            if (interface_type == GI_INFO_TYPE_UNION) {
                if (gtype == G_TYPE_NONE) {
                    // Can't handle unions without a GType
                    return throw_not_introspectable_unboxed_type(
                        cx, callable, self->arg_name);
                }

                self->marshal_in = gjs_marshal_union_in_in;
                // This is a smart marshaller, no release needed
                return true;
            }

            // generic boxed type
            if (gtype == G_TYPE_NONE && self->transfer != GI_TRANSFER_NOTHING) {
                // Can't transfer ownership of a structure type not
                // registered as a boxed
                return throw_not_introspectable_unboxed_type(cx, callable,
                                                             self->arg_name);
            }

            self->marshal_in = gjs_marshal_boxed_in_in;
            // This is a smart marshaller, no release needed
            return true;
        } break;

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
            // Don't know how to handle this interface type (should not happen
            // in practice, for typelibs emitted by g-ir-compiler)
            return throw_not_introspectable_argument(cx, callable,
                                                     self->arg_name);
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_arg_cache_build_normal_in_arg(JSContext* cx,
                                              GjsArgumentCache* self,
                                              GICallableInfo* callable,
                                              GITypeTag tag) {
    // "Normal" in arguments are those arguments that don't require special
    // processing, and don't touch other arguments.
    // Main categories are:
    // - void*
    // - small numbers (fit in 32bit)
    // - big numbers (need a double)
    // - strings
    // - enums/flags (different from numbers in the way they're exposed in GI)
    // - objects (GObjects, boxed, unions, etc.)
    // - hashes
    // - sequences (null-terminated arrays, lists, etc.)

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

        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(&self->type_info);
            return gjs_arg_cache_build_interface_in_arg(cx, self, callable,
                                                        interface_info);
        }

        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
        default:
            // FIXME: Falling back to the generic marshaller
            self->marshal_in = gjs_marshal_generic_in_in;
            self->release = gjs_marshal_generic_in_release;
    }

    return true;
}

bool gjs_arg_cache_build_instance(JSContext* cx, GjsArgumentCache* self,
                                  GICallableInfo* callable) {
    GIBaseInfo* interface_info = g_base_info_get_container(callable);  // !owned

    self->arg_pos = -2;
    self->arg_name = "instance parameter";
    self->transfer = g_callable_info_get_instance_ownership_transfer(callable);
    // Some calls accept null for the instance, but generally in an object
    // oriented language it's wrong to call a method on null
    self->nullable = false;
    self->skip_out = true;

    // These cases could be covered by the generic marshaller, except that
    // there's no way to get GITypeInfo for a method's instance parameter.
    // Instead, special-case the arguments here that would otherwise go through
    // the generic marshaller.
    // See: https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/334
    GIInfoType info_type = g_base_info_get_type(interface_info);
    if (info_type == GI_INFO_TYPE_STRUCT &&
        g_struct_info_is_gtype_struct(interface_info)) {
        self->marshal_in = gjs_marshal_gtype_struct_instance_in;
        self->release = gjs_marshal_skipped_release;
        return true;
    }
    if (info_type == GI_INFO_TYPE_OBJECT) {
        GType gtype = g_registered_type_info_get_g_type(interface_info);

        if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            self->marshal_in = gjs_marshal_param_instance_in;
            self->release = gjs_marshal_skipped_release;
            return true;
        }
    }

    bool ok = gjs_arg_cache_build_interface_in_arg(cx, self, callable,
                                                   interface_info);
    // Don't set marshal_out, it should not be called for the instance
    // parameter
    self->release = gjs_marshal_skipped_release;
    return ok;
}

bool gjs_arg_cache_build_arg(JSContext* cx, GjsArgumentCache* self,
                             GjsArgumentCache* arguments, int gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    self->arg_pos = gi_index;
    self->arg_name = g_base_info_get_name(arg);
    g_arg_info_load_type(arg, &self->type_info);
    self->transfer = g_arg_info_get_ownership_transfer(arg);
    self->nullable = g_arg_info_may_be_null(arg);
    self->is_return = false;

    if (direction == GI_DIRECTION_IN)
        self->skip_out = true;
    else if (direction == GI_DIRECTION_OUT)
        self->skip_in = true;
    *inc_counter_out = true;

    if (direction == GI_DIRECTION_OUT && g_arg_info_is_caller_allocates(arg)) {
        GjsAutoBaseInfo interface_info =
            g_type_info_get_interface(&self->type_info);
        g_assert(interface_info);

        GIInfoType interface_type = g_base_info_get_type(interface_info);

        size_t size;
        if (interface_type == GI_INFO_TYPE_STRUCT) {
            size = g_struct_info_get_size(interface_info);
        } else if (interface_type == GI_INFO_TYPE_UNION) {
            size = g_union_info_get_size(interface_info);
        } else {
            gjs_throw(cx,
                      "Unsupported type %s for argument %s with (out "
                      "caller-allocates)",
                      g_info_type_to_string(interface_type), self->arg_name);
            return false;
        }

        self->marshal_in = gjs_marshal_caller_allocates_in;
        self->marshal_out = gjs_marshal_generic_out_out;
        self->release = gjs_marshal_caller_allocates_release;
        self->contents.caller_allocates_size = size;

        return true;
    }

    GITypeTag type_tag = g_type_info_get_tag(&self->type_info);
    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info =
            g_type_info_get_interface(&self->type_info);
        if (interface_info.type() == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                // Can't do callbacks for out or inout
                gjs_throw(cx,
                          "Function %s.%s has a callback out-argument %s, not "
                          "supported",
                          g_base_info_get_namespace(callable),
                          g_base_info_get_name(callable), self->arg_name);
                return false;
            }

            if (strcmp(interface_info.name(), "DestroyNotify") == 0 &&
                strcmp(interface_info.ns(), "GLib") == 0) {
                // We don't know (yet) what to do with GDestroyNotify appearing
                // before a callback. If the callback comes later in the
                // argument list, then the null marshaller will be
                // overwritten with the 'skipped' one. If no callback follows,
                // then this is probably an unsupported function, so the
                // function invocation code will check this and throw.
                self->marshal_in = nullptr;
                self->marshal_out = gjs_marshal_skipped_out;
                self->release = gjs_marshal_skipped_release;
                *inc_counter_out = false;
            } else {
                self->marshal_in = gjs_marshal_callback_in;
                self->marshal_out = gjs_marshal_skipped_out;
                self->release = gjs_marshal_callback_release;

                int destroy_pos = g_arg_info_get_destroy(arg);
                int closure_pos = g_arg_info_get_closure(arg);

                if (destroy_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[destroy_pos]);

                if (closure_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[closure_pos]);

                if (destroy_pos >= 0 && closure_pos < 0) {
                    gjs_throw(cx,
                              "Function %s.%s has a GDestroyNotify but no "
                              "user_data, not supported",
                              g_base_info_get_namespace(callable),
                              g_base_info_get_name(callable));
                    return false;
                }

                self->contents.callback.scope = g_arg_info_get_scope(arg);
                self->contents.callback.destroy_pos = destroy_pos;
                self->contents.callback.closure_pos = closure_pos;
            }

            return true;
        }
    }

    if (type_tag == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_type(&self->type_info) == GI_ARRAY_TYPE_C) {
        int length_pos = g_type_info_get_array_length(&self->type_info);

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
                // Even if we skip the length argument most of time, we need to
                // do some basic initialization here.
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
                // we already collected length_pos, remove it
                *inc_counter_out = false;
            }

            return true;
        }
    }

    if (direction == GI_DIRECTION_IN) {
        bool ok =
            gjs_arg_cache_build_normal_in_arg(cx, self, callable, type_tag);
        self->marshal_out = gjs_marshal_skipped_out;
        return ok;
    }

    if (direction == GI_DIRECTION_INOUT) {
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
