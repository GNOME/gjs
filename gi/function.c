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

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>
#include <girffi.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

typedef struct {
    GIFunctionInfo *info;
    guint n_destroy_notifies;
    guint expected_js_argc;
    guint js_out_argc;
    guint inout_argc;
    GIFunctionInvoker invoker;
} Function;

static struct JSClass gjs_function_class;

typedef struct {
    GSList *arguments; /* List of jsval, that need to be unrooted after call*/
    GSList *invoke_infos; /* gjs_callback_invoke_info_free for each*/
    gint arg_index;
} GjsCallbackInfo;

typedef struct {
    JSContext *context;
    GICallableInfo *info;
    jsval function;
    ffi_cif *cif;
    gboolean need_free_arg_types;
    ffi_closure *closure;
    GjsCallbackInfo callback_info;
/* We can not munmap closure in ffi_callback.
 * This memory page will be used after it.
 * So we need to know when GjsCallbackInvokeInfo becoming unneeded.*/
    gboolean in_use;
} GjsCallbackInvokeInfo;

static void gjs_callback_invoke_info_free_content(gpointer user_data);

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
gjs_callback_invoke_info_free(gpointer data)
{
    GjsCallbackInvokeInfo *invoke_info = (GjsCallbackInvokeInfo *)data;

    gjs_callback_invoke_info_free_content(invoke_info);
    if (invoke_info->info)
        g_base_info_unref((GIBaseInfo*)invoke_info->info);
    munmap(invoke_info->closure, sizeof(ffi_closure));
    if (invoke_info->need_free_arg_types)
        g_free(invoke_info->cif->arg_types);
    g_slice_free(ffi_cif, invoke_info->cif);

    g_slice_free(GjsCallbackInvokeInfo, data);
}

static void
gjs_callback_invoke_info_free_content(gpointer user_data)
{
    GSList *l;
    GjsCallbackInvokeInfo *invoke_info = (GjsCallbackInvokeInfo*)user_data;

    for (l = invoke_info->callback_info.arguments; l; l = g_slist_next(l)) {
        jsval *val = l->data;
        JS_RemoveRoot(invoke_info->context, val);
        g_slice_free(jsval, val);
    }
    g_slist_free(invoke_info->callback_info.arguments);
    invoke_info->callback_info.arguments = NULL;

    g_slist_foreach(invoke_info->callback_info.invoke_infos, (GFunc)gjs_callback_invoke_info_free, NULL);
    g_slist_free(invoke_info->callback_info.invoke_infos);
    invoke_info->callback_info.invoke_infos = NULL;
}

/* This is our main entry point for ffi_closure callbacks.
 * (there's a similar one for GDestroyNotify callbacks below)
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
    GjsCallbackInvokeInfo *invoke_info = (GjsCallbackInvokeInfo *) data;
    int i, n_args;
    jsval *jsargs, rval;
    GITypeInfo ret_type;

    g_assert(invoke_info != NULL);

    n_args = g_callable_info_get_n_args(invoke_info->info);

    g_assert(n_args >= 0);

    jsargs = (jsval*)g_new0(jsval, n_args);
    for (i = 0; i < n_args; i++) {
        GIArgInfo arg_info;
        GITypeInfo type_info;

        g_callable_info_load_arg(invoke_info->info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        if (g_type_info_get_tag(&type_info) == GI_TYPE_TAG_VOID) {
            jsargs[i] = *((jsval*)args[i]);
        } else if (!gjs_value_from_g_argument(invoke_info->context,
                                              &jsargs[i],
                                              &type_info,
                                              args[i])) {
            gjs_throw(invoke_info->context, "could not convert argument of callback");
            g_free(jsargs);
            return;
        }
    }

    if (!JS_CallFunctionValue(invoke_info->context,
                              NULL,
                              invoke_info->function,
                              n_args,
                              jsargs,
                              &rval)) {
        gjs_throw(invoke_info->context, "Couldn't call callback");
        g_free(jsargs);
        return;
    }

    g_callable_info_load_return_type(invoke_info->info, &ret_type);

    if (!gjs_value_to_g_argument(invoke_info->context,
                                 rval,
                                 &ret_type,
                                 "callback",
                                 GJS_ARGUMENT_RETURN_VALUE,
                                 FALSE,
                                 TRUE,
                                 result)) {
        gjs_throw(invoke_info->context, "Couldn't convert res value");
        g_free(jsargs);
        result = NULL;
        return;
    }
    g_free(jsargs);

    gjs_callback_invoke_info_free_content(invoke_info);
    invoke_info->in_use = FALSE;
}

/* For GI_SCOPE_TYPE_NOTIFIED callback we provide our own
 * ffi closure so we can easily get the user data instead
 * of always having to map the user_data argument on the
 * invokation side.
 */
static void
gjs_destroy_notify_callback_closure(ffi_cif *cif,
                                    void *result,
                                    void **args,
                                    void *data)
{
    GjsCallbackInvokeInfo *info = (GjsCallbackInvokeInfo *)data;

    info->in_use = FALSE;

    gjs_callback_invoke_info_free_content(info);
}

static gboolean
gjs_destroy_notify_create(GjsCallbackInvokeInfo *info)
{
    static const ffi_type *destroy_notify_args[] = {&ffi_type_pointer};
    ffi_status status;

    g_assert(info);

    info->need_free_arg_types = FALSE;

    info->closure = mmap(NULL, sizeof (ffi_closure),
                         PROT_EXEC | PROT_READ | PROT_WRITE,
                         MAP_ANON | MAP_PRIVATE, -1, sysconf (_SC_PAGE_SIZE));
    if (!info->closure) {
        gjs_throw(info->context, "mmap failed\n");
        return FALSE;
    }

    info->cif = g_slice_new(ffi_cif);

    status = ffi_prep_cif(info->cif, FFI_DEFAULT_ABI,
                          1, &ffi_type_void,
                          (ffi_type**)destroy_notify_args);
    if (status != FFI_OK) {
        gjs_throw(info->context, "ffi_prep_cif failed: %d\n", status);
        munmap(info->closure, sizeof (ffi_closure));
        return FALSE;
    }

    status = ffi_prep_closure(info->closure, info->cif,
                              gjs_destroy_notify_callback_closure, info);
    if (status != FFI_OK) {
        gjs_throw(info->context, "ffi_prep_cif failed: %d\n", status);
        munmap(info->closure, sizeof (ffi_closure));
        return FALSE;
    }

    if (mprotect(info->closure, sizeof (ffi_closure), PROT_READ | PROT_EXEC) == -1) {
        gjs_throw(info->context, "ffi_prep_closure failed: %s\n", strerror(errno));
        munmap(info->closure, sizeof (ffi_closure));
        return FALSE;
    }

    info->in_use = TRUE;

    return TRUE;
}

static GjsCallbackInvokeInfo*
gjs_callback_invoke_prepare(JSContext      *context,
                            jsval           function,
                            GICallableInfo *callable_info)
{
    GjsCallbackInvokeInfo *invoke_info;

    g_assert(JS_TypeOfValue(context, function) == JSTYPE_FUNCTION);

    invoke_info = g_slice_new0(GjsCallbackInvokeInfo);
    invoke_info->context = context;
    invoke_info->info = callable_info;

    g_base_info_ref((GIBaseInfo*)invoke_info->info);
    invoke_info->need_free_arg_types = TRUE;
    invoke_info->function = function;
    invoke_info->cif = g_slice_new0(ffi_cif);
    invoke_info->in_use = TRUE;
    invoke_info->callback_info.arguments = NULL;
    invoke_info->callback_info.invoke_infos = NULL;
    invoke_info->closure = g_callable_info_prepare_closure(callable_info, invoke_info->cif,
                                                           gjs_callback_closure, invoke_info);

    g_assert(invoke_info->closure);

    return invoke_info;
}

static void
gjs_callback_info_free (gpointer data)
{
    g_slice_free(GjsCallbackInfo, data);
}

static inline void
gjs_callback_info_add_argument(JSContext       *context,
                               GjsCallbackInfo *callback_info,
                               jsval            arg)
{
    jsval *v;
    /*JS_AddRoot/JS_RemoveRoot pair require same pointer*/
    v = g_slice_new(jsval);
    *v = arg;
    JS_AddRoot(context, v);
    callback_info->arguments = g_slist_prepend(callback_info->arguments, v);
}

static inline gboolean
gjs_callback_from_arguments(JSContext *context,
                            GIBaseInfo* interface_info,
                            GIArgInfo *arg_info,
                            guint8 current_arg_pos,
                            guint8 n_args,
                            guint8 *argv_pos,
                            uintN argc,
                            jsval *argv,
                            GSList **all_invoke_infos,
                            GSList **data_for_notify,
                            GSList **call_free_list,
                            gpointer *closure)
{
    GjsCallbackInvokeInfo *invoke_info;
    GSList *l;
    gboolean is_notify = FALSE;
    GjsCallbackInfo *callback_info = NULL;
    int arg_n;

    for (l = *data_for_notify; l; l = l->next) {
        GjsCallbackInfo *callback_info = l->data;
        if (callback_info->arg_index != current_arg_pos)
            continue;

        invoke_info = g_slice_new0(GjsCallbackInvokeInfo);
        invoke_info->context = context;

        if (!gjs_destroy_notify_create(invoke_info)) {
            g_slice_free(GjsCallbackInvokeInfo, invoke_info);
            return FALSE;
        }

        if (*argv_pos < argc) {
            gjs_callback_info_add_argument(context, callback_info, argv[*argv_pos]);
            (*argv_pos)--;
        }
        is_notify = TRUE;
        invoke_info->callback_info = *callback_info;
        *all_invoke_infos = g_slist_prepend(*all_invoke_infos, invoke_info);
        break;
    }

    if (is_notify)
        goto out;

    g_assert_cmpuint(*argv_pos, <, argc);
    if (JSVAL_IS_NULL(argv[*argv_pos]) || JSVAL_IS_VOID(argv[*argv_pos])) {
        *closure = NULL;
        return TRUE;
    }

    invoke_info = gjs_callback_invoke_prepare(context,
                                              argv[*argv_pos],
                                              (GICallableInfo*)interface_info);

    switch (g_arg_info_get_scope(arg_info)) {
        case GI_SCOPE_TYPE_CALL:
            *call_free_list = g_slist_prepend(*call_free_list, invoke_info);
            break;
        case GI_SCOPE_TYPE_NOTIFIED:
            g_assert(callback_info == NULL);
            callback_info = g_slice_new0(GjsCallbackInfo);

            g_assert(g_arg_info_get_destroy(arg_info) < n_args);

            gjs_callback_info_add_argument(context, callback_info, argv[*argv_pos]);
            arg_n = g_arg_info_get_closure(arg_info);
            if (arg_n > current_arg_pos && arg_n < n_args) {
                g_assert_cmpuint(arg_n, <, argc);
                gjs_callback_info_add_argument(context, callback_info, argv[arg_n]);
            }
            callback_info->arg_index = g_arg_info_get_destroy(arg_info);

            callback_info->invoke_infos = g_slist_prepend(callback_info->invoke_infos, invoke_info);
            *data_for_notify = g_slist_prepend(*data_for_notify, callback_info);
            break;
        case GI_SCOPE_TYPE_ASYNC:
            gjs_callback_info_add_argument(context, &invoke_info->callback_info, argv[*argv_pos]);

            arg_n = g_arg_info_get_closure(arg_info);
            if (arg_n > current_arg_pos && arg_n < n_args) {
                g_assert_cmpuint(arg_n, <, argc);
                gjs_callback_info_add_argument(context, &invoke_info->callback_info, argv[arg_n]);
            }
            *all_invoke_infos = g_slist_prepend(*all_invoke_infos, invoke_info);
            break;
        default:
            gjs_throw(context, "Unknown callback scope");
            gjs_callback_invoke_info_free(invoke_info);
            return FALSE;
    }

out:
    *closure = invoke_info->closure;

    return TRUE;
}

static void
gjs_free_unused_invoke_infos(GSList **invoke_infos)
{
    GSList *l, *node_for_delete = NULL;

    for (l = *invoke_infos; l; l = l->next) {
        GjsCallbackInvokeInfo *info = l->data;

         if (info->in_use)
             continue;

         gjs_callback_invoke_info_free(info);
         node_for_delete = g_slist_prepend(node_for_delete, l);
    }
    for (l = node_for_delete; l; l = l->next) {
        *invoke_infos = g_slist_delete_link(*invoke_infos, l->data);
     }
    g_slist_free(node_for_delete);
}

static JSBool
gjs_invoke_c_function(JSContext      *context,
                      Function       *function,
                      JSObject       *obj, /* "this" object */
                      uintN           argc,
                      jsval          *argv,
                      jsval          *rval)
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
    guint8 n_args, i, argv_pos;
    guint8 in_args_pos, out_args_pos, inout_args_pos;
    guint8 in_args_len, out_args_len, inout_args_len;
    guint8 can_throw_gerror, did_throw_gerror;
    GError *local_error = NULL;
    guint8 failed, postinvoke_release_failed;

    GIFunctionInfoFlags flags;
    gboolean is_method;
    GITypeInfo return_info;
    GITypeTag return_tag;
    jsval *return_values;
    guint8 next_rval;

    GSList *call_free_list = NULL; /* list of GjsCallbackInvokeInfo* */
    GSList *data_for_notify = NULL; /* list of GjsCallbackInfo* */
    GSList *callback_arg_indices = NULL; /* list of int */
    static GSList *invoke_infos = NULL;

    /* For async/notify callbacks we need to clean up some of the closure
     * data in follow up calls due to the way mmap works. ffi closures works
     * by rewriting the function trampolines, eg it modifies the page where
     * the function is mapped, that page cannot be released/mumapped as long
     * as we're currently using it, eg executing it. And as soon as we leave
     * that function we loose control, it goes back into the control of the
     * library where the function is called.
     */
    gjs_free_unused_invoke_infos(&invoke_infos);

    flags = g_function_info_get_flags(function->info);
    is_method = (flags & GI_FUNCTION_IS_METHOD) != 0;
    can_throw_gerror = (flags & GI_FUNCTION_THROWS) != 0;

    /* We allow too many args; convenient for re-using a function as a callback.
     * But we don't allow too few args, since that would break.
     */
    if (argc < function->expected_js_argc) {
        gjs_throw(context, "Too few arguments to %s %s.%s expected %d got %d",
                  is_method ? "method" : "function",
                  g_base_info_get_namespace( (GIBaseInfo*) function->info),
                  g_base_info_get_name( (GIBaseInfo*) function->info),
                  function->expected_js_argc,
                  argc);
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
    argv_pos = 0; /* index into argv */

    n_args = g_callable_info_get_n_args( (GICallableInfo*) function->info);

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

    processed_in_args = 0;
    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;

        /* gjs_debug(GJS_DEBUG_GFUNCTION, "i: %d in_args_pos: %d argv_pos: %d", i, in_args_pos, argv_pos); */

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
            gboolean convert_argument = TRUE;

            g_arg_info_load_type(&arg_info, &ainfo);
            type_tag = g_type_info_get_tag(&ainfo);

            g_assert_cmpuint(in_args_pos, <, in_args_len);
            in_value = &in_arg_cvalues[in_args_pos];

            if (g_slist_find(callback_arg_indices, GUINT_TO_POINTER((guint)i)) != NULL) {
                g_assert(type_tag == GI_TYPE_TAG_VOID);
                convert_argument = FALSE;

                g_assert_cmpuint(argv_pos, <, argc);
                in_value->v_pointer = (gpointer)argv[argv_pos];
            } else if (type_tag == GI_TYPE_TAG_VOID) {
                /* FIXME: notify/throw saying the callback annotation is wrong */
                convert_argument = FALSE;

                g_assert_cmpuint(argv_pos, <, argc);
                in_value->v_pointer = (gpointer)argv[argv_pos];
            } else if (type_tag == GI_TYPE_TAG_INTERFACE) {
                GIBaseInfo* interface_info;
                GIInfoType interface_type;

                interface_info = g_type_info_get_interface(&ainfo);

                g_assert(interface_info != NULL);

                interface_type = g_base_info_get_type(interface_info);
                if (interface_type == GI_INFO_TYPE_CALLBACK) {
                    if (!gjs_callback_from_arguments(context, interface_info, &arg_info,
                                                     i, n_args, &argv_pos, argc, argv,
                                                     &invoke_infos,
                                                     &data_for_notify, &call_free_list,
                                                     &(in_value->v_pointer))) {
                        failed = TRUE;
                        break;
                    }
                    callback_arg_indices = g_slist_prepend(callback_arg_indices,
                                                           GINT_TO_POINTER(g_arg_info_get_closure(&arg_info)));
                    convert_argument = FALSE;
                }

                g_base_info_unref(interface_info);
            }

            if (convert_argument) {
                g_assert_cmpuint(argv_pos, <, argc);
                if (!gjs_value_to_arg(context, argv[argv_pos], &arg_info,
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

            ++argv_pos;
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
    }

    g_assert(in_args_pos == (guint8)function->invoker.cif.nargs);
    ffi_call(&(function->invoker.cif), function->invoker.native_address, &return_value, in_arg_pointers);

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (can_throw_gerror) {
        did_throw_gerror = local_error != NULL;
    } else {
        did_throw_gerror = FALSE;
    }

    g_slist_foreach(call_free_list, (GFunc)gjs_callback_invoke_info_free, NULL);
    g_slist_free(call_free_list);
    g_slist_foreach(data_for_notify, (GFunc)gjs_callback_info_free, NULL);
    g_slist_free(data_for_notify);
    g_slist_free(callback_arg_indices);

    *rval = JSVAL_VOID;

    next_rval = 0; /* index into return_values */

    return_values = NULL; /* Quiet gcc warning about initialization */
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

    if (function->js_out_argc > 0 && (!failed && !did_throw_gerror)) {
        /* if we have 1 return value or out arg, return that item
         * on its own, otherwise return a JavaScript array with
         * [return value, out arg 1, out arg 2, ...]
         */
        if (function->js_out_argc == 1) {
            *rval = return_values[0];
        } else {
            JSObject *array;
            array = JS_NewArrayObject(context,
                                      function->js_out_argc,
                                      return_values);
            if (array == NULL) {
                failed = TRUE;
            } else {
                *rval = OBJECT_TO_JSVAL(array);
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
              uintN      argc,
              jsval     *argv,
              jsval     *rval)
{
    Function *priv;
    JSObject *callee;

    callee = JSVAL_TO_OBJECT(JS_ARGV_CALLEE(argv)); /* Callee is the Function object being called */

    priv = priv_from_js(context, callee);
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Call callee %p priv %p this obj %p %s", callee, priv,
                      obj, JS_GetTypeName(context,
                                          JS_TypeOfValue(context, OBJECT_TO_JSVAL(obj))));

    if (priv == NULL)
        return JS_TRUE; /* we are the prototype, or have the wrong class */

    return gjs_invoke_c_function(context, priv, obj, argc, argv, rval);
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

    if (priv->info)
        g_base_info_unref( (GIBaseInfo*) priv->info);

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
    GError *error = NULL;
    GITypeInfo return_type;

    if (!g_function_info_prep_invoker(info, &(function->invoker), &error)) {
        gjs_throw_g_error(context, error);
        return FALSE;
    }

    g_callable_info_load_return_type((GICallableInfo*)info, &return_type);
    if (g_type_info_get_tag(&return_type) != GI_TYPE_TAG_VOID)
      function->js_out_argc += 1;

    n_args = g_callable_info_get_n_args((GICallableInfo*) info);

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        guint8 destroy;

        g_callable_info_load_arg((GICallableInfo*) info, i, &arg_info);
        destroy = g_arg_info_get_destroy(&arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        if (destroy > 0 && destroy < n_args) {
            function->expected_js_argc -= 1;
            function->n_destroy_notifies += 1;
        }

        if (direction == GI_DIRECTION_IN || direction == GI_DIRECTION_INOUT)
            function->expected_js_argc += 1;
        if (direction == GI_DIRECTION_OUT || direction == GI_DIRECTION_INOUT)
            function->js_out_argc += 1;
        if (direction == GI_DIRECTION_INOUT)
            function->inout_argc += 1;
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

    function = function_new(load_context, info);
    if (function == NULL) {
        gjs_move_exception(load_context, context);
        return NULL;
    }

    if (!JS_DefineProperty(context, in_object,
                           g_base_info_get_name( (GIBaseInfo*) info),
                           OBJECT_TO_JSVAL(function),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to define function");
        return NULL;
    }

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

  memset (&function, 0, sizeof (Function));
  if (!init_cached_function_data (context, &function, info))
    return JS_FALSE;

  return gjs_invoke_c_function (context, &function, obj, argc, argv, rval);
}
