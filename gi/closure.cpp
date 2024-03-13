/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <glib.h>  // for g_assert

#include <js/CallAndConstruct.h>
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>

#include "gi/closure.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "util/log.h"

namespace Gjs {

Closure::Closure(JSContext* cx, JSObject* callable, bool root,
                 const char* description GJS_USED_VERBOSE_GCLOSURE)
    : m_cx(cx) {
    GJS_INC_COUNTER(closure);
    GClosureNotify closure_notify;

    if (root) {
        // Fully manage closure lifetime if so asked
        auto* gjs = GjsContextPrivate::from_cx(cx);
        g_assert(cx == gjs->context());
        m_callable.root(cx, callable);
        gjs->register_notifier(global_context_notifier_cb, this);
        closure_notify = [](void*, GClosure* closure) {
            static_cast<Closure*>(closure)->closure_invalidated();
        };
    } else {
        // Only mark the closure as invalid if memory is managed
        // outside (i.e. by object.c for signals)
        m_callable = callable;
        closure_notify = [](void*, GClosure* closure) {
            static_cast<Closure*>(closure)->closure_set_invalid();
        };
    }

    g_closure_add_invalidate_notifier(this, nullptr, closure_notify);

    gjs_debug_closure("Create closure %p which calls callable %p '%s'", this,
                      m_callable.debug_addr(), description);
}

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

void Closure::unset_context() {
    if (!m_cx)
        return;

    if (m_callable && m_callable.rooted()) {
        auto* gjs = GjsContextPrivate::from_cx(m_cx);
        gjs->unregister_notifier(global_context_notifier_cb, this);
    }

    m_cx = nullptr;
}

void Closure::global_context_finalized() {
    gjs_debug_closure(
        "Context global object destroy notifier on closure %p which calls "
        "callable %p",
        this, m_callable.debug_addr());

    if (m_callable) {
        // Manually unset the context as we don't need to unregister the
        // notifier here, or we'd end up touching a vector we're iterating
        m_cx = nullptr;
        reset();
        // Notify any closure reference holders they
        // may want to drop references.
        g_closure_invalidate(this);
    }
}

/* Invalidation is like "dispose" - it is guaranteed to happen at
 * finalize, but may happen before finalize. Normally, g_closure_invalidate()
 * is called when the "target" of the closure becomes invalid, so that the
 * source (the signal connection, say can be removed.) The usage above
 * in global_context_finalized() is typical. Since the target of the closure
 * is under our control, it's unlikely that g_closure_invalidate() will ever
 * be called by anyone else, but in case it ever does, it's slightly better
 * to remove the "keep alive" here rather than in the finalize notifier.
 *
 * Unlike "dispose" invalidation only happens once.
 */
void Closure::closure_invalidated() {
    GJS_DEC_COUNTER(closure);
    gjs_debug_closure("Invalidating closure %p which calls callable %p", this,
                      m_callable.debug_addr());

    if (!m_callable) {
        gjs_debug_closure("   (closure %p already dead, nothing to do)", this);
        return;
    }

    /* The context still exists, remove our destroy notifier. Otherwise we
     * would call the destroy notifier on an already-freed closure.
     *
     * This happens in the normal case, when the closure is
     * invalidated for some reason other than destruction of the
     * JSContext.
     */
    gjs_debug_closure(
        "   (closure %p's context was alive, "
        "removing our destroy notifier on global object)",
        this);

    reset();
}

void Closure::closure_set_invalid() {
    gjs_debug_closure("Invalidating signal closure %p which calls callable %p",
                      this, m_callable.debug_addr());

    m_callable.prevent_collection();
    reset();

    GJS_DEC_COUNTER(closure);
}

bool Closure::invoke(JS::HandleObject this_obj,
                     const JS::HandleValueArray& args,
                     JS::MutableHandleValue retval) {
    if (!m_callable) {
        /* We were destroyed; become a no-op */
        reset();
        return false;
    }

    JSAutoRealm ar{m_cx, m_callable.get()};

    if (gjs_log_exception(m_cx)) {
        gjs_debug_closure(
            "Exception was pending before invoking callback??? "
            "Not expected - closure %p",
            this);
    }

    JS::RootedValue v_callable{m_cx, JS::ObjectValue(*m_callable.get())};
    bool ok = JS::Call(m_cx, this_obj, v_callable, args, retval);
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(m_cx);

    if (!ok) {
        /* Exception thrown... */
        gjs_debug_closure(
            "Closure invocation failed (exception should have been thrown) "
            "closure %p callable %p",
            this, m_callable.debug_addr());
        return false;
    }

    if (gjs_log_exception_uncaught(m_cx)) {
        gjs_debug_closure(
            "Closure invocation succeeded but an exception was set"
            " - closure %p",
            m_cx);
    }

    gjs->schedule_gc_if_needed();
    return true;
}

}  // namespace Gjs
