/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <gio/gio.h>
#include <glib.h>

#include "gjs/context-private.h"
#include "gjs/mainloop.h"

class GjsEventLoop {
    uint32_t m_refcount;

 public:
    GjsEventLoop() { m_refcount = 0; }

    void ref() { m_refcount++; }

    bool unref() {
        m_refcount--;
        return true;
    }

    void spin(GjsContext* context) {
        auto priv = GjsContextPrivate::from_object(context);

        // Check if System.exit() has been called.
        if (priv->should_exit(nullptr))
            return;

        // Whether there are still sources pending.
        bool has_pending;

        GjsAutoPointer<GMainContext, GMainContext, g_main_context_unref>
            main_context(g_main_context_ref_thread_default());

        do {
            if (priv->should_exit(nullptr))
                break;

            has_pending = g_main_context_pending(main_context);

            // Only run the loop if there are pending jobs.
            if (has_pending) {
                // If a source was run, we'll iterate again regardless.
                has_pending =
                    g_main_context_iteration(main_context, m_refcount > 0) ||
                    g_main_context_pending(main_context);
            }

            // Check if System.exit() has been called.
            if (priv->should_exit(nullptr))
                break;
        } while (
            // If there are pending sources or the job queue is not empty
            (m_refcount > 0 || has_pending || !priv->empty()) &&
            // and System.exit() has not been called
            // continue spinning the event loop.
            !priv->should_exit(nullptr));
    }
};

GjsEventLoop* gjs_event_loop_new() { return new GjsEventLoop(); }

void gjs_event_loop_free(GjsEventLoop* event_loop) { delete event_loop; }

void gjs_event_loop_spin(GjsEventLoop* event_loop, GjsContext* context) {
    event_loop->spin(context);
}

void gjs_event_loop_ref(GjsEventLoop* event_loop) { event_loop->ref(); }

bool gjs_event_loop_unref(GjsEventLoop* event_loop) {
    return event_loop->unref();
}
