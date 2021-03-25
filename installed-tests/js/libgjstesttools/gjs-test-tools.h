/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

void gjs_test_tools_init(void);

void gjs_test_tools_reset(void);

void gjs_test_tools_delayed_ref(GObject* object, int interval);

void gjs_test_tools_delayed_unref(GObject* object, int interval);

void gjs_test_tools_delayed_dispose(GObject* object, int interval);

G_END_DECLS
