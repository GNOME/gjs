/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2011 Giovanni Campagna
 */

#pragma once

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

GJS_EXPORT
G_DECLARE_FINAL_TYPE(GjsDBusImplementation, gjs_dbus_implementation, GJS,
                     DBUS_IMPLEMENTATION, GDBusInterfaceSkeleton);

GJS_EXPORT
void gjs_dbus_implementation_emit_property_changed(GjsDBusImplementation* self,
                                                   char* property,
                                                   GVariant* newvalue);
GJS_EXPORT
void gjs_dbus_implementation_emit_signal(GjsDBusImplementation* self,
                                         char* signal_name,
                                         GVariant* parameters);

GJS_EXPORT
void gjs_dbus_implementation_unexport(GjsDBusImplementation* self);
GJS_EXPORT
void gjs_dbus_implementation_unexport_from_connection(
    GjsDBusImplementation* self, GDBusConnection* connection);

G_END_DECLS
