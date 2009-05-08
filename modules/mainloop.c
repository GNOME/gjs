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

#include "mainloop.h"
#include <gjs/gjs.h>

#include "../gi/closure.h"

#include <util/log.h>

#include <glib.h>

#include <jsapi.h>

static GHashTable *pending_main_loops;

static JSBool
gjs_main_loop_quit(JSContext *context,
                      JSObject  *obj,
                      uintN      argc,
                      jsval     *argv,
                      jsval     *retval)
{
    const char *cancel_id;
    GMainLoop *main_loop;

    if (argc != 1 ||
        !JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context, "quit() takes one arg, the cancel id");
        return JS_FALSE;
    }

    cancel_id = gjs_string_get_ascii_checked(context, argv[0]);
    if (cancel_id == NULL)
        return JS_FALSE;

    main_loop = g_hash_table_lookup(pending_main_loops, cancel_id);

    if (!main_loop) {
        gjs_throw(context, "No main loop with this id");
        return JS_FALSE;
    }

    g_hash_table_remove(pending_main_loops, cancel_id);

    if (!g_main_loop_is_running(main_loop)) {
        gjs_throw(context, "Main loop was stopped already");
        return JS_FALSE;
    }

    gjs_debug(GJS_DEBUG_MAINLOOP,
              "main loop %s quitting in context %p",
              cancel_id,
              context);

    g_main_loop_quit(main_loop);
    return JS_TRUE;
}

static JSBool
gjs_main_loop_run(JSContext *context,
                     JSObject  *obj,
                     uintN      argc,
                     jsval     *argv,
                     jsval     *retval)
{
    const char *cancel_id;
    GMainLoop *main_loop;

    if (argc != 1 ||
        !JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context, "run() takes one arg, the cancel id");
        return JS_FALSE;
    }

    cancel_id = gjs_string_get_ascii_checked(context, argv[0]);
    if (cancel_id == NULL)
        return JS_FALSE;

    main_loop = g_hash_table_lookup(pending_main_loops, cancel_id);

    if (!main_loop) {
        main_loop = g_main_loop_new(NULL, FALSE);
        g_hash_table_replace(pending_main_loops, g_strdup(cancel_id), main_loop);
    } else {
        g_main_loop_ref(main_loop);
    }

    gjs_debug(GJS_DEBUG_MAINLOOP,
              "main loop %s being run in context %p",
              cancel_id,
              context);

    g_main_loop_run(main_loop);
    g_main_loop_unref(main_loop);
    return JS_TRUE;
}

static gboolean
closure_source_func(void *data)
{
    jsval retval;
    GClosure *closure;
    JSBool bool_val;
    JSContext *context;

    closure = data;

    context = gjs_closure_get_context(closure);
    if (context == NULL) {
        /* closure is invalid now */
        return FALSE;
    }

    retval = JSVAL_VOID;
    JS_AddRoot(context, &retval);

    gjs_closure_invoke(closure,
                          0, NULL,
                          &retval);

    /* ValueToBoolean pretty much always succeeds, just as
     * JavaScript always makes some sense of any value in
     * an "if (value) {}" context.
     */
    if (!JS_ValueToBoolean(gjs_closure_get_context(closure),
                           retval, &bool_val))
        bool_val = FALSE;

    JS_RemoveRoot(context, &retval);

    return bool_val;
}

static void
closure_destroy_notify(void *data)
{
    GClosure *closure;

    closure = data;

    g_closure_invalidate(closure);
    g_closure_unref(closure);
}

static void
closure_invalidated(gpointer  data,
                    GClosure *closure)
{
    /* We may be invalidated because the source was removed and
     * destroy notify called, or we may be invalidated for an external
     * reason like JSContext destroy, in which case removing the
     * source here will call our destroy notify.
     *
     * If we're invalidated due to the source being removed, then
     * removing it here will be a no-op.
     */
    g_source_remove(GPOINTER_TO_UINT(data));
}

static JSBool
gjs_timeout_add(JSContext *context,
                   JSObject  *obj,
                   uintN      argc,
                   jsval     *argv,
                   jsval     *retval)
{
    GClosure *closure;
    guint interval;
    guint id;

    /* Best I can tell, there is no way to know if argv[1] is really
     * callable other than to just try it. Checking whether it's a
     * function will not detect native objects that provide
     * JSClass::call, for example.
     */

    if (argc != 2) {
        gjs_throw(context, "timeout_add() takes two args, the interval and the timeout callback");
        return JS_FALSE;
    }

    if (!JSVAL_IS_INT(argv[0])) {
        gjs_throw(context, "The interval for timeout_add must be an integer");
        return JS_FALSE;
    }

    if (!JSVAL_IS_OBJECT(argv[1])) {
        gjs_throw(context, "The callback for timeout_add is not callable");
        return JS_FALSE;
    }

    closure = gjs_closure_new(context, JSVAL_TO_OBJECT(argv[1]), "timeout");
    if (closure == NULL)
        return JS_FALSE;

    g_closure_ref(closure);
    g_closure_sink(closure);

    interval = JSVAL_TO_INT(argv[0]);

    id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                            interval,
                            closure_source_func,
                            closure,
                            closure_destroy_notify);

    /* this is needed to remove the timeout if the JSContext is
     * destroyed.
     */
    g_closure_add_invalidate_notifier(closure, GUINT_TO_POINTER(id),
                                      closure_invalidated);

    if (!JS_NewNumberValue(context, id, retval))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
gjs_timeout_add_seconds(JSContext *context,
                           JSObject  *obj,
                           uintN      argc,
                           jsval     *argv,
                           jsval     *retval)
{
    GClosure *closure;
    guint interval;
    guint id;

    /* Best I can tell, there is no way to know if argv[1] is really
     * callable other than to just try it. Checking whether it's a
     * function will not detect native objects that provide
     * JSClass::call, for example.
     */

    if (argc != 2) {
        gjs_throw(context, "timeout_add_seconds() takes two args, the interval and the timeout callback");
        return JS_FALSE;
    }

    if (!JSVAL_IS_INT(argv[0])) {
        gjs_throw(context, "The interval for timeout_add_seconds must be an integer");
        return JS_FALSE;
    }

    if (!JSVAL_IS_OBJECT(argv[1])) {
        gjs_throw(context, "The callback for timeout_add_seconds is not callable");
        return JS_FALSE;
    }

    closure = gjs_closure_new(context, JSVAL_TO_OBJECT(argv[1]), "timeout_seconds");
    if (closure == NULL)
        return JS_FALSE;

    g_closure_ref(closure);
    g_closure_sink(closure);

    interval = JSVAL_TO_INT(argv[0]);

    id = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
                                    interval,
                                    closure_source_func,
                                    closure,
                                    closure_destroy_notify);

    /* this is needed to remove the timeout if the JSContext is
     * destroyed.
     */
    g_closure_add_invalidate_notifier(closure, GUINT_TO_POINTER(id),
                                      closure_invalidated);

    if (!JS_NewNumberValue(context, id, retval))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
gjs_idle_add(JSContext *context,
                JSObject  *obj,
                uintN      argc,
                jsval     *argv,
                jsval     *retval)
{
    GClosure *closure;
    guint id;
    int priority;

    /* Best I can tell, there is no way to know if argv[0] is really
     * callable other than to just try it. Checking whether it's a
     * function will not detect native objects that provide
     * JSClass::call, for example.
     */

    if (argc < 1 ||
        !JSVAL_IS_OBJECT(argv[0]) ||
        (argc >= 2 && !JSVAL_IS_INT(argv[1]))) {
        gjs_throw(context, "idle_add() takes the idle callback and an optional priority");
        return JS_FALSE;
    }

    if (argc >= 2) {
        priority = JSVAL_TO_INT(argv[1]);
    } else {
        priority = G_PRIORITY_DEFAULT_IDLE;
    }

    closure = gjs_closure_new(context, JSVAL_TO_OBJECT(argv[0]), "idle");
    if (closure == NULL)
        return JS_FALSE;

    g_closure_ref(closure);
    g_closure_sink(closure);



    id = g_idle_add_full(priority,
                         closure_source_func,
                         closure,
                         closure_destroy_notify);

    /* this is needed to remove the idle if the JSContext is
     * destroyed.
     */
    g_closure_add_invalidate_notifier(closure, GUINT_TO_POINTER(id),
                                      closure_invalidated);

    if (!JS_NewNumberValue(context, id, retval))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
gjs_source_remove(JSContext *context,
                     JSObject  *obj,
                     uintN      argc,
                     jsval     *argv,
                     jsval     *retval)
{
    gboolean success;

    if (argc != 1 ||
        !JSVAL_IS_INT(argv[0])) {
        gjs_throw(context, "source_remove() takes one arg, the integer source id");
        return JS_FALSE;
    }

    success = g_source_remove(JSVAL_TO_INT(argv[0]));

    *retval = BOOLEAN_TO_JSVAL(success);

    return JS_TRUE;
}

JSBool
gjs_define_mainloop_stuff(JSContext      *context,
                             JSObject      *module_obj)
{
    // FIXME this should be per JSObject, and we should free it when
    // the object is finalized
    pending_main_loops = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (!JS_DefineFunction(context, module_obj,
                           "run",
                           gjs_main_loop_run,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "quit",
                           gjs_main_loop_quit,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "idle_add",
                           gjs_idle_add,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "timeout_add",
                           gjs_timeout_add,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "timeout_add_seconds",
                           gjs_timeout_add_seconds,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "source_remove",
                           gjs_source_remove,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("mainloop", gjs_define_mainloop_stuff)
