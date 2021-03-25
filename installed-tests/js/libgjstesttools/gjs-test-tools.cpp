/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#include "installed-tests/js/libgjstesttools/gjs-test-tools.h"

void gjs_test_tools_init() {}

void gjs_test_tools_reset() {}

void gjs_test_tools_delayed_ref(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_ref(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}

void gjs_test_tools_delayed_unref(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_unref(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}

void gjs_test_tools_delayed_dispose(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_run_dispose(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}
