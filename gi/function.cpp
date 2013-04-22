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

#include "arg.h"
#include "arg-cache.h"
#include "function.h"
#include "object.h"
#include "fundamental.h"
#include "boxed.h"
#include "union.h"
#include "gerror.h"
#include "closure.h"
#include "gtype.h"
#include "param.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem.h"

#include <util/log.h>

#include <girepository.h>

#include <errno.h>
#include <string.h>

/* We use guint8 for arguments; functions can't
 * have more than this.
 */
#define GJS_ARG_INDEX_INVALID G_MAXUINT8

typedef struct {
    GIFunctionInfo *info;

    GjsArgumentCache *arguments;

    uint8_t js_in_argc;
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
                *(ffi_sarg *) result = return_value->v_long;
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

/* This is our main entry point for ffi_closure callbacks.
 * ffi_prep_closure is doing pure magic and replaces the original
 * function call with this one which gives us the ffi arguments,
 * a place to store the return value and our use data.
 * In other words, everything we need to call the JS function and
 * getting the return value back.
 */
static void
gjs_callback_closure(ffi_cif *cif,
                     void *result,
                     void **ffi_args,
                     void *data)
{
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

    context = gjs_closure_get_context(trampoline->js_function);
    if (G_UNLIKELY(_gjs_context_is_sweeping(context))) {
        g_critical("Attempting to call back into JSAPI during the sweeping phase of GC. "
                   "This is most likely caused by not destroying a Clutter actor or Gtk+ "
                   "widget with ::destroy signals connected, but can also be caused by "
                   "using the destroy(), dispose(), or remove() vfuncs. "
                   "Because it would crash the application, it has been "
                   "blocked and the JS callback not invoked.");
        if (trampoline->info) {
            const char *name = g_base_info_get_name(static_cast<GIBaseInfo *>(trampoline->info));
            g_critical("The offending callback was %s()%s.", name,
                       trampoline->is_vfunc ? ", a vfunc" : "");
        }
        gjs_dumpstack();
        gjs_callback_trampoline_unref(trampoline);
        return;
    }

    JS_BeginRequest(context);
    JSAutoCompartment ac(context,
                         gjs_closure_get_callable(trampoline->js_function));

    bool can_throw_gerror = g_callable_info_can_throw_gerror(trampoline->info);
    n_args = g_callable_info_get_n_args(trampoline->info);

    g_assert(n_args >= 0);

    JS::RootedObject this_object(context);
    if (trampoline->is_vfunc) {
        auto this_gobject = static_cast<GObject *>(args[0]->v_pointer);
        this_object = gjs_object_from_g_object(context, this_gobject);

        /* "this" is not included in the GI signature, but is in the C (and
         * FFI) signature */
        c_args_offset = 1;
    }

    n_outargs = 0;
    JS::AutoValueVector jsargs(context);

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
            case PARAM_NORMAL:
                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                if (!gjs_value_from_g_argument(context, jsargs[n_jsargs++],
                                               &type_info,
                                               args[i + c_args_offset],
                                               false))
                    goto out;
                break;
            case PARAM_CALLBACK:
                /* Callbacks that accept another callback as a parameter are not
                 * supported, see gjs_callback_trampoline_new() */
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
            GITypeInfo type_info;
            g_callable_info_load_arg(trampoline->info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            g_arg_info_load_type(&arg_info, &type_info);
            if (!gjs_value_to_g_argument(context,
                                         rval,
                                         &type_info,
                                         "callback",
                                         GJS_ARGUMENT_ARGUMENT,
                                         GI_TRANSFER_NOTHING,
                                         true,
                                         *(GIArgument **)args[i + c_args_offset]))
                goto out;

            break;
        }
    } else {
        JS::RootedValue elem(context);
        JS::RootedObject out_array(context, rval.toObjectOrNull());
        gsize elem_idx = 0;
        /* more than one of a return value or an out argument.
         * Should be an array of output values. */

        if (!ret_type_is_void) {
            GIArgument argument;

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                goto out;

            if (!gjs_value_to_g_argument(context,
                                         elem,
                                         &ret_type,
                                         "callback",
                                         GJS_ARGUMENT_ARGUMENT,
                                         GI_TRANSFER_NOTHING,
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
            GITypeInfo type_info;
            g_callable_info_load_arg(trampoline->info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            g_arg_info_load_type(&arg_info, &type_info);
            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                goto out;

            if (!gjs_value_to_g_argument(context,
                                         elem,
                                         &type_info,
                                         "callback",
                                         GJS_ARGUMENT_ARGUMENT,
                                         GI_TRANSFER_NOTHING,
                                         true,
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
            auto gcx = static_cast<GjsContext *>(JS_GetContextPrivate(context));
            uint8_t code;
            if (_gjs_context_should_exit(gcx, &code))
                exit(code);

            /* Some other uncatchable exception, e.g. out of memory */
            exit(1);
        }

        /* Fill in the result with some hopefully neutral value */
        g_callable_info_load_return_type(trampoline->info, &ret_type);
        gjs_g_argument_init_default (context, &ret_type, (GArgument *) result);

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
        gjs_log_exception(context);
    }

    if (trampoline->scope == GI_SCOPE_TYPE_ASYNC) {
        completed_trampolines = g_slist_prepend(completed_trampolines, trampoline);
    }

    gjs_callback_trampoline_unref(trampoline);
    gjs_schedule_gc_if_needed(context);

    JS_EndRequest(context);
}

GjsCallbackTrampoline*
gjs_callback_trampoline_new(JSContext       *context,
                            JS::HandleValue  function,
                            GICallableInfo  *callable_info,
                            GIScopeType      scope,
                            JS::HandleObject scope_object,
                            bool             is_vfunc)
{
    GjsCallbackTrampoline *trampoline;
    int n_args, i;

    if (function.isNull()) {
        return NULL;
    }

    g_assert(JS_TypeOfValue(context, function) == JSTYPE_FUNCTION);

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
    trampoline->js_function = gjs_closure_new(context, &function.toObject(),
                                              g_base_info_get_name(callable_info),
                                              should_root);
    if (!should_root && scope_object)
        gjs_object_associate_closure(context, scope_object,
                                     trampoline->js_function);

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
                gjs_throw(context, "Callback accepts another callback as a parameter. This is not supported");
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
                        gjs_throw(context, "Callback has an array with different-direction length arg, not supported");
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
            if (!gjs_typecheck_gerror(context, obj, true))
                return false;

            out_arg->v_pointer = gjs_gerror_from_error(context, obj);
            if (transfer == GI_TRANSFER_EVERYTHING)
                out_arg->v_pointer = g_error_copy ((GError*) out_arg->v_pointer);
        } else if (type == GI_INFO_TYPE_STRUCT &&
                   g_struct_info_is_gtype_struct((GIStructInfo*) container)) {
            /* And so do GType structures */
            GType actual_gtype;
            gpointer klass;

            actual_gtype = gjs_gtype_get_actual_gtype(context, obj);

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
            if (!gjs_typecheck_boxed(context, obj, container, gtype, true))
                return false;

            out_arg->v_pointer = gjs_c_struct_from_boxed(context, obj);
            if (transfer == GI_TRANSFER_EVERYTHING) {
                if (gtype != G_TYPE_NONE)
                    out_arg->v_pointer = g_boxed_copy (gtype, out_arg->v_pointer);
                else {
                    gjs_throw (context, "Cannot transfer ownership of instance argument for non boxed structure");
                    return false;
                }
            }
        }

    } else if (type == GI_INFO_TYPE_UNION) {
        if (!gjs_typecheck_union(context, obj, container, gtype, true))
            return false;

        out_arg->v_pointer = gjs_c_union_from_union(context, obj);
        if (transfer == GI_TRANSFER_EVERYTHING)
            out_arg->v_pointer = g_boxed_copy (gtype, out_arg->v_pointer);

    } else if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_INTERFACE) {
        if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
            if (!gjs_typecheck_object(context, obj, gtype, true))
                return false;
            out_arg->v_pointer = gjs_g_object_from_object(context, obj);
            is_gobject = true;
            if (transfer == GI_TRANSFER_EVERYTHING)
                g_object_ref (out_arg->v_pointer);
        } else if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            if (!gjs_typecheck_param(context, obj, G_TYPE_PARAM, true))
                return false;
            out_arg->v_pointer = gjs_g_param_from_param(context, obj);
            if (transfer == GI_TRANSFER_EVERYTHING)
                g_param_spec_ref ((GParamSpec*) out_arg->v_pointer);
        } else if (G_TYPE_IS_INTERFACE(gtype)) {
            if (gjs_typecheck_is_object(context, obj, false)) {
                if (!gjs_typecheck_object(context, obj, gtype, true))
                    return false;
                out_arg->v_pointer = gjs_g_object_from_object(context, obj);
                is_gobject = true;
                if (transfer == GI_TRANSFER_EVERYTHING)
                    g_object_ref (out_arg->v_pointer);
            } else {
                if (!gjs_typecheck_fundamental(context, obj, gtype, true))
                    return false;
                out_arg->v_pointer = gjs_g_fundamental_from_object(context, obj);
                if (transfer == GI_TRANSFER_EVERYTHING)
                    gjs_fundamental_ref (context, out_arg->v_pointer);
            }
        } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
            if (!gjs_typecheck_fundamental(context, obj, gtype, true))
                return false;
            out_arg->v_pointer = gjs_g_fundamental_from_object(context, obj);
            if (transfer == GI_TRANSFER_EVERYTHING)
                gjs_fundamental_ref (context, out_arg->v_pointer);
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

/*
 * This function can be called in 2 different ways. You can either use
 * it to create javascript objects by providing a @js_rval argument or
 * you can decide to keep the return values in #GArgument format by
 * providing a @r_value argument.
 */
static bool
gjs_invoke_c_function(JSContext                             *context,
                      Function                              *function,
                      JS::HandleObject                       obj, /* "this" object */
                      const JS::HandleValueArray&            args,
                      mozilla::Maybe<JS::MutableHandleValue> js_rval,
                      GIArgument                            *r_value)
{
    GjsFunctionCallState state;
    gpointer *ffi_arg_pointers;
    GIFFIReturnValue return_value;
    gpointer return_value_p; /* Will point inside the union return_value */

    int processed_c_args = 0;
    int gi_argc, gi_arg_pos;
    int ffi_argc, ffi_arg_pos;
    int js_arg_pos;
    bool can_throw_gerror;
    bool did_throw_gerror = false;
    GError *local_error = nullptr, **errorp;
    bool failed, postinvoke_release_failed;

    bool is_method;
    GITypeTag return_tag;
    GSList *iter;

    /* Because we can't free a closure while we're in it, we defer
     * freeing until the next time a C function is invoked.  What
     * we should really do instead is queue it for a GC thread.
     */
    if (completed_trampolines) {
        for (iter = completed_trampolines; iter; iter = iter->next) {
            GjsCallbackTrampoline *trampoline = (GjsCallbackTrampoline *) iter->data;
            gjs_callback_trampoline_unref(trampoline);
        }
        g_slist_free(completed_trampolines);
        completed_trampolines = NULL;
    }

    is_method = g_callable_info_is_method(function->info);
    can_throw_gerror = g_callable_info_can_throw_gerror(function->info);

    /* @c_argc is the number of arguments that the underlying C
     * function takes. @gi_argc is the number of arguments the
     * GICallableInfo describes (which does not include "this" or
     * GError**). @function->js_in_argc is the number of
     * arguments we expect the JS function to take (which does not
     * include PARAM_SKIPPED args).
     *
     * @js_argc is the number of arguments that were actually passed.
     */
    if (args.length() > function->js_in_argc) {
        GjsAutoChar name = format_function_name(function, is_method);
        JS_ReportWarningUTF8(context, "Too many arguments to %s: expected %d, "
                             "got %" G_GSIZE_FORMAT, name.get(),
                             function->js_in_argc, args.length());
    } else if (args.length() < function->js_in_argc) {
        GjsAutoChar name = format_function_name(function, is_method);
        gjs_throw(context, "Too few arguments to %s: "
                  "expected %d, got %" G_GSIZE_FORMAT,
                  name.get(), function->js_in_argc, args.length());
        return false;
    }

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
     *
     * The 3 GArgument arrays are indexed by the GI argument index,
     * with the following exceptions:
     * [-1] is the return value (which can be nothing/garbage if the
     * function returns void)
     * [-2] is the instance parameter, if present
     * ffi_arg_pointers, on the other hand, represents the actual
     * C arguments, in the way ffi expects them
     *
     * Use gi_arg_pos to index inside the GArgument array
     * Use ffi_arg_pos to index inside ffi_arg_pointers
    */

    ffi_argc = function->invoker.cif.nargs;
    gi_argc = g_callable_info_get_n_args( (GICallableInfo*) function->info);

    if (is_method) {
        state.in_cvalues = g_newa(GArgument, gi_argc + 2) + 2;
        state.out_cvalues = g_newa(GArgument, gi_argc + 2) + 2;
        state.inout_original_cvalues = g_newa(GArgument, gi_argc + 2) + 2;
    } else {
        state.in_cvalues = g_newa(GArgument, gi_argc + 1) + 1;
        state.out_cvalues = g_newa(GArgument, gi_argc + 1) + 1;
        state.inout_original_cvalues = g_newa(GArgument, gi_argc + 1) + 1;
    }

    ffi_arg_pointers = g_newa(gpointer, ffi_argc);

    failed = false;
    ffi_arg_pos = 0; /* index into ffi_arg_pointers */
    js_arg_pos = 0; /* index into argv */

    if (is_method) {
        GjsArgumentCache *cache = &function->arguments[-2];
        GIArgument *in_value = &state.in_cvalues[-2];
        JS::RootedValue in_js_value(context, JS::ObjectValue(*obj));

        if (!cache->marshal_in(context, cache, &state, in_value, in_js_value))
            return false;

        ffi_arg_pointers[ffi_arg_pos] = in_value;
        ffi_arg_pos++;
    }

    processed_c_args = ffi_arg_pos;
    for (gi_arg_pos = 0; gi_arg_pos < gi_argc; gi_arg_pos++, ffi_arg_pos++) {
        GjsArgumentCache *cache = &function->arguments[gi_arg_pos];
        GIArgument *in_value = &state.in_cvalues[gi_arg_pos];
        ffi_arg_pointers[ffi_arg_pos] = in_value;

        if (!cache->marshal_in(context, cache, &state, in_value,
                               args[js_arg_pos])) {
            failed = true;
            break;
        }

        if (!gjs_arg_cache_is_skip_in(cache))
            js_arg_pos++;

        processed_c_args++;
    }

    /* Did argument conversion fail?  In that case, skip invocation and jump to release
     * processing. */
    if (failed) {
        did_throw_gerror = false;
        goto release;
    }

    if (can_throw_gerror) {
        errorp = &local_error;
        ffi_arg_pointers[ffi_arg_pos] = &errorp;
        ffi_arg_pos++;

        /* don't update processed_c_args as we deal with local_error
         * separately */
    }

    g_assert_cmpuint(ffi_arg_pos, ==, ffi_argc);
    g_assert_cmpuint(gi_arg_pos, ==, gi_argc);

    if (!gjs_arg_cache_is_skip_out(&function->arguments[-1])) {
        return_tag = g_type_info_get_tag(&function->arguments[-1].type_info);

        /* See comment for GjsFFIReturnValue above */
        if (return_tag == GI_TYPE_TAG_FLOAT)
            return_value_p = &return_value.v_float;
        else if (return_tag == GI_TYPE_TAG_DOUBLE)
            return_value_p = &return_value.v_double;
        else if (return_tag == GI_TYPE_TAG_INT64 || return_tag == GI_TYPE_TAG_UINT64)
            return_value_p = &return_value.v_uint64;
        else
            return_value_p = &return_value.v_long;
    } else {
        return_value_p = nullptr;
    }

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

    if (!gjs_arg_cache_is_skip_out(&function->arguments[-1])) {
        gi_type_info_extract_ffi_return_value(&function->arguments[-1].type_info,
                                              &return_value,
                                              &state.out_cvalues[-1]);
    }

    if (function->js_out_argc > 1)
        js_rval->setObject(*JS_NewArrayObject(context, 0));

    /* Process out arguments and return values. This loop is skipped if we fail
     * the type conversion above, or if did_throw_gerror is true. */
    js_arg_pos = 0;
    // FIXME: js_arg_pos can overrun js_out_argc sometimes here
    for (gi_arg_pos = -1; gi_arg_pos < gi_argc; gi_arg_pos++) {
        GjsArgumentCache *cache = &function->arguments[gi_arg_pos];
        GIArgument *out_value = &state.out_cvalues[gi_arg_pos];

        JS::RootedValue value(context);
        if (!cache->marshal_out(context, cache, &state, out_value, &value)) {
            failed = true;
            break;
        }

        if (!gjs_arg_cache_is_skip_out(cache)) {
            if (function->js_out_argc == 1) {
                js_rval->set(value);
            } else {
                JS::RootedObject js_rval_array(context, &js_rval->toObject());
                JS_SetElement(context, js_rval_array, js_arg_pos, value);
            }

            js_arg_pos++;
        }
    }

    g_assert(failed || did_throw_gerror || js_arg_pos == uint8_t(function->js_out_argc));

release:
    /* In this loop we use ffi_arg_pos just to ensure we don't release stuff
     * we haven't allocated yet, if we failed in type conversion above.
     * If we start from -1 (the return value), we need to process 1 more than
     * processed_c_args.
     * If we start from -2 (the instance parameter), we need to process 2 more
    */
    ffi_arg_pos = is_method ? 1 : 0;
    postinvoke_release_failed = false;
    for (gi_arg_pos = is_method ? -2 : -1;
         gi_arg_pos < gi_argc &&
             ffi_arg_pos < (processed_c_args + (is_method ? 2 : 1));
         gi_arg_pos++, ffi_arg_pos++) {
        GjsArgumentCache *cache = &function->arguments[gi_arg_pos];
        GIArgument *in_value = &state.in_cvalues[gi_arg_pos];
        GIArgument *out_value = &state.out_cvalues[gi_arg_pos];

        /* Only process in or inout arguments if we failed, the rest is garbage */
        if (failed && gjs_arg_cache_is_skip_in(cache))
            continue;

        if (!cache->release(context, cache, &state, in_value, out_value)) {
            postinvoke_release_failed = true;
            /* continue with the release even if we fail, to avoid leaks */
        }
    }

    if (postinvoke_release_failed)
        failed = true;

    g_assert_cmpuint(ffi_arg_pos, ==, processed_c_args + (is_method ? 2 : 1));

    if (!failed && did_throw_gerror) {
        gjs_throw_g_error(context, local_error);
        return false;
    } else if (failed) {
        return false;
    } else {
        return true;
    }
}

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
    /* Careful! function->arguments is one/two inside an array */
    if (function->arguments) {
        bool is_method = g_callable_info_is_method(function->info);

        if (is_method)
            g_free(&function->arguments[-2]);
        else
            g_free(&function->arguments[-1]);
        function->arguments = nullptr;
    }

    g_clear_pointer(&function->info, g_base_info_unref);

    g_function_invoker_destroy(&function->invoker);
}

static void
function_finalize(JSFreeOp *fop,
                  JSObject *obj)
{
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

static bool
get_num_arguments (JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    rec.rval().setInt32(priv->js_in_argc);
    return true;
}

static bool
function_to_string (JSContext *context,
                    guint      argc,
                    JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    gchar *string;
    bool free;
    bool ret = false;
    int i, n_args, n_jsargs;
    GString *arg_names_str;
    gchar *arg_names;

    if (priv == NULL) {
        string = (gchar *) "function () {\n}";
        free = false;
        goto out;
    }

    free = true;

    n_args = g_callable_info_get_n_args(priv->info);
    n_jsargs = 0;
    arg_names_str = g_string_new("");
    for (i = 0; i < n_args; i++) {
        GIArgInfo arg_info;

        if (gjs_arg_cache_is_skip_in(&priv->arguments[i]))
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

    if (g_base_info_get_type(priv->info) == GI_INFO_TYPE_FUNCTION) {
        string = g_strdup_printf("function %s(%s) {\n\t/* proxy for native symbol %s(); */\n}",
                                 g_base_info_get_name ((GIBaseInfo *) priv->info),
                                 arg_names,
                                 g_function_info_get_symbol ((GIFunctionInfo *) priv->info));
    } else {
        string = g_strdup_printf("function %s(%s) {\n\t/* proxy for native symbol */\n}",
                                 g_base_info_get_name ((GIBaseInfo *) priv->info),
                                 arg_names);
    }

    g_free(arg_names);

 out:
    if (gjs_string_from_utf8(context, string, rec.rval()))
        ret = true;

    if (free)
        g_free(string);
    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_function_class_ops = {
    NULL,  /* addProperty */
    NULL,  /* deleteProperty */
    NULL,  /* getProperty */
    NULL,  /* setProperty */
    NULL,  /* enumerate */
    NULL,  /* resolve */
    nullptr,  /* mayResolve */
    function_finalize,
    function_call
};

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

static void
throw_not_introspectable_argument(JSContext      *cx,
                                  GICallableInfo *function,
                                  GIArgInfo      *arg)
{
    gjs_throw(cx, "Function %s.%s cannot be called: argument '%s' is not "
              "introspectable.",
              g_base_info_get_namespace(function),
              g_base_info_get_name(function),
              g_base_info_get_name(arg));
}

static bool
init_cached_function_data (JSContext      *context,
                           Function       *function,
                           GType           gtype,
                           GICallableInfo *info)
{
    guint8 i, n_args;
    GError *error = NULL;
    GIInfoType info_type;

    info_type = g_base_info_get_type((GIBaseInfo *)info);

    if (info_type == GI_INFO_TYPE_FUNCTION) {
        if (!g_function_info_prep_invoker((GIFunctionInfo *)info,
                                          &(function->invoker),
                                          &error)) {
            gjs_throw_g_error(context, error);
            return false;
        }
    } else if (info_type == GI_INFO_TYPE_VFUNC) {
        gpointer addr;

        addr = g_vfunc_info_get_address((GIVFuncInfo *)info, gtype, &error);
        if (error != NULL) {
            if (error->code != G_INVOKE_ERROR_SYMBOL_NOT_FOUND)
                gjs_throw_g_error(context, error);

            g_clear_error(&error);
            return false;
        }

        if (!g_function_invoker_new_for_address(addr, info,
                                                &(function->invoker),
                                                &error)) {
            gjs_throw_g_error(context, error);
            return false;
        }
    }

    bool is_method = g_callable_info_is_method(info);
    n_args = g_callable_info_get_n_args((GICallableInfo *) info);

    /* arguments is one or two inside an array of n_args + 2, so
     * arguments[-1] is the return value (which can be skipped if void)
     * arguments[-2] is the instance parameter */
    GjsArgumentCache *arguments;
    if (is_method)
        arguments = g_new0(GjsArgumentCache, n_args + 2) + 2;
    else
        arguments = g_new0(GjsArgumentCache, n_args + 1) + 1;

    if (is_method) {
        if (!gjs_arg_cache_build_instance(&arguments[-2], info)) {
            gjs_throw(context, "Function %s.%s cannot be called: the instance parameter is not introspectable.",
                      g_base_info_get_namespace((GIBaseInfo*) info),
                      g_base_info_get_name((GIBaseInfo*) info));
            return false;
        }
    }

    bool inc_counter;
    if (!gjs_arg_cache_build_return(&arguments[-1], arguments,
                                    info, inc_counter)) {
        gjs_throw(context, "Function %s.%s cannot be called: the return value "
                  "is not introspectable.",
                  g_base_info_get_namespace(info),
                  g_base_info_get_name(info));
        return false;
    }
    int out_argc = inc_counter ? 1 : 0;
    int in_argc = 0;

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;

        if (gjs_arg_cache_is_skip_in(&arguments[i]) ||
            gjs_arg_cache_is_skip_out(&arguments[i]))
            continue;

        g_callable_info_load_arg((GICallableInfo*) info, i, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        if (!gjs_arg_cache_build_arg(&arguments[i], arguments, i, direction,
                                     &arg_info, info, inc_counter)) {
            throw_not_introspectable_argument(context, info, &arg_info);
            return false;
        }

        if (inc_counter) {
            if (direction == GI_DIRECTION_IN) {
                in_argc++;
            } else if (direction == GI_DIRECTION_INOUT) {
                in_argc++;
                out_argc++;
            } else { /* GI_DIRECTION_OUT */
                out_argc++;
            }
        }
    }

    function->arguments = arguments;

    function->js_in_argc = in_argc;
    function->js_out_argc = out_argc;
    function->info = info;

    g_base_info_ref((GIBaseInfo*) function->info);

    return true;
}

static inline JSObject *
gjs_builtin_function_get_proto(JSContext *cx)
{
    JS::RootedObject global(cx, gjs_get_import_global(cx));
    return JS_GetFunctionPrototype(cx, global);
}

GJS_DEFINE_PROTO_FUNCS_WITH_PARENT(function, builtin_function)

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

    JSAutoRequest ar(context);

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
