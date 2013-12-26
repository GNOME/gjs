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
#include <gjs/debug-connection.h>

struct _GjsDebugConnectionPrivate
{
    GjsDebugConnectionDisposeCallback callback;
    gpointer                          user_data;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsDebugConnection,
                           gjs_debug_connection,
                           G_TYPE_OBJECT);

static void
gjs_debug_connection_init(GjsDebugConnection *connection)
{
    connection->priv = (GjsDebugConnectionPrivate *) gjs_debug_connection_get_instance_private(connection);
}

static void
gjs_debug_connection_dispose(GObject *object)
{
    GjsDebugConnection *connection = GJS_DEBUG_CONNECTION(object);
    g_return_if_fail(connection->priv->callback);
    (*connection->priv->callback)(connection, connection->priv->user_data);
}

static void
gjs_debug_connection_class_init(GjsDebugConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = gjs_debug_connection_dispose;
}

GjsDebugConnection *
gjs_debug_connection_new(GjsDebugConnectionDisposeCallback callback,
                         gpointer                          user_data)
{
    GjsDebugConnection *connection = GJS_DEBUG_CONNECTION(g_object_new(GJS_TYPE_DEBUG_CONNECTION, NULL));
    connection->priv->callback = callback;
    connection->priv->user_data = user_data;

    return connection;
}
