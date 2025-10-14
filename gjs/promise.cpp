// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>
// SPDX-FileCopyrightText: 2021 Marco Trevisan <mail@3v1n0.net>

#include <config.h>

#include <stddef.h>  // for size_t

#include <string>  // for string methods

#include <gio/gio.h>
#include <glib-object.h>

#include <js/CallAndConstruct.h>  // for JS::IsCallable
#include <js/CallArgs.h>
#include <js/PropertyAndElement.h>  // for JS_DefineFunctions
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewPlainObject
#include <jsfriendapi.h>  // for RunJobs

#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/promise.h"
#include "util/log.h"

/**
 * promise.cpp - This file implements a custom GSource, PromiseJobQueueSource,
 * which handles promise dispatching within GJS. Custom GSources are able to
 * control under which conditions they dispatch. PromiseJobQueueSource will
 * always dispatch if even a single Promise is enqueued and will continue
 * dispatching until all Promises (also known as "Jobs" within SpiderMonkey)
 * are run. While this does technically mean Promises can starve the mainloop
 * if run recursively, this is intentional. Within JavaScript Promises are
 * considered "microtasks" and a microtask must run before any other task
 * continues.
 *
 * PromiseJobQueueSource is attached to the thread's default GMainContext with
 * a default priority of -1000. This is 10x the priority of G_PRIORITY_HIGH and
 * no application code should attempt to override this.
 *
 * See doc/Custom-GSources.md for more background information on custom
 * GSources and microtasks
 */

namespace Gjs {

/**
 * PromiseJobDispatcher::Source:
 *
 * A custom GSource which handles draining our job queue.
 */
class PromiseJobDispatcher::Source : public GSource {
    // The private GJS context this source runs within.
    GjsContextPrivate* m_gjs;
    // The main context this source attaches to.
    AutoMainContext m_main_context;
    // The cancellable that stops this source.
    AutoUnref<GCancellable> m_cancellable;
    AutoPointer<GSource, GSource, g_source_unref> m_cancellable_source;

    // G_PRIORITY_HIGH is normally -100, we set 10 times that to ensure our
    // source always has the greatest priority. This means our prepare will
    // be called before other sources, and prepare will determine whether
    // we dispatch.
    static constexpr int PRIORITY = 10 * G_PRIORITY_HIGH;

    // GSource custom functions
    static GSourceFuncs source_funcs;

    // Called to determine whether the source should run (dispatch) in the
    // next event loop iteration. If the job queue is not empty we return true
    // to schedule a dispatch.
    gboolean prepare(int* timeout [[maybe_unused]]) { return !m_gjs->empty(); }

    gboolean dispatch() {
        if (g_cancellable_is_cancelled(m_cancellable))
            return G_SOURCE_REMOVE;

        // The ready time is sometimes set to 0 to kick us out of polling,
        // we need to reset the value here or this source will always be the
        // next one to execute. (it will starve the other sources)
        g_source_set_ready_time(this, -1);

        // Drain the job queue.
        js::RunJobs(m_gjs->context());

        return G_SOURCE_CONTINUE;
    }

 public:
    /**
     * Source::Source:
     * @gjs: the GJS object
     * @main_context: GLib main context to associate with the source
     *
     * Constructs a new GSource for the PromiseJobDispatcher and adds a
     * reference to the associated main context.
     */
    Source(GjsContextPrivate* gjs, GMainContext* main_context)
        : m_gjs(gjs),
          m_main_context(main_context, TakeOwnership{}),
          m_cancellable(g_cancellable_new()),
          m_cancellable_source(g_cancellable_source_new(m_cancellable)) {
        g_source_set_priority(this, PRIORITY);
        g_source_set_static_name(this, "GjsPromiseJobQueueSource");

        // Add our cancellable source to our main source,
        // this will trigger the main source if our cancellable
        // is cancelled.
        g_source_add_child_source(this, m_cancellable_source);
    }

    void* operator new(size_t size) {
        return g_source_new(&source_funcs, size);
    }
    void operator delete(void* p) { g_source_unref(static_cast<GSource*>(p)); }

    bool is_running() { return !!g_source_get_context(this); }

    /**
     * Source::cancel:
     *
     * Trigger the cancellable, detaching our source.
     */
    void cancel() { g_cancellable_cancel(m_cancellable); }
    /**
     * Source::reset:
     *
     * Reset the cancellable and prevent the source from stopping, overriding a
     * previous cancel() call. Called by PromiseJobDispatcher::start() to ensure
     * the custom source will start.
     */
    void reset() {
        if (!g_cancellable_is_cancelled(m_cancellable))
            return;

        gjs_debug(GJS_DEBUG_MAINLOOP, "Uncancelling promise job dispatcher");

        if (is_running())
            g_source_remove_child_source(this, m_cancellable_source);
        else
            g_source_destroy(m_cancellable_source);

        // Drop the old cancellable and create a new one, as per
        // https://docs.gtk.org/gio/method.Cancellable.reset.html
        m_cancellable = g_cancellable_new();
        m_cancellable_source = g_cancellable_source_new(m_cancellable);
        g_source_add_child_source(this, m_cancellable_source);
    }
};

GSourceFuncs PromiseJobDispatcher::Source::source_funcs = {
    [](GSource* source, int* timeout) {
        return static_cast<Source*>(source)->prepare(timeout);
    },
    nullptr,  // check
    [](GSource* source, GSourceFunc, void*) {
        return static_cast<Source*>(source)->dispatch();
    },
    [](GSource* source) { static_cast<Source*>(source)->~Source(); },
};

PromiseJobDispatcher::PromiseJobDispatcher(GjsContextPrivate* gjs)
    // Acquire a guaranteed reference to this thread's default main context
    : m_main_context(g_main_context_ref_thread_default()),
      // Create and reference our custom GSource
      m_source(std::make_unique<Source>(gjs, m_main_context)) {}

PromiseJobDispatcher::~PromiseJobDispatcher() {
    g_source_destroy(m_source.get());
}

bool PromiseJobDispatcher::is_running() { return m_source->is_running(); }

void PromiseJobDispatcher::start() {
    // Reset the cancellable
    m_source->reset();

    // Don't re-attach if the task is already running
    if (is_running())
        return;

    gjs_debug(GJS_DEBUG_MAINLOOP, "Starting promise job dispatcher");
    g_source_attach(m_source.get(), m_main_context);
}

void PromiseJobDispatcher::stop() {
    gjs_debug(GJS_DEBUG_MAINLOOP, "Stopping promise job dispatcher");
    m_source->cancel();
}

};  // namespace Gjs

GJS_JSAPI_RETURN_CONVENTION
bool drain_microtask_queue(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    js::RunJobs(cx);

    args.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool set_main_loop_hook(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject callback(cx);
    if (!gjs_parse_call_args(cx, "setMainLoopHook", args, "o", "callback",
                             &callback)) {
        return false;
    }

    if (!JS::IsCallable(callback)) {
        gjs_throw(cx, "Main loop hook must be callable");
        return false;
    }

    gjs_debug(GJS_DEBUG_MAINLOOP, "Set main loop hook to %s",
              gjs_debug_object(callback).c_str());

    GjsContextPrivate* priv = GjsContextPrivate::from_cx(cx);
    if (!priv->set_main_loop_hook(callback)) {
        gjs_throw(
            cx,
            "A mainloop is already running. Did you already call runAsync()?");
        return false;
    }

    args.rval().setUndefined();
    return true;
}

JSFunctionSpec gjs_native_promise_module_funcs[] = {
    JS_FN("drainMicrotaskQueue", &drain_microtask_queue, 0, 0),
    JS_FN("setMainLoopHook", &set_main_loop_hook, 1, 0), JS_FS_END};

bool gjs_define_native_promise_stuff(JSContext* cx,
                                     JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));
    if (!module)
        return false;
    return JS_DefineFunctions(cx, module, gjs_native_promise_module_funcs);
}
