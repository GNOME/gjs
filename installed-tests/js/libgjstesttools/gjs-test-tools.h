/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#ifndef GJS_TEST_TOOL_EXTERN
#    define GJS_TEST_TOOL_EXTERN
#endif

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_init(void);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_reset(void);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_ref(GObject* object);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_unref(GObject* object);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_ref(GObject* object, int interval);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_unref(GObject* object, int interval);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_delayed_dispose(GObject* object, int interval);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_ref_other_thread(GObject* object, GError** error);

GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_ref_other_thread(GObject* object, int interval,
                                                 GError** error);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_unref_other_thread(GObject* object, GError** error);

GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_unref_other_thread(GObject* object,
                                                   int interval,
                                                   GError** error);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_emit_test_signal_other_thread(GObject* object,
                                                  GError** error);

GJS_TEST_TOOL_EXTERN
GThread* gjs_test_tools_delayed_ref_unref_other_thread(GObject* object,
                                                       int interval,
                                                       GError** error);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_run_dispose_other_thread(GObject* object, GError** error);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_object(GObject* object);

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_object_unreffed(GObject* object);

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_saved();

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_steal_saved();

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_peek_saved();

GJS_TEST_TOOL_EXTERN
int gjs_test_tools_get_saved_ref_count();

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_clear_saved();

GJS_TEST_TOOL_EXTERN
void gjs_test_tools_save_weak(GObject* object);

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_weak();

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_weak_other_thread(GError** error);

GJS_TEST_TOOL_EXTERN
GObject* gjs_test_tools_get_disposed(GObject* object);

GJS_TEST_TOOL_EXTERN
int gjs_test_tools_open_bytes(GBytes* bytes, GError** error);

GJS_TEST_TOOL_EXTERN
GBytes* gjs_test_tools_new_unaligned_bytes(size_t len);

GJS_TEST_TOOL_EXTERN
GBytes* gjs_test_tools_new_static_bytes();

G_END_DECLS
