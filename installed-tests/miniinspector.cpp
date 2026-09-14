/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento

#include <locale.h>

#include <glib-object.h>

#include <gjs/gjs.h>

int main() {
    setlocale(LC_ALL, "");

    GjsContext* gjs_context = gjs_context_new();

    gjs_context_setup_inspector(gjs_context);

    g_object_unref(gjs_context);
    return 0;
}
