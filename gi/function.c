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

#include "function.h"
#include "arg.h"
#include "object.h"
#include "boxed.h"
#include "union.h"
#include <gjs/gjs.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>
#include <girffi.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* We use guint8 for arguments; functions can't
 * have more than this.
 */
#define GJS_ARG_INDEX_INVALID G_MAXUINT8

typedef struct {
    GIFunctionInfo *info;

    /* We only support at most one of each of these */
    guint8 callback_index;
    guint8 destroy_notify_index;
    guint8 user_data_index;

    guint8 expected_js_argc;
    guint8 js_out_argc;
    guint8 inout_argc;
    GIFunctionInvoker invoker;
} Function;

static struct JSClass gjs_function_class;

/* Because we can't free the mmap'd data for a callback
 * while it's in use, this list keeps track of ones that
 * will be freed the next time we invoke a C function.
 */
static GSList *completed_trampolines = NULL;  /* GjsCallbackTrampoline */

static gboolean trampoline_globals_initialized = FALSE;
static struct {
    GICallableInfo *info;
    ffi_cif cif;
    ffi_closure *closure;
} global_destroy_trampoline;

typedef struct {
    JSContext *context;
    GICallableInfo *info;
    jsval js_function;
    ffi_cif cif;
    ffi_closure *closure;
    GIScopeType scope;
} GjsCallbackTrampoline;

GJS_DEFINE_PRIV_FROM_JS(Function, gjs_function_class)

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
function_new_resolve(JSContext *context,
                     JSObject  *obj,
                     jsval      id,
                     uintN      flags,
                     JSObject **objp)
{
    Function *priv;
    const char *name;

    *objp = NULL;

    if (!gjs_get_string_id(id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GFUNCTION, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_TRUE; /* we are the prototype, or have the wrong class */

    return JS_TRUE;
}

static void
gjs_callback_trampoline_free(GjsCallbackTrampoline *trampoline)
{
    JS_RemoveRoot(trampoline->context, &trampoline->js_function);
    g_callable_info_free_closure(trampoline->info, trampoline->closure);
    g_base_info_unref( (GIBaseInfo*) trampoline->info);
    g_slice_free(GjsCallbackTrampoline, trampoline);
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
                     void **args,
                     void *data)
{
    GjsCallbackTrampoline *trampoline;
    int i, n_args, n_jsargs;
    jsval *jsargs, rval;
    GITypeInfo ret_type;
    gboolean success = FALSE;

    trampoline = data;
    g_assert(trampoline);

    n_args = g_callable_info_get_n_args(trampoline->info);

    g_assert(n_args >= 0);

    jsargs = (jsval*)g_newa(jsval, n_args);
    for (i = 0, n_jsargs = 0; i < n_args; i++) {
        GIArgInfo arg_info;
        GITypeInfo type_info;

        g_callable_info_load_arg(trampoline->info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        /* Skip void * arguments */
        if (g_type_info_get_tag(&type_info) == GI_TYPE_TAG_VOID)
            continue;

        if (!gjs_value_from_g_argument(trampoline->context,
                                       &jsargs[n_jsargs++],
                                       &type_info,
                                       args[i]))
            goto out;
    }

    if (!JS_CallFunctionValue(trampoline->context,
                              NULL,
                              trampoline->js_function,
                              n_jsargs,
                              jsargs,
                              &rval)) {
        goto out;
    }

    g_callable_info_load_return_type(trampoline->info, &ret_type);

    if (!gjs_value_to_g_argument(trampoline->context,
                                 rval,
                                 &ret_type,
                                 "callback",
                                 GJS_ARGUMENT_RETURN_VALUE,
                                 FALSE,
                                 TRUE,
                                 result)) {
        goto out;
    }

    success = TRUE;

out:
    if (!success) {
        gjs_log_exception (trampoline->context, NULL);

        /* Fill in the result with some hopefully neutral value */
        g_callable_info_load_return_type(trampoline->info, &ret_type);
        gjs_g_argument_init_default (trampoline->context, &ret_type, result);
    }

    if (trampoline->scope == GI_SCOPE_TYPE_ASYNC) {
        completed_trampolines = g_slist_prepend(completed_trampolines, trampoline);
    }
}

/* The global entry point for any invocations of GDestroyNotify;
 * look up the callback through the user_data and then free it.
 */
static void
gjs_destroy_notify_callback_closure(ffi_cif *cif,
                                    void *result,
                                    void **args,
                                    void *data)
{
    GjsCallbackTrampoline *trampoline = *(void**)(args[0]);

    g_assert(trampoline);
    gjs_callback_trampoline_free(trampoline);
}

/* Called when we first see a function that uses a callback */
static void
gjs_init_callback_statics ()
{
    if (G_LIKELY(trampoline_globals_initialized))
      return;
    trampoline_globals_initialized = TRUE;

    global_destroy_trampoline.info = g_irepository_find_by_name(NULL, "GLib", "DestroyNotify");
    g_assert(global_destroy_trampoline.info != NULL);
    g_assert(g_base_info_get_type(global_destroy_trampoline.info) == GI_INFO_TYPE_CALLBACK);


    global_destroy_trampoline.closure = g_callable_info_prepare_closure(global_destroy_trampoline.info,
                                                                        &global_destroy_trampoline.cif,
                                                                        gjs_destroy_notify_callback_closure,
                                                                        NULL);
}

static GjsCallbackTrampoline*
gjs_callback_trampoline_new(JSContext      *context,
                            jsval           function,
                            GICallableInfo *callable_info,
                            GIScopeType     scope,
                            void          **destroy_notify)
{
    GjsCallbackTrampoline *trampoline;

    if (function == JSVAL_NULL) {
        *destroy_notify = NULL;
        return NULL;
    }

    g_assert(JS_TypeOfValue(context, function) == JSTYPE_FUNCTION);

    trampoline = g_slice_new(GjsCallbackTrampoline);
    trampoline->context = context;
    trampoline->info = callable_info;
    g_base_info_ref((GIBaseInfo*)trampoline->info);
    trampoline->js_function = function;
    JS_AddRoot(context, &trampoline->js_function);
    trampoline->closure = g_callable_info_prepare_closure(callable_info, &trampoline->cif,
                                                          gjs_callback_closure, trampoline);

    trampoline->scope = scope;
    if (scope == GI_SCOPE_TYPE_NOTIFIED) {
        *destroy_notify = global_destroy_trampoline.closure;
    } else {
        *destroy_notify = NULL;
    }

    return trampoline;
}

static JSBool
init_callback_args_for_invocation(JSContext               *context,
                                  Function                *function,
                                  guint8                   n_args,
                                  uintN                    js_argc,
                                  jsval                   *js_argv,
                                  GjsCallbackTrampoline  **trampoline_out,
                                  void                   **destroy_notify_out)
{
    GIArgInfo callback_arg;
    GITypeInfo callback_type;
    GITypeInfo *callback_info;
    GIScopeType scope;
    gboolean found_js_function;
    jsval js_function;
    guint8 i, js_argv_pos;

    if (function->callback_index == GJS_ARG_INDEX_INVALID) {
        *trampoline_out = *destroy_notify_out = NULL;
        return JS_TRUE;
    }

    g_callable_info_load_arg( (GICallableInfo*) function->info, function->callback_index, &callback_arg);
    scope = g_arg_info_get_scope(&callback_arg);
    g_arg_info_load_type(&callback_arg, &callback_type);
    g_assert(g_type_info_get_tag(&callback_type) == GI_TYPE_TAG_INTERFACE);
    callback_info = g_type_info_get_interface(&callback_type);
    g_assert(g_base_info_get_type(callback_info) == GI_INFO_TYPE_CALLBACK);

    /* Find the JS function passed for the callback */
    found_js_function = FALSE;
    js_function = JSVAL_NULL;
    for (i = 0, js_argv_pos = 0; i < n_args; i++) {
        if (i == function->callback_index) {
            js_function = js_argv[js_argv_pos];
            found_js_function = TRUE;
            break;
        } else if (i == function->user_data_index
                   || i == function->destroy_notify_index) {
            continue;
        }
        js_argv_pos++;
    }

    if (!found_js_function
        || !(js_function == JSVAL_NULL || JS_TypeOfValue(context, js_function) == JSTYPE_FUNCTION)) {
        gjs_throw(context, "Error invoking %s.%s: Invalid callback given for argument %s",
                  g_base_info_get_namespace( (GIBaseInfo*) function->info),
                  g_base_info_get_name( (GIBaseInfo*) function->info),
                  g_base_info_get_name( (GIBaseInfo*) &callback_arg));
        g_base_info_unref( (GIBaseInfo*) callback_info);
        return JS_FALSE;
    }

    *trampoline_out = gjs_callback_trampoline_new(context, js_function,
                                                  (GICallbackInfo*) callback_info,
                                                  scope,
                                                  destroy_notify_out);
    g_base_info_unref( (GIBaseInfo*) callback_info);
    return JS_TRUE;
}

static JSBool
gjs_invoke_c_function(JSContext      *context,
                      Function       *function,
                      JSObject       *obj, /* "this" object */
                      uintN           js_argc,
                      jsval          *js_argv,
                      jsval          *js_rval)
{
    /* These first four are arrays which hold argument pointers.
     * @in_arg_cvalues: C values which are passed on input (in or inout)
     * @out_arg_cvalues: C values which are returned as arguments (out or inout)
     * @inout_original_arg_cvalues: For the special case of (inout) args, we need to
     *  keep track of the original values we passed into the function, in case we
     *  need to free it.
     * @in_arg_pointers: For passing data to FFI, we need to create another layer
     *  of indirection; this array is a pointer to an element in in_arg_cvalues
     *  or out_arg_cvalues.
     * @return_value: The actual return value of the C function, i.e. not an (out) param
     */
    GArgument *in_arg_cvalues;
    GArgument *out_arg_cvalues;
    GArgument *inout_original_arg_cvalues;
    gpointer *in_arg_pointers;
    GArgument return_value;

    guint8 processed_in_args;
    guint8 n_args, i, js_argv_pos;
    guint8 in_args_pos, out_args_pos, inout_args_pos;
    guint8 in_args_len, out_args_len, inout_args_len;
    guint8 can_throw_gerror, did_throw_gerror;
    GError *local_error = NULL;
    guint8 failed, postinvoke_release_failed;

    GIFunctionInfoFlags flags;
    gboolean is_method;
    GITypeInfo return_info;
    GITypeTag return_tag;
    jsval *return_values = NULL;
    guint8 next_rval = 0; /* index into return_values */
    GSList *iter;
    GjsCallbackTrampoline *callback_trampoline;
    void *destroy_notify;

    /* Because we can't free a closure while we're in it, we defer
     * freeing until the next time a C function is invoked.  What
     * we should really do instead is queue it for a GC thread.
     */
    if (completed_trampolines) {
        for (iter = completed_trampolines; iter; iter = iter->next) {
            GjsCallbackTrampoline *trampoline = iter->data;
            gjs_callback_trampoline_free(trampoline);
        }
        g_slist_free(completed_trampolines);
        completed_trampolines = NULL;
    }

    flags = g_function_info_get_flags(function->info);
    is_method = (flags & GI_FUNCTION_IS_METHOD) != 0;
    can_throw_gerror = (flags & GI_FUNCTION_THROWS) != 0;
    n_args = g_callable_info_get_n_args( (GICallableInfo*) function->info);

    /* We allow too many args; convenient for re-using a function as a callback.
     * But we don't allow too few args, since that would break.
     */
    if (js_argc < function->expected_js_argc) {
        gjs_throw(context, "Too few arguments to %s %s.%s expected %d got %d",
                  is_method ? "method" : "function",
                  g_base_info_get_namespace( (GIBaseInfo*) function->info),
                  g_base_info_get_name( (GIBaseInfo*) function->info),
                  function->expected_js_argc,
                  js_argc);
        return JS_FALSE;
    }

    /* Check if we have a callback; if so, process all the arguments (callback, destroy_notify, user_data)
     * at once to avoid having special cases in the main loop below.
     */
    if (!init_callback_args_for_invocation(context, function, n_args, js_argc, js_argv,
                                           &callback_trampoline, &destroy_notify)) {
        return JS_FALSE;
    }

    g_callable_info_load_return_type( (GICallableInfo*) function->info, &return_info);
    return_tag = g_type_info_get_tag(&return_info);

    in_args_len = function->invoker.cif.nargs;
    out_args_len = function->js_out_argc;
    inout_args_len = function->inout_argc;

    if (return_tag != GI_TYPE_TAG_VOID) {
        --out_args_len;
    }

    in_arg_cvalues = g_newa(GArgument, in_args_len);
    in_arg_pointers = g_newa(gpointer, in_args_len);
    out_arg_cvalues = g_newa(GArgument, out_args_len);
    inout_original_arg_cvalues = g_newa(GArgument, inout_args_len);

    failed = FALSE;
    in_args_pos = 0; /* index into in_arg_cvalues */
    out_args_pos = 0; /* index into out_arg_cvalues */
    inout_args_pos = 0; /* index into inout_original_arg_cvalues */
    js_argv_pos = 0; /* index into argv */

    if (is_method) {
        GIBaseInfo *container = g_base_info_get_container((GIBaseInfo *) function->info);
        GIInfoType type = g_base_info_get_type(container);

        g_assert_cmpuint(0, <, in_args_len);

        if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_BOXED) {
            in_arg_cvalues[0].v_pointer = gjs_c_struct_from_boxed(context, obj);
        } else if (type == GI_INFO_TYPE_UNION) {
            in_arg_cvalues[0].v_pointer = gjs_c_union_from_union(context, obj);
        } else { /* by fallback is always object */
            in_arg_cvalues[0].v_pointer = gjs_g_object_from_object(context, obj);
        }
        in_arg_pointers[0] = &in_arg_cvalues[0];
        ++in_args_pos;
    }

    processed_in_args = in_args_pos;
    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        gboolean arg_removed = FALSE;

        /* gjs_debug(GJS_DEBUG_GFUNCTION, "i: %d in_args_pos: %d argv_pos: %d", i, in_args_pos, js_argv_pos); */

        g_callable_info_load_arg( (GICallableInfo*) function->info, i, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        g_assert_cmpuint(in_args_pos, <, in_args_len);
        in_arg_pointers[in_args_pos] = &in_arg_cvalues[in_args_pos];

        if (direction == GI_DIRECTION_OUT) {
            g_assert_cmpuint(out_args_pos, <, out_args_len);
            g_assert_cmpuint(in_args_pos, <, in_args_len);

            out_arg_cvalues[out_args_pos].v_pointer = NULL;
            in_arg_cvalues[in_args_pos].v_pointer = &out_arg_cvalues[out_args_pos];
            out_args_pos++;
        } else {
            GArgument *in_value;
            GITypeTag type_tag;
            GITypeInfo ainfo;

            g_arg_info_load_type(&arg_info, &ainfo);
            type_tag = g_type_info_get_tag(&ainfo);

            g_assert_cmpuint(in_args_pos, <, in_args_len);
            in_value = &in_arg_cvalues[in_args_pos];

            /* First check for callback stuff */
            if (i == function->callback_index) {
                if (callback_trampoline)
                    in_value->v_pointer = callback_trampoline->closure;
                else
                    in_value->v_pointer = NULL;
            } else if (i == function->user_data_index) {
                in_value->v_pointer = callback_trampoline;
                arg_removed = TRUE;
            } else if (i == function->destroy_notify_index) {
                in_value->v_pointer = destroy_notify;
                arg_removed = TRUE;
            } else {
                /* Ok, now just convert argument normally */
                g_assert_cmpuint(js_argv_pos, <, js_argc);
                if (!gjs_value_to_arg(context, js_argv[js_argv_pos], &arg_info,
                                      in_value)) {
                    failed = TRUE;
                    break;
                }
            }

            if (!failed && direction == GI_DIRECTION_INOUT) {
                g_assert_cmpuint(in_args_pos, <, in_args_len);
                g_assert_cmpuint(out_args_pos, <, out_args_len);
                g_assert_cmpuint(inout_args_pos, <, inout_args_len);

                out_arg_cvalues[out_args_pos] = inout_original_arg_cvalues[inout_args_pos] = in_arg_cvalues[in_args_pos];
                in_arg_cvalues[in_args_pos].v_pointer = &out_arg_cvalues[out_args_pos];
                out_args_pos++;
                inout_args_pos++;
            }

            if (!arg_removed)
                ++js_argv_pos;
        }

        ++in_args_pos;
        processed_in_args = in_args_pos;

        if (failed)
            break;
    }

    /* Did argument conversion fail?  In that case, skip invocation and jump to release
     * processing. */
    if (failed) {
        did_throw_gerror = FALSE;
        goto release;
    }

    if (can_throw_gerror) {
        g_assert_cmpuint(in_args_pos, <, in_args_len);
        in_arg_cvalues[in_args_pos].v_pointer = &local_error;
        in_arg_pointers[in_args_pos] = &(in_arg_cvalues[in_args_pos]);
        in_args_pos++;

        /* don't update processed_in_args as we deal with local_error
         * separately */
    }

    g_assert_cmpuint(in_args_pos, ==, (guint8)function->invoker.cif.nargs);
    g_assert_cmpuint(inout_args_pos, ==, inout_args_len);
    g_assert_cmpuint(out_args_pos, ==, out_args_len);
    ffi_call(&(function->invoker.cif), function->invoker.native_address, &return_value, in_arg_pointers);

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (can_throw_gerror) {
        did_throw_gerror = local_error != NULL;
    } else {
        did_throw_gerror = FALSE;
    }

    *js_rval = JSVAL_VOID;

    /* Only process return values if the function didn't throw */
    if (function->js_out_argc > 0 && !did_throw_gerror) {
        return_values = g_newa(jsval, function->js_out_argc);
        gjs_set_values(context, return_values, function->js_out_argc, JSVAL_VOID);
        gjs_root_value_locations(context, return_values, function->js_out_argc);

        if (return_tag != GI_TYPE_TAG_VOID) {
            gboolean arg_failed;

            g_assert_cmpuint(next_rval, <, function->js_out_argc);
            arg_failed = !gjs_value_from_g_argument(context, &return_values[next_rval],
                                                    &return_info, (GArgument*)&return_value);
            if (arg_failed)
                failed = TRUE;

            /* Free GArgument, the jsval should have ref'd or copied it */
            if (!arg_failed &&
                !gjs_g_argument_release(context,
                                        g_callable_info_get_caller_owns((GICallableInfo*) function->info),
                                        &return_info,
                                        (GArgument*)&return_value))
                failed = TRUE;

            ++next_rval;
        }
    }

release:
    if (callback_trampoline &&
        callback_trampoline->scope == GI_SCOPE_TYPE_CALL) {
        gjs_callback_trampoline_free(callback_trampoline);
    }

    /* We walk over all args, release in args (if allocated) and convert
     * all out args to JS
     */
    in_args_pos = is_method ? 1 : 0; /* index into in_args */
    out_args_pos = 0;
    inout_args_pos = 0;

    postinvoke_release_failed = FALSE;
    for (i = 0; i < n_args && in_args_pos < processed_in_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo arg_type_info;

        g_callable_info_load_arg( (GICallableInfo*) function->info, i, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        g_arg_info_load_type(&arg_info, &arg_type_info);

        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT) {
            GArgument *arg;
            GITransfer transfer;

            if (direction == GI_DIRECTION_IN) {
                g_assert_cmpuint(in_args_pos, <, in_args_len);
                arg = &in_arg_cvalues[in_args_pos];
                transfer = g_arg_info_get_ownership_transfer(&arg_info);
            } else {
                g_assert_cmpuint(inout_args_pos, <, inout_args_len);
                arg = &inout_original_arg_cvalues[inout_args_pos];
                ++inout_args_pos;
                /* For inout, transfer refers to what we get back from the function; for
                 * the temporary C value we allocated, clearly we're responsible for
                 * freeing it.
                 */
                transfer = GI_TRANSFER_EVERYTHING;
            }
            if (!gjs_g_argument_release_in_arg(context,
                                               transfer,
                                               &arg_type_info,
                                               arg)) {
                postinvoke_release_failed = TRUE;
            }
        }

        ++in_args_pos;

        /* Don't free out arguments if function threw an exception or we failed
         * earlier - note "postinvoke_release_failed" is separate from "failed".  We
         * sync them up after this loop.
         */
        if (did_throw_gerror || failed)
            continue;

        if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT) {
            GArgument *arg;
            gboolean arg_failed;

            g_assert(next_rval < function->js_out_argc);

            g_assert_cmpuint(out_args_pos, <, out_args_len);
            arg = &out_arg_cvalues[out_args_pos];

            arg_failed = FALSE;
            if (!gjs_value_from_g_argument(context,
                                           &return_values[next_rval],
                                           &arg_type_info,
                                           arg)) {
                arg_failed = TRUE;
                postinvoke_release_failed = TRUE;
            }

            /* Free GArgument, the jsval should have ref'd or copied it */
            if (!arg_failed)
                gjs_g_argument_release(context,
                                       g_arg_info_get_ownership_transfer(&arg_info),
                                       &arg_type_info,
                                       arg);

            ++next_rval;
            ++out_args_pos;
        }
    }

    if (postinvoke_release_failed)
        failed = TRUE;

    g_assert(failed || did_throw_gerror || next_rval == (guint8)function->js_out_argc);
    g_assert_cmpuint(in_args_pos, ==, processed_in_args);

    if (!(did_throw_gerror || failed)) {
        g_assert_cmpuint(out_args_pos, ==, out_args_len);
        g_assert_cmpuint(inout_args_pos, ==, inout_args_len);
    }

    if (function->js_out_argc > 0 && (!failed && !did_throw_gerror)) {
        /* if we have 1 return value or out arg, return that item
         * on its own, otherwise return a JavaScript array with
         * [return value, out arg 1, out arg 2, ...]
         */
        if (function->js_out_argc == 1) {
            *js_rval = return_values[0];
        } else {
            JSObject *array;
            array = JS_NewArrayObject(context,
                                      function->js_out_argc,
                                      return_values);
            if (array == NULL) {
                failed = TRUE;
            } else {
                *js_rval = OBJECT_TO_JSVAL(array);
            }
        }

        gjs_unroot_value_locations(context, return_values, function->js_out_argc);
    }

    if (!failed && did_throw_gerror) {
        gjs_throw(context, "Error invoking %s.%s: %s",
                  g_base_info_get_namespace( (GIBaseInfo*) function->info),
                  g_base_info_get_name( (GIBaseInfo*) function->info),
                  local_error->message);
        g_error_free(local_error);
        return JS_FALSE;
    } else if (failed) {
        return JS_FALSE;
    } else {
        return JS_TRUE;
    }
}

/* this macro was introduced with JSFastNative in 2007 */
#ifndef JS_ARGV_CALLEE
#define JS_ARGV_CALLEE(argv)    ((argv)[-2])
#endif

static JSBool
function_call(JSContext *context,
              JSObject  *obj, /* "this" object, not the function object */
              uintN      js_argc,
              jsval     *js_argv,
              jsval     *rval)
{
    Function *priv;
    JSObject *callee;

    callee = JSVAL_TO_OBJECT(JS_ARGV_CALLEE(js_argv)); /* Callee is the Function object being called */

    priv = priv_from_js(context, callee);
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Call callee %p priv %p this obj %p %s", callee, priv,
                      obj, JS_GetTypeName(context,
                                          JS_TypeOfValue(context, OBJECT_TO_JSVAL(obj))));

    if (priv == NULL)
        return JS_TRUE; /* we are the prototype, or have the wrong class */

    return gjs_invoke_c_function(context, priv, obj, js_argc, js_argv, rval);
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
static JSBool
function_constructor(JSContext *context,
                     JSObject  *obj,
                     uintN      argc,
                     jsval     *argv,
                     jsval     *retval)
{
    Function *priv;

    priv = g_slice_new0(Function);

    GJS_INC_COUNTER(function);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "function constructor, obj %p priv %p", obj, priv);

    return JS_TRUE;
}

/* Does not actually free storage for structure, just
 * reverses init_cached_function_data
 */
static void
uninit_cached_function_data (Function *function)
{
    if (function->info)
        g_base_info_unref( (GIBaseInfo*) function->info);
    g_function_invoker_destroy(&function->invoker);
}

static void
function_finalize(JSContext *context,
                  JSObject  *obj)
{
    Function *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    uninit_cached_function_data(priv);

    GJS_DEC_COUNTER(function);
    g_slice_free(Function, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_function_class = {
    "GIRepositoryFunction", /* means "new GIRepositoryFunction()" works */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) function_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    function_finalize,
    NULL,
    NULL,
    function_call,
    NULL, NULL, NULL, NULL, NULL
};

static JSPropertySpec gjs_function_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_function_proto_funcs[] = {
    { NULL }
};

static gboolean
init_cached_function_data (JSContext      *context,
                           Function       *function,
                           GIFunctionInfo *info)
{
    guint8 i, n_args;
    gboolean is_method;
    GError *error = NULL;
    GITypeInfo return_type;

    if (!g_function_info_prep_invoker(info, &(function->invoker), &error)) {
        gjs_throw_g_error(context, error);
        return FALSE;
    }

    is_method = (g_function_info_get_flags(info) & GI_FUNCTION_IS_METHOD) != 0;

    g_callable_info_load_return_type((GICallableInfo*)info, &return_type);
    if (g_type_info_get_tag(&return_type) != GI_TYPE_TAG_VOID)
      function->js_out_argc += 1;

    n_args = g_callable_info_get_n_args((GICallableInfo*) info);

    function->callback_index = GJS_ARG_INDEX_INVALID;
    function->destroy_notify_index = GJS_ARG_INDEX_INVALID;
    function->user_data_index = GJS_ARG_INDEX_INVALID;

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo type_info;
        guint8 destroy, closure;
        GITypeTag type_tag;

        g_callable_info_load_arg((GICallableInfo*) info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);
        type_tag = g_type_info_get_tag(&type_info);
        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            GIBaseInfo* interface_info;
            GIInfoType interface_type;

            interface_info = g_type_info_get_interface(&type_info);
            interface_type = g_base_info_get_type(interface_info);
            if (interface_type == GI_INFO_TYPE_CALLBACK &&
                i != function->destroy_notify_index) {
                if (function->callback_index != GJS_ARG_INDEX_INVALID) {
                    gjs_throw(context, "Function %s.%s has multiple callbacks, not supported",
                              g_base_info_get_namespace( (GIBaseInfo*) info),
                              g_base_info_get_name( (GIBaseInfo*) info));
                    g_base_info_unref(interface_info);
                    return FALSE;
                }
                function->callback_index = i;
                gjs_init_callback_statics();
            }
            g_base_info_unref(interface_info);
        }
        destroy = g_arg_info_get_destroy(&arg_info);
        closure = g_arg_info_get_closure(&arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        if (destroy > 0 && destroy < n_args) {
            function->expected_js_argc -= 1;
            if (function->destroy_notify_index != GJS_ARG_INDEX_INVALID) {
                gjs_throw(context, "Function %s has multiple GDestroyNotify, not supported",
                          g_base_info_get_name((GIBaseInfo*)info));
                return FALSE;
            }
            function->destroy_notify_index = destroy;
        }

        if (closure > 0 && closure < n_args) {
            function->expected_js_argc -= 1;
            if (function->user_data_index != GJS_ARG_INDEX_INVALID) {
                gjs_throw(context, "Function %s has multiple user_data arguments, not supported",
                          g_base_info_get_name((GIBaseInfo*)info));
                return FALSE;
            }
            function->user_data_index = closure;
        }

        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT)
            function->expected_js_argc += 1;
        if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT)
            function->js_out_argc += 1;
        if (direction == GI_DIRECTION_INOUT)
            function->inout_argc += 1;
    }


    if (function->callback_index != GJS_ARG_INDEX_INVALID
        && function->destroy_notify_index != GJS_ARG_INDEX_INVALID
        && function->user_data_index == GJS_ARG_INDEX_INVALID) {
        gjs_throw(context, "Function %s.%s has a GDestroyNotify but no user_data, not supported",
                  g_base_info_get_namespace( (GIBaseInfo*) info),
                  g_base_info_get_name( (GIBaseInfo*) info));
        return JS_FALSE;
    }

    function->info = info;

    g_base_info_ref((GIBaseInfo*) function->info);

    return TRUE;
}

static JSObject*
function_new(JSContext      *context,
             GIFunctionInfo *info)
{
    JSObject *function;
    JSObject *global;
    Function *priv;

    /* put constructor for GIRepositoryFunction() in the global namespace */
    global = JS_GetGlobalObject(context);

    if (!gjs_object_has_property(context, global, gjs_function_class.name)) {
        JSObject *prototype;
        JSObject *parent_proto;

        parent_proto = NULL;

        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 parent_proto,
                                 &gjs_function_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 function_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_function_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_function_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            gjs_fatal("Can't init class %s", gjs_function_class.name);

        g_assert(gjs_object_has_property(context, global, gjs_function_class.name));

        gjs_debug(GJS_DEBUG_GFUNCTION, "Initialized class %s prototype %p",
                  gjs_function_class.name, prototype);
    }

    function = JS_ConstructObject(context, &gjs_function_class, NULL, NULL);
    if (function == NULL) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to construct function");
        return NULL;
    }

    priv = priv_from_js(context, function);
    if (!init_cached_function_data(context, priv, info))
      return NULL;

    return function;
}

JSObject*
gjs_define_function(JSContext      *context,
                    JSObject       *in_object,
                    GIFunctionInfo *info)
{
    JSObject *function;
    JSContext *load_context;

    load_context = gjs_runtime_get_load_context(JS_GetRuntime(context));
    JS_BeginRequest(load_context);

    function = function_new(load_context, info);
    if (function == NULL) {
        gjs_move_exception(load_context, context);

        JS_EndRequest(load_context);
        return NULL;
    }

    if (!JS_DefineProperty(context, in_object,
                           g_base_info_get_name( (GIBaseInfo*) info),
                           OBJECT_TO_JSVAL(function),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to define function");

        JS_EndRequest(load_context);
        return NULL;
    }

    JS_EndRequest(load_context);
    return function;
}


JSBool
gjs_invoke_c_function_uncached (JSContext      *context,
                                GIFunctionInfo *info,
                                JSObject       *obj,
                                uintN           argc,
                                jsval          *argv,
                                jsval          *rval)
{
  Function function;
  JSBool result;

  memset (&function, 0, sizeof (Function));
  if (!init_cached_function_data (context, &function, info))
    return JS_FALSE;

  result = gjs_invoke_c_function (context, &function, obj, argc, argv, rval);
  uninit_cached_function_data (&function);
  return result;
}
