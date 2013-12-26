/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#ifndef GJS_DEBUG_CONNECTION_H
#define GJS_DEBUG_CONNECTION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GJS_TYPE_DEBUG_CONNECTION gjs_debug_connection_get_type()

#define GJS_DEBUG_CONNECTION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), \
    GJS_TYPE_DEBUG_CONNECTION, GjsDebugConnection))

#define GJS_DEBUG_CONNECTION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), \
    GJS_TYPE_DEBUG_CONNECTION, GjsDebugConnectionClass))

#define GJS_IS_DEBUG_CONNECTION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
    GJS_TYPE_DEBUG_CONNECTION))

#define GJS_IS_DEBUG_CONNECTION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), \
    GJS_TYPE_DEBUG_CONNECTION))

#define GJS_DEBUG_CONNECTION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), \
    GJS_TYPE_DEBUG_CONNECTION, GjsDebugConnectionClass))

typedef struct _GjsDebugConnection GjsDebugConnection;
typedef struct _GjsDebugConnectionClass GjsDebugConnectionClass;
typedef struct _GjsDebugConnectionPrivate GjsDebugConnectionPrivate;

struct _GjsDebugConnectionClass
{
    GObjectClass parent_class;
};

struct _GjsDebugConnection
{
    GObject parent;

    /*< private >*/
    GjsDebugConnectionPrivate *priv;
};

typedef void (*GjsDebugConnectionDisposeCallback)(GjsDebugConnection *connection,
                                                  gpointer           user_data);

GjsDebugConnection * gjs_debug_connection_new(GjsDebugConnectionDisposeCallback callback,
                                              gpointer                          user_data);

GType gjs_debug_connection_get_type(void);

G_END_DECLS

#endif
