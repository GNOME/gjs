/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include <util/log.h>

#include "foreign.h"
#include "value.h"
#include "closure.h"
#include "arg.h"
#include "param.h"
#include "object.h"
#include "fundamental.h"
#include "boxed.h"
#include "union.h"
#include "gtype.h"
#include "gerror.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <girepository.h>

static bool gjs_value_from_g_value_internal(JSContext    *context,
                                            JS::Value    *value_p,
                                            const GValue *gvalue,
                                            bool          no_copy,
                                            GSignalQuery *signal_query,
                                            gint          arg_n);

/*
 * Gets signal introspection info about closure, or NULL if not found. Currently
 * only works for signals on introspected GObjects, not signals on GJS-defined
 * GObjects nor standalone closures. The return value must be unreffed.
 */
static GISignalInfo *
get_signal_info_if_available(GSignalQuery *signal_query)
{
    GIBaseInfo *obj;
    GIInfoType info_type;
    GISignalInfo *signal_info = NULL;

    if (!signal_query->itype)
        return NULL;

    obj = g_irepository_find_by_gtype(NULL, signal_query->itype);
    if (!obj)
        return NULL;

    info_type = g_base_info_get_type (obj);
    if (info_type == GI_INFO_TYPE_OBJECT)
      signal_info = g_object_info_find_signal((GIObjectInfo*)obj,
                                              signal_query->signal_name);
    else if (info_type == GI_INFO_TYPE_INTERFACE)
      signal_info = g_interface_info_find_signal((GIInterfaceInfo*)obj,
                                                 signal_query->signal_name);
    g_base_info_unref((GIBaseInfo*)obj);

    return signal_info;
}

/*
 * Fill in value_p with a JS array, converted from a C array stored as a pointer
 * in array_value, with its length stored in array_length_value.
 */
static bool
gjs_value_from_array_and_length_values(JSContext    *context,
                                       JS::Value    *value_p,
                                       GITypeInfo   *array_type_info,
                                       const GValue *array_value,
                                       const GValue *array_length_value,
                                       bool          no_copy,
                                       GSignalQuery *signal_query,
                                       int           array_length_arg_n)
{
    JS::Value array_length;
    GArgument array_arg;

    g_assert(G_VALUE_HOLDS_POINTER(array_value));
    g_assert(G_VALUE_HOLDS_INT(array_length_value));

    if (!gjs_value_from_g_value_internal(context, &array_length,
                                         array_length_value, no_copy,
                                         signal_query, array_length_arg_n))
        return false;

    array_arg.v_pointer = g_value_get_pointer(array_value);

    return gjs_value_from_explicit_array(context, value_p, array_type_info,
                                         &array_arg, array_length.toInt32());
}

static void
closure_marshal(GClosure        *closure,
                GValue          *return_value,
                guint            n_param_values,
                const GValue    *param_values,
                gpointer         invocation_hint,
                gpointer         marshal_data)
{
    JSContext *context;
    JSRuntime *runtime;
    JSObject *obj;
    int argc;
    JS::Value rval;
    int i;
    GSignalQuery signal_query = { 0, };
    GISignalInfo *signal_info;
    bool *skip;
    int *array_len_indices_for;
    GITypeInfo **type_info_for;
    int argv_index;

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Marshal closure %p",
                      closure);

    if (!gjs_closure_is_valid(closure)) {
        /* We were destroyed; become a no-op */
        return;
    }

    context = gjs_closure_get_context(closure);
    runtime = JS_GetRuntime(context);
    if (G_UNLIKELY (gjs_runtime_is_sweeping(runtime))) {
        GSignalInvocationHint *hint = (GSignalInvocationHint*) invocation_hint;

        g_critical("Attempting to call back into JSAPI during the sweeping phase of GC. "
                   "This is most likely caused by not destroying a Clutter actor or Gtk+ "
                   "widget with ::destroy signals connected, but can also be caused by "
                   "using the destroy() or dispose() vfuncs. Because it would crash the "
                   "application, it has been blocked and the JS callback not invoked.");
        if (hint) {
            gpointer instance;
            g_signal_query(hint->signal_id, &signal_query);

            instance = g_value_peek_pointer(&param_values[0]);
            g_critical("The offending signal was %s on %s %p.", signal_query.signal_name,
                       g_type_name(G_TYPE_FROM_INSTANCE(instance)), instance);
        }
        /* A gjs_dumpstack() would be nice here, but we can't,
           because that works by creating a new Error object and
           reading the stack property, which is the worst possible
           idea during a GC session.
        */
        return;
    }

    obj = gjs_closure_get_callable(closure);
    JS_BeginRequest(context);
    JSAutoCompartment ac(context, obj);

    argc = n_param_values;
    rval = JS::UndefinedValue();
    JS::AutoValueVector argv(context);
    for (i = 0; i < argc; i++)
        argv.append(JS::UndefinedValue());

    JS_AddValueRoot(context, &rval);

    if (marshal_data) {
        /* we are used for a signal handler */
        guint signal_id;

        signal_id = GPOINTER_TO_UINT(marshal_data);

        g_signal_query(signal_id, &signal_query);

        if (!signal_query.signal_id) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called on invalid signal");
            goto cleanup;
        }

        if (signal_query.n_params + 1 != n_param_values) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Signal handler being called with wrong number of parameters");
            goto cleanup;
        }
    }

    /* Check if any parameters, such as array lengths, need to be eliminated
     * before we invoke the closure.
     */
    skip = g_newa(bool, argc);
    memset(skip, 0, sizeof (bool) * argc);
    array_len_indices_for = g_newa(int, argc);
    for(i = 0; i < argc; i++)
        array_len_indices_for[i] = -1;
    type_info_for = g_newa(GITypeInfo *, argc);
    memset(type_info_for, 0, sizeof (gpointer) * argc);

    signal_info = get_signal_info_if_available(&signal_query);
    if (signal_info) {
        /* Start at argument 1, skip the instance parameter */
        for (i = 1; i < argc; ++i) {
            GIArgInfo *arg_info;
            int array_len_pos;

            arg_info = g_callable_info_get_arg(signal_info, i - 1);
            type_info_for[i] = g_arg_info_get_type(arg_info);

            array_len_pos = g_type_info_get_array_length(type_info_for[i]);
            if (array_len_pos != -1) {
                skip[array_len_pos + 1] = true;
                array_len_indices_for[i] = array_len_pos + 1;
            }

            g_base_info_unref((GIBaseInfo *)arg_info);
        }

        g_base_info_unref((GIBaseInfo *)signal_info);
    }

    argv_index = 0;
    for (i = 0; i < argc; ++i) {
        const GValue *gval = &param_values[i];
        bool no_copy;
        int array_len_index;
        bool res;

        if (skip[i])
            continue;

        no_copy = false;

        if (i >= 1 && signal_query.signal_id) {
            no_copy = (signal_query.param_types[i - 1] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0;
        }

        array_len_index = array_len_indices_for[i];
        if (array_len_index != -1) {
            const GValue *array_len_gval = &param_values[array_len_index];
            res = gjs_value_from_array_and_length_values(context,
                                                         &argv[argv_index],
                                                         type_info_for[i],
                                                         gval, array_len_gval,
                                                         no_copy, &signal_query,
                                                         array_len_index);
        } else {
            res = gjs_value_from_g_value_internal(context, &argv[argv_index],
                                                  gval, no_copy, &signal_query,
                                                  i);
        }

        if (!res) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert arg %d in order to invoke closure",
                      i);
            gjs_log_exception(context);
            goto cleanup;
        }

        argv_index++;
    }

    for (i = 1; i < argc; i++)
        if (type_info_for[i])
            g_base_info_unref((GIBaseInfo *)type_info_for[i]);

    gjs_closure_invoke(closure, argv_index, &argv[0], &rval);

    if (return_value != NULL) {
        if (rval.isUndefined()) {
            /* something went wrong invoking, error should be set already */
            goto cleanup;
        }

        if (!gjs_value_to_g_value(context, rval, return_value)) {
            gjs_debug(GJS_DEBUG_GCLOSURE,
                      "Unable to convert return value when invoking closure");
            gjs_log_exception(context);
            goto cleanup;
        }
    }

 cleanup:
    JS_RemoveValueRoot(context, &rval);
    JS_EndRequest(context);
}

GClosure*
gjs_closure_new_for_signal(JSContext  *context,
                           JSObject   *callable,
                           const char *description,
                           guint       signal_id)
{
    GClosure *closure;

    closure = gjs_closure_new(context, callable, description, false);

    g_closure_set_meta_marshal(closure, GUINT_TO_POINTER(signal_id), closure_marshal);

    return closure;
}

GClosure*
gjs_closure_new_marshaled (JSContext    *context,
                           JSObject     *callable,
                           const char   *description)
{
    GClosure *closure;

    closure = gjs_closure_new(context, callable, description, true);

    g_closure_set_marshal(closure, closure_marshal);

    return closure;
}

static GType
gjs_value_guess_g_type(JSContext *context,
                       JS::Value  value)
{
    if (value.isNull())
        return G_TYPE_POINTER;

    if (value.isString())
        return G_TYPE_STRING;

    if (value.isInt32())
        return G_TYPE_INT;

    if (value.isDouble())
        return G_TYPE_DOUBLE;

    if (value.isBoolean())
        return G_TYPE_BOOLEAN;

    if (value.isObject()) {
        JS::RootedObject obj(context, &value.toObject());
        return gjs_gtype_get_actual_gtype(context, obj);
    }

    return G_TYPE_INVALID;
}

static bool
gjs_value_to_g_value_internal(JSContext    *context,
                              JS::Value     value,
                              GValue       *gvalue,
                              bool          no_copy)
{
    GType gtype;

    gtype = G_VALUE_TYPE(gvalue);

    if (gtype == 0) {
        gtype = gjs_value_guess_g_type(context, value);

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
            gchar *utf8_string;

            if (!gjs_string_to_utf8(context, value, &utf8_string))
                return false;

            g_value_take_string(gvalue, utf8_string);
        } else {
            gjs_throw(context,
                      "Wrong type %s; string expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_CHAR) {
        gint32 i;
        if (JS::ToInt32(context, value, &i) && i >= SCHAR_MIN && i <= SCHAR_MAX) {
            g_value_set_schar(gvalue, (signed char)i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; char expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_UCHAR) {
        guint16 i;
        if (JS::ToUint16(context, value, &i) && i <= UCHAR_MAX) {
            g_value_set_uchar(gvalue, (unsigned char)i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; unsigned char expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_INT) {
        gint32 i;
        if (JS::ToInt32(context, value, &i)) {
            g_value_set_int(gvalue, i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; integer expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_DOUBLE) {
        gdouble d;
        if (JS::ToNumber(context, value, &d)) {
            g_value_set_double(gvalue, d);
        } else {
            gjs_throw(context,
                      "Wrong type %s; double expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_FLOAT) {
        gdouble d;
        if (JS::ToNumber(context, value, &d)) {
            g_value_set_float(gvalue, d);
        } else {
            gjs_throw(context,
                      "Wrong type %s; float expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_UINT) {
        guint32 i;
        if (JS::ToUint32(context, value, &i)) {
            g_value_set_uint(gvalue, i);
        } else {
            gjs_throw(context,
                      "Wrong type %s; unsigned integer expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (gtype == G_TYPE_BOOLEAN) {
        /* JS::ToBoolean() can't fail */
        g_value_set_boolean(gvalue, JS::ToBoolean(value));
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GObject *gobj;

        gobj = NULL;
        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (!gjs_typecheck_object(context, obj, gtype, true))
                return false;

            gobj = gjs_g_object_from_object(context, obj);
        } else {
            gjs_throw(context,
                      "Wrong type %s; object %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return false;
        }

        g_value_set_object(gvalue, gobj);
    } else if (gtype == G_TYPE_STRV) {
        jsid length_name;
        JSBool found_length;

        length_name = gjs_context_get_const_string(context, GJS_STRING_LENGTH);
        if (value.isNull()) {
            /* do nothing */
        } else if (JS_HasPropertyById(context, &value.toObject(), length_name, &found_length) &&
                   found_length) {
            JS::Value length_value;
            guint32 length;

            if (!gjs_object_require_property(context,
                                             &value.toObject(), NULL,
                                             length_name,
                                             &length_value) ||
                !JS::ToUint32(context, length_value, &length)) {
                gjs_throw(context,
                          "Wrong type %s; strv expected",
                          gjs_get_type_name(value));
                return false;
            } else {
                void *result;
                char **strv;

                if (!gjs_array_to_strv (context,
                                        value,
                                        length, &result))
                    return false;
                /* cast to strv in a separate step to avoid type-punning */
                strv = (char**) result;
                g_value_take_boxed (gvalue, strv);
            }
        } else {
            gjs_throw(context,
                      "Wrong type %s; strv expected",
                      gjs_get_type_name(value));
            return false;
        }
    } else if (g_type_is_a(gtype, G_TYPE_BOXED)) {
        void *gboxed;

        gboxed = NULL;
        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (g_type_is_a(gtype, G_TYPE_ERROR)) {
                /* special case GError */
                if (!gjs_typecheck_gerror(context, obj, true))
                    return false;

                gboxed = gjs_gerror_from_error(context, obj);
            } else {
                GIBaseInfo *registered = g_irepository_find_by_gtype (NULL, gtype);

                /* We don't necessarily have the typelib loaded when
                   we first see the structure... */
                if (registered) {
                    GIInfoType info_type = g_base_info_get_type (registered);

                    if (info_type == GI_INFO_TYPE_STRUCT &&
                        g_struct_info_is_foreign ((GIStructInfo*)registered)) {
                        GArgument arg;

                        if (!gjs_struct_foreign_convert_to_g_argument (context, value,
                                                                       registered,
                                                                       NULL,
                                                                       GJS_ARGUMENT_ARGUMENT,
                                                                       GI_TRANSFER_NOTHING,
                                                                       true, &arg))
                            return false;

                        gboxed = arg.v_pointer;
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
                    if (gjs_typecheck_union(context, obj, NULL, gtype, false)) {
                        gboxed = gjs_c_union_from_union(context, obj);
                    } else {
                        if (!gjs_typecheck_boxed(context, obj, NULL, gtype, true))
                            return false;

                        gboxed = gjs_c_struct_from_boxed(context, obj);
                    }
                }
            }
        } else {
            gjs_throw(context,
                      "Wrong type %s; boxed type %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return false;
        }

        if (no_copy)
            g_value_set_static_boxed(gvalue, gboxed);
        else
            g_value_set_boxed(gvalue, gboxed);
    } else if (g_type_is_a(gtype, G_TYPE_VARIANT)) {
        GVariant *variant = NULL;

        if (value.isNull()) {
            /* nothing to do */
        } else if (value.isObject()) {
            JS::RootedObject obj(context, &value.toObject());

            if (!gjs_typecheck_boxed(context, obj, NULL, G_TYPE_VARIANT, true))
                return false;

            variant = (GVariant*) gjs_c_struct_from_boxed(context, obj);
        } else {
            gjs_throw(context,
                      "Wrong type %s; boxed type %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return false;
        }

        g_value_set_variant (gvalue, variant);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        int64_t value_int64;

        if (JS::ToInt64(context, value, &value_int64)) {
            GEnumValue *v;
            gpointer gtype_class = g_type_class_ref(gtype);

            /* See arg.c:_gjs_enum_to_int() */
            v = g_enum_get_value(G_ENUM_CLASS(gtype_class),
                                 (int)value_int64);
            g_type_class_unref(gtype_class);
            if (v == NULL) {
                gjs_throw(context,
                          "%d is not a valid value for enumeration %s",
                          value.toInt32(), g_type_name(gtype));
                return false;
            }

            g_value_set_enum(gvalue, v->value);
        } else {
            gjs_throw(context,
                         "Wrong type %s; enum %s expected",
                         gjs_get_type_name(value),
                         g_type_name(gtype));
            return false;
        }
    } else if (g_type_is_a(gtype, G_TYPE_FLAGS)) {
        int64_t value_int64;

        if (JS::ToInt64(context, value, &value_int64)) {
            if (!_gjs_flags_value_is_valid(context, gtype, value_int64))
                return false;

            /* See arg.c:_gjs_enum_to_int() */
            g_value_set_flags(gvalue, (int)value_int64);
        } else {
            gjs_throw(context,
                      "Wrong type %s; flags %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return false;
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
            gjs_throw(context,
                      "Wrong type %s; param type %s expected",
                      gjs_get_type_name(value),
                      g_type_name(gtype));
            return false;
        }

        g_value_set_param(gvalue, (GParamSpec*) gparam);
    } else if (g_type_is_a(gtype, G_TYPE_GTYPE)) {
        GType type;

        if (!value.isObject()) {
            gjs_throw(context, "Wrong type %s; expect a GType object",
                      gjs_get_type_name(value));
            return false;
        }

        JS::RootedObject obj(context, &value.toObject());
        type = gjs_gtype_get_actual_gtype(context, obj);
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
        if (JS::ToInt32(context, value, &i)) {
            GValue int_value = { 0, };
            g_value_init(&int_value, G_TYPE_INT);
            g_value_set_int(&int_value, i);
            g_value_transform(&int_value, gvalue);
        } else {
            gjs_throw(context,
                      "Wrong type %s; integer expected",
                      gjs_get_type_name(value));
            return false;
        }
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
gjs_value_to_g_value(JSContext    *context,
                     JS::Value     value,
                     GValue       *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, false);
}

bool
gjs_value_to_g_value_no_copy(JSContext    *context,
                             JS::Value     value,
                             GValue       *gvalue)
{
    return gjs_value_to_g_value_internal(context, value, gvalue, true);
}

static JS::Value
convert_int_to_enum (GType  gtype,
                     int    v)
{
    double v_double;

    if (v > 0 && v < G_MAXINT) {
        /* Optimize the unambiguous case */
        v_double = v;
    } else {
        GIBaseInfo *info;

        /* Need to distinguish between negative integers and unsigned integers */

        info = g_irepository_find_by_gtype(g_irepository_get_default(), gtype);
        g_assert (info);

        v_double = _gjs_enum_from_int ((GIEnumInfo *)info, v);
        g_base_info_unref(info);
    }

    return JS::NumberValue(v_double);
}

static bool
gjs_value_from_g_value_internal(JSContext    *context,
                                JS::Value    *value_p,
                                const GValue *gvalue,
                                bool          no_copy,
                                GSignalQuery *signal_query,
                                gint          arg_n)
{
    GType gtype;

    gtype = G_VALUE_TYPE(gvalue);

    gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                      "Converting gtype %s to JS::Value",
                      g_type_name(gtype));

    if (gtype == G_TYPE_STRING) {
        const char *v;
        v = g_value_get_string(gvalue);
        if (v == NULL) {
            gjs_debug_marshal(GJS_DEBUG_GCLOSURE,
                              "Converting NULL string to JS::NullValue()");
            *value_p = JS::NullValue();
        } else {
            if (!gjs_string_from_utf8(context, v, -1, value_p))
                return false;
        }
    } else if (gtype == G_TYPE_CHAR) {
        char v;
        v = g_value_get_schar(gvalue);
        *value_p = JS::Int32Value(v);
    } else if (gtype == G_TYPE_UCHAR) {
        unsigned char v;
        v = g_value_get_uchar(gvalue);
        *value_p = JS::Int32Value(v);
    } else if (gtype == G_TYPE_INT) {
        int v;
        v = g_value_get_int(gvalue);
        *value_p = JS::NumberValue(v);
    } else if (gtype == G_TYPE_UINT) {
        guint v;
        v = g_value_get_uint(gvalue);
        *value_p = JS::NumberValue(v);
    } else if (gtype == G_TYPE_DOUBLE) {
        double d;
        d = g_value_get_double(gvalue);
        *value_p = JS::NumberValue(d);
    } else if (gtype == G_TYPE_FLOAT) {
        double d;
        d = g_value_get_float(gvalue);
        *value_p = JS::NumberValue(d);
    } else if (gtype == G_TYPE_BOOLEAN) {
        bool v;
        v = g_value_get_boolean(gvalue);
        *value_p = JS::BooleanValue(!!v);
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT) || g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GObject *gobj;
        JSObject *obj;

        gobj = (GObject*) g_value_get_object(gvalue);

        obj = gjs_object_from_g_object(context, gobj);
        *value_p = JS::ObjectOrNullValue(obj);
    } else if (gtype == G_TYPE_STRV) {
        if (!gjs_array_from_strv (context,
                                  value_p,
                                  (const char**) g_value_get_boxed (gvalue))) {
            gjs_throw(context, "Failed to convert strv to array");
            return false;
        }
    } else if (g_type_is_a(gtype, G_TYPE_HASH_TABLE) ||
               g_type_is_a(gtype, G_TYPE_ARRAY) ||
               g_type_is_a(gtype, G_TYPE_BYTE_ARRAY) ||
               g_type_is_a(gtype, G_TYPE_PTR_ARRAY)) {
        gjs_throw(context,
                  "Unable to introspect element-type of container in GValue");
        return false;
    } else if (g_type_is_a(gtype, G_TYPE_BOXED) ||
               g_type_is_a(gtype, G_TYPE_VARIANT)) {
        GjsBoxedCreationFlags boxed_flags;
        GIBaseInfo *info;
        void *gboxed;
        JSObject *obj;

        if (g_type_is_a(gtype, G_TYPE_BOXED))
            gboxed = g_value_get_boxed(gvalue);
        else
            gboxed = g_value_get_variant(gvalue);
        boxed_flags = GJS_BOXED_CREATION_NONE;

        /* special case GError */
        if (g_type_is_a(gtype, G_TYPE_ERROR)) {
            obj = gjs_error_from_gerror(context, (GError*) gboxed, false);
            *value_p = JS::ObjectOrNullValue(obj);

            return true;
        }

        /* The only way to differentiate unions and structs is from
         * their g-i info as both GBoxed */
        info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                           gtype);
        if (info == NULL) {
            gjs_throw(context,
                      "No introspection information found for %s",
                      g_type_name(gtype));
            return false;
        }

        if (g_base_info_get_type(info) == GI_INFO_TYPE_STRUCT &&
            g_struct_info_is_foreign((GIStructInfo*)info)) {
            bool ret;
            GIArgument arg;
            arg.v_pointer = gboxed;
            ret = gjs_struct_foreign_convert_from_g_argument(context, value_p, info, &arg);
            g_base_info_unref(info);
            return ret;
        }

        switch (g_base_info_get_type(info)) {
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_STRUCT:
            if (no_copy)
                boxed_flags = (GjsBoxedCreationFlags) (boxed_flags | GJS_BOXED_CREATION_NO_COPY);
            obj = gjs_boxed_from_c_struct(context, (GIStructInfo *)info, gboxed, boxed_flags);
            break;
        case GI_INFO_TYPE_UNION:
            obj = gjs_union_from_c_union(context, (GIUnionInfo *)info, gboxed);
            break;
        default:
            gjs_throw(context,
                      "Unexpected introspection type %d for %s",
                      g_base_info_get_type(info),
                      g_type_name(gtype));
            g_base_info_unref(info);
            return false;
        }

        *value_p = JS::ObjectOrNullValue(obj);
        g_base_info_unref(info);
    } else if (g_type_is_a(gtype, G_TYPE_ENUM)) {
        *value_p = convert_int_to_enum(gtype, g_value_get_enum(gvalue));
    } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
        GParamSpec *gparam;
        JSObject *obj;

        gparam = g_value_get_param(gvalue);

        obj = gjs_param_from_g_param(context, gparam);
        *value_p = JS::ObjectOrNullValue(obj);
    } else if (signal_query && g_type_is_a(gtype, G_TYPE_POINTER)) {
        bool res;
        GArgument arg;
        GIArgInfo *arg_info;
        GIBaseInfo *obj;
        GISignalInfo *signal_info;
        GITypeInfo type_info;

        obj = g_irepository_find_by_gtype(NULL, signal_query->itype);
        if (!obj) {
            gjs_throw(context, "Signal argument with GType %s isn't introspectable",
                      g_type_name(signal_query->itype));
            return false;
        }

        signal_info = g_object_info_find_signal((GIObjectInfo*)obj, signal_query->signal_name);

        if (!signal_info) {
            gjs_throw(context, "Unknown signal.");
            g_base_info_unref((GIBaseInfo*)obj);
            return false;
        }
        arg_info = g_callable_info_get_arg(signal_info, arg_n - 1);
        g_arg_info_load_type(arg_info, &type_info);

        g_assert(("Check gjs_value_from_array_and_length_values() before calling"
                  " gjs_value_from_g_value_internal()",
                  g_type_info_get_array_length(&type_info) == -1));

        arg.v_pointer = g_value_get_pointer(gvalue);

        res = gjs_value_from_g_argument(context, value_p, &type_info, &arg, true);

        g_base_info_unref((GIBaseInfo*)arg_info);
        g_base_info_unref((GIBaseInfo*)signal_info);
        g_base_info_unref((GIBaseInfo*)obj);
        return res;
    } else if (g_type_is_a(gtype, G_TYPE_POINTER)) {
        gpointer pointer;

        pointer = g_value_get_pointer(gvalue);

        if (pointer == NULL) {
            *value_p = JS::NullValue();
        } else {
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
        *value_p = JS::NumberValue(v);
    } else if (g_value_type_transformable(gtype, G_TYPE_INT)) {
        GValue int_value = { 0, };
        int v;
        g_value_init(&int_value, G_TYPE_INT);
        g_value_transform(gvalue, &int_value);
        v = g_value_get_int(&int_value);
        *value_p = JS::NumberValue(v);
    } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
        /* The gtype is none of the above, it should be a custom
           fundamental type. */
        JSObject *obj;
        obj = gjs_fundamental_from_g_value(context, (const GValue*)gvalue, gtype);
        if (obj == NULL)
            return false;
        else
            *value_p = JS::ObjectValue(*obj);
    } else {
        gjs_throw(context,
                  "Don't know how to convert GType %s to JavaScript object",
                  g_type_name(gtype));
        return false;
    }

    return true;
}

bool
gjs_value_from_g_value(JSContext    *context,
                       JS::Value    *value_p,
                       const GValue *gvalue)
{
    return gjs_value_from_g_value_internal(context, value_p, gvalue, false, NULL, 0);
}
