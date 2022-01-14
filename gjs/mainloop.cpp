/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <glib.h>

#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/mainloop.h"

namespace Gjs {

void MainLoop::spin(GjsContextPrivate* gjs) {
    // Check if System.exit() has been called.
    if (gjs->should_exit(nullptr))
        return;

    GjsAutoPointer<GMainContext, GMainContext, g_main_context_unref>
        main_context(g_main_context_ref_thread_default());

    do {
        bool blocking = can_block();

        // Only run the loop if there are pending jobs.
        if (g_main_context_pending(main_context))
            g_main_context_iteration(main_context, blocking);
    } while (
        // If System.exit() has not been called
        !gjs->should_exit(nullptr) &&
        // and there are pending sources or the job queue is not empty
        // continue spinning the event loop.
        (can_block() || !gjs->empty()));
}

};  // namespace Gjs
