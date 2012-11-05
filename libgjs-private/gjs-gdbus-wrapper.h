/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2011 Giovanni Campagna
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __GJS_UTIL_DBUS_H__
#define __GJS_UTIL_DBUS_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GjsDBusImplementation        GjsDBusImplementation;
typedef struct _GjsDBusImplementationClass   GjsDBusImplementationClass;
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

struct _GjsDBusImplementationClass {
    GDBusInterfaceSkeletonClass parent_class;
};

GType                  gjs_dbus_implementation_get_type (void);

void                   gjs_dbus_implementation_emit_property_changed (GjsDBusImplementation *self, gchar *property, GVariant *newvalue);
void                   gjs_dbus_implementation_emit_signal           (GjsDBusImplementation *self, gchar *signal_name, GVariant *parameters);

G_END_DECLS

#endif  /* __GJS_UTIL_DBUS_H__ */
