/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC.
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
#ifndef __GJS_UTIL_DBUS_PRIVATE_H__
#define __GJS_UTIL_DBUS_PRIVATE_H__

#include <glib.h>
#include <gjs-dbus/dbus.h>
#include <gjs-dbus/dbus-proxy.h>

G_BEGIN_DECLS

typedef struct {
    DBusBusType bus_type;
    void *where_connection_was;
    GjsDBusProxy *driver_proxy;
    GHashTable *json_ifaces;
    GSList *name_ownership_monitors;
    GHashTable *name_watches;

    GSList *all_signal_watchers;

    /* These signal watcher tables are maps from a
     * string to a GSList of GjsSignalWatcher,
     * and they are lazily created if a signal watcher
     * needs to be looked up by the given key.
     */
    GHashTable *signal_watchers_by_unique_sender;
    GHashTable *signal_watchers_by_path;
    GHashTable *signal_watchers_by_iface;
    GHashTable *signal_watchers_by_signal;
    /* These are matching on well-known name only,
     * or watching all signals
     */
    GSList     *signal_watchers_in_no_table;

} GjsDBusInfo;

GjsDBusInfo*      _gjs_dbus_ensure_info                     (DBusConnection *connection);
void              _gjs_dbus_dispose_info                    (DBusConnection *connection);
void              _gjs_dbus_process_pending_signal_watchers (DBusConnection *connection,
                                                             GjsDBusInfo    *info);
DBusHandlerResult _gjs_dbus_signal_watch_filter_message     (DBusConnection *connection,
                                                             DBusMessage    *message,
                                                             void           *data);
void              _gjs_dbus_set_matching_name_owner_changed (DBusConnection *connection,
                                                             const char     *bus_name,
                                                             gboolean        matched);
void              _gjs_dbus_ensure_connect_idle             (DBusBusType     bus_type);

G_END_DECLS

#endif  /* __GJS_UTIL_DBUS_PRIVATE_H__ */
