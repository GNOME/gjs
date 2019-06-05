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

#include <string.h>
#include <limits.h>
#include <util/log.h>

#include "closure.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem-private.h"

struct Closure {
    explicit Closure(JSContext* context_)
        : context(context_), func(GjsMaybeOwned<JSFunction*>::newWrapper()) {}

    bool valid() { return func && *func; }
    JSContext *context;
    GjsMaybeOwned<JSFunction*>::Ptr func;
};

struct GjsClosure {
    GClosure base;

    /* We need a separate object to be able to call
       the C++ constructor without stomping on the base */
    Closure priv;
};

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
 * - invalidation by unref, e.g. when a signal is disconnected, closure is unref'd
 * - invalidation because we were invoked while the context was dead
 * - invalidation through finalization (we were garbage collected)
 *
 * These don't have to happen in the same order; garbage collection can
 * be either before, or after, context destruction.
 *
 */

static void
invalidate_js_pointers(GjsClosure *gc)
{
    Closure *c;

    c = &gc->priv;

    if (!c->valid())
        return;

    c->func->reset();
    c->context = NULL;

    /* Notify any closure reference holders they
     * may want to drop references.
     */
    g_closure_invalidate(&gc->base);
}

static void global_context_finalized(JS::HandleFunction func, void* data) {
    GjsClosure *gc = (GjsClosure*) data;
    Closure *c = &gc->priv;

    gjs_debug_closure(
        "Context global object destroy notifier on closure %p which calls "
        "object %p",
        c, c->func->debug_addr());

    if (c->valid()) {
        g_assert(*c->func == func.get());

        invalidate_js_pointers(gc);
    }
}

/* Invalidation is like "dispose" - it is guaranteed to happen at
 * finalize, but may happen before finalize. Normally, g_closure_invalidate()
 * is called when the "target" of the closure becomes invalid, so that the
 * source (the signal connection, say can be removed.) The usage above
 * in invalidate_js_pointers() is typical. Since the target of the closure
 * is under our control, it's unlikely that g_closure_invalidate() will ever
 * be called by anyone else, but in case it ever does, it's slightly better
 * to remove the "keep alive" here rather than in the finalize notifier.
 *
 * Unlike "dispose" invalidation only happens once.
 */
static void closure_invalidated(void*, GClosure* closure) {
    Closure *c;

    c = &((GjsClosure*) closure)->priv;

    GJS_DEC_COUNTER(closure);
    gjs_debug_closure("Invalidating closure %p which calls function %p",
                      closure, c->func->debug_addr());

    if (!c->valid()) {
        gjs_debug_closure("   (closure %p already dead, nothing to do)",
                          closure);
        return;
    }

    /* The context still exists, remove our destroy notifier. Otherwise we
     * would call the destroy notifier on an already-freed closure.
     *
     * This happens in the normal case, when the closure is
     * invalidated for some reason other than destruction of the
     * JSContext.
     */
    gjs_debug_closure("   (closure %p's context was alive, "
                      "removing our destroy notifier on global object)",
                      closure);

    c->func->reset();
    c->context = nullptr;
}

static void closure_set_invalid(void*, GClosure* closure) {
    Closure *self = &((GjsClosure*) closure)->priv;

    gjs_debug_closure("Invalidating signal closure %p which calls function %p",
                      closure, self->func->debug_addr());

    self->func->prevent_collection();
    self->func->reset();
    self->context = nullptr;

    GJS_DEC_COUNTER(closure);
}

static void closure_finalize(void*, GClosure* closure) {
    Closure *self = &((GjsClosure*) closure)->priv;

    self->~Closure();
}

bool
gjs_closure_invoke(GClosure                   *closure,
                   JS::HandleObject            this_obj,
                   const JS::HandleValueArray& args,
                   JS::MutableHandleValue      retval,
                   bool                        return_exception)
{
    Closure *c;
    JSContext *context;

    c = &((GjsClosure*) closure)->priv;

    if (!c->valid()) {
        /* We were destroyed; become a no-op */
        c->context = NULL;
        return false;
    }

    context = c->context;
    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, JS_GetFunctionObject(*c->func));

    if (JS_IsExceptionPending(context)) {
        gjs_debug_closure("Exception was pending before invoking callback??? "
                          "Not expected - closure %p", closure);
        gjs_log_exception(context);
    }

    JS::RootedFunction func(context, *c->func);
    if (!JS::Call(context, this_obj, func, args, retval)) {
        /* Exception thrown... */
        gjs_debug_closure(
            "Closure invocation failed (exception should have been thrown) "
            "closure %p function %p",
            closure, c->func->debug_addr());
        /* If an exception has been thrown, log it, unless the caller
         * explicitly wants to handle it manually (for example to turn it
         * into a GError), in which case it replaces the return value
         * (which is not valid anyway) */
        if (JS_IsExceptionPending(context)) {
            if (return_exception)
                JS_GetPendingException(context, retval);
            else
                gjs_log_exception(context);
        } else {
            retval.setUndefined();
            gjs_debug_closure("Closure invocation failed but no exception was set?"
                              "closure %p", closure);
        }
        return false;
    }

    if (gjs_log_exception(context)) {
        gjs_debug_closure("Closure invocation succeeded but an exception was set"
                          " - closure %p", closure);
    }

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    gjs->schedule_gc_if_needed();
    return true;
}

bool
gjs_closure_is_valid(GClosure *closure)
{
    Closure *c;

    c = &((GjsClosure*) closure)->priv;

    return c->context != NULL;
}

JSContext*
gjs_closure_get_context(GClosure *closure)
{
    Closure *c;

    c = &((GjsClosure*) closure)->priv;

    return c->context;
}

JSFunction* gjs_closure_get_callable(GClosure* closure) {
    Closure *c;

    c = &((GjsClosure*) closure)->priv;
    g_assert(c->func);

    return *c->func;
}

void
gjs_closure_trace(GClosure *closure,
                  JSTracer *tracer)
{
    Closure *c;

    c = &((GjsClosure*) closure)->priv;

    if (!c->valid())
        return;

    c->func->trace(tracer, "signal connection");
}

GClosure* gjs_closure_new(JSContext* context, JSFunction* callable,
                          const char* description GJS_USED_VERBOSE_GCLOSURE,
                          bool root_function) {
    GjsClosure *gc;
    Closure *c;

    gc = (GjsClosure*) g_closure_new_simple(sizeof(GjsClosure), NULL);
    c = new (&gc->priv) Closure(context);

    /* The saved context is used for lifetime management, so that the closure will
     * be torn down with the context that created it. The context could be attached to
     * the default context of the runtime using if we wanted the closure to survive
     * the context that created it.
     */
    JS_BeginRequest(context);

    GJS_INC_COUNTER(closure);

    if (root_function) {
        /* Fully manage closure lifetime if so asked */
        c->func->root(context, callable, global_context_finalized, gc);

        g_closure_add_invalidate_notifier(&gc->base, NULL, closure_invalidated);
    } else {
        *c->func = callable;
        /* Only mark the closure as invalid if memory is managed
           outside (i.e. by object.c for signals) */
        g_closure_add_invalidate_notifier(&gc->base, NULL, closure_set_invalid);
    }

    g_closure_add_finalize_notifier(&gc->base, NULL, closure_finalize);

    gjs_debug_closure("Create closure %p which calls function %p '%s'", gc,
                      c->func->debug_addr(), description);

    JS_EndRequest(context);

    return &gc->base;
}
