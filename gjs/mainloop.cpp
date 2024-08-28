/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <glib.h>

#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/mainloop.h"

namespace Gjs {

bool MainLoop::spin(GjsContextPrivate* gjs) {
    if (m_exiting)
        return false;

    // Check if System.exit() has been called.
    if (gjs->should_exit(nullptr)) {
        // Return false to indicate the loop is exiting due to an exit call,
        // the queue is likely not empty
        debug("Not spinning loop because System.exit called");
        exit();
        return false;
    }

    Gjs::AutoPointer<GMainContext, GMainContext, g_main_context_unref>
        main_context{g_main_context_ref_thread_default()};

    debug("Spinning loop until released or hook cleared");
    do {
        bool blocking = can_block();

        // Only run the loop if there are pending jobs.
        if (g_main_context_pending(main_context))
            g_main_context_iteration(main_context, blocking);

        // If System.exit() has not been called
        if (gjs->should_exit(nullptr)) {
            debug("Stopped spinning loop because System.exit called");
            exit();
            return false;
        }
    } while (
        // and there is not a pending main loop hook
        !gjs->has_main_loop_hook() &&
        // and there are pending sources or the job queue is not empty
        // continue spinning the event loop.
        (can_block() || !gjs->empty()));

    return true;
}

};  // namespace Gjs
