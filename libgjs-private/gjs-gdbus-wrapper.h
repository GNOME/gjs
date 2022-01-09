/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2011 Giovanni Campagna
 */

#ifndef LIBGJS_PRIVATE_GJS_GDBUS_WRAPPER_H_
#define LIBGJS_PRIVATE_GJS_GDBUS_WRAPPER_H_

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

typedef struct _GjsDBusImplementationPrivate GjsDBusImplementationPrivate;

#define GJS_TYPE_DBUS_IMPLEMENTATION              (gjs_dbus_implementation_get_type ())
#define GJS_DBUS_IMPLEMENTATION(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GJS_TYPE_DBUS_IMPLEMENTATION, GjsDBusImplementation))
#define GJS_DBUS_IMPLEMENTATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GJS_TYPE_DBUS_IMPLEMENTATION, GjsDBusImplementationClass))
#define GJS_IS_DBUS_IMPLEMENTATION(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GJS_TYPE_DBUS_IMPLEMENTATION))
#define GJS_IS_DBUS_IMPLEMENTATION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GJS_TYPE_DBUS_IMPLEMENTATION))
#define GJS_DBUS_IMPLEMENTATION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GJS_TYPE_DBUS_IMPLEMENTATION, GjsDBusImplementationClass))

struct _GjsDBusImplementation {
    GDBusInterfaceSkeleton parent;

    GjsDBusImplementationPrivate *priv;
};
typedef struct _GjsDBusImplementation GjsDBusImplementation;

struct _GjsDBusImplementationClass {
    GDBusInterfaceSkeletonClass parent_class;
};
typedef struct _GjsDBusImplementationClass GjsDBusImplementationClass;

GJS_EXPORT
GType                  gjs_dbus_implementation_get_type (void);

GJS_EXPORT
void                   gjs_dbus_implementation_emit_property_changed (GjsDBusImplementation *self, gchar *property, GVariant *newvalue);
GJS_EXPORT
void                   gjs_dbus_implementation_emit_signal           (GjsDBusImplementation *self, gchar *signal_name, GVariant *parameters);

GJS_EXPORT
void gjs_dbus_implementation_unexport(GjsDBusImplementation* self);
GJS_EXPORT
void gjs_dbus_implementation_unexport_from_connection(
    GjsDBusImplementation* self, GDBusConnection* connection);

G_END_DECLS

#endif /* LIBGJS_PRIVATE_GJS_GDBUS_WRAPPER_H_ */
