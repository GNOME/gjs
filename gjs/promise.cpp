/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>

#include "gjs/context-private.h"
#include "gjs/promise.h"

// G_PRIORITY_HIGH is -100, we set -1000 to ensure our source
// always has the greatest priority. This means our prepare will
// be called before other sources, and prepare will determine whether
// we dispatch.
#define GJS_PROMISE_JOB_QUEUE_SOURCE_PRIORITY -1000

class GjsPromiseJobQueueSource;

typedef gboolean (*GjsPromiseJobQueueSourceFunc)(void* promise_queue_source);

/**
 * A private class which holds the state for GjsPromiseJobQueueSource
 * GSources and the GSourceFuncs for the source behavior.
 */
class GjsPromiseJobQueueSource {
 public:
    // The parent source.
    GSource parent;
    // The private GJS context this source runs within.
    GjsContextPrivate* cx;
    // The thread-default GMainContext
    GMainContext* main_context;
    GCancellable* cancellable;
    int source_id;

 private:
    // Called to determine whether the source should run (dispatch) in the
    // next event loop iteration. If the job queue is not empty we return true
    // to schedule a dispatch, if the job queue has been empty we quit the main
    // loop. This should return execution to gjs_spin_event_loop which may
    // restart the loop if additional jobs are added.
    static gboolean prepare(GSource* source, gint* timeout [[maybe_unused]]) {
        auto promise_queue_source =
            reinterpret_cast<GjsPromiseJobQueueSource*>(source);

        GjsContextPrivate* cx = promise_queue_source->cx;
        if (!cx->empty())
            return true;

        g_main_context_wakeup(promise_queue_source->main_context);
        return false;
    }

    // If the job queue is empty, dispatch will quit the event loop
    // otherwise it will drain the job queue. Dispatch must always
    // return G_SOURCE_CONTINUE, it should never remove the source
    // from the loop.
    static gboolean dispatch(GSource* source, GSourceFunc callback,
                             gpointer data [[maybe_unused]]) {
        auto promise_queue_source =
            reinterpret_cast<GjsPromiseJobQueueSource*>(source);

        GjsPromiseJobQueueSourceFunc func =
            reinterpret_cast<GjsPromiseJobQueueSourceFunc>(callback);

        // The ready time is sometimes set to 0 to kick us out of polling,
        // we need to reset the value here or this source will always be the
        // next one to execute. (it will starve the other sources)
        g_source_set_ready_time(source, -1);

        func(promise_queue_source);

        return G_SOURCE_CONTINUE;
    }

    // Removes the GjsPrivateContext reference.
    static void finalize(GSource* source) {
        auto promise_queue_source =
            reinterpret_cast<GjsPromiseJobQueueSource*>(source);

        promise_queue_source->cx = nullptr;

        g_main_context_unref(promise_queue_source->main_context);
        promise_queue_source->main_context = nullptr;
    }

    static gboolean callback(void* source) {
        auto promise_queue_source =
            reinterpret_cast<GjsPromiseJobQueueSource*>(source);
        if (g_cancellable_is_cancelled(promise_queue_source->cancellable))
            return G_SOURCE_REMOVE;

        GjsContextPrivate* cx = promise_queue_source->cx;
        if (cx->empty()) {
            g_main_context_wakeup(promise_queue_source->main_context);
        }

        // Drain the job queue.
        cx->runJobs(cx->context());

        return G_SOURCE_CONTINUE;
    }

    // g_source_new does not accept const values so
    // this static member is defined outside of the
    // class body.
    static GSourceFuncs source_funcs;

 public:
    // Creates a new GSource with this class' state and source_funcs.
    static GSource* create(GjsContextPrivate* cx, GCancellable* cancellable) {
        g_return_val_if_fail(cx != nullptr, nullptr);
        g_return_val_if_fail(
            cancellable == nullptr || G_IS_CANCELLABLE(cancellable), nullptr);

        GSource* source =
            g_source_new(&source_funcs, sizeof(GjsPromiseJobQueueSource));
        g_source_set_priority(source, GJS_PROMISE_JOB_QUEUE_SOURCE_PRIORITY);
        g_source_set_callback(source, &callback, nullptr, nullptr);
        g_source_set_name(source, "GjsPromiseJobQueueSource");

        // TODO(ewlsh): Do we need this?
        // g_source_set_can_recurse(source, true);
        auto promise_queue_source =
            reinterpret_cast<GjsPromiseJobQueueSource*>(source);
        promise_queue_source->cx = cx;
        promise_queue_source->main_context =
            g_main_context_ref_thread_default();
        promise_queue_source->source_id = -1;
        promise_queue_source->cancellable = cancellable;

        g_assert(promise_queue_source->main_context);

        // Add a cancellable source.
        GSource* cancellable_source = g_cancellable_source_new(cancellable);
        g_source_set_dummy_callback(cancellable_source);
        g_source_add_child_source(source, cancellable_source);
        g_source_unref(cancellable_source);

        return source;
    }
};

GSourceFuncs GjsPromiseJobQueueSource::source_funcs = {
    &GjsPromiseJobQueueSource::prepare,
    nullptr,
    &GjsPromiseJobQueueSource::dispatch,
    &GjsPromiseJobQueueSource::finalize,
    nullptr,
    nullptr,
};

/**
 * gjs_promise_job_queue_source_new:
 *
 * @brief Creates a new GjsPromiseJobQueueSource GSource with an
 * optional cancellable.
 *
 * @param cx the current JSContext
 * @param cancellable an optional cancellable
 *
 * @returns the created source
 */
GSource* gjs_promise_job_queue_source_new(GjsContextPrivate* cx,
                                          GCancellable* cancellable) {
    return GjsPromiseJobQueueSource::create(cx, cancellable);
}

void gjs_promise_job_queue_source_attach(GSource* source) {
    auto promise_queue_source =
        reinterpret_cast<GjsPromiseJobQueueSource*>(source);

    promise_queue_source->source_id =
        g_source_attach(source, promise_queue_source->main_context);
}

void gjs_promise_job_queue_source_remove(GSource* source) {
    auto promise_queue_source =
        reinterpret_cast<GjsPromiseJobQueueSource*>(source);

    g_source_remove(promise_queue_source->source_id);
    g_source_destroy(source);
    g_source_unref(source);
}

void gjs_promise_job_queue_source_wakeup(GSource* source) {
    g_source_set_ready_time(source, 0);
}

GJS_JSAPI_RETURN_CONVENTION
static bool run_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    auto gjs = GjsContextPrivate::from_cx(cx);
    gjs->runJobs(cx);

    args.rval().setUndefined();
    return true;
}

static JSFunctionSpec gjs_native_promise_module_funcs[] = {
    JS_FN("run", run_func, 2, 0), JS_FS_END};

bool gjs_define_native_promise_stuff(JSContext* cx,
                                     JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, gjs_native_promise_module_funcs);
}
