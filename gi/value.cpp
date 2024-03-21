/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stddef.h>  // for NULL
#include <stdint.h>

#include <sstream>
#include <string>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/BigInt.h>
#include <js/CharacterEncoding.h>
#include <js/Conversions.h>
#include <js/Exception.h>
#include <js/GCVector.h>  // for RootedVector
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/experimental/TypedData.h>
#include <jsapi.h>  // for InformalValueTypeName, JS_Get...
#include <jsfriendapi.h>  // for JS_GetObjectFunction

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/foreign.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/objectbox.h"
#include "util/log.h"

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_value_from_g_value_internal(JSContext*, JS::MutableHandleValue,
                                            const GValue*, bool no_copy = false,
                                            bool is_introspected_signal = false,
                                            GIArgInfo* = nullptr,
                                            GITypeInfo* = nullptr);

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_arg_set_from_gvalue(JSContext* cx, GIArgument* arg,
                                    const GValue* value) {
    switch (G_VALUE_TYPE(value)) {
        case G_TYPE_CHAR:
            gjs_arg_set(arg, g_value_get_schar(value));
            return true;
        case G_TYPE_UCHAR:
            gjs_arg_set(arg, g_value_get_uchar(value));
            return true;
        case G_TYPE_BOOLEAN:
            gjs_arg_set(arg, g_value_get_boolean(value));
            return true;
        case G_TYPE_INT:
            gjs_arg_set(arg, g_value_get_int(value));
            return true;
        case G_TYPE_UINT:
            gjs_arg_set(arg, g_value_get_uint(value));
            return true;
        case G_TYPE_LONG:
            gjs_arg_set<long, GJS_TYPE_TAG_LONG>(  // NOLINT(runtime/int)
                arg, g_value_get_long(value));
            return true;
        case G_TYPE_ULONG:
            gjs_arg_set<unsigned long,  // NOLINT(runtime/int)
                        GJS_TYPE_TAG_LONG>(arg, g_value_get_ulong(value));
            return true;
        case G_TYPE_INT64:
            gjs_arg_set(arg, int64_t{g_value_get_int64(value)});
            return true;
        case G_TYPE_UINT64:
            gjs_arg_set(arg, uint64_t{g_value_get_uint64(value)});
            return true;
        case G_TYPE_FLOAT:
            gjs_arg_set(arg, g_value_get_float(value));
            return true;
        case G_TYPE_DOUBLE:
            gjs_arg_set(arg, g_value_get_double(value));
            return true;
        case G_TYPE_STRING:
            gjs_arg_set(arg, g_value_get_string(value));
            return true;
        case G_TYPE_POINTER:
            gjs_arg_set(arg, g_value_get_pointer(value));
            return true;
        case G_TYPE_VARIANT:
            gjs_arg_set(arg, g_value_get_variant(value));
            return true;
        default: {
            if (g_value_fits_pointer(value)) {
                gjs_arg_set(arg, g_value_peek_pointer(value));
                return true;
            }

            GType gtype = G_VALUE_TYPE(value);

            if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
                gjs_arg_set(arg, g_value_get_flags(value));
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_ENUM)) {
                gjs_arg_set(arg, g_value_get_enum(value));
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_GTYPE)) {
                gjs_arg_set<GType, GI_TYPE_TAG_GTYPE>(arg,
                                                      g_value_get_gtype(value));
                return true;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                gjs_arg_set(arg, g_value_get_param(value));
                return true;
            }
        }
    }

    gjs_throw(cx, "No know GArgument conversion for %s",
              G_VALUE_TYPE_NAME(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool maybe_release_signal_value(JSContext* cx,
                                       GjsAutoArgInfo const& arg_info,
                                       GITypeInfo* type_info,
                                       const GValue* gvalue,
                                       GITransfer transfer) {
    if (transfer == GI_TRANSFER_NOTHING)
        return true;

    GIArgument arg;
    if (!gjs_arg_set_from_gvalue(cx, &arg, gvalue))
        return false;

    if (!gjs_gi_argument_release(cx, transfer, type_info,
                                 GjsArgumentFlags::ARG_OUT, &arg)) {
        gjs_throw(cx, "Cannot release argument %s value, we're gonna leak!",
                  arg_info.name());
        return false;
    }

    return true;
}

/*
 * Gets signal introspection info about closure, or NULL if not found. Currently
 * only works for signals on introspected GObjects, not signals on GJS-defined
 * GObjects nor standalone closures. The return value must be unreffed.
 */
[[nodiscard]] static GjsAutoSignalInfo get_signal_info_if_available(
    GSignalQuery* signal_query) {
    if (!signal_query->itype)
        return nullptr;

    GjsAutoBaseInfo obj =
        g_irepository_find_by_gtype(nullptr, signal_query->itype);
    if (!obj)
        return nullptr;

    GIInfoType info_type = obj.type();
    if (info_type == GI_INFO_TYPE_OBJECT)
        return g_object_info_find_signal(obj, signal_query->signal_name);
    else if (info_type == GI_INFO_TYPE_INTERFACE)
        return g_interface_info_find_signal(obj, signal_query->signal_name);

    return nullptr;
}

/*
 * Fill in value_p with a JS array, converted from a C array stored as a pointer
 * in array_value, with its length stored in array_length_value.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool gjs_value_from_array_and_length_values(
    JSContext* context, JS::MutableHandleValue value_p,
    GITypeInfo* array_type_info, const GValue* array_value,
    GIArgInfo* array_length_arg_info, GITypeInfo* array_length_type_info,
    const GValue* array_length_value, bool no_copy,
    bool is_introspected_signal) {
    JS::RootedValue array_length(context);

    g_assert(G_VALUE_HOLDS_POINTER(array_value));
    g_assert(G_VALUE_HOLDS_INT(array_length_value));

    if (!gjs_value_from_g_value_internal(
            context, &array_length, array_length_value, no_copy, is_introspected_signal,
            array_length_arg_info, array_length_type_info))
        return false;

    GIArgument array_arg;
    gjs_arg_set(&array_arg, g_value_get_pointer(array_value));

    return gjs_value_from_explicit_array(
        context, value_p, array_type_info,
        no_copy ? GI_TRANSFER_NOTHING : GI_TRANSFER_EVERYTHING, &array_arg,
        array_length.toInt32());
}

// FIXME(3v1n0): Move into closure.cpp one day...
void Gjs::Closure::marshal(GValue* return_value, unsigned n_param_values,
                           const GValue* param_values, void* invocation_hint,
                           void* marshal_data) {
    JSContext *context;
    unsigned i;
    GSignalQuery signal_query = { 0, };

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE, "Marshal closure %p", this);

    if (!is_valid()) {
        /* We were destroyed; become a no-op */
        return;
    }

    context = m_cx;
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    if (G_UNLIKELY(gjs->sweeping())) {
        GSignalInvocationHint *hint = (GSignalInvocationHint*) invocation_hint;
        std::ostringstream message;

        message << "Attempting to call back into JSAPI during the sweeping "
                   "phase of GC. This is most likely caused by not destroying "
                   "a Clutter actor or Gtk+ widget with ::destroy signals "
                   "connected, but can also be caused by using the destroy(), "
                   "dispose(), or remove() vfuncs. Because it would crash the "
                   "application, it has been blocked and the JS callback not "
                   "invoked.";
        if (hint) {
            gpointer instance;
            g_signal_query(hint->signal_id, &signal_query);

            instance = g_value_peek_pointer(&param_values[0]);
            message << "\nThe offending signal was " << signal_query.signal_name
                    << " on " << g_type_name(G_TYPE_FROM_INSTANCE(instance))
                    << " " << instance << ".";
        }
        message << "\n" << gjs_dumpstack_string();
        g_critical("%s", message.str().c_str());
        return;
    }

    JSAutoRealm ar(context, callable());

    if (marshal_data) {
        /* we are used for a signal handler */
        guint signal_id;

        signal_id = GPOINTER_TO_UINT(marshal_data);

        g_signal_query(signal_id, &signal_query);

        if (!signal_query.signal_id) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called on invalid signal");
            return;
        }

        if (signal_query.n_params + 1 != n_param_values) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called with wrong number of parameters");
            return;
        }
    }

    /* Check if any parameters, such as array lengths, need to be eliminated
     * before we invoke the closure.
     */
    struct ArgumentDetails {
        int array_len_index_for = -1;
        bool skip = false;
        GITypeInfo type_info;
        GjsAutoArgInfo arg_info;
    };
    std::vector<ArgumentDetails> args_details(n_param_values);
    bool needs_cleanup = false;

    GjsAutoSignalInfo signal_info = get_signal_info_if_available(&signal_query);
    if (signal_info) {
        /* Start at argument 1, skip the instance parameter */
        for (i = 1; i < n_param_values; ++i) {
            int array_len_pos;

            ArgumentDetails& arg_details = args_details[i];
            arg_details.arg_info = g_callable_info_get_arg(signal_info, i - 1);
            g_arg_info_load_type(arg_details.arg_info, &arg_details.type_info);

            array_len_pos =
                g_type_info_get_array_length(&arg_details.type_info);
            if (array_len_pos != -1) {
                args_details[array_len_pos + 1].skip = true;
                arg_details.array_len_index_for = array_len_pos + 1;
            }

            if (!needs_cleanup &&
                g_arg_info_get_ownership_transfer(arg_details.arg_info) !=
                    GI_TRANSFER_NOTHING)
                needs_cleanup = true;
        }
    }

    JS::RootedValueVector argv(context);
    /* May end up being less */
    if (!argv.reserve(n_param_values))
        g_error("Unable to reserve space");
    JS::RootedValue argv_to_append(context);
    bool is_introspected_signal = !!signal_info;
    for (i = 0; i < n_param_values; ++i) {
        const GValue* gval = &param_values[i];
        ArgumentDetails& arg_details = args_details[i];
        bool no_copy;
        bool res;

        if (arg_details.skip)
            continue;

        no_copy = false;

        if (i >= 1 && signal_query.signal_id) {
            no_copy = (signal_query.param_types[i - 1] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0;
        }

        if (arg_details.array_len_index_for != -1) {
            const GValue* array_len_gval =
                &param_values[arg_details.array_len_index_for];
            ArgumentDetails& array_len_details =
                args_details[arg_details.array_len_index_for];
            res = gjs_value_from_array_and_length_values(
                context, &argv_to_append, &arg_details.type_info, gval,
                array_len_details.arg_info, &array_len_details.type_info,
                array_len_gval, no_copy, is_introspected_signal);
        } else {
            res = gjs_value_from_g_value_internal(
                context, &argv_to_append, gval, no_copy, is_introspected_signal,
                arg_details.arg_info, &arg_details.type_info);
        }

        if (!res) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert arg %d in order to invoke closure",
                      i);
            gjs_log_exception(context);
            return;
        }

        argv.infallibleAppend(argv_to_append);
    }

    JS::RootedValue rval(context);

    if (!invoke(nullptr, argv, &rval)) {
        if (JS_IsExceptionPending(context)) {
            gjs_log_exception_uncaught(context);
        } else {
            // "Uncatchable" exception thrown, we have to exit. This
            // matches the closure exit handling in function.cpp
            uint8_t code;
            if (gjs->should_exit(&code))
                gjs->exit_immediately(code);

            // Some other uncatchable exception, e.g. out of memory
            JSFunction* fn = JS_GetObjectFunction(callable());
            std::string descr =
                fn ? "function " + gjs_debug_string(JS_GetFunctionDisplayId(fn))
                   : "callable object " + gjs_debug_object(callable());
            g_error("Call to %s terminated with uncatchable exception",
                    descr.c_str());
        }
    }

    if (needs_cleanup) {
        for (i = 0; i < n_param_values; ++i) {
            ArgumentDetails& arg_details = args_details[i];
            if (!arg_details.arg_info)
                continue;

            GITransfer transfer =
                g_arg_info_get_ownership_transfer(arg_details.arg_info);

            if (transfer == GI_TRANSFER_NOTHING)
                continue;

            if (!maybe_release_signal_value(context, arg_details.arg_info,
                                            &arg_details.type_info,
                                            &param_values[i], transfer)) {
                gjs_log_exception(context);
                return;
            }
        }
    }

    // null return_value means the closure wasn't expected to return a value.
    // Discard the JS function's return value in that case.
    if (return_value != NULL) {
        if (rval.isUndefined()) {
            // Either an exception was thrown and logged, or the JS function
            // returned undefined. Leave the GValue uninitialized.
            // FIXME: not sure what happens on the other side with an
            // uninitialized GValue!
            return;
        }

        if (!gjs_value_to_g_value(context, rval, return_value)) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert return value when invoking closure");
            gjs_log_exception(context);
            return;
        }
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_value_guess_g_type(JSContext* context, JS::Value value,
                                   GType* gtype_out) {
    g_assert(gtype_out && "Invalid return location");

    if (value.isNull()) {
        *gtype_out = G_TYPE_POINTER;
        return true;
    }
    if (value.isString()) {
        *gtype_out = G_TYPE_STRING;
        return true;
    }
    if (value.isInt32()) {
        *gtype_out = G_TYPE_INT;
        return true;
    }
    if (value.isDouble()) {
        *gtype_out = G_TYPE_DOUBLE;
        return true;
    }
    if (value.isBoolean()) {
        *gtype_out = G_TYPE_BOOLEAN;
        return true;
    }
    if (value.isBigInt()) {
        // Assume that if the value is negative or within the int64_t limit,
        // then we're handling a signed integer, otherwise unsigned.
        int64_t ignored;
        if (JS::BigIntIsNegative(value.toBigInt()) ||
            JS::BigIntFits(value.toBigInt(), &ignored))
            *gtype_out = G_TYPE_INT64;
        else
            *gtype_out = G_TYPE_UINT64;
        return true;
    }
    if (value.isObject()) {
        JS::RootedObject obj(context, &value.toObject());
        return gjs_gtype_get_actual_gtype(context, obj, gtype_out);
    }

    *gtype_out = G_TYPE_INVALID;
    return true;
}

static bool throw_expect_type(JSContext* cx, JS::HandleValue value,
                              const char* expected_type, GType gtype = 0,
                              bool out_of_range = false) {
    JS::UniqueChars val_str;
    out_of_range = (out_of_range && value.isNumeric());

    if (out_of_range) {
        JS::RootedString str(cx, JS::ToString(cx, value));
        if (str)
            val_str = JS_EncodeStringToUTF8(cx, str);
    }

    gjs_throw(cx, "Wrong type %s; %s%s%s expected%s%s",
              JS::InformalValueTypeName(value), expected_type, gtype ? " " : "",
              gtype ? g_type_name(gtype) : "",
              out_of_range ? ". But it's out of range: " : "",
              out_of_range ? val_str.get() : "");
    return false;  /* for convenience */
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_value_to_g_value_internal(JSContext      *context,
                              JS::HandleValue value,
                              GValue         *gvalue,
                              bool            no_copy)
{
    GType gtype;
    bool out_of_range = false;

    gtype = G_VALUE_TYPE(gvalue);

    if (value.isObject()) {
        JS::RootedObject obj(context, &value.toObject());
        GType boxed_gtype;

        if (!gjs_gtype_get_actual_gtype(context, obj, &boxed_gtype))
            return false;

        // Don't unbox GValue if the GValue's gtype is GObject.Value
        if (g_type_is_a(boxed_gtype, G_TYPE_VALUE) && gtype != G_TYPE_VALUE) {
            if (no_copy) {
                gjs_throw(
                    context,
                    "Cannot convert GObject.Value object without copying.");
                return false;
            }

            GValue* source = BoxedBase::to_c_ptr<GValue>(context, obj);
            // Only initialize the value if it doesn't have a type
            // and our source GValue has been initialized
            GType source_gtype = G_VALUE_TYPE(source);
            if (gtype == 0) {
                if (source_gtype == 0) {
                    gjs_throw(context,
                              "GObject.Value is not initialized with a type");
                    return false;
                }
                g_value_init(gvalue, source_gtype);
            }

            GType dest_gtype = G_VALUE_TYPE(gvalue);
            if (!g_value_type_compatible(source_gtype, dest_gtype)) {
                gjs_throw(context, "GObject.Value expected GType %s, found %s",
                          g_type_name(dest_gtype), g_type_name(source_gtype));
                return false;
            }

            g_value_copy(source, gvalue);
            return true;
        }
    }

    if (gtype == 0) {
        if (!gjs_value_guess_g_type(context, value, &gtype))
            return false;

        if (gtype == G_TYPE_INVALID) {
            gjs_throw(context, "Could not guess unspecified GValue type");
            return false;
        }

        gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                          "Guessed GValue type %s from JS Value",
                          g_type_name(gtype));

        g_value_init(gvalue, gtype);
    }

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Converting JS::Value to gtype %s",
                      g_type_name(gtype));


    if (gtype == G_TYPE_STRING) {
        /* Don't use ValueToString since we don't want to just toString()
         * everything automatically
         */
        if (value.isNull()) {
            g_value_set_string(gvalue, NULL);
        } else if (value.isString()) {
            JS::RootedString str(context, value.toString());
            JS::UniqueChars utf8_string(JS_EncodeStringToUTF8(context, str));
            if (!utf8_string)
                return false;

            g_value_set_string(gvalue, utf8_string.get());
        } else {
            return throw_expect_type(context, value, "string");
        }
    } else if (gtype == G_TYPE_CHAR) {
        int32_t i;
        if (Gjs::js_value_to_c_checked<signed char>(context, value, &i,
                                                    &out_of_range) &&
            !out_of_range) {
            g_value_set_schar(gvalue, static_cast<signed char>(i));
        } else {
            return throw_expect_type(context, value, "char", 0, out_of_range);
        }
    } else if (gtype == G_TYPE_UCHAR) {
        uint32_t i;
        if (Gjs::js_value_to_c_checked<unsigned char>(context, value, &i,
                                                      &out_of_range) &&
            !out_of_range) {
            g_value_set_uchar(gvalue, (unsigned char)i);
        } else {
            return throw_expect_type(context, value, "unsigned char", 0,
                                     out_of_range);
        }
    } else if (gtype == G_TYPE_INT) {
        gint32 i;
        if (Gjs::js_value_to_c(context, value, &i)) {
            g_value_set_int(gvalue, i);
        } else {
            return throw_expect_type(context, value, "integer");
        }
    } else if (gtype == G_TYPE_INT64) {
        int64_t i;
        if (Gjs::js_value_to_c_checked<int64_t>(context, value, &i,
                                                &out_of_range) &&
            !out_of_range) {
            g_value_set_int64(gvalue, i);
        } else {
            return throw_expect_type(context, value, "64-bit integer", 0,
                                     out_of_range);
        }
    } else if (gtype == G_TYPE_DOUBLE) {
        gdouble d;
        if (Gjs::js_value_to_c(context, value, &d)) {
            g_value_set_double(gvalue, d);
        } else {
            return throw_expect_type(context, value, "double");
        }
    } else if (gtype == G_TYPE_FLOAT) {
        gdouble d;
        if (Gjs::js_value_to_c_checked<float>(context, value, &d,
                                              &out_of_range) &&
            !out_of_range) {
            g_value_set_float(gvalue, d);
        } else {
            return throw_expect_type(context, value, "float", 0, out_of_range);
        }
    } else if (gtype == G_TYPE_UINT) {
        guint32 i;
        if (Gjs::js_value_to_c(context, value, &i)) {
            g_value_set_uint(gvalue, i);
        } else {
            return throw_expect_type(context, value, "unsigned integer");
        }
    } else if (gtype == G_TYPE_UINT64) {
        uint64_t i;
        if (Gjs::js_value_to_c_checked<uint64_t>(context, value, &i,
                                                 &out_of_range) &&
            !out_of_range) {
            g_value_set_uint64(gvalue, i);
        } else {
            return throw_expect_type(context, value, "unsigned 64-bit integer",
                                     0, out_of_range);
        }
    } else if (gtype == G_TYPE_BOOLEAN) {
        /* JS::ToBoolean() can't fail */
        g_value_set_boolean(gvalue, JS::ToBoolean(value));
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) ||
               g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GObject *gobj;

        gobj = NULL;
        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());
            if (!ObjectBase::typecheck(context, obj, nullptr, gtype) ||
                !ObjectBase::to_c_ptr(context, obj, &gobj))
                return false;
            if (!gobj)
                return true;  // treat disposed object as if value.isNull()
        } else {
            return throw_expect_type(context, value, "object", gtype);
        }

        g_value_set_object(gvalue, gobj);
    } else if (gtype == G_TYPE_STRV) {
        if (value.isNull())
            return true;

        bool is_array;
        if (!JS::IsArrayObject(context, value, &is_array))
            return false;
        if (!is_array)
            return throw_expect_type(context, value, "strv");

        JS::RootedObject array_obj(context, &value.toObject());
        uint32_t length;
        if (!JS::GetArrayLength(context, array_obj, &length))
            return throw_expect_type(context, value, "strv");

        void* result;
        if (!gjs_array_to_strv(context, value, length, &result))
            return false;

        g_value_take_boxed(gvalue, static_cast<char**>(result));
    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
        void *gboxed;

        gboxed = NULL;
        if (value.isNull())
            return true;

        /* special case GValue */
        if (gtype == G_TYPE_VALUE) {
            /* explicitly handle values that are already GValues
               to avoid infinite recursion */
            if (value.isObject()) {
                JS::RootedObject obj(context, &value.toObject());
                GType guessed_gtype;

                if (!gjs_value_guess_g_type(context, value, &guessed_gtype))
                    return false;

                if (guessed_gtype == G_TYPE_VALUE) {
                    gboxed = BoxedBase::to_c_ptr<GValue>(context, obj);
                    g_value_set_boxed(gvalue, gboxed);
                    return true;
                }
            }

            Gjs::AutoGValue nested_gvalue;
            if (!gjs_value_to_g_value(context, value, &nested_gvalue))
                return false;

            g_value_set_boxed(gvalue, &nested_gvalue);
            return true;
        }

        if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (gtype == ObjectBox::gtype()) {
                g_value_set_boxed(gvalue, ObjectBox::boxed(context, obj).get());
                return true;
            } else if (gtype == G_TYPE_ERROR) {
                /* special case GError */
                gboxed = ErrorBase::to_c_ptr(context, obj);
                if (!gboxed)
                    return false;
            } else if (gtype == G_TYPE_BYTE_ARRAY) {
                /* special case GByteArray */
                JS::RootedObject obj(context, &value.toObject());
                if (JS_IsUint8Array(obj)) {
                    g_value_take_boxed(gvalue,
                                       gjs_byte_array_get_byte_array(obj));
                    return true;
                }
            } else if (gtype == G_TYPE_ARRAY) {
                gjs_throw(context, "Converting %s to GArray is not supported",
                          JS::InformalValueTypeName(value));
                return false;
            } else if (gtype == G_TYPE_PTR_ARRAY) {
                gjs_throw(context, "Converting %s to GArray is not supported",
                          JS::InformalValueTypeName(value));
                return false;
            } else if (gtype == G_TYPE_HASH_TABLE) {
                gjs_throw(context,
                          "Converting %s to GHashTable is not supported",
                          JS::InformalValueTypeName(value));
                return false;
            } else {
                GjsAutoBaseInfo registered =
                    g_irepository_find_by_gtype(nullptr, gtype);

                /* We don't necessarily have the typelib loaded when
                   we first see the structure... */
                if (registered) {
                    GIInfoType info_type = registered.type();

                    if (info_type == GI_INFO_TYPE_STRUCT &&
                        g_struct_info_is_foreign(
                            registered.as<GIStructInfo>())) {
                        GIArgument arg;

                        if (!gjs_struct_foreign_convert_to_gi_argument(
                                context, value, registered, nullptr,
                                GJS_ARGUMENT_ARGUMENT, GI_TRANSFER_NOTHING,
                                GjsArgumentFlags::MAY_BE_NULL, &arg))
                            return false;

                        gboxed = gjs_arg_get<void*>(&arg);
                    }
                }

                /* First try a union, if that fails,
                   assume a boxed struct. Distinguishing
                   which one is expected would require checking
                   the associated GIBaseInfo, which is not necessary
                   possible, if e.g. we see the GType without
                   loading the typelib.
                */
                if (!gboxed) {
                    if (UnionBase::typecheck(context, obj, nullptr, gtype,
                                             GjsTypecheckNoThrow())) {
                        gboxed = UnionBase::to_c_ptr(context, obj);
                    } else {
                        if (!BoxedBase::typecheck(context, obj, nullptr, gtype))
                            return false;

                        gboxed = BoxedBase::to_c_ptr(context, obj);
                    }
                    if (!gboxed)
                        return false;
                }
            }
        } else {
            return throw_expect_type(context, value, "boxed type", gtype);
        }

        if (no_copy)
            g_value_set_static_boxed(gvalue, gboxed);
        else
            g_value_set_boxed(gvalue, gboxed);
    } else if (gtype == G_TYPE_VARIANT) {
        GVariant *variant = NULL;

        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (!BoxedBase::typecheck(context, obj, nullptr, G_TYPE_VARIANT))
                return false;

            variant = BoxedBase::to_c_ptr<GVariant>(context, obj);
            if (!variant)
                return false;
        } else {
            return throw_expect_type(context, value, "boxed type", gtype);
        }

        g_value_set_variant (gvalue, variant);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        int64_t value_int64;

        if (Gjs::js_value_to_c(context, value, &value_int64)) {
            GEnumValue *v;
            GjsAutoTypeClass<GEnumClass> enum_class(gtype);

            /* See arg.c:_gjs_enum_to_int() */
            v = g_enum_get_value(enum_class, (int)value_int64);
            if (v == NULL) {
                gjs_throw(context,
                          "%d is not a valid value for enumeration %s",
                          value.toInt32(), g_type_name(gtype));
                return false;
            }

            g_value_set_enum(gvalue, v->value);
        } else {
            return throw_expect_type(context, value, "enum", gtype);
        }
    } else if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
        int64_t value_int64;

        if (Gjs::js_value_to_c(context, value, &value_int64)) {
            if (!_gjs_flags_value_is_valid(context, gtype, value_int64))
                return false;

            /* See arg.c:_gjs_enum_to_int() */
            g_value_set_flags(gvalue, (int)value_int64);
        } else {
            return throw_expect_type(context, value, "flags", gtype);
        }
    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
        void *gparam;

        gparam = NULL;
        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (!gjs_typecheck_param(context, obj, gtype, true))
                return false;

            gparam = gjs_g_param_from_param(context, obj);
        } else {
            return throw_expect_type(context, value, "param type", gtype);
        }

        g_value_set_param(gvalue, (GParamSpec*) gparam);
    } else if (gtype == G_TYPE_GTYPE) {
        GType type;

        if (!value.isObject())
            return throw_expect_type(context, value, "GType object");

        JS::RootedObject obj(context, &value.toObject());
        if (!gjs_gtype_get_actual_gtype(context, obj, &type))
            return false;
        g_value_set_gtype(gvalue, type);
    } else if (g_type_is_a(gtype, G_TYPE_POINTER)) {
        if (value.isNull()) {
            /* Nothing to do */
        } else {
            gjs_throw(context,
                      "Cannot convert non-null JS value to G_POINTER");
            return false;
        }
    } else if (value.isNumber() &&
               g_value_type_transformable(G_TYPE_INT, gtype)) {
        /* Only do this crazy gvalue transform stuff after we've
         * exhausted everything else. Adding this for
         * e.g. ClutterUnit.
         */
        gint32 i;
        if (Gjs::js_value_to_c(context, value, &i)) {
            GValue int_value = { 0, };
            g_value_init(&int_value, G_TYPE_INT);
            g_value_set_int(&int_value, i);
            g_value_transform(&int_value, gvalue);
        } else {
            return throw_expect_type(context, value, "integer");
        }
    } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
        // The gtype is none of the above, it should be derived from a custom
        // fundamental type.
        if (!value.isObject())
            return throw_expect_type(context, value, "object", gtype);

        JS::RootedObject fundamental_object(context, &value.toObject());
        if (!FundamentalBase::to_gvalue(context, fundamental_object, gvalue))
            return false;
    } else {
        gjs_debug(GJS_DEBUG_GCLOSURE, "JS::Value is number %d gtype fundamental %d transformable to int %d from int %d",
                  value.isNumber(),
                  G_TYPE_IS_FUNDAMENTAL(gtype),
                  g_value_type_transformable(gtype, G_TYPE_INT),
                  g_value_type_transformable(G_TYPE_INT, gtype));

        gjs_throw(context,
                  "Don't know how to convert JavaScript object to GType %s",
                  g_type_name(gtype));
        return false;
    }

    return true;
}

bool
gjs_value_to_g_value(JSContext      *context,
                     JS::HandleValue value,
                     GValue         *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, false);
}

bool
gjs_value_to_g_value_no_copy(JSContext      *context,
                             JS::HandleValue value,
                             GValue         *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, true);
}

[[nodiscard]] static JS::Value convert_int_to_enum(GType gtype, int v) {
    double v_double;

    if (v > 0 && v < G_MAXINT) {
        /* Optimize the unambiguous case */
        v_double = v;
    } else {
        /* Need to distinguish between negative integers and unsigned integers */
        GjsAutoEnumInfo info = g_irepository_find_by_gtype(nullptr, gtype);

        // Native enums don't have type info, assume
        // they are signed to avoid crashing when
        // they are exposed to JS.
        if (!info) {
            v_double = int64_t(v);
        } else {
            v_double = _gjs_enum_from_int(info, v);
        }
    }

    return JS::NumberValue(v_double);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_value_from_g_value_internal(JSContext* context,
                                            JS::MutableHandleValue value_p,
                                            const GValue* gvalue, bool no_copy,
                                            bool is_introspected_signal,
                                            GIArgInfo* arg_info,
                                            GITypeInfo* type_info) {
    GType gtype;

    gtype = G_VALUE_TYPE(gvalue);

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Converting gtype %s to JS::Value",
                      g_type_name(gtype));

    if (gtype != G_TYPE_STRV && g_value_fits_pointer(gvalue) &&
        g_value_peek_pointer(gvalue) == nullptr) {
        // In theory here we should throw if !g_arg_info_may_be_null(arg_info)
        // however most signals don't explicitly mark themselves as nullable,
        // so better to avoid this.
        gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                          "Converting NULL %s to JS::NullValue()",
                          g_type_name(gtype));
        value_p.setNull();
        return true;
    }

    if (gtype == G_TYPE_STRING) {
        return gjs_string_from_utf8(context, g_value_get_string(gvalue),
                                    value_p);
    } else if (gtype == G_TYPE_CHAR) {
        signed char v;
        v = g_value_get_schar(gvalue);
        value_p.setInt32(v);
    } else if (gtype == G_TYPE_UCHAR) {
        unsigned char v;
        v = g_value_get_uchar(gvalue);
        value_p.setInt32(v);
    } else if (gtype == G_TYPE_INT) {
        int v;
        v = g_value_get_int(gvalue);
        value_p.set(JS::NumberValue(v));
    } else if (gtype == G_TYPE_UINT) {
        guint v;
        v = g_value_get_uint(gvalue);
        value_p.setNumber(v);
    } else if (gtype == G_TYPE_DOUBLE) {
        double d;
        d = g_value_get_double(gvalue);
        value_p.setNumber(JS::CanonicalizeNaN(d));
    } else if (gtype == G_TYPE_FLOAT) {
        double d;
        d = g_value_get_float(gvalue);
        value_p.setNumber(JS::CanonicalizeNaN(d));
    } else if (gtype == G_TYPE_BOOLEAN) {
        bool v;
        v = g_value_get_boolean(gvalue);
        value_p.setBoolean(!!v);
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        return ObjectInstance::set_value_from_gobject(
            context, static_cast<GObject*>(g_value_get_object(gvalue)),
            value_p);
    } else if (gtype == G_TYPE_STRV) {
        if (!gjs_array_from_strv (context,
                                  value_p,
                                  (const char**) g_value_get_boxed (gvalue))) {
            gjs_throw(context, "Failed to convert strv to array");
            return false;
        }
    } else if (gtype == G_TYPE_ARRAY || gtype == G_TYPE_BYTE_ARRAY ||
               gtype == G_TYPE_PTR_ARRAY) {
        if (gtype == G_TYPE_BYTE_ARRAY) {
            auto* byte_array = static_cast<GByteArray*>(g_value_get_boxed(gvalue));
            JSObject* array =
                gjs_byte_array_from_byte_array(context, byte_array);
            if (!array) {
                gjs_throw(context,
                          "Couldn't convert GByteArray to a Uint8Array");
                return false;
            }
            value_p.setObject(*array);
            return true;
        }

        if (!is_introspected_signal || !arg_info) {
            gjs_throw(context, "Unknown signal");
            return false;
        }

        GITransfer transfer = g_arg_info_get_ownership_transfer(arg_info);
        GjsAutoTypeInfo element_info = g_type_info_get_param_type(type_info, 0);
        if (!gjs_array_from_g_value_array(context, value_p, element_info,
                                          transfer, gvalue)) {
            gjs_throw(context, "Failed to convert array");
            return false;
        }
    } else if (gtype == G_TYPE_HASH_TABLE) {
        if (!arg_info) {
            gjs_throw(context, "Failed to get GValue from Hash Table without"
                      "signal information");
            return false;
        }
        GjsAutoTypeInfo key_info = g_type_info_get_param_type(type_info, 0);
        GjsAutoTypeInfo value_info = g_type_info_get_param_type(type_info, 1);
        GITransfer transfer = g_arg_info_get_ownership_transfer(arg_info);

        if (!gjs_object_from_g_hash(
                context, value_p, key_info, value_info, transfer,
                static_cast<GHashTable*>(g_value_get_boxed(gvalue)))) {
            gjs_throw(context, "Failed to convert Hash Table");
            return false;
        }
    } else if (g_type_is_a(gtype, G_TYPE_BOXED) || gtype == G_TYPE_VARIANT) {
        void *gboxed;
        JSObject *obj;

        if (g_type_is_a(gtype, G_TYPE_BOXED))
            gboxed = g_value_get_boxed(gvalue);
        else
            gboxed = g_value_get_variant(gvalue);

        if (gtype == ObjectBox::gtype()) {
            obj = ObjectBox::object_for_c_ptr(context,
                                              static_cast<ObjectBox*>(gboxed));
            if (!obj)
                return false;
            value_p.setObject(*obj);
            return true;
        }

        /* special case GError */
        if (gtype == G_TYPE_ERROR) {
            obj = ErrorInstance::object_for_c_ptr(context,
                                                  static_cast<GError*>(gboxed));
            if (!obj)
                return false;
            value_p.setObject(*obj);
            return true;
        }

        /* special case GValue */
        if (gtype == G_TYPE_VALUE) {
            return gjs_value_from_g_value(context, value_p,
                                          static_cast<GValue *>(gboxed));
        }

        /* The only way to differentiate unions and structs is from
         * their g-i info as both GBoxed */
        GjsAutoBaseInfo info = g_irepository_find_by_gtype(nullptr, gtype);
        if (!info) {
            gjs_throw(context,
                      "No introspection information found for %s",
                      g_type_name(gtype));
            return false;
        }

        if (info.type() == GI_INFO_TYPE_STRUCT &&
            g_struct_info_is_foreign(info)) {
            GIArgument arg;
            gjs_arg_set(&arg, gboxed);
            return gjs_struct_foreign_convert_from_gi_argument(context, value_p,
                                                               info, &arg);
        }

        GIInfoType type = info.type();
        if (type == GI_INFO_TYPE_BOXED || type == GI_INFO_TYPE_STRUCT) {
            if (no_copy)
                obj = BoxedInstance::new_for_c_struct(context, info, gboxed,
                                                      BoxedInstance::NoCopy());
            else
                obj = BoxedInstance::new_for_c_struct(context, info, gboxed);
        } else if (type == GI_INFO_TYPE_UNION) {
            obj = UnionInstance::new_for_c_union(context, info, gboxed);
        } else {
            gjs_throw(context, "Unexpected introspection type %d for %s",
                      info.type(), g_type_name(gtype));
            return false;
        }

        value_p.setObjectOrNull(obj);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        value_p.set(convert_int_to_enum(gtype, g_value_get_enum(gvalue)));
    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
        GParamSpec *gparam;
        JSObject *obj;

        gparam = g_value_get_param(gvalue);

        obj = gjs_param_from_g_param(context, gparam);
        value_p.setObjectOrNull(obj);
    } else if (is_introspected_signal && g_type_is_a(gtype, G_TYPE_POINTER)) {
        if (!arg_info) {
            gjs_throw(context, "Unknown signal.");
            return false;
        }

        g_assert(((void)"Check gjs_value_from_array_and_length_values() before"
                        " calling gjs_value_from_g_value_internal()",
                  g_type_info_get_array_length(type_info) == -1));

        GIArgument arg;
        gjs_arg_set(&arg, g_value_get_pointer(gvalue));

        return gjs_value_from_gi_argument(context, value_p, type_info, &arg,
                                          true);
    } else if (gtype == G_TYPE_GTYPE) {
        GType gvalue_gtype = g_value_get_gtype(gvalue);

        if (gvalue_gtype == 0) {
            value_p.setNull();
            return true;
        }

        JS::RootedObject obj(
            context, gjs_gtype_create_gtype_wrapper(context, gvalue_gtype));
        if (!obj)
            return false;

        value_p.setObject(*obj);
    } else if (g_type_is_a(gtype, G_TYPE_POINTER)) {
        if (g_value_get_pointer(gvalue) != nullptr) {
            gjs_throw(context,
                      "Can't convert non-null pointer to JS value");
            return false;
        }
    } else if (g_value_type_transformable(gtype, G_TYPE_DOUBLE)) {
        GValue double_value = { 0, };
        double v;
        g_value_init(&double_value, G_TYPE_DOUBLE);
        g_value_transform(gvalue, &double_value);
        v = g_value_get_double(&double_value);
        value_p.setNumber(v);
    } else if (g_value_type_transformable(gtype, G_TYPE_INT)) {
        GValue int_value = { 0, };
        int v;
        g_value_init(&int_value, G_TYPE_INT);
        g_value_transform(gvalue, &int_value);
        v = g_value_get_int(&int_value);
        value_p.set(JS::NumberValue(v));
    } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
        /* The gtype is none of the above, it should be a custom
           fundamental type. */
        JS::RootedObject obj(context);
        if (!FundamentalInstance::object_for_gvalue(context, gvalue, gtype,
                                                    &obj))
            return false;

        value_p.setObjectOrNull(obj);
    } else {
        gjs_throw(context,
                  "Don't know how to convert GType %s to JavaScript object",
                  g_type_name(gtype));
        return false;
    }

    return true;
}

bool
gjs_value_from_g_value(JSContext             *context,
                       JS::MutableHandleValue value_p,
                       const GValue          *gvalue)
{
    return gjs_value_from_g_value_internal(context, value_p, gvalue, false);
}
