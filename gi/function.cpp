/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <stdint.h>
#include <stdlib.h>  // for exit
#include <string.h>  // for strcmp, memset, size_t

#include <new>

#include <ffi.h>
#include <girepository.h>
#include <girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/GCVector.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetRealmFunctionPrototype
#include <js/RootingAPI.h>
#include <js/Value.h>
#include <js/Warnings.h>
#include <jsapi.h>        // for HandleValueArray, JS_GetElement
#include <jsfriendapi.h>  // for JS_GetObjectFunction
#include <jspubtd.h>      // for JSProto_TypeError, JSTYPE_FUNCTION
#include <mozilla/Maybe.h>

#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "util/log.h"

/* We use guint8 for arguments; functions can't
 * have more than this.
 */
#define GJS_ARG_INDEX_INVALID G_MAXUINT8

typedef struct {
    GIFunctionInfo *info;

    GjsParamType *param_types;

    guint8 expected_js_argc;
    guint8 js_out_argc;
    GIFunctionInvoker invoker;
} Function;

extern struct JSClass gjs_function_class;

/* Because we can't free the mmap'd data for a callback
 * while it's in use, this list keeps track of ones that
 * will be freed the next time we invoke a C function.
 */
static GSList *completed_trampolines = NULL;  /* GjsCallbackTrampoline */

GJS_DEFINE_PRIV_FROM_JS(Function, gjs_function_class)

void
gjs_callback_trampoline_ref(GjsCallbackTrampoline *trampoline)
{
    trampoline->ref_count++;
}

void
gjs_callback_trampoline_unref(GjsCallbackTrampoline *trampoline)
{
    /* Not MT-safe, like all the rest of GJS */

    trampoline->ref_count--;
    if (trampoline->ref_count == 0) {
        g_closure_unref(trampoline->js_function);
        g_callable_info_free_closure(trampoline->info, trampoline->closure);
        g_base_info_unref( (GIBaseInfo*) trampoline->info);
        g_free (trampoline->param_types);
        g_slice_free(GjsCallbackTrampoline, trampoline);
    }
}

static void
set_return_ffi_arg_from_giargument (GITypeInfo  *ret_type,
                                    void        *result,
                                    GIArgument  *return_value)
{
    switch (g_type_info_get_tag(ret_type)) {
    case GI_TYPE_TAG_VOID:
        g_assert_not_reached();
    case GI_TYPE_TAG_INT8:
        *(ffi_sarg *) result = return_value->v_int8;
        break;
    case GI_TYPE_TAG_UINT8:
        *(ffi_arg *) result = return_value->v_uint8;
        break;
    case GI_TYPE_TAG_INT16:
        *(ffi_sarg *) result = return_value->v_int16;
        break;
    case GI_TYPE_TAG_UINT16:
        *(ffi_arg *) result = return_value->v_uint16;
        break;
    case GI_TYPE_TAG_INT32:
        *(ffi_sarg *) result = return_value->v_int32;
        break;
    case GI_TYPE_TAG_UINT32:
    case GI_TYPE_TAG_BOOLEAN:
    case GI_TYPE_TAG_UNICHAR:
        *(ffi_arg *) result = return_value->v_uint32;
        break;
    case GI_TYPE_TAG_INT64:
        *(ffi_sarg *) result = return_value->v_int64;
        break;
    case GI_TYPE_TAG_INTERFACE:
        {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;

            interface_info = g_type_info_get_interface(ret_type);
            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS)
                *(ffi_sarg *) result = return_value->v_int;
            else
                *(ffi_arg *) result = (ffi_arg) return_value->v_pointer;

            g_base_info_unref(interface_info);
        }
        break;
    case GI_TYPE_TAG_UINT64:
    /* Other primitive and pointer types need to squeeze into 64-bit ffi_arg too */
    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
    case GI_TYPE_TAG_GTYPE:
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
    default:
        *(ffi_arg *) result = (ffi_arg) return_value->v_uint64;
        break;
    }
}

static void
warn_about_illegal_js_callback(const GjsCallbackTrampoline *trampoline,
                               const char *when,
                               const char *reason)
{
    g_critical("Attempting to run a JS callback %s. This is most likely caused "
               "by %s. Because it would crash the application, it has been "
               "blocked.", when, reason);
    if (trampoline->info) {
        const char *name = g_base_info_get_name(trampoline->info);
        g_critical("The offending callback was %s()%s.", name,
                   trampoline->is_vfunc ? ", a vfunc" : "");
    }
    return;
}

/* This is our main entry point for ffi_closure callbacks.
 * ffi_prep_closure is doing pure magic and replaces the original
 * function call with this one which gives us the ffi arguments,
 * a place to store the return value and our use data.
 * In other words, everything we need to call the JS function and
 * getting the return value back.
 */
static void gjs_callback_closure(ffi_cif* cif G_GNUC_UNUSED, void* result,
                                 void** ffi_args, void* data) {
    JSContext *context;
    GjsCallbackTrampoline *trampoline;
    int i, n_args, n_jsargs, n_outargs, c_args_offset = 0;
    GITypeInfo ret_type;
    bool success = false;
    bool ret_type_is_void;
    auto args = reinterpret_cast<GIArgument **>(ffi_args);

    trampoline = (GjsCallbackTrampoline *) data;
    g_assert(trampoline);
    gjs_callback_trampoline_ref(trampoline);

    if (G_UNLIKELY(!gjs_closure_is_valid(trampoline->js_function))) {
        warn_about_illegal_js_callback(trampoline, "during shutdown",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs");
        gjs_dumpstack();
        gjs_callback_trampoline_unref(trampoline);
        return;
    }

    context = gjs_closure_get_context(trampoline->js_function);
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    if (G_UNLIKELY(gjs->sweeping())) {
        warn_about_illegal_js_callback(trampoline, "during garbage collection",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs");
        gjs_dumpstack();
        gjs_callback_trampoline_unref(trampoline);
        return;
    }

    if (G_UNLIKELY(!gjs->is_owner_thread())) {
        warn_about_illegal_js_callback(trampoline, "on a different thread",
            "an API not intended to be used in JS");
        gjs_callback_trampoline_unref(trampoline);
        return;
    }

    JSAutoRealm ar(context, JS_GetFunctionObject(gjs_closure_get_callable(
                                trampoline->js_function)));

    bool can_throw_gerror = g_callable_info_can_throw_gerror(trampoline->info);
    n_args = g_callable_info_get_n_args(trampoline->info);

    g_assert(n_args >= 0);

    JS::RootedObject this_object(context);
    if (trampoline->is_vfunc) {
        GObject* gobj = G_OBJECT(args[0]->v_pointer);
        if (gobj) {
            this_object = ObjectInstance::wrapper_from_gobject(context, gobj);
            if (!this_object) {
                gjs_log_exception(context);
                return;
            }
        }

        /* "this" is not included in the GI signature, but is in the C (and
         * FFI) signature */
        c_args_offset = 1;
    }

    n_outargs = 0;
    JS::RootedValueVector jsargs(context);

    if (!jsargs.reserve(n_args))
        g_error("Unable to reserve space for vector");

    JS::RootedValue rval(context);

    for (i = 0, n_jsargs = 0; i < n_args; i++) {
        GIArgInfo arg_info;
        GITypeInfo type_info;
        GjsParamType param_type;

        g_callable_info_load_arg(trampoline->info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        /* Skip void * arguments */
        if (g_type_info_get_tag(&type_info) == GI_TYPE_TAG_VOID)
            continue;

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_OUT) {
            n_outargs++;
            continue;
        }

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_INOUT)
            n_outargs++;

        param_type = trampoline->param_types[i];

        switch (param_type) {
            case PARAM_SKIPPED:
                continue;
            case PARAM_ARRAY: {
                gint array_length_pos = g_type_info_get_array_length(&type_info);
                GIArgInfo array_length_arg;
                GITypeInfo arg_type_info;
                JS::RootedValue length(context);

                g_callable_info_load_arg(trampoline->info, array_length_pos, &array_length_arg);
                g_arg_info_load_type(&array_length_arg, &arg_type_info);
                if (!gjs_value_from_g_argument(context, &length, &arg_type_info,
                                               args[array_length_pos + c_args_offset],
                                               true))
                    goto out;

                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                if (!gjs_value_from_explicit_array(context, jsargs[n_jsargs++],
                                                   &type_info,
                                                   args[i + c_args_offset],
                                                   length.toInt32()))
                    goto out;
                break;
            }
            case PARAM_NORMAL: {
                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                GIArgument* arg = args[i + c_args_offset];
                if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_INOUT)
                    arg = *reinterpret_cast<GIArgument**>(arg);

                if (!gjs_value_from_g_argument(context, jsargs[n_jsargs++],
                                               &type_info, arg, false))
                    goto out;
                break;
            }
            case PARAM_CALLBACK:
                /* Callbacks that accept another callback as a parameter are not
                 * supported, see gjs_callback_trampoline_new() */
            case PARAM_UNKNOWN:
                // PARAM_UNKNOWN is currently not ever set on a callback's args.
            default:
                g_assert_not_reached();
        }
    }

    if (!gjs_closure_invoke(trampoline->js_function, this_object, jsargs, &rval,
                            true))
        goto out;

    g_callable_info_load_return_type(trampoline->info, &ret_type);
    ret_type_is_void = g_type_info_get_tag (&ret_type) == GI_TYPE_TAG_VOID;

    if (n_outargs == 0 && ret_type_is_void) {
        /* void return value, no out args, nothing to do */
    } else if (n_outargs == 0) {
        GIArgument argument;
        GITransfer transfer;

        transfer = g_callable_info_get_caller_owns (trampoline->info);
        /* non-void return value, no out args. Should
         * be a single return value. */
        if (!gjs_value_to_g_argument(context,
                                     rval,
                                     &ret_type,
                                     "callback",
                                     GJS_ARGUMENT_RETURN_VALUE,
                                     transfer,
                                     true,
                                     &argument))
            goto out;

        set_return_ffi_arg_from_giargument(&ret_type,
                                           result,
                                           &argument);
    } else if (n_outargs == 1 && ret_type_is_void) {
        /* void return value, one out args. Should
         * be a single return value. */
        for (i = 0; i < n_args; i++) {
            GIArgInfo arg_info;
            g_callable_info_load_arg(trampoline->info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            if (!gjs_value_to_arg(context, rval, &arg_info,
                                  *reinterpret_cast<GIArgument **>(args[i + c_args_offset])))
                goto out;

            break;
        }
    } else {
        bool is_array = rval.isObject();
        if (!JS_IsArrayObject(context, rval, &is_array))
            goto out;

        if (!is_array) {
            JSFunction* fn = gjs_closure_get_callable(trampoline->js_function);
            gjs_throw(context,
                      "Function %s (%s.%s) returned unexpected value, "
                      "expecting an Array",
                      gjs_debug_string(JS_GetFunctionDisplayId(fn)).c_str(),
                      g_base_info_get_namespace(trampoline->info),
                      g_base_info_get_name(trampoline->info));
            goto out;
        }

        JS::RootedValue elem(context);
        JS::RootedObject out_array(context, rval.toObjectOrNull());
        gsize elem_idx = 0;
        /* more than one of a return value or an out argument.
         * Should be an array of output values. */

        if (!ret_type_is_void) {
            GIArgument argument;
            GITransfer transfer = g_callable_info_get_caller_owns(trampoline->info);

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                goto out;

            if (!gjs_value_to_g_argument(context,
                                         elem,
                                         &ret_type,
                                         "callback",
                                         GJS_ARGUMENT_ARGUMENT,
                                         transfer,
                                         true,
                                         &argument))
                goto out;

            set_return_ffi_arg_from_giargument(&ret_type,
                                               result,
                                               &argument);

            elem_idx++;
        }

        for (i = 0; i < n_args; i++) {
            GIArgInfo arg_info;
            g_callable_info_load_arg(trampoline->info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                goto out;

            if (!gjs_value_to_arg(context, elem, &arg_info,
                                  *(GIArgument **)args[i + c_args_offset]))
                goto out;

            elem_idx++;
        }
    }

    success = true;

out:
    if (!success) {
        if (!JS_IsExceptionPending(context)) {
            /* "Uncatchable" exception thrown, we have to exit. We may be in a
             * main loop, or maybe not, but there's no way to tell, so we have
             * to exit here instead of propagating the exception back to the
             * original calling JS code. */
            uint8_t code;
            if (gjs->should_exit(&code))
                exit(code);

            /* Some other uncatchable exception, e.g. out of memory */
            JSFunction* fn = gjs_closure_get_callable(trampoline->js_function);
            g_error("Function %s (%s.%s) terminated with uncatchable exception",
                    gjs_debug_string(JS_GetFunctionDisplayId(fn)).c_str(),
                    g_base_info_get_namespace(trampoline->info),
                    g_base_info_get_name(trampoline->info));
        }

        /* Fill in the result with some hopefully neutral value */
        g_callable_info_load_return_type(trampoline->info, &ret_type);
        gjs_gi_argument_init_default(&ret_type,
                                     static_cast<GIArgument*>(result));

        /* If the callback has a GError** argument and invoking the closure
         * returned an error, try to make a GError from it */
        if (can_throw_gerror && rval.isObject()) {
            JS::RootedObject exc_object(context, &rval.toObject());
            GError *local_error = gjs_gerror_make_from_error(context, exc_object);

            if (local_error) {
                /* the GError ** pointer is the last argument, and is not
                 * included in the n_args */
                GIArgument *error_argument = args[n_args + c_args_offset];
                auto gerror = static_cast<GError **>(error_argument->v_pointer);
                g_propagate_error(gerror, local_error);
                JS_ClearPendingException(context);  /* don't log */
            }
        } else if (!rval.isUndefined()) {
            JS_SetPendingException(context, rval);
        }
        gjs_log_exception_uncaught(context);
    }

    if (trampoline->scope == GI_SCOPE_TYPE_ASYNC) {
        completed_trampolines = g_slist_prepend(completed_trampolines, trampoline);
    }

    gjs_callback_trampoline_unref(trampoline);
    gjs->schedule_gc_if_needed();
}

/* The global entry point for any invocations of GDestroyNotify;
 * look up the callback through the user_data and then free it.
 */
static void
gjs_destroy_notify_callback(gpointer data)
{
    GjsCallbackTrampoline *trampoline = (GjsCallbackTrampoline *) data;

    g_assert(trampoline);
    gjs_callback_trampoline_unref(trampoline);
}

GjsCallbackTrampoline* gjs_callback_trampoline_new(
    JSContext* context, JS::HandleFunction function,
    GICallableInfo* callable_info, GIScopeType scope,
    JS::HandleObject scope_object, bool is_vfunc) {
    GjsCallbackTrampoline *trampoline;
    int n_args, i;

    g_assert(function);

    trampoline = g_slice_new(GjsCallbackTrampoline);
    new (trampoline) GjsCallbackTrampoline();
    trampoline->ref_count = 1;
    trampoline->info = callable_info;
    g_base_info_ref((GIBaseInfo*)trampoline->info);

    /* The rule is:
     * - async and call callbacks are rooted
     * - callbacks in GObjects methods are traced from the object
     *   (and same for vfuncs, which are associated with a GObject prototype)
     */
    bool should_root = scope != GI_SCOPE_TYPE_NOTIFIED || !scope_object;
    trampoline->js_function = gjs_closure_new(
        context, function, g_base_info_get_name(callable_info), should_root);
    if (!should_root && scope_object) {
        ObjectBase* scope_priv = ObjectBase::for_js(context, scope_object);
        if (!scope_priv) {
            gjs_throw(context, "Signal connected to wrong type of object");
            return nullptr;
        }

        scope_priv->associate_closure(context, trampoline->js_function);
    }

    /* Analyze param types and directions, similarly to init_cached_function_data */
    n_args = g_callable_info_get_n_args(trampoline->info);
    trampoline->param_types = g_new0(GjsParamType, n_args);

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo type_info;
        GITypeTag type_tag;

        if (trampoline->param_types[i] == PARAM_SKIPPED)
            continue;

        g_callable_info_load_arg(trampoline->info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        direction = g_arg_info_get_direction(&arg_info);
        type_tag = g_type_info_get_tag(&type_info);

        if (direction != GI_DIRECTION_IN) {
            /* INOUT and OUT arguments are handled differently. */
            continue;
        }

        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;

            interface_info = g_type_info_get_interface(&type_info);
            interface_type = g_base_info_get_type(interface_info);
            if (interface_type == GI_INFO_TYPE_CALLBACK) {
                gjs_throw(context,
                          "%s %s accepts another callback as a parameter. This "
                          "is not supported",
                          is_vfunc ? "VFunc" : "Callback",
                          g_base_info_get_name(callable_info));
                g_base_info_unref(interface_info);
                return NULL;
            }
            g_base_info_unref(interface_info);
        } else if (type_tag == GI_TYPE_TAG_ARRAY) {
            if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
                int array_length_pos = g_type_info_get_array_length(&type_info);

                if (array_length_pos >= 0 && array_length_pos < n_args) {
                    GIArgInfo length_arg_info;

                    g_callable_info_load_arg(trampoline->info, array_length_pos, &length_arg_info);
                    if (g_arg_info_get_direction(&length_arg_info) != direction) {
                        gjs_throw(context,
                                  "%s %s has an array with different-direction "
                                  "length argument. This is not supported",
                                  is_vfunc ? "VFunc" : "Callback",
                                  g_base_info_get_name(callable_info));
                        return NULL;
                    }

                    trampoline->param_types[array_length_pos] = PARAM_SKIPPED;
                    trampoline->param_types[i] = PARAM_ARRAY;
                }
            }
        }
    }

    trampoline->closure = g_callable_info_prepare_closure(callable_info, &trampoline->cif,
                                                          gjs_callback_closure, trampoline);

    trampoline->scope = scope;
    trampoline->is_vfunc = is_vfunc;

    return trampoline;
}

/* an helper function to retrieve array lengths from a GArgument
   (letting the compiler generate good instructions in case of
   big endian machines) */
GJS_USE
static unsigned long
get_length_from_arg (GArgument *arg, GITypeTag tag)
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

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_fill_method_instance(JSContext       *context,
                         JS::HandleObject obj,
                         Function        *function,
                         GIArgument      *out_arg,
                         bool&            is_gobject)
{
    GIBaseInfo *container = g_base_info_get_container((GIBaseInfo *) function->info);
    GIInfoType type = g_base_info_get_type(container);
    GType gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo *)container);
    GITransfer transfer = g_callable_info_get_instance_ownership_transfer (function->info);

    is_gobject = false;

    if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_BOXED) {
        /* GError must be special cased */
        if (g_type_is_a(gtype, G_TYPE_ERROR)) {
            if (!ErrorBase::transfer_to_gi_argument(context, obj, out_arg,
                                                    GI_DIRECTION_OUT, transfer))
                return false;
        } else if (type == GI_INFO_TYPE_STRUCT &&
                   g_struct_info_is_gtype_struct((GIStructInfo*) container)) {
            /* And so do GType structures */
            GType actual_gtype;
            gpointer klass;

            if (!gjs_gtype_get_actual_gtype(context, obj, &actual_gtype))
                return false;

            if (actual_gtype == G_TYPE_NONE) {
                gjs_throw(context, "Invalid GType class passed for instance parameter");
                return false;
            }

            /* We use peek here to simplify reference counting (we just ignore
               transfer annotation, as GType classes are never really freed)
               We know that the GType class is referenced at least once when
               the JS constructor is initialized.
            */

            if (g_type_is_a(actual_gtype, G_TYPE_INTERFACE))
                klass = g_type_default_interface_peek(actual_gtype);
            else
                klass = g_type_class_peek(actual_gtype);

            out_arg->v_pointer = klass;
        } else {
            if (!BoxedBase::transfer_to_gi_argument(context, obj, out_arg,
                                                    GI_DIRECTION_OUT, transfer,
                                                    gtype, container))
                return false;
        }

    } else if (type == GI_INFO_TYPE_UNION) {
        if (!UnionBase::transfer_to_gi_argument(context, obj, out_arg,
                                                GI_DIRECTION_OUT, transfer,
                                                gtype, container))
            return false;

    } else if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_INTERFACE) {
        if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
            if (!ObjectBase::transfer_to_gi_argument(
                    context, obj, out_arg, GI_DIRECTION_OUT, transfer, gtype))
                return false;
            is_gobject = true;
        } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            if (!gjs_typecheck_param(context, obj, G_TYPE_PARAM, true))
                return false;
            out_arg->v_pointer = gjs_g_param_from_param(context, obj);
            if (transfer == GI_TRANSFER_EVERYTHING)
                g_param_spec_ref ((GParamSpec*) out_arg->v_pointer);
        } else if (G_TYPE_IS_INTERFACE(gtype)) {
            if (ObjectBase::check_jsclass(context, obj)) {
                if (!ObjectBase::transfer_to_gi_argument(context, obj, out_arg,
                                                         GI_DIRECTION_OUT,
                                                         transfer, gtype))
                    return false;
                is_gobject = true;
            } else {
                if (!FundamentalBase::transfer_to_gi_argument(
                        context, obj, out_arg, GI_DIRECTION_OUT, transfer,
                        gtype))
                    return false;
            }
        } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
            if (!FundamentalBase::transfer_to_gi_argument(
                    context, obj, out_arg, GI_DIRECTION_OUT, transfer, gtype))
                return false;
        } else {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "%s.%s is not an object instance neither a fundamental instance of a supported type",
                             g_base_info_get_namespace(container),
                             g_base_info_get_name(container));
            return false;
        }

    } else {
        g_assert_not_reached();
    }

    return true;
}

/* Intended for error messages. Return value must be freed */
GJS_USE
static char *
format_function_name(Function *function,
                     bool      is_method)
{
    auto baseinfo = static_cast<GIBaseInfo *>(function->info);
    if (is_method)
        return g_strdup_printf("method %s.%s.%s",
                               g_base_info_get_namespace(baseinfo),
                               g_base_info_get_name(g_base_info_get_container(baseinfo)),
                               g_base_info_get_name(baseinfo));
    return g_strdup_printf("function %s.%s",
                           g_base_info_get_namespace(baseinfo),
                           g_base_info_get_name(baseinfo));
}

static void
complete_async_calls(void)
{
    if (completed_trampolines) {
        for (GSList *iter = completed_trampolines; iter; iter = iter->next) {
            auto trampoline = static_cast<GjsCallbackTrampoline *>(iter->data);
            gjs_callback_trampoline_unref(trampoline);
        }
        g_slist_free(completed_trampolines);
        completed_trampolines = nullptr;
    }
}

/*
 * This function can be called in 2 different ways. You can either use
 * it to create javascript objects by providing a @js_rval argument or
 * you can decide to keep the return values in #GArgument format by
 * providing a @r_value argument.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_invoke_c_function(JSContext                             *context,
                      Function                              *function,
                      JS::HandleObject                       obj, /* "this" object */
                      const JS::HandleValueArray&            args,
                      mozilla::Maybe<JS::MutableHandleValue> js_rval,
                      GIArgument                            *r_value)
{
    /* These first four are arrays which hold argument pointers.
     * @in_arg_cvalues: C values which are passed on input (in or inout)
     * @out_arg_cvalues: C values which are returned as arguments (out or inout)
     * @inout_original_arg_cvalues: For the special case of (inout) args, we need to
     *  keep track of the original values we passed into the function, in case we
     *  need to free it.
     * @ffi_arg_pointers: For passing data to FFI, we need to create another layer
     *  of indirection; this array is a pointer to an element in in_arg_cvalues
     *  or out_arg_cvalues.
     * @return_value: The actual return value of the C function, i.e. not an (out) param
     */
    GArgument *in_arg_cvalues;
    GArgument *out_arg_cvalues;
    GArgument *inout_original_arg_cvalues;
    gpointer *ffi_arg_pointers;
    GIFFIReturnValue return_value;
    gpointer return_value_p; /* Will point inside the union return_value */
    GArgument return_gargument;

    guint8 processed_c_args = 0;
    guint8 gi_argc, gi_arg_pos;
    guint8 c_argc, c_arg_pos;
    guint8 js_arg_pos;
    bool can_throw_gerror;
    bool did_throw_gerror = false;
    GError *local_error = NULL;
    bool failed, postinvoke_release_failed;

    bool is_method;
    bool is_object_method = false;
    GITypeInfo return_info;
    GITypeTag return_tag;
    JS::RootedValueVector return_values(context);
    guint8 next_rval = 0; /* index into return_values */

    /* Because we can't free a closure while we're in it, we defer
     * freeing until the next time a C function is invoked.  What
     * we should really do instead is queue it for a GC thread.
     */
    complete_async_calls();

    is_method = g_callable_info_is_method(function->info);
    can_throw_gerror = g_callable_info_can_throw_gerror(function->info);

    c_argc = function->invoker.cif.nargs;
    gi_argc = g_callable_info_get_n_args( (GICallableInfo*) function->info);

    /* @c_argc is the number of arguments that the underlying C
     * function takes. @gi_argc is the number of arguments the
     * GICallableInfo describes (which does not include "this" or
     * GError**). @function->expected_js_argc is the number of
     * arguments we expect the JS function to take (which does not
     * include PARAM_SKIPPED args).
     *
     * @args.length() is the number of arguments that were actually passed.
     */
    if (args.length() > function->expected_js_argc) {
        GjsAutoChar name = format_function_name(function, is_method);
        if (!JS::WarnUTF8(context,
                          "Too many arguments to %s: expected %d, "
                          "got %" G_GSIZE_FORMAT,
                          name.get(), function->expected_js_argc,
                          args.length()))
            return false;
    } else if (args.length() < function->expected_js_argc) {
        GjsAutoChar name = format_function_name(function, is_method);
        gjs_throw(context, "Too few arguments to %s: "
                  "expected %d, got %" G_GSIZE_FORMAT,
                  name.get(), function->expected_js_argc, args.length());
        return false;
    }

    g_callable_info_load_return_type( (GICallableInfo*) function->info, &return_info);
    return_tag = g_type_info_get_tag(&return_info);

    in_arg_cvalues = g_newa(GArgument, c_argc);
    ffi_arg_pointers = g_newa(gpointer, c_argc);
    out_arg_cvalues = g_newa(GArgument, c_argc);
    inout_original_arg_cvalues = g_newa(GArgument, c_argc);

    failed = false;
    c_arg_pos = 0; /* index into in_arg_cvalues, etc */
    js_arg_pos = 0; /* index into argv */

    if (is_method) {
        if (!gjs_fill_method_instance(context, obj, function,
                                      &in_arg_cvalues[0], is_object_method))
            return false;
        ffi_arg_pointers[0] = &in_arg_cvalues[0];
        ++c_arg_pos;
    }

    processed_c_args = c_arg_pos;
    for (gi_arg_pos = 0; gi_arg_pos < gi_argc; gi_arg_pos++, c_arg_pos++) {
        GIDirection direction;
        GIArgInfo arg_info;
        bool arg_removed = false;

        g_callable_info_load_arg( (GICallableInfo*) function->info, gi_arg_pos, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION,
            "Processing argument '%s' (direction %d), %d/%d GI args, "
            "%d/%d C args, %d/%zu JS args",
            g_base_info_get_name(&arg_info), direction, gi_arg_pos, gi_argc,
            c_arg_pos, c_argc, js_arg_pos, args.length());

        g_assert_cmpuint(c_arg_pos, <, c_argc);
        ffi_arg_pointers[c_arg_pos] = &in_arg_cvalues[c_arg_pos];

        if (direction == GI_DIRECTION_OUT) {
            if (g_arg_info_is_caller_allocates(&arg_info)) {
                GITypeTag type_tag;
                GITypeInfo ainfo;

                g_arg_info_load_type(&arg_info, &ainfo);
                type_tag = g_type_info_get_tag(&ainfo);

                if (type_tag == GI_TYPE_TAG_INTERFACE) {
                    GIBaseInfo* interface_info;
                    GIInfoType interface_type;
                    gsize size;

                    interface_info = g_type_info_get_interface(&ainfo);
                    g_assert(interface_info != NULL);

                    interface_type = g_base_info_get_type(interface_info);

                    if (interface_type == GI_INFO_TYPE_STRUCT) {
                        size = g_struct_info_get_size((GIStructInfo*)interface_info);
                    } else if (interface_type == GI_INFO_TYPE_UNION) {
                        size = g_union_info_get_size((GIUnionInfo*)interface_info);
                    } else {
                        failed = true;
                    }

                    g_base_info_unref((GIBaseInfo*)interface_info);

                    if (!failed) {
                        in_arg_cvalues[c_arg_pos].v_pointer = g_slice_alloc0(size);
                        out_arg_cvalues[c_arg_pos].v_pointer = in_arg_cvalues[c_arg_pos].v_pointer;
                    }
                } else {
                    failed = true;
                }
                if (failed)
                    gjs_throw(context, "Unsupported type %s for (out caller-allocates)", g_type_tag_to_string(type_tag));
            } else {
                out_arg_cvalues[c_arg_pos].v_pointer = NULL;
                in_arg_cvalues[c_arg_pos].v_pointer = &out_arg_cvalues[c_arg_pos];
            }
        } else {
            GArgument *in_value;
            GITypeInfo ainfo;
            GjsParamType param_type;

            g_arg_info_load_type(&arg_info, &ainfo);

            in_value = &in_arg_cvalues[c_arg_pos];

            param_type = function->param_types[gi_arg_pos];

            switch (param_type) {
            case PARAM_CALLBACK: {
                GICallableInfo *callable_info;
                GIScopeType scope = g_arg_info_get_scope(&arg_info);
                GjsCallbackTrampoline *trampoline;
                ffi_closure *closure;
                JS::HandleValue current_arg = args[js_arg_pos];

                if (current_arg.isNull() && g_arg_info_may_be_null(&arg_info)) {
                    closure = NULL;
                    trampoline = NULL;
                } else {
                    if (!(JS_TypeOfValue(context, current_arg) == JSTYPE_FUNCTION)) {
                        gjs_throw(context, "Error invoking %s.%s: Expected function for callback argument %s, got %s",
                                  g_base_info_get_namespace( (GIBaseInfo*) function->info),
                                  g_base_info_get_name( (GIBaseInfo*) function->info),
                                  g_base_info_get_name( (GIBaseInfo*) &arg_info),
                                  JS::InformalValueTypeName(current_arg));
                        failed = true;
                        break;
                    }

                    JS::RootedFunction func(
                        context, JS_GetObjectFunction(&current_arg.toObject()));
                    callable_info = (GICallableInfo*) g_type_info_get_interface(&ainfo);
                    trampoline = gjs_callback_trampoline_new(
                        context, func, callable_info, scope,
                        is_object_method ? obj : nullptr, false);
                    closure = trampoline->closure;
                    g_base_info_unref(callable_info);
                }

                gint destroy_pos = g_arg_info_get_destroy(&arg_info);
                gint closure_pos = g_arg_info_get_closure(&arg_info);
                if (destroy_pos >= 0) {
                    gint c_pos = is_method ? destroy_pos + 1 : destroy_pos;
                    g_assert (function->param_types[destroy_pos] == PARAM_SKIPPED);
                    in_arg_cvalues[c_pos].v_pointer = trampoline ? (gpointer) gjs_destroy_notify_callback : NULL;
                }
                if (closure_pos >= 0) {
                    gint c_pos = is_method ? closure_pos + 1 : closure_pos;
                    g_assert (function->param_types[closure_pos] == PARAM_SKIPPED);
                    in_arg_cvalues[c_pos].v_pointer = trampoline;
                }

                if (trampoline && scope != GI_SCOPE_TYPE_CALL) {
                    /* Add an extra reference that will be cleared when collecting
                       async calls, or when GDestroyNotify is called */
                    gjs_callback_trampoline_ref(trampoline);
                }
                in_value->v_pointer = closure;
                break;
            }
            case PARAM_SKIPPED:
                arg_removed = true;
                break;
            case PARAM_ARRAY: {
                GIArgInfo array_length_arg;

                gint array_length_pos = g_type_info_get_array_length(&ainfo);
                gsize length;

                if (!gjs_value_to_explicit_array(context, args[js_arg_pos],
                                                 &arg_info, in_value, &length)) {
                    failed = true;
                    break;
                }

                g_callable_info_load_arg(function->info, array_length_pos, &array_length_arg);

                array_length_pos += is_method ? 1 : 0;
                JS::RootedValue v_length(context, JS::Int32Value(length));
                if (!gjs_value_to_arg(context, v_length, &array_length_arg,
                                      in_arg_cvalues + array_length_pos)) {
                    failed = true;
                    break;
                }
                /* Also handle the INOUT for the length here */
                if (direction == GI_DIRECTION_INOUT) {
                    if (in_value->v_pointer == NULL) { 
                        /* Special case where we were given JS null to
                         * also pass null for length, and not a
                         * pointer to an integer that derefs to 0.
                         */
                        in_arg_cvalues[array_length_pos].v_pointer = NULL;
                        out_arg_cvalues[array_length_pos].v_pointer = NULL;
                        inout_original_arg_cvalues[array_length_pos].v_pointer = NULL;
                    } else {
                        out_arg_cvalues[array_length_pos] = inout_original_arg_cvalues[array_length_pos] = *(in_arg_cvalues + array_length_pos);
                        in_arg_cvalues[array_length_pos].v_pointer = &out_arg_cvalues[array_length_pos];
                    }
                }
                break;
            }
            case PARAM_NORMAL: {
                /* Ok, now just convert argument normally */
                g_assert_cmpuint(js_arg_pos, <, args.length());
                if (!gjs_value_to_arg(context, args[js_arg_pos], &arg_info,
                                      in_value))
                    failed = true;

                break;
            }

            case PARAM_UNKNOWN:
                gjs_throw(context,
                          "Error invoking %s.%s: impossible to determine what "
                          "to pass to the '%s' argument. It may be that the "
                          "function is unsupported, or there may be a bug in "
                          "its annotations.",
                          g_base_info_get_namespace(function->info),
                          g_base_info_get_name(function->info),
                          g_base_info_get_name(&arg_info));
                failed = true;
                break;

            default:
                ;
            }

            if (direction == GI_DIRECTION_INOUT && !arg_removed && !failed) {
                out_arg_cvalues[c_arg_pos] = inout_original_arg_cvalues[c_arg_pos] = in_arg_cvalues[c_arg_pos];
                in_arg_cvalues[c_arg_pos].v_pointer = &out_arg_cvalues[c_arg_pos];
            }

            if (failed) {
                /* Exit from the loop */
                break;
            }

            if (!failed && !arg_removed)
                ++js_arg_pos;
        }

        if (failed)
            break;

        processed_c_args++;
    }

    /* Did argument conversion fail?  In that case, skip invocation and jump to release
     * processing. */
    if (failed) {
        did_throw_gerror = false;
        goto release;
    }

    if (can_throw_gerror) {
        g_assert_cmpuint(c_arg_pos, <, c_argc);
        in_arg_cvalues[c_arg_pos].v_pointer = &local_error;
        ffi_arg_pointers[c_arg_pos] = &(in_arg_cvalues[c_arg_pos]);
        c_arg_pos++;

        /* don't update processed_c_args as we deal with local_error
         * separately */
    }

    g_assert_cmpuint(c_arg_pos, ==, c_argc);
    g_assert_cmpuint(gi_arg_pos, ==, gi_argc);

    /* See comment for GjsFFIReturnValue above */
    if (return_tag == GI_TYPE_TAG_FLOAT)
        return_value_p = &return_value.v_float;
    else if (return_tag == GI_TYPE_TAG_DOUBLE)
        return_value_p = &return_value.v_double;
    else if (return_tag == GI_TYPE_TAG_INT64 || return_tag == GI_TYPE_TAG_UINT64)
        return_value_p = &return_value.v_uint64;
    else
        return_value_p = &return_value.v_long;
    ffi_call(&(function->invoker.cif), FFI_FN(function->invoker.native_address), return_value_p, ffi_arg_pointers);

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (can_throw_gerror) {
        did_throw_gerror = local_error != NULL;
    } else {
        did_throw_gerror = false;
    }

    if (js_rval)
        js_rval.ref().setUndefined();

    /* Only process return values if the function didn't throw */
    if (function->js_out_argc > 0 && !did_throw_gerror) {
        for (size_t i = 0; i < function->js_out_argc; i++)
            if (!return_values.append(JS::UndefinedValue()))
                g_error("Unable to append to vector");

        if (return_tag != GI_TYPE_TAG_VOID) {
            GITransfer transfer = g_callable_info_get_caller_owns((GICallableInfo*) function->info);
            bool arg_failed = false;
            gint array_length_pos;

            g_assert_cmpuint(next_rval, <, function->js_out_argc);

            gi_type_info_extract_ffi_return_value(&return_info, &return_value, &return_gargument);

            array_length_pos = g_type_info_get_array_length(&return_info);
            if (array_length_pos >= 0) {
                GIArgInfo array_length_arg;
                GITypeInfo arg_type_info;
                JS::RootedValue length(context);

                g_callable_info_load_arg(function->info, array_length_pos, &array_length_arg);
                g_arg_info_load_type(&array_length_arg, &arg_type_info);
                array_length_pos += is_method ? 1 : 0;
                arg_failed = !gjs_value_from_g_argument(context, &length,
                                                        &arg_type_info,
                                                        &out_arg_cvalues[array_length_pos],
                                                        true);
                if (!arg_failed && js_rval) {
                    arg_failed = !gjs_value_from_explicit_array(context,
                                                                return_values[next_rval],
                                                                &return_info,
                                                                &return_gargument,
                                                                length.toInt32());
                }
                if (!arg_failed &&
                    !r_value &&
                    !gjs_g_argument_release_out_array(context,
                                                      transfer,
                                                      &return_info,
                                                      length.toInt32(),
                                                      &return_gargument))
                    failed = true;
            } else {
                if (js_rval)
                    arg_failed = !gjs_value_from_g_argument(context,
                                                            return_values[next_rval],
                                                            &return_info, &return_gargument,
                                                            true);
                /* Free GArgument, the JS::Value should have ref'd or copied it */
                if (!arg_failed &&
                    !r_value &&
                    !gjs_g_argument_release(context,
                                            transfer,
                                            &return_info,
                                            &return_gargument))
                    failed = true;
            }
            if (arg_failed)
                failed = true;

            ++next_rval;
        }
    }

release:
    /* We walk over all args, release in args (if allocated) and convert
     * all out args to JS
     */
    c_arg_pos = is_method ? 1 : 0;
    postinvoke_release_failed = false;
    for (gi_arg_pos = 0; gi_arg_pos < gi_argc && c_arg_pos < processed_c_args; gi_arg_pos++, c_arg_pos++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo arg_type_info;
        GjsParamType param_type;

        g_callable_info_load_arg( (GICallableInfo*) function->info, gi_arg_pos, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        g_arg_info_load_type(&arg_info, &arg_type_info);
        param_type = function->param_types[gi_arg_pos];

        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT) {
            GArgument *arg;
            GITransfer transfer;

            if (direction == GI_DIRECTION_IN) {
                arg = &in_arg_cvalues[c_arg_pos];

                // If we failed before calling the function, or if the function
                // threw an exception, then any GI_TRANSFER_EVERYTHING or
                // GI_TRANSFER_CONTAINER parameters were not transferred. Treat
                // them as GI_TRANSFER_NOTHING so that they are freed.
                if (!failed && !did_throw_gerror)
                    transfer = g_arg_info_get_ownership_transfer(&arg_info);
                else
                    transfer = GI_TRANSFER_NOTHING;
            } else {
                arg = &inout_original_arg_cvalues[c_arg_pos];
                /* For inout, transfer refers to what we get back from the function; for
                 * the temporary C value we allocated, clearly we're responsible for
                 * freeing it.
                 */
                transfer = GI_TRANSFER_NOTHING;
            }

            gjs_debug_marshal(
                GJS_DEBUG_GFUNCTION,
                "Releasing in-argument '%s' (direction %d, transfer %d), "
                "%d/%d GI args, %d/%d C args",
                g_base_info_get_name(&arg_info), direction, transfer,
                gi_arg_pos, gi_argc, c_arg_pos, processed_c_args);

            if (param_type == PARAM_CALLBACK) {
                ffi_closure *closure = (ffi_closure *) arg->v_pointer;
                if (closure) {
                    GjsCallbackTrampoline *trampoline = (GjsCallbackTrampoline *) closure->user_data;
                    /* CallbackTrampolines are refcounted because for notified/async closures
                       it is possible to destroy it while in call, and therefore we cannot check
                       its scope at this point */
                    gjs_callback_trampoline_unref(trampoline);
                    arg->v_pointer = NULL;
                }
            } else if (param_type == PARAM_ARRAY) {
                gsize length;
                GIArgInfo array_length_arg;
                GITypeInfo array_length_type;
                gint array_length_pos = g_type_info_get_array_length(&arg_type_info);

                g_assert(array_length_pos >= 0);

                g_callable_info_load_arg(function->info, array_length_pos, &array_length_arg);
                g_arg_info_load_type(&array_length_arg, &array_length_type);

                array_length_pos += is_method ? 1 : 0;

                length = get_length_from_arg(in_arg_cvalues + array_length_pos,
                                             g_type_info_get_tag(&array_length_type));

                if (!gjs_g_argument_release_in_array(context,
                                                     transfer,
                                                     &arg_type_info,
                                                     length,
                                                     arg)) {
                    postinvoke_release_failed = true;
                }
            } else if (param_type == PARAM_NORMAL) {
                if (!gjs_g_argument_release_in_arg(context,
                                                   transfer,
                                                   &arg_type_info,
                                                   arg)) {
                    postinvoke_release_failed = true;
                }
            }
        }

        /* Don't free out arguments if function threw an exception or we failed
         * earlier - note "postinvoke_release_failed" is separate from "failed".  We
         * sync them up after this loop.
         */
        if (did_throw_gerror || failed)
            continue;

        if ((direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT) && param_type != PARAM_SKIPPED) {
            GArgument *arg;
            bool arg_failed = false;
            gint array_length_pos;
            JS::RootedValue array_length(context, JS::Int32Value(0));
            GITransfer transfer;

            g_assert(next_rval < function->js_out_argc);

            arg = &out_arg_cvalues[c_arg_pos];

            array_length_pos = g_type_info_get_array_length(&arg_type_info);

            if (js_rval) {
                if (array_length_pos >= 0) {
                    GIArgInfo array_length_arg;
                    GITypeInfo array_length_type_info;

                    g_callable_info_load_arg(function->info, array_length_pos, &array_length_arg);
                    g_arg_info_load_type(&array_length_arg, &array_length_type_info);
                    array_length_pos += is_method ? 1 : 0;
                    arg_failed = !gjs_value_from_g_argument(context, &array_length,
                                                            &array_length_type_info,
                                                            &out_arg_cvalues[array_length_pos],
                                                            true);
                    if (!arg_failed) {
                        arg_failed = !gjs_value_from_explicit_array(context,
                                                                    return_values[next_rval],
                                                                    &arg_type_info,
                                                                    arg,
                                                                    array_length.toInt32());
                    }
                } else {
                    arg_failed = !gjs_value_from_g_argument(context,
                                                            return_values[next_rval],
                                                            &arg_type_info,
                                                            arg,
                                                            true);
                }
            }

            if (arg_failed)
                postinvoke_release_failed = true;

            /* Free GArgument, the JS::Value should have ref'd or copied it */
            transfer = g_arg_info_get_ownership_transfer(&arg_info);
            if (!arg_failed) {
                if (array_length_pos >= 0) {
                    if (!gjs_g_argument_release_out_array(
                            context, transfer, &arg_type_info,
                            array_length.toInt32(), arg))
                        postinvoke_release_failed = true;
                } else {
                    if (!gjs_g_argument_release(context, transfer,
                                                &arg_type_info, arg))
                        postinvoke_release_failed = true;
                }
            }

            /* For caller-allocates, what happens here is we allocate
             * a structure above, then gjs_value_from_g_argument calls
             * g_boxed_copy on it, and takes ownership of that.  So
             * here we release the memory allocated above.  It would be
             * better to special case this and directly hand JS the boxed
             * object and tell gjs_boxed it owns the memory, but for now
             * this works OK.  We could also alloca() the structure instead
             * of slice allocating.
             */
            if (g_arg_info_is_caller_allocates(&arg_info)) {
                GITypeTag type_tag;
                GIBaseInfo* interface_info;
                GIInfoType interface_type;
                gsize size;

                type_tag = g_type_info_get_tag(&arg_type_info);
                g_assert(type_tag == GI_TYPE_TAG_INTERFACE);
                interface_info = g_type_info_get_interface(&arg_type_info);
                interface_type = g_base_info_get_type(interface_info);
                if (interface_type == GI_INFO_TYPE_STRUCT) {
                    size = g_struct_info_get_size((GIStructInfo*)interface_info);
                } else if (interface_type == GI_INFO_TYPE_UNION) {
                    size = g_union_info_get_size((GIUnionInfo*)interface_info);
                } else {
                    g_assert_not_reached();
                }

                g_slice_free1(size, out_arg_cvalues[c_arg_pos].v_pointer);
                g_base_info_unref((GIBaseInfo*)interface_info);
            }

            ++next_rval;
        }
    }

    if (postinvoke_release_failed)
        failed = true;

    g_assert(failed || did_throw_gerror || next_rval == (guint8)function->js_out_argc);
    g_assert_cmpuint(c_arg_pos, ==, processed_c_args);

    if (function->js_out_argc > 0 && (!failed && !did_throw_gerror)) {
        /* if we have 1 return value or out arg, return that item
         * on its own, otherwise return a JavaScript array with
         * [return value, out arg 1, out arg 2, ...]
         */
        if (js_rval) {
            if (function->js_out_argc == 1) {
                js_rval.ref().set(return_values[0]);
            } else {
                JSObject *array;
                array = JS_NewArrayObject(context, return_values);
                if (array == NULL) {
                    failed = true;
                } else {
                    js_rval.ref().setObject(*array);
                }
            }
        }

        if (r_value) {
            *r_value = return_gargument;
        }
    }

    if (!failed && did_throw_gerror) {
        return gjs_throw_gerror(context, local_error);
    } else if (failed) {
        return false;
    } else {
        return true;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool
function_call(JSContext *context,
              unsigned   js_argc,
              JS::Value *vp)
{
    GJS_GET_THIS(context, js_argc, vp, js_argv, object);
    JS::RootedObject callee(context, &js_argv.callee());

    bool success;
    Function *priv;
    JS::RootedValue retval(context);

    priv = priv_from_js(context, callee);
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Call callee %p priv %p this obj %p", callee.get(),
                      priv, object.get());

    if (priv == NULL)
        return true; /* we are the prototype, or have the wrong class */

    success = gjs_invoke_c_function(context, priv, object, js_argv,
                                    mozilla::Some<JS::MutableHandleValue>(&retval),
                                    NULL);
    if (success)
        js_argv.rval().set(retval);

    return success;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(function)

/* Does not actually free storage for structure, just
 * reverses init_cached_function_data
 */
static void
uninit_cached_function_data (Function *function)
{
    if (function->info)
        g_base_info_unref( (GIBaseInfo*) function->info);
    if (function->param_types)
        g_free(function->param_types);

    g_function_invoker_destroy(&function->invoker);
}

static void function_finalize(JSFreeOp*, JSObject* obj) {
    Function *priv;

    priv = (Function *) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    uninit_cached_function_data(priv);

    GJS_DEC_COUNTER(function);
    g_slice_free(Function, priv);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
get_num_arguments (JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    int n_args, n_jsargs, i;

    if (priv == NULL)
        return false;

    n_args = g_callable_info_get_n_args(priv->info);
    n_jsargs = 0;
    for (i = 0; i < n_args; i++) {
        GIArgInfo arg_info;

        if (priv->param_types[i] == PARAM_SKIPPED)
            continue;

        g_callable_info_load_arg(priv->info, i, &arg_info);

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_OUT)
            continue;

        n_jsargs++;
    }

    rec.rval().setInt32(n_jsargs);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
function_to_string (JSContext *context,
                    guint      argc,
                    JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    int i, n_args, n_jsargs;
    GString *arg_names_str;
    gchar *arg_names;

    if (priv == NULL) {
        JSString* retval = JS_NewStringCopyZ(context, "function () {\n}");
        if (!retval)
            return false;
        rec.rval().setString(retval);
        return true;
    }

    n_args = g_callable_info_get_n_args(priv->info);
    n_jsargs = 0;
    arg_names_str = g_string_new("");
    for (i = 0; i < n_args; i++) {
        GIArgInfo arg_info;

        if (priv->param_types[i] == PARAM_SKIPPED)
            continue;

        g_callable_info_load_arg(priv->info, i, &arg_info);

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_OUT)
            continue;

        if (n_jsargs > 0)
            g_string_append(arg_names_str, ", ");

        n_jsargs++;
        g_string_append(arg_names_str, g_base_info_get_name(&arg_info));
    }
    arg_names = g_string_free(arg_names_str, false);

    GjsAutoChar descr;
    if (g_base_info_get_type(priv->info) == GI_INFO_TYPE_FUNCTION) {
        descr = g_strdup_printf(
            "function %s(%s) {\n\t/* wrapper for native symbol %s(); */\n}",
            g_base_info_get_name(priv->info), arg_names,
            g_function_info_get_symbol(priv->info));
    } else {
        descr = g_strdup_printf(
            "function %s(%s) {\n\t/* wrapper for native symbol */\n}",
            g_base_info_get_name(priv->info), arg_names);
    }

    g_free(arg_names);

    return gjs_string_from_utf8(context, descr, rec.rval());
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_function_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    function_finalize,
    function_call};

struct JSClass gjs_function_class = {
    "GIRepositoryFunction", /* means "new GIRepositoryFunction()" works */
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
    &gjs_function_class_ops
};

static JSPropertySpec gjs_function_proto_props[] = {
    JS_PSG("length", get_num_arguments, JSPROP_PERMANENT),
    JS_PS_END
};

/* The original Function.prototype.toString complains when
   given a GIRepository function as an argument */
static JSFunctionSpec gjs_function_proto_funcs[] = {
    JS_FN("toString", function_to_string, 0, 0),
    JS_FS_END
};

static JSFunctionSpec *gjs_function_static_funcs = nullptr;

GJS_JSAPI_RETURN_CONVENTION
static bool
init_cached_function_data (JSContext      *context,
                           Function       *function,
                           GType           gtype,
                           GICallableInfo *info)
{
    guint8 i, n_args;
    int array_length_pos;
    GError *error = NULL;
    GITypeInfo return_type;
    GIInfoType info_type;

    info_type = g_base_info_get_type((GIBaseInfo *)info);

    if (info_type == GI_INFO_TYPE_FUNCTION) {
        if (!g_function_info_prep_invoker((GIFunctionInfo *)info,
                                          &(function->invoker),
                                          &error)) {
            return gjs_throw_gerror(context, error);
        }
    } else if (info_type == GI_INFO_TYPE_VFUNC) {
        gpointer addr;

        addr = g_vfunc_info_get_address((GIVFuncInfo *)info, gtype, &error);
        if (error != NULL) {
            if (error->code != G_INVOKE_ERROR_SYMBOL_NOT_FOUND)
                return gjs_throw_gerror(context, error);

            g_clear_error(&error);
            return false;
        }

        if (!g_function_invoker_new_for_address(addr, info,
                                                &(function->invoker),
                                                &error)) {
            return gjs_throw_gerror(context, error);
        }
    }

    g_callable_info_load_return_type((GICallableInfo*)info, &return_type);
    if (g_type_info_get_tag(&return_type) != GI_TYPE_TAG_VOID)
        function->js_out_argc += 1;

    n_args = g_callable_info_get_n_args((GICallableInfo*) info);
    function->param_types = g_new0(GjsParamType, n_args);

    array_length_pos = g_type_info_get_array_length(&return_type);
    if (array_length_pos >= 0 && array_length_pos < n_args)
        function->param_types[array_length_pos] = PARAM_SKIPPED;

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo type_info;
        int destroy = -1;
        int closure = -1;
        GITypeTag type_tag;

        if (function->param_types[i] == PARAM_SKIPPED)
            continue;

        g_callable_info_load_arg((GICallableInfo*) info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        direction = g_arg_info_get_direction(&arg_info);
        type_tag = g_type_info_get_tag(&type_info);

        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;

            interface_info = g_type_info_get_interface(&type_info);
            interface_type = g_base_info_get_type(interface_info);
            if (interface_type == GI_INFO_TYPE_CALLBACK) {
                if (strcmp(g_base_info_get_name(interface_info), "DestroyNotify") == 0 &&
                    strcmp(g_base_info_get_namespace(interface_info), "GLib") == 0) {
                    // We don't know (yet) what to do with GDestroyNotify
                    // appearing before a callback. If the callback comes later
                    // in the argument list, then PARAM_UNKNOWN will be
                    // overwritten with PARAM_SKIPPED. If no callback follows,
                    // then this is probably an unsupported function, so the
                    // value will remain PARAM_UNKNOWN.
                    function->param_types[i] = PARAM_UNKNOWN;
                } else {
                    function->param_types[i] = PARAM_CALLBACK;
                    function->expected_js_argc += 1;

                    destroy = g_arg_info_get_destroy(&arg_info);
                    closure = g_arg_info_get_closure(&arg_info);

                    if (destroy >= 0 && destroy < n_args)
                        function->param_types[destroy] = PARAM_SKIPPED;

                    if (closure >= 0 && closure < n_args)
                        function->param_types[closure] = PARAM_SKIPPED;

                    if (destroy >= 0 && closure < 0) {
                        gjs_throw(context, "Function %s.%s has a GDestroyNotify but no user_data, not supported",
                                  g_base_info_get_namespace( (GIBaseInfo*) info),
                                  g_base_info_get_name( (GIBaseInfo*) info));
                        g_base_info_unref(interface_info);
                        g_free(function->param_types);
                        return false;
                    }
                }
            }
            g_base_info_unref(interface_info);
        } else if (type_tag == GI_TYPE_TAG_ARRAY) {
            if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
                array_length_pos = g_type_info_get_array_length(&type_info);

                if (array_length_pos >= 0 && array_length_pos < n_args) {
                    GIArgInfo length_arg_info;

                    g_callable_info_load_arg((GICallableInfo*) info, array_length_pos, &length_arg_info);
                    if (g_arg_info_get_direction(&length_arg_info) != direction) {
                        gjs_throw(context, "Function %s.%s has an array with different-direction length arg, not supported",
                                  g_base_info_get_namespace( (GIBaseInfo*) info),
                                  g_base_info_get_name( (GIBaseInfo*) info));
                        g_free(function->param_types);
                        return false;
                    }

                    function->param_types[array_length_pos] = PARAM_SKIPPED;
                    function->param_types[i] = PARAM_ARRAY;

                    if (array_length_pos < i) {
                        /* we already collected array_length_pos, remove it */
                        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT)
                            function->expected_js_argc -= 1;
                        if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT)
                            function->js_out_argc -= 1;
                    }
                }
            }
        }

        if (function->param_types[i] == PARAM_NORMAL ||
            function->param_types[i] == PARAM_ARRAY) {
            if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT)
                function->expected_js_argc += 1;
            if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT)
                function->js_out_argc += 1;
        }
    }

    function->info = info;

    g_base_info_ref((GIBaseInfo*) function->info);

    return true;
}

GJS_USE
static inline JSObject *
gjs_builtin_function_get_proto(JSContext *cx)
{
    return JS::GetRealmFunctionPrototype(cx);
}

GJS_DEFINE_PROTO_FUNCS_WITH_PARENT(function, builtin_function)

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
function_new(JSContext      *context,
             GType           gtype,
             GICallableInfo *info)
{
    Function *priv;

    JS::RootedObject proto(context);
    if (!gjs_function_define_proto(context, nullptr, &proto))
        return nullptr;

    JS::RootedObject function(context,
        JS_NewObjectWithGivenProto(context, &gjs_function_class, proto));
    if (!function) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to construct function");
        return NULL;
    }

    priv = g_slice_new0(Function);

    GJS_INC_COUNTER(function);

    g_assert(priv_from_js(context, function) == NULL);
    JS_SetPrivate(function, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "function constructor, obj %p priv %p", function.get(),
                        priv);

    if (!init_cached_function_data(context, priv, gtype, (GICallableInfo *)info))
      return NULL;

    return function;
}

GJS_JSAPI_RETURN_CONVENTION
JSObject*
gjs_define_function(JSContext       *context,
                    JS::HandleObject in_object,
                    GType            gtype,
                    GICallableInfo  *info)
{
    GIInfoType info_type;
    gchar *name;
    bool free_name;

    info_type = g_base_info_get_type((GIBaseInfo *)info);

    JS::RootedObject function(context, function_new(context, gtype, info));
    if (!function)
        return NULL;

    if (info_type == GI_INFO_TYPE_FUNCTION) {
        name = (gchar *) g_base_info_get_name((GIBaseInfo*) info);
        free_name = false;
    } else if (info_type == GI_INFO_TYPE_VFUNC) {
        name = g_strdup_printf("vfunc_%s", g_base_info_get_name((GIBaseInfo*) info));
        free_name = true;
    } else {
        g_assert_not_reached ();
    }

    if (!JS_DefineProperty(context, in_object, name, function,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to define function");
        function = NULL;
    }

    if (free_name)
        g_free(name);

    return function;
}


bool
gjs_invoke_c_function_uncached(JSContext                  *context,
                               GIFunctionInfo             *info,
                               JS::HandleObject            obj,
                               const JS::HandleValueArray& args,
                               JS::MutableHandleValue      rval)
{
  Function function;
  bool result;

  memset (&function, 0, sizeof (Function));
  if (!init_cached_function_data (context, &function, 0, info))
      return false;

  result = gjs_invoke_c_function(context, &function, obj, args,
                                 mozilla::Some(rval), NULL);
  uninit_cached_function_data (&function);
  return result;
}

bool
gjs_invoke_constructor_from_c(JSContext                  *context,
                              JS::HandleObject            constructor,
                              JS::HandleObject            obj,
                              const JS::HandleValueArray& args,
                              GIArgument                 *rvalue)
{
    Function *priv;

    priv = priv_from_js(context, constructor);

    mozilla::Maybe<JS::MutableHandleValue> m_jsrval;
    return gjs_invoke_c_function(context, priv, obj, args, m_jsrval, rvalue);
}
