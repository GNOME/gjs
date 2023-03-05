/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#pragma once

#include <config.h>

#include <glib.h>

#include "util/log.h"

class GjsContextPrivate;

namespace Gjs {

class MainLoop {
    // grefcounts start at one and become invalidated when they are decremented
    // to zero. So the actual hold count is equal to the "ref" count minus 1.
    // We nonetheless use grefcount here because it takes care of dealing with
    // integer overflow for us.
    grefcount m_hold_count;
    bool m_exiting;

    void debug(const char* msg) {
        gjs_debug(GJS_DEBUG_MAINLOOP, "Main loop instance %p: %s", this, msg);
    }

    [[nodiscard]] bool can_block() {
        // Don't block if exiting
        if (m_exiting)
            return false;

        g_assert(!g_ref_count_compare(&m_hold_count, 0) &&
                 "main loop released too many times");

        // If the reference count is not zero or one, the loop is being held.
        return !g_ref_count_compare(&m_hold_count, 1);
    }

    void exit() {
        m_exiting = true;

        // Reset the reference count to 1 to exit
        g_ref_count_init(&m_hold_count);
    }

 public:
    MainLoop() : m_exiting(false) { g_ref_count_init(&m_hold_count); }
    ~MainLoop() {
        g_assert(g_ref_count_compare(&m_hold_count, 1) &&
                 "mismatched hold/release on main loop");
    }

    void hold() {
        // Don't allow new holds after exit() is called
        if (m_exiting)
            return;

        debug("hold");
        g_ref_count_inc(&m_hold_count);
    }

    void release() {
        // Ignore releases after exit(), exit() resets the refcount
        if (m_exiting)
            return;

        debug("release");
        bool zero [[maybe_unused]] = g_ref_count_dec(&m_hold_count);
        g_assert(!zero && "main loop released too many times");
    }

    [[nodiscard]] bool spin(GjsContextPrivate*);
};

};  // namespace Gjs
