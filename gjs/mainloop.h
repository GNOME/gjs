/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#pragma once

#include <config.h>

#include <glib.h>

class GjsContextPrivate;

namespace Gjs {

class MainLoop {
    // grefcounts start at one and become invalidated when they are decremented
    // to zero. So the actual hold count is equal to the "ref" count minus 1.
    // We nonetheless use grefcount here because it takes care of dealing with
    // integer overflow for us.
    grefcount m_hold_count;

    [[nodiscard]] bool can_block() {
        g_assert(!g_ref_count_compare(&m_hold_count, 0) &&
                 "main loop released too many times");

        // If the reference count is not zero or one, the loop is being held.
        return !g_ref_count_compare(&m_hold_count, 1);
    }

 public:
    MainLoop() { g_ref_count_init(&m_hold_count); }
    ~MainLoop() {
        g_assert(g_ref_count_compare(&m_hold_count, 1) &&
                 "mismatched hold/release on main loop");
    }

    void hold() { g_ref_count_inc(&m_hold_count); }

    void release() {
        bool zero [[maybe_unused]] = g_ref_count_dec(&m_hold_count);
        g_assert(!zero && "main loop released too many times");
    }

    void spin(GjsContextPrivate*);
};

};  // namespace Gjs
