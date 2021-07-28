/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>

#include <config.h>

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <limits>
#include <unordered_set>  // for unordered_set::erase(), insert()

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
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/foreign.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"  // for GjsTypecheckNoThrow
#include "gjs/byteArray.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"

enum ExpectedType {
    OBJECT,
    FUNCTION,
    STRING,
    LAST,
};

static const char* expected_type_names[] = {"object", "function", "string"};
static_assert(G_N_ELEMENTS(expected_type_names) == ExpectedType::LAST,
              "Names must match the values in ExpectedType");

// A helper function to retrieve array lengths from a GIArgument (letting the
// compiler generate good instructions in case of big endian machines)
[[nodiscard]] size_t gjs_g_argument_get_array_length(GITypeTag tag,
                                                     GIArgument* arg) {
    switch (tag) {
        case GI_TYPE_TAG_INT8:
            return gjs_arg_get<int8_t>(arg);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_get<uint8_t>(arg);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_get<int16_t>(arg);
        case GI_TYPE_TAG_UINT16:
            return gjs_arg_get<uint16_t>(arg);
        case GI_TYPE_TAG_INT32:
            return gjs_arg_get<int32_t>(arg);
        case GI_TYPE_TAG_UINT32:
            return gjs_arg_get<uint32_t>(arg);
        case GI_TYPE_TAG_INT64:
            return gjs_arg_get<int64_t>(arg);
        case GI_TYPE_TAG_UINT64:
            return gjs_arg_get<uint64_t>(arg);
        default:
            g_assert_not_reached();
    }
}

static void gjs_g_argument_set_array_length(GITypeTag tag, GIArgument* arg,
                                            size_t value) {
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
        default:
            g_assert_not_reached();
    }
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
static bool gjs_marshal_not_introspectable_in(JSContext* cx,
                                              GjsArgumentCache* self,
                                              GjsFunctionCallState* state,
                                              GIArgument*, JS::HandleValue) {
    static const char* reason_strings[] = {
        "callback out-argument",
        "DestroyNotify argument with no callback",
        "DestroyNotify argument with no user data",
        "type not supported for (transfer container)",
        "type not supported for (out caller-allocates)",
        "boxed type with transfer not registered as a GType",
        "union type not registered as a GType",
        "type not supported by introspection",
    };
    static_assert(
        G_N_ELEMENTS(reason_strings) == NotIntrospectableReason::LAST_REASON,
        "Explanations must match the values in NotIntrospectableReason");

    gjs_throw(cx,
              "Function %s() cannot be called: argument '%s' with type %s is "
              "not introspectable because it has a %s",
              state->display_name().get(), self->arg_name,
              g_type_tag_to_string(g_type_info_get_tag(&self->type_info)),
              reason_strings[self->contents.reason]);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    return gjs_value_to_g_argument(cx, value, &self->type_info, self->arg_name,
                                   self->is_return_value()
                                       ? GJS_ARGUMENT_RETURN_VALUE
                                       : GJS_ARGUMENT_ARGUMENT,
                                   self->transfer, self->flags, arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_inout_in(JSContext* cx, GjsArgumentCache* self,
                                         GjsFunctionCallState* state,
                                         GIArgument* arg,
                                         JS::HandleValue value) {
    if (!gjs_marshal_generic_in_in(cx, self, state, arg, value))
        return false;

    int ix = self->arg_pos;
    state->out_cvalue(ix) = state->inout_original_cvalue(ix) = *arg;
    gjs_arg_set(arg, &state->out_cvalue(ix));
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
            self->transfer, self->flags, &data, &length))
        return false;

    uint8_t length_pos = self->contents.array.length_pos;
    gjs_g_argument_set_array_length(self->contents.array.length_tag,
                                    &state->in_cvalue(length_pos), length);
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

    uint8_t length_pos = self->contents.array.length_pos;
    uint8_t ix = self->arg_pos;

    if (!gjs_arg_get<void*>(arg)) {
        // Special case where we were given JS null to also pass null for
        // length, and not a pointer to an integer that derefs to 0.
        gjs_arg_unset<void*>(&state->in_cvalue(length_pos));
        gjs_arg_unset<int>(&state->out_cvalue(length_pos));
        gjs_arg_unset<int>(&state->inout_original_cvalue(length_pos));

        gjs_arg_unset<void*>(&state->out_cvalue(ix));
        gjs_arg_unset<void*>(&state->inout_original_cvalue(ix));
    } else {
        state->out_cvalue(length_pos) =
            state->inout_original_cvalue(length_pos) =
                state->in_cvalue(length_pos);
        gjs_arg_set(&state->in_cvalue(length_pos),
                    &state->out_cvalue(length_pos));

        state->out_cvalue(ix) = state->inout_original_cvalue(ix) = *arg;
        gjs_arg_set(arg, &state->out_cvalue(ix));
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_callback_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState* state,
                                    GIArgument* arg, JS::HandleValue value) {
    GjsCallbackTrampoline* trampoline;
    ffi_closure* closure;

    if (value.isNull() && (self->flags & GjsArgumentFlags::MAY_BE_NULL)) {
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
        bool is_object_method = !!state->instance_object;
        trampoline = GjsCallbackTrampoline::create(
            cx, func, callable_info, self->contents.callback.scope,
            is_object_method, false);
        if (!trampoline)
            return false;
        if (self->contents.callback.scope == GI_SCOPE_TYPE_NOTIFIED &&
            is_object_method) {
            auto* priv = ObjectInstance::for_js(cx, state->instance_object);
            if (!priv) {
                gjs_throw(cx, "Signal connected to wrong type of object");
                return false;
            }

            if (!priv->associate_closure(cx, trampoline))
                return false;
        }
        closure = trampoline->closure();
    }

    if (self->has_callback_destroy()) {
        uint8_t destroy_pos = self->contents.callback.destroy_pos;
        GDestroyNotify destroy_notify = nullptr;
        if (trampoline) {
            /* Adding another reference and a DestroyNotify that unsets it */
            g_closure_ref(trampoline);
            destroy_notify = [](void* data) {
                g_assert(data);
                g_closure_unref(static_cast<GClosure*>(data));
            };
        }
        gjs_arg_set(&state->in_cvalue(destroy_pos), destroy_notify);
    }
    if (self->has_callback_closure()) {
        uint8_t closure_pos = self->contents.callback.closure_pos;
        gjs_arg_set(&state->in_cvalue(closure_pos), trampoline);
    }

    GIScopeType scope = self->contents.callback.scope;
    bool keep_forever = scope == GI_SCOPE_TYPE_NOTIFIED &&
                        !self->has_callback_destroy();
    if (trampoline && (scope == GI_SCOPE_TYPE_ASYNC || keep_forever)) {
        // Add an extra reference that will be cleared when garbage collecting
        // async calls or never for notified callbacks without destroy
        g_closure_ref(trampoline);
    }
    gjs_arg_set(arg, closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_out_in(JSContext*, GjsArgumentCache* self,
                                       GjsFunctionCallState* state,
                                       GIArgument* arg, JS::HandleValue) {
    // Default value in case a broken C function doesn't fill in the pointer
    gjs_arg_unset<void*>(&state->out_cvalue(self->arg_pos));
    gjs_arg_set(arg, &gjs_arg_member<void*>(&state->out_cvalue(self->arg_pos)));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_caller_allocates_in(JSContext*, GjsArgumentCache* self,
                                            GjsFunctionCallState* state,
                                            GIArgument* arg, JS::HandleValue) {
    void* blob = g_malloc0(self->contents.caller_allocates_size);
    gjs_arg_set(arg, blob);
    gjs_arg_set(&state->out_cvalue(self->arg_pos), blob);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_null_in_in(JSContext* cx, GjsArgumentCache* self,
                                   GjsFunctionCallState*, GIArgument* arg,
                                   JS::HandleValue) {
    return self->handle_nullable(cx, arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_boolean_in_in(JSContext*, GjsArgumentCache*,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    gjs_arg_set(arg, JS::ToBoolean(value));
    return true;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION inline static bool gjs_arg_set_from_js_value(
    JSContext* cx, const JS::HandleValue& value, GArgument* arg,
    GjsArgumentCache* self) {
    bool out_of_range = false;

    if (!gjs_arg_set_from_js_value<T>(cx, value, arg, &out_of_range)) {
        if (out_of_range) {
            gjs_throw(cx, "Argument %s: value is out of range for %s",
                      self->arg_name, Gjs::static_type_name<T>());
        }

        return false;
    }

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "%s set to value %s (type %s)",
                      GjsAutoChar(gjs_argument_display_name(
                                      self->arg_name, GJS_ARGUMENT_ARGUMENT))
                          .get(),
                      std::to_string(gjs_arg_get<T>(arg)).c_str(),
                      Gjs::static_type_name<T>());

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_numeric_in_in(JSContext* cx, GjsArgumentCache* self,
                                      GjsFunctionCallState*, GIArgument* arg,
                                      JS::HandleValue value) {
    switch (self->contents.number.number_tag) {
        case GI_TYPE_TAG_INT8:
            return gjs_arg_set_from_js_value<int8_t>(cx, value, arg, self);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_set_from_js_value<uint8_t>(cx, value, arg, self);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_set_from_js_value<int16_t>(cx, value, arg, self);
        case GI_TYPE_TAG_UINT16:
            return gjs_arg_set_from_js_value<uint16_t>(cx, value, arg, self);
        case GI_TYPE_TAG_INT32:
            return gjs_arg_set_from_js_value<int32_t>(cx, value, arg, self);
        case GI_TYPE_TAG_DOUBLE:
            return gjs_arg_set_from_js_value<double>(cx, value, arg, self);
        case GI_TYPE_TAG_FLOAT:
            return gjs_arg_set_from_js_value<float>(cx, value, arg, self);
        case GI_TYPE_TAG_INT64:
            return gjs_arg_set_from_js_value<int64_t>(cx, value, arg, self);
        case GI_TYPE_TAG_UINT64:
            return gjs_arg_set_from_js_value<uint64_t>(cx, value, arg, self);
        case GI_TYPE_TAG_UINT32:
            return gjs_arg_set_from_js_value<uint32_t>(cx, value, arg, self);
        default:
            g_assert_not_reached();
    }
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
    if (!(flags & GjsArgumentFlags::MAY_BE_NULL))
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

    if (self->flags & GjsArgumentFlags::FILENAME) {
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
    if (!Gjs::js_value_to_c(cx, value, &number))
        return false;

    // Unpack the values from their uint32_t bitfield. See note in
    // gjs_arg_cache_build_enum_bounds().
    int64_t min, max;
    if (self->flags & GjsArgumentFlags::UNSIGNED) {
        min = self->contents.enum_type.enum_min;
        max = self->contents.enum_type.enum_max;
    } else {
        min = static_cast<int32_t>(self->contents.enum_type.enum_min);
        max = static_cast<int32_t>(self->contents.enum_type.enum_max);
    }

    if (number > max || number < min) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for enum argument %s",
                  number, self->arg_name);
        return false;
    }

    if (self->flags & GjsArgumentFlags::UNSIGNED)
        gjs_arg_set<unsigned, GI_TYPE_TAG_INTERFACE>(arg, number);
    else
        gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(arg, number);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_flags_in_in(JSContext* cx, GjsArgumentCache* self,
                                    GjsFunctionCallState*, GIArgument* arg,
                                    JS::HandleValue value) {
    int64_t number;
    if (!Gjs::js_value_to_c(cx, value, &number))
        return false;

    if ((uint64_t(number) & self->contents.flags_mask) != uint64_t(number)) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for flags argument %s",
                  number, self->arg_name);
        return false;
    }

    // We cast to unsigned because that's what makes sense, but then we
    // put it in the v_int slot because that's what we use to unmarshal
    // flags types at the moment.
    gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(arg, static_cast<unsigned>(number));
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
        self->transfer, self->flags, arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gvalue_in_in(JSContext* cx, GjsArgumentCache*,
                                     GjsFunctionCallState* state,
                                     GIArgument* arg, JS::HandleValue value) {
    if (value.isObject()) {
        JS::RootedObject obj(cx, &value.toObject());
        GType gtype;

        if (!gjs_gtype_get_actual_gtype(cx, obj, &gtype))
            return false;

        if (gtype == G_TYPE_VALUE) {
            gjs_arg_set(arg, BoxedBase::to_c_ptr<GValue>(cx, obj));
            state->ignore_release.insert(arg);
            return true;
        }
    }

    Gjs::AutoGValue gvalue;

    if (!gjs_value_to_g_value(cx, value, &gvalue))
        return false;

    gjs_arg_set(arg, g_boxed_copy(G_TYPE_VALUE, &gvalue));

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
    GClosure* closure = Gjs::Closure::create_marshaled(cx, func, "boxed");
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
static bool gjs_marshal_interface_in_in(JSContext* cx, GjsArgumentCache* self,
                                        GjsFunctionCallState*, GIArgument* arg,
                                        JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());

    // Could be a GObject interface that's missing a prerequisite,
    // or could be a fundamental
    if (ObjectBase::typecheck(cx, object, nullptr, gtype,
                              GjsTypecheckNoThrow())) {
        return ObjectBase::transfer_to_gi_argument(
                        cx, object, arg, GI_DIRECTION_IN, self->transfer, gtype);
    }

    // If this typecheck fails, then it's neither an object nor a
    // fundamental
    return FundamentalBase::transfer_to_gi_argument(
            cx, object, arg, GI_DIRECTION_IN, self->transfer, gtype);
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
static bool gjs_marshal_fundamental_in_in(JSContext* cx, GjsArgumentCache* self,
                                          GjsFunctionCallState*,
                                          GIArgument* arg,
                                          JS::HandleValue value) {
    if (value.isNull())
        return self->handle_nullable(cx, arg);

    GType gtype = self->contents.object.gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!value.isObject())
        return report_gtype_mismatch(cx, self->arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return FundamentalBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, self->transfer, gtype);
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
    uint8_t length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &state->out_cvalue(length_pos);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    return gjs_value_from_explicit_array(cx, value, &self->type_info, arg,
                                         length);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_skipped_release(JSContext*, GjsArgumentCache*,
                                        GjsFunctionCallState*,
                                        GIArgument* in_arg [[maybe_unused]],
                                        GIArgument* out_arg [[maybe_unused]]) {
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_in_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg, GIArgument* out_arg [[maybe_unused]]) {
    GITransfer transfer =
        state->call_completed() ? self->transfer : GI_TRANSFER_NOTHING;
    return gjs_g_argument_release_in_arg(cx, transfer, &self->type_info,
                                         in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_generic_out_release(JSContext* cx,
                                            GjsArgumentCache* self,
                                            GjsFunctionCallState*,
                                            GIArgument* in_arg [[maybe_unused]],
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

    GIArgument* original_out_arg = &state->inout_original_cvalue(self->arg_pos);
    if (!gjs_g_argument_release_in_arg(cx, GI_TRANSFER_NOTHING,
                                       &self->type_info, original_out_arg))
        return false;

    return gjs_marshal_generic_out_release(cx, self, state, in_arg, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_out_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg [[maybe_unused]], GIArgument* out_arg) {
    uint8_t length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &state->out_cvalue(length_pos);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_in_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg, GIArgument* out_arg [[maybe_unused]]) {
    uint8_t length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &state->in_cvalue(length_pos);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    GITransfer transfer =
        state->call_completed() ? self->transfer : GI_TRANSFER_NOTHING;

    return gjs_g_argument_release_in_array(cx, transfer, &self->type_info,
                                           length, in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_explicit_array_inout_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg [[maybe_unused]], GIArgument* out_arg) {
    uint8_t length_pos = self->contents.array.length_pos;
    GIArgument* length_arg = &state->in_cvalue(length_pos);
    GITypeTag length_tag = self->contents.array.length_tag;
    size_t length = gjs_g_argument_get_array_length(length_tag, length_arg);

    // For inout, transfer refers to what we get back from the function; for
    // the temporary C value we allocated, clearly we're responsible for
    // freeing it.

    GIArgument* original_out_arg = &state->inout_original_cvalue(self->arg_pos);
    if (gjs_arg_get<void*>(original_out_arg) != gjs_arg_get<void*>(out_arg) &&
        !gjs_g_argument_release_in_array(cx, GI_TRANSFER_NOTHING,
                                         &self->type_info, length,
                                         original_out_arg))
        return false;

    return gjs_g_argument_release_out_array(cx, self->transfer,
                                            &self->type_info, length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_caller_allocates_release(JSContext*, GjsArgumentCache*,
                                                 GjsFunctionCallState*,
                                                 GIArgument* in_arg,
                                                 GIArgument* out_arg
                                                 [[maybe_unused]]) {
    g_free(gjs_arg_steal<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_callback_release(JSContext*, GjsArgumentCache*,
                                         GjsFunctionCallState*,
                                         GIArgument* in_arg,
                                         GIArgument* out_arg [[maybe_unused]]) {
    auto* closure = gjs_arg_get<ffi_closure*>(in_arg);
    if (!closure)
        return true;

    g_closure_unref(static_cast<GClosure*>(closure->user_data));
    // CallbackTrampolines are refcounted because for notified/async closures
    // it is possible to destroy it while in call, and therefore we cannot
    // check its scope at this point
    gjs_arg_unset<void*>(in_arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_string_in_release(JSContext*, GjsArgumentCache*,
                                          GjsFunctionCallState*,
                                          GIArgument* in_arg,
                                          GIArgument* out_arg
                                          [[maybe_unused]]) {
    g_free(gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_foreign_in_release(
    JSContext* cx, GjsArgumentCache* self, GjsFunctionCallState* state,
    GIArgument* in_arg, GIArgument* out_arg [[maybe_unused]]) {
    bool ok = true;

    GITransfer transfer =
        state->call_completed() ? self->transfer : GI_TRANSFER_NOTHING;

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
                                         GIArgument* out_arg [[maybe_unused]]) {
    GType gtype = self->contents.object.gtype;

    g_assert(g_type_is_a(gtype, G_TYPE_BOXED));

    if (!gjs_arg_get<void*>(in_arg))
        return true;

    g_boxed_free(gtype, gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_marshal_gvalue_in_maybe_release(JSContext* cx,
                                                GjsArgumentCache* self,
                                                GjsFunctionCallState* state,
                                                GIArgument* in_arg,
                                                GIArgument* out_arg) {
    if (state->ignore_release.erase(in_arg))
        return true;

    return gjs_marshal_boxed_in_release(cx, self, state, in_arg, out_arg);
}

static void gjs_arg_cache_interface_free(GjsArgumentCache* self) {
    g_clear_pointer(&self->contents.object.info, g_base_info_unref);
}

static const GjsArgumentMarshallers skip_all_marshallers = {
    gjs_marshal_skipped_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

// .in is ignored for the return value
static const GjsArgumentMarshallers return_value_marshallers = {
    nullptr,  // no in
    gjs_marshal_generic_out_out,  // out
    gjs_marshal_generic_out_release,  // release
};

static const GjsArgumentMarshallers return_array_marshallers = {
    gjs_marshal_generic_out_in,  // in
    gjs_marshal_explicit_array_out_out,  // out
    gjs_marshal_explicit_array_out_release,  // release
};

static const GjsArgumentMarshallers array_length_out_marshallers = {
    gjs_marshal_generic_out_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers fallback_in_marshallers = {
    gjs_marshal_generic_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_generic_in_release,  // release
};

static const GjsArgumentMarshallers fallback_interface_in_marshallers = {
    gjs_marshal_generic_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_generic_in_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers fallback_inout_marshallers = {
    gjs_marshal_generic_inout_in,  // in
    gjs_marshal_generic_out_out,  // out
    gjs_marshal_generic_inout_release,  // release
};

static const GjsArgumentMarshallers fallback_out_marshallers = {
    gjs_marshal_generic_out_in,  // in
    gjs_marshal_generic_out_out,  // out
    gjs_marshal_generic_out_release,  // release
};

static const GjsArgumentMarshallers invalid_in_marshallers = {
    gjs_marshal_not_introspectable_in,  // in, always throws
    gjs_marshal_skipped_out,            // out
    gjs_marshal_skipped_release,        // release
};

static const GjsArgumentMarshallers enum_in_marshallers = {
    gjs_marshal_enum_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers flags_in_marshallers = {
    gjs_marshal_flags_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers foreign_struct_in_marshallers = {
    gjs_marshal_foreign_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_foreign_in_release,  // release
};

static const GjsArgumentMarshallers foreign_struct_instance_in_marshallers = {
    gjs_marshal_foreign_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers gvalue_in_marshallers = {
    gjs_marshal_gvalue_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers gvalue_in_transfer_none_marshallers = {
    gjs_marshal_gvalue_in_in,             // in
    gjs_marshal_skipped_out,              // out
    gjs_marshal_gvalue_in_maybe_release,  // release
    gjs_arg_cache_interface_free,         // free
};

static const GjsArgumentMarshallers gclosure_in_marshallers = {
    gjs_marshal_gclosure_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers gclosure_in_transfer_none_marshallers = {
    gjs_marshal_gclosure_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_boxed_in_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers gbytes_in_marshallers = {
    gjs_marshal_gbytes_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers gbytes_in_transfer_none_marshallers = {
    gjs_marshal_gbytes_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_boxed_in_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers object_in_marshallers = {
    gjs_marshal_object_in_in,  // in
    gjs_marshal_skipped_out,  // out
    // This is a smart marshaller, no release needed
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers interface_in_marshallers = {
    gjs_marshal_interface_in_in,  // in
    gjs_marshal_skipped_out,  // out
    // This is a smart marshaller, no release needed
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers fundamental_in_marshallers = {
    gjs_marshal_fundamental_in_in,  // in
    gjs_marshal_skipped_out,        // out
    // This is a smart marshaller, no release needed
    gjs_marshal_skipped_release,   // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers union_in_marshallers = {
    gjs_marshal_union_in_in,  // in
    gjs_marshal_skipped_out,  // out
    // This is a smart marshaller, no release needed
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers boxed_in_marshallers = {
    gjs_marshal_boxed_in_in,  // in
    gjs_marshal_skipped_out,  // out
    // This is a smart marshaller, no release needed
    gjs_marshal_skipped_release,  // release
    gjs_arg_cache_interface_free,  // free
};

static const GjsArgumentMarshallers null_in_marshallers = {
    gjs_marshal_null_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers boolean_in_marshallers = {
    gjs_marshal_boolean_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers numeric_in_marshallers = {
    gjs_marshal_numeric_in_in,    // in
    gjs_marshal_skipped_out,      // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers unichar_in_marshallers = {
    gjs_marshal_unichar_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers gtype_in_marshallers = {
    gjs_marshal_gtype_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers string_in_marshallers = {
    gjs_marshal_string_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers string_in_transfer_none_marshallers = {
    gjs_marshal_string_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_string_in_release,  // release
};

// .out is ignored for the instance parameter
static const GjsArgumentMarshallers gtype_struct_instance_in_marshallers = {
    gjs_marshal_gtype_struct_instance_in,  // in
    nullptr,  // no out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers param_instance_in_marshallers = {
    gjs_marshal_param_instance_in,  // in
    nullptr,  // no out
    gjs_marshal_skipped_release,  // release
};

static const GjsArgumentMarshallers callback_in_marshallers = {
    gjs_marshal_callback_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_callback_release,  // release
};

static const GjsArgumentMarshallers c_array_in_marshallers = {
    gjs_marshal_explicit_array_in_in,  // in
    gjs_marshal_skipped_out,  // out
    gjs_marshal_explicit_array_in_release,  // release
};

static const GjsArgumentMarshallers c_array_inout_marshallers = {
    gjs_marshal_explicit_array_inout_in,  // in
    gjs_marshal_explicit_array_out_out,  // out
    gjs_marshal_explicit_array_inout_release,  // release
};

static const GjsArgumentMarshallers c_array_out_marshallers = {
    gjs_marshal_generic_out_in,  // in
    gjs_marshal_explicit_array_out_out,  // out
    gjs_marshal_explicit_array_out_release,  // release
};

static const GjsArgumentMarshallers caller_allocates_out_marshallers = {
    gjs_marshal_caller_allocates_in,  // in
    gjs_marshal_generic_out_out,  // out
    gjs_marshal_caller_allocates_release,  // release
};

static inline void gjs_arg_cache_set_skip_all(GjsArgumentCache* self) {
    self->marshallers = &skip_all_marshallers;
    self->flags =
        static_cast<GjsArgumentFlags>(GjsArgumentFlags::SKIP_IN | GjsArgumentFlags::SKIP_OUT);
}

void gjs_arg_cache_build_return(GjsArgumentCache* self,
                                GjsArgumentCache* arguments,
                                GICallableInfo* callable,
                                bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    g_callable_info_load_return_type(callable, &self->type_info);

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_VOID &&
        !g_type_info_is_pointer(&self->type_info)) {
        *inc_counter_out = false;
        gjs_arg_cache_set_skip_all(self);
        return;
    }

    *inc_counter_out = true;
    self->set_return_value();
    self->transfer = g_callable_info_get_caller_owns(callable);

    if (g_type_info_get_tag(&self->type_info) == GI_TYPE_TAG_ARRAY) {
        int length_pos = g_type_info_get_array_length(&self->type_info);
        if (length_pos >= 0) {
            gjs_arg_cache_set_skip_all(&arguments[length_pos]);

            // Even if we skip the length argument most of the time, we need to
            // do some basic initialization here.
            arguments[length_pos].set_arg_pos(length_pos);
            arguments[length_pos].marshallers = &array_length_out_marshallers;

            self->marshallers = &return_array_marshallers;

            self->set_array_length_pos(length_pos);

            GIArgInfo length_arg;
            g_callable_info_load_arg(callable, length_pos, &length_arg);
            GITypeInfo length_type;
            g_arg_info_load_type(&length_arg, &length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&length_type);

            return;
        }
    }

    // marshal_in is ignored for the return value, but skip_in is not (it is
    // used in the failure release path)
    self->flags = static_cast<GjsArgumentFlags>(self->flags | GjsArgumentFlags::SKIP_IN);
    self->marshallers = &return_value_marshallers;
}

static void gjs_arg_cache_build_enum_bounds(GjsArgumentCache* self,
                                            GIEnumInfo* enum_info) {
    int64_t min = std::numeric_limits<int64_t>::max();
    int64_t max = std::numeric_limits<int64_t>::min();
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);

        if (value > max)
            max = value;
        if (value < min)
            min = value;
    }

    // From the docs for g_value_info_get_value(): "This will always be
    // representable as a 32-bit signed or unsigned value. The use of gint64 as
    // the return type is to allow both."
    // We stuff them both into unsigned 32-bit fields, and use a flag to tell
    // whether we have to compare them as signed.
    self->contents.enum_type.enum_min = static_cast<uint32_t>(min);
    self->contents.enum_type.enum_max = static_cast<uint32_t>(max);

    if (min >= 0 && max > std::numeric_limits<int32_t>::max())
        self->flags = static_cast<GjsArgumentFlags>(self->flags | GjsArgumentFlags::UNSIGNED);
    else
        self->flags = static_cast<GjsArgumentFlags>(self->flags & ~GjsArgumentFlags::UNSIGNED);
}

static void gjs_arg_cache_build_flags_mask(GjsArgumentCache* self,
                                           GIEnumInfo* enum_info) {
    uint64_t mask = 0;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);
        // From the docs for g_value_info_get_value(): "This will always be
        // representable as a 32-bit signed or unsigned value. The use of
        // gint64 as the return type is to allow both."
        // We stuff both into an unsigned, int-sized field, matching the
        // internal representation of flags in GLib (which uses guint).
        mask |= static_cast<unsigned>(value);
    }

    self->contents.flags_mask = mask;
}

namespace arg_cache {
[[nodiscard]] static inline bool is_gdk_atom(GIBaseInfo* info) {
    return strcmp("Atom", g_base_info_get_name(info)) == 0 &&
           strcmp("Gdk", g_base_info_get_namespace(info)) == 0;
}
}  // namespace arg_cache

static void gjs_arg_cache_build_interface_in_arg(GjsArgumentCache* self,
                                                 GIBaseInfo* interface_info,
                                                 bool is_instance_param) {
    GIInfoType interface_type = g_base_info_get_type(interface_info);

    // We do some transfer magic later, so let's ensure we don't mess up.
    // Should not happen in practice.
    if (G_UNLIKELY(self->transfer == GI_TRANSFER_CONTAINER)) {
        self->marshallers = &invalid_in_marshallers;
        self->contents.reason = INTERFACE_TRANSFER_CONTAINER;
        return;
    }

    switch (interface_type) {
        case GI_INFO_TYPE_ENUM:
            gjs_arg_cache_build_enum_bounds(self, interface_info);
            self->marshallers = &enum_in_marshallers;
            return;

        case GI_INFO_TYPE_FLAGS:
            gjs_arg_cache_build_flags_mask(self, interface_info);
            self->marshallers = &flags_in_marshallers;
            return;

        case GI_INFO_TYPE_STRUCT:
            if (g_struct_info_is_foreign(interface_info)) {
                if (is_instance_param)
                    self->marshallers = &foreign_struct_instance_in_marshallers;
                else
                    self->marshallers = &foreign_struct_in_marshallers;
                return;
            }
            [[fallthrough]];
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_UNION: {
            GType gtype = g_registered_type_info_get_g_type(interface_info);

            if (!is_instance_param && interface_type == GI_INFO_TYPE_STRUCT &&
                gtype == G_TYPE_NONE &&
                !g_struct_info_is_gtype_struct(interface_info)) {
                // This covers cases such as GTypeInstance
                self->marshallers = &fallback_in_marshallers;
                return;
            }

            self->contents.object.gtype = gtype;
            self->contents.object.info = g_base_info_ref(interface_info);

            // Transfer handling is a bit complex here, because some of our _in
            // marshallers know not to copy stuff if we don't need to.

            if (gtype == G_TYPE_VALUE) {
                if (self->arg_pos == GjsArgumentCache::INSTANCE_PARAM)
                    self->marshallers = &boxed_in_marshallers;
                else if (self->transfer == GI_TRANSFER_NOTHING && !is_instance_param)
                    self->marshallers = &gvalue_in_transfer_none_marshallers;
                else
                    self->marshallers = &gvalue_in_marshallers;
                return;
            }

            if (arg_cache::is_gdk_atom(interface_info)) {
                // Fall back to the generic marshaller
                self->marshallers = &fallback_interface_in_marshallers;
                return;
            }

            if (gtype == G_TYPE_CLOSURE) {
                if (self->transfer == GI_TRANSFER_NOTHING && !is_instance_param)
                    self->marshallers = &gclosure_in_transfer_none_marshallers;
                else
                    self->marshallers = &gclosure_in_marshallers;
                return;
            }

            if (gtype == G_TYPE_BYTES) {
                if (self->transfer == GI_TRANSFER_NOTHING && !is_instance_param)
                    self->marshallers = &gbytes_in_transfer_none_marshallers;
                else
                    self->marshallers = &gbytes_in_marshallers;
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                self->marshallers = &object_in_marshallers;
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                // Fall back to the generic marshaller
                self->marshallers = &fallback_interface_in_marshallers;
                return;
            }

            if (interface_type == GI_INFO_TYPE_UNION) {
                if (gtype == G_TYPE_NONE) {
                    // Can't handle unions without a GType
                    self->marshallers = &invalid_in_marshallers;
                    self->contents.reason = UNREGISTERED_UNION;
                    return;
                }

                self->marshallers = &union_in_marshallers;
                return;
            }

            if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                self->marshallers = &fundamental_in_marshallers;
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                self->marshallers = &interface_in_marshallers;
                return;
            }

            // generic boxed type
            if (gtype == G_TYPE_NONE && self->transfer != GI_TRANSFER_NOTHING) {
                // Can't transfer ownership of a structure type not
                // registered as a boxed
                self->marshallers = &invalid_in_marshallers;
                self->contents.reason = UNREGISTERED_BOXED_WITH_TRANSFER;
            }

            self->marshallers = &boxed_in_marshallers;
            return;
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
            self->marshallers = &invalid_in_marshallers;
            self->contents.reason = UNSUPPORTED_TYPE;
    }
}

static void gjs_arg_cache_build_normal_in_arg(GjsArgumentCache* self,
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

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            self->marshallers = &null_in_marshallers;
            break;

        case GI_TYPE_TAG_BOOLEAN:
            self->marshallers = &boolean_in_marshallers;
            break;

        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
            self->marshallers = &numeric_in_marshallers;
            self->contents.number.number_tag = tag;
            break;

        case GI_TYPE_TAG_UNICHAR:
            self->marshallers = &unichar_in_marshallers;
            break;

        case GI_TYPE_TAG_GTYPE:
            self->marshallers = &gtype_in_marshallers;
            break;

        case GI_TYPE_TAG_FILENAME:
            if (self->transfer == GI_TRANSFER_NOTHING)
                self->marshallers = &string_in_transfer_none_marshallers;
            else
                self->marshallers = &string_in_marshallers;
            self->flags = static_cast<GjsArgumentFlags>(self->flags | GjsArgumentFlags::FILENAME);
            break;

        case GI_TYPE_TAG_UTF8:
            if (self->transfer == GI_TRANSFER_NOTHING)
                self->marshallers = &string_in_transfer_none_marshallers;
            else
                self->marshallers = &string_in_marshallers;
            self->flags = static_cast<GjsArgumentFlags>(self->flags & ~GjsArgumentFlags::FILENAME);
            break;

        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(&self->type_info);
            gjs_arg_cache_build_interface_in_arg(
                self, interface_info, /* is_instance_param = */ false);
            return;
        }

        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
        default:
            // FIXME: Falling back to the generic marshaller
            self->marshallers = &fallback_in_marshallers;
    }
}

void gjs_arg_cache_build_instance(GjsArgumentCache* self,
                                  GICallableInfo* callable) {
    GIBaseInfo* interface_info = g_base_info_get_container(callable);  // !owned

    self->set_instance_parameter();
    self->transfer = g_callable_info_get_instance_ownership_transfer(callable);

    // These cases could be covered by the generic marshaller, except that
    // there's no way to get GITypeInfo for a method's instance parameter.
    // Instead, special-case the arguments here that would otherwise go through
    // the generic marshaller.
    // See: https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/334
    GIInfoType info_type = g_base_info_get_type(interface_info);
    if (info_type == GI_INFO_TYPE_STRUCT &&
        g_struct_info_is_gtype_struct(interface_info)) {
        self->marshallers = &gtype_struct_instance_in_marshallers;
        return;
    }
    if (info_type == GI_INFO_TYPE_OBJECT) {
        GType gtype = g_registered_type_info_get_g_type(interface_info);

        if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            self->marshallers = &param_instance_in_marshallers;
            return;
        }
    }

    gjs_arg_cache_build_interface_in_arg(self, interface_info,
                                         /* is_instance_param = */ true);
}

void gjs_arg_cache_build_arg(GjsArgumentCache* self,
                             GjsArgumentCache* arguments, uint8_t gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    self->set_arg_pos(gi_index);
    self->arg_name = g_base_info_get_name(arg);
    g_arg_info_load_type(arg, &self->type_info);
    self->transfer = g_arg_info_get_ownership_transfer(arg);

    GjsArgumentFlags flags = GjsArgumentFlags::NONE;
    if (g_arg_info_may_be_null(arg))
        flags |= GjsArgumentFlags::MAY_BE_NULL;
    if (g_arg_info_is_caller_allocates(arg))
        flags |= GjsArgumentFlags::CALLER_ALLOCATES;

    if (direction == GI_DIRECTION_IN)
        flags |= GjsArgumentFlags::SKIP_OUT;
    else if (direction == GI_DIRECTION_OUT)
        flags |= GjsArgumentFlags::SKIP_IN;
    *inc_counter_out = true;

    self->flags = flags;

    GITypeTag type_tag = g_type_info_get_tag(&self->type_info);
    if (direction == GI_DIRECTION_OUT &&
        (flags & GjsArgumentFlags::CALLER_ALLOCATES)) {
        if (type_tag != GI_TYPE_TAG_INTERFACE) {
            self->marshallers = &invalid_in_marshallers;
            self->contents.reason = OUT_CALLER_ALLOCATES_NON_STRUCT;
            return;
        }

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
            self->marshallers = &invalid_in_marshallers;
            self->contents.reason = OUT_CALLER_ALLOCATES_NON_STRUCT;
            return;
        }

        self->marshallers = &caller_allocates_out_marshallers;
        self->contents.caller_allocates_size = size;

        return;
    }

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info =
            g_type_info_get_interface(&self->type_info);
        if (interface_info.type() == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                // Can't do callbacks for out or inout
                self->marshallers = &invalid_in_marshallers;
                self->contents.reason = CALLBACK_OUT;
                return;
            }

            if (strcmp(interface_info.name(), "DestroyNotify") == 0 &&
                strcmp(interface_info.ns(), "GLib") == 0) {
                // We don't know (yet) what to do with GDestroyNotify appearing
                // before a callback. If the callback comes later in the
                // argument list, then the invalid marshallers will be
                // overwritten with the 'skipped' one. If no callback follows,
                // then this is probably an unsupported function, so the
                // function invocation code will check this and throw.
                self->marshallers = &invalid_in_marshallers;
                self->contents.reason = DESTROY_NOTIFY_NO_CALLBACK;
                *inc_counter_out = false;
            } else {
                self->marshallers = &callback_in_marshallers;

                int destroy_pos = g_arg_info_get_destroy(arg);
                int closure_pos = g_arg_info_get_closure(arg);

                if (destroy_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[destroy_pos]);

                if (closure_pos >= 0)
                    gjs_arg_cache_set_skip_all(&arguments[closure_pos]);

                if (destroy_pos >= 0 && closure_pos < 0) {
                    self->marshallers = &invalid_in_marshallers;
                    self->contents.reason = DESTROY_NOTIFY_NO_USER_DATA;
                    return;
                }

                self->contents.callback.scope = g_arg_info_get_scope(arg);
                self->set_callback_destroy_pos(destroy_pos);
                self->set_callback_closure_pos(closure_pos);
            }

            return;
        }
    }

    if (type_tag == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_type(&self->type_info) == GI_ARRAY_TYPE_C) {
        int length_pos = g_type_info_get_array_length(&self->type_info);

        if (length_pos >= 0) {
            GjsArgumentCache *cached_length = &arguments[length_pos];
            bool skip_length =
                !(cached_length->skip_in() && cached_length->skip_out());
            if (skip_length)
                gjs_arg_cache_set_skip_all(cached_length);

            if (direction == GI_DIRECTION_IN) {
                self->marshallers = &c_array_in_marshallers;
            } else if (direction == GI_DIRECTION_INOUT) {
                self->marshallers = &c_array_inout_marshallers;
            } else {
                // Even if we skip the length argument most of time, we need to
                // do some basic initialization here.
                arguments[length_pos].set_arg_pos(length_pos);
                arguments[length_pos].marshallers =
                    &array_length_out_marshallers;

                self->marshallers = &c_array_out_marshallers;
            }

            self->set_array_length_pos(length_pos);

            GIArgInfo length_arg;
            g_callable_info_load_arg(callable, length_pos, &length_arg);
            GITypeInfo length_type;
            g_arg_info_load_type(&length_arg, &length_type);
            self->contents.array.length_tag = g_type_info_get_tag(&length_type);

            if (length_pos < gi_index && skip_length) {
                // we already collected length_pos, remove it
                *inc_counter_out = false;
            }

            return;
        }
    }

    if (direction == GI_DIRECTION_IN)
        gjs_arg_cache_build_normal_in_arg(self, type_tag);
    else if (direction == GI_DIRECTION_INOUT)
        self->marshallers = &fallback_inout_marshallers;
    else
        self->marshallers = &fallback_out_marshallers;
}
