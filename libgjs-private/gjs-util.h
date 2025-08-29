/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
 */

#ifndef LIBGJS_PRIVATE_GJS_UTIL_H_
#define LIBGJS_PRIVATE_GJS_UTIL_H_

#include <locale.h>

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

/**
 * GjsCompareDataFunc:
 * @a: a value
 * @b: a value to compare with
 * @user_data: user data
 *
 * Specifies the type of a comparison function used to compare two
 * values.  The function should return a negative integer if the first
 * value comes before the second, 0 if they are equal, or a positive
 * integer if the first value comes after the second.
 *
 * Returns: negative value if @a < @b; zero if @a = @b; positive
 *          value if @a > @b
 */
typedef int (*GjsCompareDataFunc)(const GObject *a, const GObject *b,
                                  void *user_data);

GJS_EXPORT
unsigned gjs_list_store_insert_sorted(GListStore *store, GObject *item,
                                      GjsCompareDataFunc compare_func,
                                      void *user_data);

GJS_EXPORT
void gjs_list_store_sort(GListStore *store, GjsCompareDataFunc compare_func,
                         void *user_data);

GJS_EXPORT
GObject* gjs_gtk_custom_sorter_new(GjsCompareDataFunc sort_func,
                                   void* user_data, GDestroyNotify destroy);

GJS_EXPORT
void gjs_gtk_custom_sorter_set_sort_func(GObject* sorter,
                                         GjsCompareDataFunc sort_func,
                                         void* user_data,
                                         GDestroyNotify destroy);

/**
 * GjsGLogWriterFunc:
 * @level: the log level
 * @fields: a dictionary variant with type a{sms}
 * @user_data: user data
 */
typedef GLogWriterOutput (*GjsGLogWriterFunc)(GLogLevelFlags level,
                                              const GVariant* fields,
                                              void* user_data);

GJS_EXPORT
void gjs_log_set_writer_func(GjsGLogWriterFunc func, gpointer user_data,
                             GDestroyNotify user_data_free);

GJS_EXPORT
void gjs_log_set_writer_default();

/* For imports.gettext */
typedef enum
{
  GJS_LOCALE_CATEGORY_ALL = LC_ALL,
  GJS_LOCALE_CATEGORY_COLLATE = LC_COLLATE,
  GJS_LOCALE_CATEGORY_CTYPE = LC_CTYPE,
  GJS_LOCALE_CATEGORY_MESSAGES = LC_MESSAGES,
  GJS_LOCALE_CATEGORY_MONETARY = LC_MONETARY,
  GJS_LOCALE_CATEGORY_NUMERIC = LC_NUMERIC,
  GJS_LOCALE_CATEGORY_TIME = LC_TIME
} GjsLocaleCategory;

GJS_EXPORT
const char* gjs_set_thread_locale(GjsLocaleCategory category,
                                  const char* locale);
GJS_EXPORT
void        gjs_textdomain               (const char *domain);
GJS_EXPORT
void        gjs_bindtextdomain           (const char *domain,
                                          const char *location);
GJS_EXPORT
GType       gjs_locale_category_get_type (void) G_GNUC_CONST;

/* For imports.overrides.GObject */
GJS_EXPORT
GParamFlags gjs_param_spec_get_flags (GParamSpec *pspec);
GJS_EXPORT
GType       gjs_param_spec_get_value_type (GParamSpec *pspec);
GJS_EXPORT
GType       gjs_param_spec_get_owner_type (GParamSpec *pspec);

/**
 * GjsBindingTransformFunc:
 * @binding:
 * @from_value:
 * @to_value: (out):
 * @user_data:
 */
typedef gboolean (*GjsBindingTransformFunc)(GBinding* binding,
                                            const GValue* from_value,
                                            GValue* to_value, void* user_data);

/**
 * gjs_g_object_bind_property_full:
 * @source:
 * @source_property:
 * @target:
 * @target_property:
 * @flags:
 * @to_callback: (scope notified) (nullable) (closure to_data):
 * @to_data:
 * @to_notify: (destroy to_data):
 * @from_callback: (scope notified) (nullable) (closure from_data):
 * @from_data:
 * @from_notify: (destroy from_data):
 *
 * Returns: (transfer none):
 */
GJS_EXPORT
GBinding* gjs_g_object_bind_property_full(
    GObject* source, const char* source_property, GObject* target,
    const char* target_property, GBindingFlags flags,
    GjsBindingTransformFunc to_callback, void* to_data,
    GDestroyNotify to_notify, GjsBindingTransformFunc from_callback,
    void* from_data, GDestroyNotify from_notify);

/**
 * gjs_g_binding_group_bind_full:
 * @source:
 * @source_property:
 * @target:
 * @target_property:
 * @flags:
 * @to_callback: (scope notified) (nullable) (closure to_data):
 * @to_data:
 * @to_notify: (destroy to_data):
 * @from_callback: (scope notified) (nullable) (closure from_data):
 * @from_data:
 * @from_notify: (destroy from_data):
 */
GJS_EXPORT
void gjs_g_binding_group_bind_full(
    GBindingGroup* source, const char* source_property, GObject* target,
    const char* target_property, GBindingFlags flags,
    GjsBindingTransformFunc to_callback, void* to_data,
    GDestroyNotify to_notify, GjsBindingTransformFunc from_callback,
    void* from_data, GDestroyNotify from_notify);

/* For imports.overrides.Gtk */
GJS_EXPORT
void gjs_gtk_container_child_set_property(GObject* container, GObject* child,
                                          const char* property,
                                          const GValue* value);

GJS_EXPORT
void gjs_clear_terminal(void);

G_END_DECLS

#endif /* LIBGJS_PRIVATE_GJS_UTIL_H_ */
