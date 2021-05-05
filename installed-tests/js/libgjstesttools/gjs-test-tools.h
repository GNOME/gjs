/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#ifdef _GI_EXTERN
#define _GJS_TEST_TOOL_EXTERN _GI_EXTERN
#else
#define _GJS_TEST_TOOL_EXTERN
#endif

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_init(void);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_reset(void);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_ref(GObject* object, int interval);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_unref(GObject* object, int interval);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_dispose(GObject* object, int interval);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_ref_other_thread(GObject* object, GError** error);

_GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_ref_other_thread(GObject* object, int interval,
                                                 GError** error);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_unref_other_thread(GObject* object, GError** error);

_GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_unref_other_thread(GObject* object,
                                                   int interval,
                                                   GError** error);

_GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_ref_unref_other_thread(GObject* object,
                                                       int interval,
                                                       GError** error);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_run_dispose_other_thread(GObject* object, GError** error);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_object(GObject* object);

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_object_unreffed(GObject* object);

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_saved();

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_steal_saved();

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_peek_saved();

_GJS_TEST_TOOL_EXTERN
int gjs_test_tools_get_saved_ref_count();

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_clear_saved();

_GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_weak(GObject* object);

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_weak();

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_weak_other_thread(GError** error);

_GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_disposed(GObject* object);

_GJS_TEST_TOOL_EXTERN
int gjs_test_tools_open_bytes(GBytes* bytes, GError** error);

G_END_DECLS
