/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  LiTL, LLC
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

#include <limits.h>
#include <util/log.h>

#include "closure.h"
#include "keep-alive.h"
#include <gjs/mem.h>
#include <gjs/jsapi-util.h>

typedef struct {
    GClosure base;
    JSRuntime *runtime;
    JSContext *context;
    JSObject *obj;
} Closure;

/*
 * Memory management of closures is "interesting" because we're keeping around
 * a JSContext* and then trying to use it spontaneously from the main loop.
 * I don't think that's really quite kosher, and perhaps the problem is that
 * (in xulrunner) we just need to save a different context.
 *
 * Or maybe the right fix is to create our own context just for this?
 *
 * But for the moment, we save the context that was used to create the closure.
 *
 * Here's the problem: this context can be destroyed. AFTER the
 * context is destroyed, or at least potentially after, the objects in
 * the context's global object may be garbage collected. Remember that
 * JSObject* belong to a runtime, not a context.
 *
 * There is apparently no robust way to track context destruction in
 * SpiderMonkey, because the context can be destroyed without running
 * the garbage collector, and xulrunner takes over the JS_SetContextCallback()
 * callback. So there's no callback for us.
 *
 * So, when we go to use our context, we iterate the contexts in the runtime
 * and see if ours is still in the valid list, and decide to invalidate
 * the closure if it isn't.
 *
 * The closure can thus be destroyed in several cases:
 * - invalidation by say signal disconnection; we get invalidate callback
 * - invalidation because we were invoked while the context was dead
 * - invalidation through finalization (we were garbage collected)
 *
 * These don't have to happen in the same order; garbage collection can
 * be either before, or after, context destruction.
 *
 */

static void
invalidate_js_pointers(Closure *c)
{
    if (c->obj == NULL)
        return;

    c->obj = NULL;
    c->context = NULL;
    c->runtime = NULL;

    /* disconnects from signals, for example...
     * potentially a dangerous re-entrancy but
     * we'll have to risk it.
     */
    g_closure_invalidate(&c->base);
}

static void
global_context_finalized(JSObject *obj,
                         void     *data)
{
    Closure *c;

    c = data;

    gjs_debug(GJS_DEBUG_GCLOSURE,
              "Context destroy notifier on closure %p which calls object %p",
              c, c->obj);

    if (c->obj != NULL) {
        g_assert(c->obj == obj);

        invalidate_js_pointers(c);
    }

    /* The "Keep Alive" (garbage collector) owns one reference. */
    g_closure_unref(&c->base);
}


static void
check_context_valid(Closure *c)
{
    JSContext *a_context;
    JSContext *iter;

    if (c->runtime == NULL)
        return;

    iter = NULL;
    while ((a_context = JS_ContextIterator(c->runtime,
                                           &iter)) != NULL) {
        if (a_context == c->context) {
            return;
        }
    }

    gjs_debug(GJS_DEBUG_GCLOSURE,
              "Context %p no longer exists, invalidating closure %p which calls object %p",
              c->context, c, c->obj);

    /* Did not find the context. */
    invalidate_js_pointers(c);
}

/* Invalidation is like "dispose" - it happens on signal disconnect,
 * is guaranteed to happen at finalize, but may happen before finalize
 */
static void
closure_invalidated(gpointer data,
                    GClosure *closure)
{
    Closure *c;

    c = (Closure*) closure;

    gjs_debug(GJS_DEBUG_GCLOSURE,
              "Invalidating closure %p which calls object %p",
              closure, c->obj);

    if (c->obj) {
        gjs_keep_alive_remove_global_child(c->context,
                                              global_context_finalized,
                                              c->obj,
                                              c);

        c->obj = NULL;
        c->context = NULL;
        c->runtime = NULL;

        /* The "Keep Alive" (garbage collector) owns one reference,
         * since we removed ourselves from the keep-alive we'll
         * never be collected so drop the ref here
         */
        g_closure_unref(&c->base);
    }
}

static void
closure_finalized(gpointer data,
                  GClosure *closure)
{
    GJS_DEC_COUNTER(closure);
}

void
gjs_closure_invoke(GClosure *closure,
                   int       argc,
                   jsval    *argv,
                   jsval    *retval)
{
    Closure *c;
    JSContext *context;

    c = (Closure*) closure;

    check_context_valid(c);
    context = c->context;

    if (c->obj == NULL) {
        /* We were destroyed; become a no-op */
        c->context = NULL;
        return;
    }

    if (JS_IsExceptionPending(context)) {
        gjs_debug(GJS_DEBUG_GCLOSURE,
                  "Exception was pending before invoking callback??? Not expected");
        gjs_log_exception(c->context, NULL);
    }

    if (!gjs_call_function_value(context,
                                 NULL, /* "this" object; NULL is some kind of default presumably */
                                 OBJECT_TO_JSVAL(c->obj),
                                 argc,
                                 argv,
                                 retval)) {
        /* Exception thrown... */
        gjs_debug(GJS_DEBUG_GCLOSURE,
                  "Closure invocation failed (exception should have been thrown) closure %p callable %p",
                  closure, c->obj);
        if (!gjs_log_exception(context, NULL))
            gjs_debug(GJS_DEBUG_ERROR,
                      "Closure invocation failed but no exception was set?");
        return;
    }

    if (gjs_log_exception(context, NULL)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Closure invocation succeeded but an exception was set");
    }
}

JSContext*
gjs_closure_get_context(GClosure *closure)
{
    Closure *c;

    c = (Closure*) closure;

    check_context_valid(c);

    return c->context;
}

JSObject*
gjs_closure_get_callable(GClosure *closure)
{
    Closure *c;

    c = (Closure*) closure;

    return c->obj;
}

GClosure*
gjs_closure_new(JSContext  *context,
                   JSObject   *callable,
                   const char *description)
{
    Closure *c;

    c = (Closure*) g_closure_new_simple(sizeof(Closure), NULL);
    c->runtime = JS_GetRuntime(context);
    /* Closure are executed in our special "load-context" (one per runtime).
     * This ensures that the context is still alive when the closure
     * is invoked (as long as the runtime lives)
     */
    c->context = gjs_runtime_get_load_context(c->runtime);
    c->obj = callable;

    GJS_INC_COUNTER(closure);
    /* the finalize notifier right now is purely to track the counter
     * of how many closures are alive.
     */
    g_closure_add_finalize_notifier(&c->base, NULL, closure_finalized);

    gjs_keep_alive_add_global_child(c->context,
                                    global_context_finalized,
                                    c->obj,
                                    c);

    /* The "Keep Alive" (garbage collector) owns one reference. */
    g_closure_ref(&c->base);

    g_closure_add_invalidate_notifier(&c->base, NULL, closure_invalidated);

    gjs_debug(GJS_DEBUG_GCLOSURE,
              "Create closure %p which calls object %p '%s'",
              c, c->obj, description);

    return &c->base;
}
