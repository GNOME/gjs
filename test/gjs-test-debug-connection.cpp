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

typedef struct _GjsDebugConnectionDestroyCallbackMockUserData
{
    gboolean was_called;
    GjsDebugConnection *connection;
} GjsDebugConnectionDestroyCallbackMockUserData;

static void
dispose_connection_callback(GjsDebugConnection *debug_connection,
                            gpointer           user_data)
{
    GjsDebugConnectionDestroyCallbackMockUserData *data = (GjsDebugConnectionDestroyCallbackMockUserData *) user_data;
    data->connection = debug_connection;
    data->was_called = TRUE;
}

static void
gjstest_debug_connection_destroy_callback_is_called_on_unref(void)
{
    GjsDebugConnectionDestroyCallbackMockUserData data =
    {
        FALSE,
        NULL
    };

    GjsDebugConnection *connection = gjs_debug_connection_new(dispose_connection_callback,
                                                              &data);
    g_object_unref(connection);

    g_assert(data.was_called == TRUE);
}

static void
gjstest_debug_connection_destroy_callback_called_with_connection_as_first_arg (void)
{
    GjsDebugConnectionDestroyCallbackMockUserData data =
    {
        FALSE,
        NULL
    };

    GjsDebugConnection *connection = gjs_debug_connection_new(dispose_connection_callback,
                                                              &data);
    g_object_unref(connection);

    g_assert(data.connection == connection);
}

void add_tests_for_debug_connection()
{
    g_test_add_func("/gjs/debug/connection/called_on_unref", gjstest_debug_connection_destroy_callback_is_called_on_unref);
    g_test_add_func("/gjs/debug/connection/first_arg_is_connection", gjstest_debug_connection_destroy_callback_called_with_connection_as_first_arg);
}
