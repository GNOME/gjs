/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC. */

#include <config.h>

#include "dbus.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <string.h>
#include <stdlib.h>

#include "gjs-dbus/dbus-private.h"
#include "gjs-dbus/dbus-proxy.h"
#include "glib.h"

#include "util/log.h"

typedef struct {
    const GjsDBusConnectFuncs *funcs;
    void *data;
    unsigned int opened : 1;
} ConnectFuncs;

typedef enum {
    NAME_NOT_REQUESTED,
    NAME_PRIMARY_OWNER,
    NAME_IN_QUEUE,
    NAME_NOT_OWNED
} NameOwnershipState;

typedef struct {
    char *name;
    const GjsDBusJsonMethod *methods;
    int n_methods;
} GjsJsonIface;

typedef struct {
    DBusBusType bus_type;
    /* If prev_state != state then we may need to notify */
    NameOwnershipState prev_state;
    NameOwnershipState state;
    const GjsDBusNameOwnerFuncs *funcs;
    void *data;
    unsigned int id;
} GjsNameOwnershipMonitor;

typedef struct {
    char *name;
    char *current_owner;
    GSList *watchers;
} GjsNameWatch;

typedef struct {
    GjsDBusWatchNameFlags flags;
    const GjsDBusWatchNameFuncs *funcs;
    void *data;
    DBusBusType bus_type;
    GjsNameWatch *watch;
    guint notify_idle;
    int refcount;
    guint destroyed : 1;
} GjsNameWatcher;

typedef struct {
    DBusBusType bus_type;
    char *name;
    GjsNameWatcher *watcher;
} GjsPendingNameWatcher;

static DBusConnection *session_bus_weak_ref = NULL;
static GSList *session_bus_weak_refs = NULL;
static DBusConnection *system_bus_weak_ref = NULL;
static GSList *system_bus_weak_refs = NULL;
static guint session_connect_idle_id = 0;
static guint system_connect_idle_id = 0;
static GSList *all_connect_funcs = NULL;

static GSList *pending_name_ownership_monitors = NULL;
static GSList *pending_name_watchers = NULL;

#define GJS_DBUS_NAME_OWNER_MONITOR_INVALID_ID 0

static unsigned int global_monitor_id = 0;

static DBusHandlerResult disconnect_filter_message             (DBusConnection   *connection,
                                                                DBusMessage      *message,
                                                                void             *data);
static DBusHandlerResult name_ownership_monitor_filter_message (DBusConnection   *connection,
                                                                DBusMessage      *message,
                                                                void             *data);
static void              process_name_ownership_monitors       (DBusConnection   *connection,
                                                                GjsDBusInfo      *info);
static void              name_watch_remove_watcher             (GjsNameWatch     *watch,
                                                                GjsNameWatcher   *watcher);
static DBusHandlerResult name_watch_filter_message             (DBusConnection   *connection,
                                                                DBusMessage      *message,
                                                                void             *data);
static void              process_pending_name_watchers         (DBusConnection   *connection,
                                                                GjsDBusInfo      *info);
static void              json_iface_free                       (GjsJsonIface     *iface);
static void              info_free                             (GjsDBusInfo      *info);
static gboolean          notify_watcher_name_appeared          (gpointer data);

static dbus_int32_t info_slot = -1;
GjsDBusInfo*
_gjs_dbus_ensure_info(DBusConnection *connection)
{
    GjsDBusInfo *info;

    dbus_connection_allocate_data_slot(&info_slot);

    info = dbus_connection_get_data(connection, info_slot);

    if (info == NULL) {
        info = g_slice_new0(GjsDBusInfo);

        info->where_connection_was = connection;

        if (connection == session_bus_weak_ref)
            info->bus_type = DBUS_BUS_SESSION;
        else if (connection == system_bus_weak_ref)
            info->bus_type = DBUS_BUS_SYSTEM;
        else
            g_error("Unknown bus type opened in %s", __FILE__);

        info->json_ifaces = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  NULL, (GFreeFunc) json_iface_free);
        info->name_watches = g_hash_table_new(g_str_hash, g_str_equal);
        dbus_connection_set_data(connection, info_slot, info, (DBusFreeFunction) info_free);

        dbus_connection_add_filter(connection, name_ownership_monitor_filter_message,
                                   NULL, NULL);
        dbus_connection_add_filter(connection, name_watch_filter_message,
                                   NULL, NULL);
        dbus_connection_add_filter(connection, _gjs_dbus_signal_watch_filter_message,
                                   NULL, NULL);

        /* Important: disconnect_filter_message() must be LAST so
         * it runs last when the disconnect message arrives.
         */
        dbus_connection_add_filter(connection, disconnect_filter_message,
                                   NULL, NULL);

        /* caution, this could get circular if proxy_new() goes back around
         * and tries to use dbus.c - but we'll fix it when it happens.
         * Also, this refs the connection ...
         */
        info->driver_proxy =
            gjs_dbus_proxy_new(connection,
                               DBUS_SERVICE_DBUS,
                               DBUS_PATH_DBUS,
                               DBUS_INTERFACE_DBUS);
    }

    return info;
}

void
_gjs_dbus_dispose_info(DBusConnection *connection)
{
    GjsDBusInfo *info;

    if (info_slot < 0)
        return;

    info = dbus_connection_get_data(connection, info_slot);

    if (info != NULL) {

        gjs_debug(GJS_DEBUG_DBUS, "Disposing info on connection %p",
                connection);

        /* the driver proxy refs the connection, we want
         * to break that cycle.
         */
        g_object_unref(info->driver_proxy);
        info->driver_proxy = NULL;

        dbus_connection_set_data(connection, info_slot, NULL, NULL);

        dbus_connection_free_data_slot(&info_slot);
    }
}

DBusConnection*
gjs_dbus_get_weak_ref(DBusBusType which_bus)
{
    if (which_bus == DBUS_BUS_SESSION) {
        return session_bus_weak_ref;
    } else if (which_bus == DBUS_BUS_SYSTEM) {
        return system_bus_weak_ref;
    } else {
        g_assert_not_reached();
    }
}

static DBusHandlerResult
disconnect_filter_message(DBusConnection   *connection,
                          DBusMessage      *message,
                          void             *data)
{
    /* We should be running after all other filters */
    if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
        gjs_debug(GJS_DEBUG_DBUS, "Disconnected in %s", G_STRFUNC);

        _gjs_dbus_dispose_info(connection);

        if (session_bus_weak_ref == connection)
            session_bus_weak_ref = NULL;

        if (system_bus_weak_ref == connection)
            system_bus_weak_ref = NULL;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusConnection*
try_connecting(DBusBusType which_bus)
{

    DBusGConnection *gconnection;
    DBusConnection *connection;
    GError *error;

    connection = gjs_dbus_get_weak_ref(which_bus);
    if (connection != NULL)
        return connection;

    gjs_debug(GJS_DEBUG_DBUS, "trying to connect to message bus");

    error = NULL;
    gconnection = dbus_g_bus_get(which_bus,
                                 &error);
    if (gconnection == NULL) {
        gjs_debug(GJS_DEBUG_DBUS, "bus connection failed: %s",
                error->message);
        g_error_free(error);
        return NULL;
    }

    connection = dbus_g_connection_get_connection(gconnection);

    /* Disable this because all our apps will be well-behaved! */
    dbus_connection_set_exit_on_disconnect(connection, FALSE);

    if (which_bus == DBUS_BUS_SESSION &&
        session_bus_weak_ref == NULL) {
        GSList *l;
        session_bus_weak_ref = connection;
        for (l = session_bus_weak_refs; l != NULL; l = l->next) {
            DBusConnection **connection_p = l->data;
            *connection_p = session_bus_weak_ref;
        }
    } else if (which_bus == DBUS_BUS_SYSTEM &&
               system_bus_weak_ref == NULL) {
        GSList *l;
        system_bus_weak_ref = connection;
        for (l = system_bus_weak_refs; l != NULL; l = l->next) {
            DBusConnection **connection_p = l->data;
            *connection_p = system_bus_weak_ref;
        }
    }

    dbus_g_connection_unref(gconnection); /* rely on libdbus holding a ref */

    gjs_debug(GJS_DEBUG_DBUS, "Successfully connected");

    return connection;
}

static gboolean
connect_idle(void *data)
{
    GSList *l;
    DBusConnection *connection;
    GjsDBusInfo *info;
    DBusBusType bus_type;

    bus_type = GPOINTER_TO_INT(data);

    if (bus_type == DBUS_BUS_SESSION)
        session_connect_idle_id = 0;
    else if (bus_type == DBUS_BUS_SYSTEM)
        system_connect_idle_id = 0;
    else
        g_assert_not_reached();

    gjs_debug(GJS_DEBUG_DBUS, "connection idle with %d connect listeners to traverse", g_slist_length(all_connect_funcs));

    connection = try_connecting(bus_type);
    if (connection == NULL) {
        if (bus_type == DBUS_BUS_SESSION) {
            g_printerr("Lost connection to session bus, exiting\n");
            exit(1);
        } else {
            /* Here it would theoretically make sense to reinstall the
             * idle as a timeout or something, but we don't for now,
             * just wait for something to trigger a reconnect. It is
             * not a situation that should happen in reality (we won't
             * restart the system bus without rebooting).
             */
        }
        return FALSE;
    }

    info = _gjs_dbus_ensure_info(connection);

    /* We first need to call AddMatch on all signal watchers.
     * This is so if on connect, the app calls methods to get
     * the state the signal notifies the app of changes in,
     * the match rule is added before the "get current state"
     * methods are called. Otherwise there's a race where
     * a signal can be missed between a "get current state" method
     * call reply and the AddMatch.
     */
    _gjs_dbus_process_pending_signal_watchers(connection, info);

    /* We want the app to see notification of connection opening,
     * THEN other notifications, so notify it's open first.
     */

    for (l = all_connect_funcs; l != NULL; l = l->next) {
        ConnectFuncs *f;
        f = l->data;

        if (!f->opened && f->funcs->which_bus == bus_type) {
            f->opened = TRUE;
            (* f->funcs->opened) (connection, f->data);
        }
    }

    /* These two invoke application callbacks, unlike
     * _gjs_dbus_process_pending_signal_watchers(), so should come after
     * the above calls to the "connection opened" callbacks.
     */

    process_name_ownership_monitors(connection, info);

    process_pending_name_watchers(connection, info);

    return FALSE;
}

void
_gjs_dbus_ensure_connect_idle(DBusBusType bus_type)
{
    if (bus_type == DBUS_BUS_SESSION) {
        if (session_connect_idle_id == 0) {
            /* We use G_PRIORITY_HIGH to ensure that any deferred
             * work (such as setting up exports) happens *before*
             * potentially reading any messages from the socket.  If we
             * didn't, this could lead to race conditions.  See
             * https://bugzilla.gnome.org/show_bug.cgi?id=646246
             */
            session_connect_idle_id = g_timeout_add_full(G_PRIORITY_HIGH, 0, connect_idle,
                                                         GINT_TO_POINTER(bus_type), NULL);
        }
    } else if (bus_type == DBUS_BUS_SYSTEM) {
        if (system_connect_idle_id == 0) {
            system_connect_idle_id = g_timeout_add_full(G_PRIORITY_HIGH, 0, connect_idle,
                                                        GINT_TO_POINTER(bus_type), NULL);
        }
    } else {
        g_assert_not_reached();
    }
}

static void
internal_add_connect_funcs(const GjsDBusConnectFuncs *funcs,
                           void                      *data,
                           gboolean                   sync_notify)
{
    ConnectFuncs *f;

    f = g_slice_new0(ConnectFuncs);
    f->funcs = funcs;
    f->data = data;
    f->opened = FALSE;

    all_connect_funcs = g_slist_prepend(all_connect_funcs, f);

    if (sync_notify) {
        /* sync_notify means IF we are already connected
         * (we have a weak ref != NULL) then notify
         * right away before we return.
         */
        DBusConnection *connection;

        connection = gjs_dbus_get_weak_ref(f->funcs->which_bus);

        if (connection != NULL && !f->opened) {
            f->opened = TRUE;
            (* f->funcs->opened) (connection, f->data);
        }
    }
}

/* this should guarantee that the funcs are only called async, which is why
 * it does not try_connecting right away; the idea is to defer to inside the
 * main loop.
 */
void
gjs_dbus_add_connect_funcs(const GjsDBusConnectFuncs *funcs,
                           void                      *data)
{
    internal_add_connect_funcs(funcs, data, FALSE);
}

/* The sync_notify flavor calls the open notification right away if
 * we are already connected.
 */
void
gjs_dbus_add_connect_funcs_sync_notify(const GjsDBusConnectFuncs *funcs,
                                       void                      *data)
{
    internal_add_connect_funcs(funcs, data, TRUE);
}

void
gjs_dbus_remove_connect_funcs(const GjsDBusConnectFuncs *funcs,
                              void                      *data)
{
    ConnectFuncs *f;
    GSList *l;

    f = NULL;
    for (l = all_connect_funcs; l != NULL; l = l->next) {
        f = l->data;

        if (f->funcs == funcs &&
            f->data == data)
            break;
    }

    if (l == NULL) {
        g_warning("Could not find functions matching %p %p", funcs, data);
        return;
    }
    g_assert(l->data == f);

    all_connect_funcs = g_slist_delete_link(all_connect_funcs, l);
    g_slice_free(ConnectFuncs, f);
}

void
gjs_dbus_add_bus_weakref(DBusBusType      which_bus,
                         DBusConnection **connection_p)
{
    if (which_bus == DBUS_BUS_SESSION) {
        *connection_p = session_bus_weak_ref;
        session_bus_weak_refs = g_slist_prepend(session_bus_weak_refs, connection_p);
    } else if (which_bus == DBUS_BUS_SYSTEM) {
        *connection_p = system_bus_weak_ref;
        system_bus_weak_refs = g_slist_prepend(system_bus_weak_refs, connection_p);
    } else {
        g_assert_not_reached();
    }

    _gjs_dbus_ensure_connect_idle(which_bus);
}

void
gjs_dbus_remove_bus_weakref(DBusBusType      which_bus,
                            DBusConnection **connection_p)
{
    if (which_bus == DBUS_BUS_SESSION) {
        *connection_p = NULL;
        session_bus_weak_refs = g_slist_remove(session_bus_weak_refs, connection_p);
    } else if (which_bus == DBUS_BUS_SYSTEM) {
        *connection_p = NULL;
        system_bus_weak_refs = g_slist_remove(system_bus_weak_refs, connection_p);
    } else {
        g_assert_not_reached();
    }
}

void
gjs_dbus_try_connecting_now(DBusBusType which_bus)
{
    try_connecting(which_bus);
}

static GjsJsonIface*
json_iface_new(const char *name,
               const GjsDBusJsonMethod *methods,
               int n_methods)
{
    GjsJsonIface *iface;

    iface = g_slice_new0(GjsJsonIface);
    iface->name = g_strdup(name);
    iface->methods = methods;
    iface->n_methods = n_methods;

    return iface;
}

static void
json_iface_free(GjsJsonIface *iface)
{
    g_free(iface->name);
    g_slice_free(GjsJsonIface, iface);
}

static GjsNameOwnershipMonitor*
name_ownership_monitor_new(DBusBusType                  bus_type,
                           const GjsDBusNameOwnerFuncs *funcs,
                           void                        *data)
{
    GjsNameOwnershipMonitor *monitor;

    monitor = g_slice_new0(GjsNameOwnershipMonitor);
    monitor->bus_type = bus_type;
    monitor->prev_state = NAME_NOT_REQUESTED;
    monitor->state = NAME_NOT_REQUESTED;
    monitor->funcs = funcs;
    monitor->data = data;
    monitor->id = ++global_monitor_id;

    return  monitor;
}

static void
name_ownership_monitor_free(GjsNameOwnershipMonitor *monitor)
{

    g_slice_free(GjsNameOwnershipMonitor, monitor);
}

static GjsNameWatch*
name_watch_new(const char *name)
{
    GjsNameWatch *watch;

    watch = g_slice_new0(GjsNameWatch);
    watch->name = g_strdup(name);

    /* For unique names, we assume the owner is itself,
     * so we default to "exists" and maybe emit "vanished",
     * while with well-known names we do the opposite.
     */
    if (*watch->name == ':') {
        watch->current_owner = g_strdup(watch->name);
    }

    return watch;
}

static void
name_watch_free(GjsNameWatch *watch)
{
    g_assert(watch->watchers == NULL);

    g_free(watch->name);
    g_free(watch->current_owner);
    g_slice_free(GjsNameWatch, watch);
}

static GjsNameWatcher*
name_watcher_new(GjsDBusWatchNameFlags        flags,
                 const GjsDBusWatchNameFuncs *funcs,
                 void                        *data,
                 DBusBusType                  bus_type)
{
    GjsNameWatcher *watcher;

    watcher = g_slice_new0(GjsNameWatcher);
    watcher->flags = flags;
    watcher->funcs = funcs;
    watcher->data = data;
    watcher->bus_type = bus_type;
    watcher->watch = NULL;
    watcher->refcount = 1;

    return watcher;
}

static void
name_watcher_ref(GjsNameWatcher *watcher)
{
    watcher->refcount += 1;
}

static void
name_watcher_unref(GjsNameWatcher *watcher)
{
    watcher->refcount -= 1;

    if (watcher->refcount == 0)
        g_slice_free(GjsNameWatcher, watcher);
}

static void
info_free(GjsDBusInfo *info)
{
    void *key;
    void *value;

    gjs_debug(GJS_DEBUG_DBUS, "Destroy notify invoked on bus connection info for %p",
            info->where_connection_was);

    if (info->where_connection_was == session_bus_weak_ref)
        session_bus_weak_ref = NULL;

    if (info->where_connection_was == system_bus_weak_ref)
        system_bus_weak_ref = NULL;

    /* This could create some strange re-entrancy so do it first.
     * If we processed a disconnect message, this should have been done
     * already at that time, but if we were finalized without that,
     * it may not have been.
     */
    if (info->driver_proxy != NULL) {
        g_object_unref(info->driver_proxy);
        info->driver_proxy = NULL;
    }

    while (info->name_ownership_monitors != NULL) {
        name_ownership_monitor_free(info->name_ownership_monitors->data);
        info->name_ownership_monitors = g_slist_remove(info->name_ownership_monitors,
                                                       info->name_ownership_monitors->data);
    }

    {
      GHashTableIter iter;
      g_hash_table_iter_init (&iter, info->name_watches);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GjsNameWatch *watch = value;

        g_hash_table_iter_steal (&iter);

        while (watch->watchers) {
            name_watch_remove_watcher(watch, watch->watchers->data);
        }

        name_watch_free(watch);
      }
    }

    if (info->signal_watchers_by_unique_sender) {
        g_hash_table_destroy(info->signal_watchers_by_unique_sender);
    }

    if (info->signal_watchers_by_path) {
        g_hash_table_destroy(info->signal_watchers_by_path);
    }

    if (info->signal_watchers_by_iface) {
        g_hash_table_destroy(info->signal_watchers_by_iface);
    }

    if (info->signal_watchers_by_signal) {
        g_hash_table_destroy(info->signal_watchers_by_signal);
    }

    g_hash_table_destroy(info->name_watches);
    g_hash_table_destroy(info->json_ifaces);
    g_slice_free(GjsDBusInfo, info);
}

static DBusHandlerResult
name_ownership_monitor_filter_message(DBusConnection *connection,
                                      DBusMessage    *message,
                                      void           *data)
{
    GjsDBusInfo *info;
    gboolean states_changed;

    info = _gjs_dbus_ensure_info(connection);

    states_changed = FALSE;

    if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameLost") &&
        dbus_message_has_sender(message, DBUS_SERVICE_DBUS)) {
        const char *name = NULL;
        if (dbus_message_get_args(message, NULL,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID)) {
            GSList *l;

            gjs_debug(GJS_DEBUG_DBUS, "Lost name %s", name);

            for (l = info->name_ownership_monitors; l != NULL; l = l->next) {
                GjsNameOwnershipMonitor *monitor;

                monitor = l->data;

                if (monitor->state == NAME_PRIMARY_OWNER &&
                    strcmp(name, monitor->funcs->name) == 0) {
                    monitor->prev_state = monitor->state;
                    monitor->state = NAME_NOT_OWNED;
                    states_changed = TRUE;
                    /* keep going, don't break, there may be more matches */
                }
            }
        } else {
            gjs_debug(GJS_DEBUG_DBUS, "NameLost has wrong arguments???");
        }
    } else if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameAcquired") &&
               dbus_message_has_sender(message, DBUS_SERVICE_DBUS)) {
        const char *name = NULL;
        if (dbus_message_get_args(message, NULL,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID)) {
            GSList *l;

            gjs_debug(GJS_DEBUG_DBUS, "Acquired name %s", name);

            for (l = info->name_ownership_monitors; l != NULL; l = l->next) {
                GjsNameOwnershipMonitor *monitor;

                monitor = l->data;

                if (monitor->state != NAME_PRIMARY_OWNER &&
                    strcmp(name, monitor->funcs->name) == 0) {
                    monitor->prev_state = monitor->state;
                    monitor->state = NAME_PRIMARY_OWNER;
                    states_changed = TRUE;
                    /* keep going, don't break, there may be more matches */
                }
            }
        } else {
            gjs_debug(GJS_DEBUG_DBUS, "NameAcquired has wrong arguments???");
        }
    } else if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
        GSList *l;

        gjs_debug(GJS_DEBUG_DBUS, "Disconnected in %s", G_STRFUNC);

        for (l = info->name_ownership_monitors; l != NULL; l = l->next) {
            GjsNameOwnershipMonitor *monitor;

            monitor = l->data;

            if (monitor->state != NAME_NOT_REQUESTED) {
                /* Set things up to re-request the name */
                monitor->prev_state = monitor->state;
                monitor->state = NAME_NOT_REQUESTED;
                states_changed = TRUE;
            }
        }

        /* FIXME move the monitors back to the pending list so they'll be found on reconnect */
    }

    if (states_changed)
        process_name_ownership_monitors(connection, info);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
process_name_ownership_monitors(DBusConnection *connection,
                                GjsDBusInfo    *info)
{
    GSList *l;
    gboolean connected;
    GSList *still_pending;

    /* First pull anything out of pending queue */

    still_pending = NULL;
    while (pending_name_ownership_monitors != NULL) {
        GjsNameOwnershipMonitor *monitor;

        monitor = pending_name_ownership_monitors->data;
        pending_name_ownership_monitors =
            g_slist_remove(pending_name_ownership_monitors,
                           pending_name_ownership_monitors->data);

        if (monitor->bus_type == info->bus_type) {
            info->name_ownership_monitors =
                g_slist_prepend(info->name_ownership_monitors,
                                monitor);
        } else {
            still_pending = g_slist_prepend(still_pending, monitor);
        }
    }
    g_assert(pending_name_ownership_monitors == NULL);
    pending_name_ownership_monitors = still_pending;

    /* Now send notifications to the app */

    connected = dbus_connection_get_is_connected(connection);

    if (connected) {
        for (l = info->name_ownership_monitors; l != NULL; l = l->next) {
            GjsNameOwnershipMonitor *monitor;

            monitor = l->data;

            if (monitor->state == NAME_NOT_REQUESTED) {
                int result;
                unsigned int flags;
                DBusError derror;

                flags = DBUS_NAME_FLAG_ALLOW_REPLACEMENT;
                if (monitor->funcs->type == GJS_DBUS_NAME_SINGLE_INSTANCE)
                    flags |= DBUS_NAME_FLAG_DO_NOT_QUEUE;

                dbus_error_init(&derror);
                result = dbus_bus_request_name(connection,
                                               monitor->funcs->name,
                                               flags,
                                               &derror);

                /* log 'error' word only when one occurred */
                if (derror.message != NULL) {
                    gjs_debug(GJS_DEBUG_DBUS, "Requested name %s result %d error %s",
                            monitor->funcs->name, result, derror.message);
                } else {
                    gjs_debug(GJS_DEBUG_DBUS, "Requested name %s result %d",
                            monitor->funcs->name, result);
                }

                dbus_error_free(&derror);

                /* An important feature of this code is that we always
                 * transition from NOT_REQUESTED to something else when
                 * a name monitor is first added, so we always notify
                 * the app either "acquired" or "lost" and don't
                 * leave the app in limbo.
                 *
                 * This means the app can "get going" when it gets the name
                 * and exit when it loses it, and that will just work
                 * since one or the other will always happen on startup.
                 */

                monitor->prev_state = monitor->state;

                if (result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
                    result == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) {
                    monitor->state = NAME_PRIMARY_OWNER;
                } else if (result == DBUS_REQUEST_NAME_REPLY_IN_QUEUE) {
                    monitor->state = NAME_IN_QUEUE;
                } else if (result == DBUS_REQUEST_NAME_REPLY_EXISTS) {
                    monitor->state = NAME_NOT_OWNED;
                } else {
                    /* reply code we don't understand? */
                    monitor->state = NAME_NOT_OWNED;
                }
            }
        }
    }

    /* Do notifications with a list copy for extra safety
     * (for true safety we also need to refcount each monitor
     * and have a "destroyed" flag)
     */
    l = g_slist_copy(info->name_ownership_monitors);
    while (l != NULL) {
        GjsNameOwnershipMonitor *monitor;

        monitor = l->data;
        l = g_slist_remove(l, l->data);

        if (monitor->prev_state != monitor->state) {
            monitor->prev_state = monitor->state;

            if (monitor->state == NAME_PRIMARY_OWNER) {
                gjs_debug(GJS_DEBUG_DBUS, "Notifying acquired %s",
                        monitor->funcs->name);
                (* monitor->funcs->acquired) (connection, monitor->funcs->name, monitor->data);
            } else if (monitor->state != NAME_PRIMARY_OWNER) {
                gjs_debug(GJS_DEBUG_DBUS, "Notifying lost %s",
                        monitor->funcs->name);
                (* monitor->funcs->lost) (connection, monitor->funcs->name, monitor->data);
            }
        }
    }
}

unsigned int
gjs_dbus_acquire_name (DBusBusType                  bus_type,
                       const GjsDBusNameOwnerFuncs *funcs,
                       void                        *data)
{
    GjsNameOwnershipMonitor *monitor;

    monitor = name_ownership_monitor_new(bus_type, funcs, data);
    pending_name_ownership_monitors = g_slist_prepend(pending_name_ownership_monitors, monitor);

    _gjs_dbus_ensure_connect_idle(bus_type);

    return monitor->id;
}

static void
release_name_internal (DBusBusType                  bus_type,
                       const GjsDBusNameOwnerFuncs *funcs,
                       void                        *data,
                       unsigned int                 id)
{
    GjsDBusInfo *info;
    GSList *l;
    GjsNameOwnershipMonitor *monitor;
    DBusConnection *connection;

    connection = gjs_dbus_get_weak_ref(bus_type);
    if (!connection)
        return;

    info = _gjs_dbus_ensure_info(connection);

    /* Check first pending list */
    for (l = pending_name_ownership_monitors; l; l = l->next) {
        monitor = l->data;
        /* If the id is valid an matches, we are done */
        if (monitor->state == NAME_PRIMARY_OWNER &&
            ((id != GJS_DBUS_NAME_OWNER_MONITOR_INVALID_ID && monitor->id == id) ||
             (monitor->funcs == funcs &&
              monitor->data == data))) {
            dbus_bus_release_name(connection, monitor->funcs->name, NULL);
            pending_name_ownership_monitors =
                g_slist_remove(pending_name_ownership_monitors,
                               monitor);
            name_ownership_monitor_free(monitor);
            /* If the monitor was in the pending list it
             * can't be in the processed list
             */
            return;
        }
    }

    for (l = info->name_ownership_monitors; l; l = l->next) {
        monitor = l->data;
        /* If the id is valid an matches, we are done */
        if (monitor->state == NAME_PRIMARY_OWNER &&
            ((id != GJS_DBUS_NAME_OWNER_MONITOR_INVALID_ID && monitor->id == id) ||
             (monitor->funcs == funcs &&
              monitor->data == data))) {
            dbus_bus_release_name(connection, monitor->funcs->name, NULL);
            info->name_ownership_monitors = g_slist_remove(info->name_ownership_monitors,
                                                           monitor);
            name_ownership_monitor_free(monitor);
            break;
        }
    }
}

void
gjs_dbus_release_name_by_id (DBusBusType  bus_type,
                             unsigned int id)
{
    release_name_internal(bus_type, NULL, NULL, id);
}

void
gjs_dbus_release_name (DBusBusType                  bus_type,
                       const GjsDBusNameOwnerFuncs *funcs,
                       void                        *data)
{
    release_name_internal(bus_type, funcs, data,
                          GJS_DBUS_NAME_OWNER_MONITOR_INVALID_ID);
}

static void
notify_name_owner_changed(DBusConnection *connection,
                          const char     *name,
                          const char     *new_owner)
{
    GjsDBusInfo *info;
    GjsNameWatch *watch;
    GSList *l, *watchers;
    gchar *old_owner;

    info = _gjs_dbus_ensure_info(connection);

    if (*new_owner == '\0')
        new_owner = NULL;

    watch = g_hash_table_lookup(info->name_watches, name);

    if (watch == NULL)
        return;

    if ((watch->current_owner == new_owner) ||
        (watch->current_owner && new_owner &&
         strcmp(watch->current_owner, new_owner) == 0)) {
        /* No change */
        return;
    }

    /* we copy the list before iterating, because the
     * callbacks may modify it */
    watchers = g_slist_copy(watch->watchers);
    g_slist_foreach(watchers, (GFunc)name_watcher_ref, NULL);

    /* copy the old owner in case the watch is removed in
     * the callbacks */
    old_owner = g_strdup(watch->current_owner);

    /* vanish the old owner */
    if (old_owner != NULL) {
        for (l = watchers;
             l != NULL;
             l = l->next) {
            GjsNameWatcher *watcher = l->data;

            if (watcher->notify_idle != 0) {
                /* Name owner changed before we notified
                 * the watcher of the initial name. We will notify
                 * him now of the old name, then that this name
                 * vanished.
                 *
                 * This is better than not sending calling any
                 * callback, it might for instance trigger destroying
                 * signal watchers on the unique name.
                 */
                g_source_remove(watcher->notify_idle);
                notify_watcher_name_appeared(watcher);
            }

            if (!watcher->destroyed) {
                (* watcher->funcs->vanished) (connection,
                                              name,
                                              old_owner,
                                              watcher->data);
            }
        }
    }

    /* lookup for the watch again, since it might have vanished
     * if all watchers were removed in the watcher->vanished
     * callbacks */
    watch = g_hash_table_lookup(info->name_watches, name);

    if (watch) {
        g_free(watch->current_owner);
        watch->current_owner = g_strdup(new_owner);
    }

    /* appear the new owner */
    if (new_owner != NULL) {
        for (l = watchers;
             l != NULL;
             l = l->next) {
            GjsNameWatcher *watcher = l->data;

            if (!watcher->destroyed) {
                (* watcher->funcs->appeared) (connection,
                                              name,
                                              new_owner,
                                              watcher->data);
            }
        }
    }

    /* now destroy our copy */
    g_slist_foreach(watchers, (GFunc)name_watcher_unref, NULL);
    g_slist_free(watchers);

    g_free(old_owner);
}

static DBusHandlerResult
name_watch_filter_message(DBusConnection *connection,
                          DBusMessage    *message,
                          void           *data)
{
    GjsDBusInfo *info;

    info = _gjs_dbus_ensure_info(connection);
    (void) info;

    if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        dbus_message_has_sender(message, DBUS_SERVICE_DBUS)) {
        const char *name = NULL;
        const char *old_owner = NULL;
        const char *new_owner = NULL;
        if (dbus_message_get_args(message, NULL,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_STRING, &old_owner,
                                  DBUS_TYPE_STRING, &new_owner,
                                  DBUS_TYPE_INVALID)) {
            gjs_debug(GJS_DEBUG_DBUS, "NameOwnerChanged %s:   %s -> %s",
                    name, old_owner, new_owner);

            notify_name_owner_changed(connection, name, new_owner);
        } else {
            gjs_debug(GJS_DEBUG_DBUS, "NameOwnerChanged has wrong arguments???");
        }
    } else if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL, "Disconnected")) {

        gjs_debug(GJS_DEBUG_DBUS, "Disconnected in %s", G_STRFUNC);

        /* FIXME set all current owners to NULL, and move watches back to the pending
         * list so they are found on reconnect.
         */
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void
_gjs_dbus_set_matching_name_owner_changed(DBusConnection *connection,
                                          const char     *bus_name,
                                          gboolean        matched)
{
    char *s;

    gjs_debug(GJS_DEBUG_DBUS, "%s NameOwnerChanged on name '%s'",
              matched ? "Matching" : "No longer matching",
              bus_name);

    s = g_strdup_printf("type='signal',sender='"
                        DBUS_SERVICE_DBUS
                        "',interface='"
                        DBUS_INTERFACE_DBUS
                        "',member='"
                        "NameOwnerChanged"
                        "',arg0='%s'",
                        bus_name);

    if (matched)
        dbus_bus_add_match(connection,
                           s, NULL); /* asking for error would make this block */
    else
        dbus_bus_remove_match(connection, s, NULL);

    g_free(s);
}

static void
on_start_service_reply(GjsDBusProxy    *proxy,
                       DBusMessage     *message,
                       char            *name)
{
    gjs_debug(GJS_DEBUG_DBUS, "Got successful reply to service '%s' start", name);
    g_free(name);
}

static void
on_start_service_error(GjsDBusProxy    *proxy,
                       const char      *error_name,
                       const char      *error_message,
                       char            *name)
{
    gjs_debug(GJS_DEBUG_DBUS, "Got error starting service '%s': %s: %s",
            name, error_name, error_message);
    g_free(name);
}

void
gjs_dbus_start_service(DBusConnection *connection,
                       const char     *name)
{
    DBusMessage *message;
    dbus_uint32_t flags;
    GjsDBusInfo *info;

    gjs_debug(GJS_DEBUG_DBUS, "Starting service '%s'",
            name);

    info = _gjs_dbus_ensure_info(connection);

    message = gjs_dbus_proxy_new_method_call(info->driver_proxy,
                                             "StartServiceByName");

    flags = 0;
    if (dbus_message_append_args(message,
                                 DBUS_TYPE_STRING, &name,
                                 DBUS_TYPE_UINT32, &flags,
                                 DBUS_TYPE_INVALID)) {
        gjs_dbus_proxy_send(info->driver_proxy,
                            message,
                            (GjsDBusProxyReplyFunc)on_start_service_reply,
                            (GjsDBusProxyErrorReplyFunc)on_start_service_error,
                            g_strdup(name));
    } else {
        gjs_debug(GJS_DEBUG_DBUS, "No memory appending args to StartServiceByName");
    }

    dbus_message_unref(message);
}

typedef struct {
    DBusConnection *connection;
    char *name;
    GjsDBusWatchNameFlags flags;
} GetOwnerRequest;

static GetOwnerRequest*
get_owner_request_new(DBusConnection       *connection,
                      const char           *name,
                      GjsDBusWatchNameFlags flags)
{
    GetOwnerRequest *gor;

    gor = g_slice_new0(GetOwnerRequest);
    gor->connection = connection;
    gor->name = g_strdup(name);
    gor->flags = flags;
    dbus_connection_ref(connection);

    return gor;
}

static void
get_owner_request_free(GetOwnerRequest *gor)
{
    dbus_connection_unref(gor->connection);
    g_free(gor->name);
    g_slice_free(GetOwnerRequest, gor);
}

static void
on_get_owner_reply(DBusPendingCall *pending,
                   void            *user_data)
{
    DBusMessage *reply;
    GetOwnerRequest *gor;

    gor = user_data;

    reply = dbus_pending_call_steal_reply(pending);
    if (reply == NULL) {
        g_warning("NULL reply in on_get_owner_reply?");
        return;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        const char *current_owner = NULL;

        if (!dbus_message_get_args(reply, NULL,
                                   DBUS_TYPE_STRING, &current_owner,
                                   DBUS_TYPE_INVALID)) {
            gjs_debug(GJS_DEBUG_DBUS, "GetNameOwner has wrong args '%s'",
                    dbus_message_get_signature(reply));
        } else {
            gjs_debug(GJS_DEBUG_DBUS, "Got owner '%s' for name '%s'",
                    current_owner, gor->name);
            if (current_owner != NULL) {
                notify_name_owner_changed(gor->connection,
                                          gor->name,
                                          current_owner);
            }
        }
    } else if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        if (g_str_equal(dbus_message_get_error_name(reply),
                        DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            gjs_debug(GJS_DEBUG_DBUS, "'%s' was not running",
                    gor->name);
            if (gor->flags & GJS_DBUS_NAME_START_IF_NOT_FOUND) {
                gjs_debug(GJS_DEBUG_DBUS, "  (starting it up)");
                gjs_dbus_start_service(gor->connection, gor->name);
            } else {
                /* no owner for now, notify app */
                notify_name_owner_changed(gor->connection,
                                          gor->name,
                                          "");
            }
        } else {
            gjs_debug(GJS_DEBUG_DBUS, "Error getting owner of name '%s': %s",
                    gor->name,
                    dbus_message_get_error_name(reply));

            /* Notify no owner for now, ensuring the app
             * gets advised "appeared" or "vanished",
             * one or the other.
             */
            notify_name_owner_changed(gor->connection,
                                      gor->name,
                                      "");
        }
    } else {
        gjs_debug(GJS_DEBUG_DBUS, "Nonsensical reply type to GetNameOwner");
    }

    dbus_message_unref(reply);
}

static void
request_name_owner(DBusConnection *connection,
                   GjsDBusInfo    *info,
                   GjsNameWatch   *watch)
{
    DBusMessage *message;
    DBusPendingCall *call;

    message = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
                                           DBUS_PATH_DBUS,
                                           DBUS_INTERFACE_DBUS,
                                           "GetNameOwner");
    if (message == NULL)
        g_error("no memory");

    if (!dbus_message_append_args(message,
                                  DBUS_TYPE_STRING, &watch->name,
                                  DBUS_TYPE_INVALID))
        g_error("no memory");

    call = NULL;
    dbus_connection_send_with_reply(connection, message, &call, -1);
    dbus_message_unref(message);

    if (call != NULL) {
        GetOwnerRequest *gor;
        GjsDBusWatchNameFlags flags;
        GSList *l;

        gjs_debug(GJS_DEBUG_DBUS, "Sent GetNameOwner for '%s'",
                watch->name);

        flags = 0;
        for (l = watch->watchers;
             l != NULL;
             l = l->next) {
            GjsNameWatcher *watcher = l->data;

            if (watcher->flags & GJS_DBUS_NAME_START_IF_NOT_FOUND)
                flags |= GJS_DBUS_NAME_START_IF_NOT_FOUND;
        }

        gor = get_owner_request_new(connection, watch->name, flags);

        if (!dbus_pending_call_set_notify(call, on_get_owner_reply,
                                          gor,
                                          (DBusFreeFunction) get_owner_request_free))
            g_error("no memory");

        /* the connection will hold a ref to the pending call */
        dbus_pending_call_unref(call);
    } else {
        gjs_debug(GJS_DEBUG_DBUS, "GetNameOwner for '%s' not sent, connection disconnected",
                watch->name);
    }
}

static gboolean
notify_watcher_name_appeared(gpointer data)
{
    GjsNameWatcher *watcher;
    DBusConnection *connection;

    watcher = data;
    watcher->notify_idle = 0;

    connection = gjs_dbus_get_weak_ref(watcher->bus_type);

    if (!connection)
        return FALSE;

    (* watcher->funcs->appeared) (connection,
                                  watcher->watch->name,
                                  watcher->watch->current_owner,
                                  watcher->data);
    return FALSE;
}

static void
create_watch_for_watcher(DBusConnection *connection,
                         GjsDBusInfo    *info,
                         const char     *name,
                         GjsNameWatcher *watcher)
{
    GjsNameWatch *watch;

    watch = g_hash_table_lookup(info->name_watches, name);
    if (watch == NULL) {
        watch = name_watch_new(name);

        g_hash_table_replace(info->name_watches, watch->name, watch);

        watch->watchers = g_slist_prepend(watch->watchers, watcher);

        _gjs_dbus_set_matching_name_owner_changed(connection, watch->name, TRUE);

        request_name_owner(connection, info, watch);
    } else {
        watch->watchers = g_slist_prepend(watch->watchers, watcher);
    }
    name_watcher_ref(watcher);

    watcher->watch = watch;

}

static void
process_pending_name_watchers(DBusConnection *connection,
                              GjsDBusInfo    *info)
{
    GSList *still_pending;

    still_pending = NULL;
    while (pending_name_watchers != NULL) {
        GjsPendingNameWatcher *pending;
        GjsNameWatch *watch;

        pending = pending_name_watchers->data;
        pending_name_watchers = g_slist_remove(pending_name_watchers,
                                               pending_name_watchers->data);

        if (pending->bus_type != info->bus_type) {
            still_pending = g_slist_prepend(still_pending, pending);
            continue;
        }

        create_watch_for_watcher(connection,
                                 info,
                                 pending->name,
                                 pending->watcher);

        watch = pending->watcher->watch;

        /* If we already know the owner, let the new watcher know */
        if (watch->current_owner != NULL) {
            (* pending->watcher->funcs->appeared) (connection,
                                                   watch->name,
                                                   watch->current_owner,
                                                   pending->watcher->data);
        }

        g_free(pending->name);
        name_watcher_unref(pending->watcher);
        g_slice_free(GjsPendingNameWatcher, pending);
    }

    g_assert(pending_name_watchers == NULL);
    pending_name_watchers = still_pending;
}

static void
name_watch_remove_watcher(GjsNameWatch     *watch,
                          GjsNameWatcher   *watcher)
{
    watch->watchers = g_slist_remove(watch->watchers,
                                     watcher);

    if (watcher->notify_idle) {
        g_source_remove(watcher->notify_idle);
        watcher->notify_idle = 0;
    }

    watcher->destroyed = TRUE;
    name_watcher_unref(watcher);
}

void
gjs_dbus_watch_name(DBusBusType                  bus_type,
                    const char                  *name,
                    GjsDBusWatchNameFlags        flags,
                    const GjsDBusWatchNameFuncs *funcs,
                    void                        *data)
{
    GjsNameWatcher *watcher;
    DBusConnection *connection;

    gjs_debug(GJS_DEBUG_DBUS, "Adding watch on name '%s'",
            name);

    watcher = name_watcher_new(flags, funcs, data, bus_type);

    connection = gjs_dbus_get_weak_ref(bus_type);

    if (connection) {
        GjsDBusInfo *info;

        info = _gjs_dbus_ensure_info(connection);

        create_watch_for_watcher(connection,
                                 info,
                                 name,
                                 watcher);
        /* The initial reference is now transferred to the watch */
        name_watcher_unref(watcher);

        /* If we already know the owner, notify the user in an idle */
        if (watcher->watch->current_owner) {
            watcher->notify_idle =
                g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                notify_watcher_name_appeared,
                                watcher,
                                (GDestroyNotify)name_watcher_unref);
            name_watcher_ref(watcher);
        }

    } else {
        GjsPendingNameWatcher *pending;

        pending = g_slice_new0(GjsPendingNameWatcher);

        pending->bus_type = bus_type;
        pending->name = g_strdup(name);
        pending->watcher = watcher;

        pending_name_watchers = g_slist_prepend(pending_name_watchers, pending);

        _gjs_dbus_ensure_connect_idle(pending->bus_type);
    }
}

void
gjs_dbus_unwatch_name(DBusBusType                  bus_type,
                      const char                  *name,
                      const GjsDBusWatchNameFuncs *funcs,
                      void                        *data)
{
    DBusConnection *connection;
    GjsDBusInfo *info;
    GjsNameWatch *watch;
    GSList *l;
    GjsNameWatcher *watcher;

    gjs_debug(GJS_DEBUG_DBUS, "Removing watch on name '%s'",
            name);

    connection = gjs_dbus_get_weak_ref(bus_type);
    if (connection == NULL) {
        /* right now our state is entirely hosed if we disconnect
         * (we don't move the watchers out of the connection data),
         * so can't do much here without larger changes to the file
         */
        g_warning("Have not implemented disconnect handling");
        return;
    }

    info = _gjs_dbus_ensure_info(connection);

    /* could still be pending */
    process_pending_name_watchers(connection, info);

    watch = g_hash_table_lookup(info->name_watches, name);

    if (watch == NULL) {
        g_warning("attempt to unwatch name %s but nobody is watching that",
                  name);
        return;
    }

    watcher = NULL;
    for (l = watch->watchers; l != NULL; l = l->next) {
        watcher = l->data;

        if (watcher->funcs == funcs &&
            watcher->data == data)
            break;
    }

    if (l == NULL) {
        g_warning("Could not find a watch on %s matching %p %p",
                  name, funcs, data);
        return;
    }
    g_assert(l->data == watcher);

    name_watch_remove_watcher(watch, watcher);

    /* Clear out the watch if it's gone */
    if (watch->watchers == NULL) {
        g_hash_table_remove(info->name_watches, watch->name);

        _gjs_dbus_set_matching_name_owner_changed(connection, watch->name, FALSE);

        name_watch_free(watch);
    }
}

const char*
gjs_dbus_get_watched_name_owner(DBusBusType   bus_type,
                                const char   *name)
{
    DBusConnection *connection;
    GjsNameWatch *watch;
    GjsDBusInfo *info;

    connection = gjs_dbus_get_weak_ref(bus_type);
    if (connection == NULL) {
        return NULL;
    }

    info = _gjs_dbus_ensure_info(connection);

    /* could still be pending */
    process_pending_name_watchers(connection, info);

    watch = g_hash_table_lookup(info->name_watches, name);
    if (watch == NULL) {
        g_warning("Tried to get owner of '%s' but there is no watch on it",
                  name);
        return NULL;
    }

    return watch->current_owner;
}

void
gjs_dbus_register_json(DBusConnection          *connection,
                       const char              *iface_name,
                       const GjsDBusJsonMethod *methods,
                       int                      n_methods)
{
    GjsDBusInfo *info;
    GjsJsonIface *iface;

    info = _gjs_dbus_ensure_info(connection);

    iface = json_iface_new(iface_name, methods, n_methods);

    g_hash_table_replace(info->json_ifaces, iface->name, iface);
}

void
gjs_dbus_unregister_json(DBusConnection *connection,
                         const char     *iface_name)
{
    GjsDBusInfo *info;

    info = _gjs_dbus_ensure_info(connection);

    g_hash_table_remove(info->json_ifaces, iface_name);
}

typedef struct {
    DBusConnection *connection;
    /* strict aliasing rules require us to relate the 'gobj' field to a
     * void * type here, in order to be able to pass it to
     * g_object_*_weak_pointer (which takes a void **) */
    union { GObject *gobj; void *weak_ptr; };
    char *iface_name;
} GjsDBusGObject;

static void
gobj_path_unregistered(DBusConnection  *connection,
                       void            *user_data)
{
    GjsDBusGObject *g;

    g = user_data;

    if (g->gobj) {
        g_object_remove_weak_pointer(g->gobj, &g->weak_ptr /* aka, gobj */);
        g->gobj = NULL;
    }

    g_free(g->iface_name);
    g_slice_free(GjsDBusGObject, g);
}

static DBusHandlerResult
gobj_path_message(DBusConnection  *connection,
                  DBusMessage     *message,
                  void            *user_data)
{
    GjsDBusGObject *g;
    GjsDBusInfo *info;
    GjsJsonIface *iface;
    const char *message_iface;
    const char *message_method;
    DBusError derror;
    int i;
    const GjsDBusJsonMethod *method;
    DBusMessageIter arg_iter, dict_iter;

    info = _gjs_dbus_ensure_info(connection);
    g = user_data;

    gjs_debug(GJS_DEBUG_DBUS, "Received message to iface %s gobj %p",
            g->iface_name, g->gobj);

    if (g->gobj == NULL) {
        /* GObject was destroyed */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_error_init(&derror);

    message_iface = dbus_message_get_interface(message);

    /* FIXME implement Introspectable() just to enable dbus debugger */

    if (message_iface != NULL &&
        strcmp(message_iface, g->iface_name) != 0) {

        dbus_set_error(&derror, DBUS_ERROR_UNKNOWN_METHOD,
                       "Interface '%s' not implemented by this object, did you mean '%s'?",
                       message_iface, g->iface_name);

        goto out;
    }

    iface = g_hash_table_lookup(info->json_ifaces,
                                g->iface_name);
    if (iface == NULL) {
        g_warning("Object registered with iface %s but that iface is not registered",
                  g->iface_name);
        dbus_set_error(&derror, DBUS_ERROR_UNKNOWN_METHOD,
                       "Bug - '%s' is not registered",
                       g->iface_name);
        goto out;
    }

    method = NULL;
    message_method = dbus_message_get_member(message);
    for (i = 0; i < iface->n_methods; ++i) {
        if (strcmp(message_method, iface->methods[i].name) == 0) {
            method = &iface->methods[i];
            break;
        }
    }

    if (method == NULL) {
        dbus_set_error(&derror, DBUS_ERROR_UNKNOWN_METHOD,
                       "Interface '%s' has no method '%s'",
                       g->iface_name, message_method);
        goto out;
    }

    if (!dbus_message_has_signature(message, "a{sv}")) {
        dbus_set_error(&derror, DBUS_ERROR_INVALID_ARGS,
                       "Method %s.%s should have 1 argument which is a dictionary",
                       g->iface_name, message_method);
        goto out;
    }

    dbus_message_iter_init(message, &arg_iter);
    dbus_message_iter_recurse(&arg_iter, &dict_iter);

    if (method->sync_func != NULL) {
        DBusMessage *reply;
        DBusMessageIter out_arg_iter, out_dict_iter;

        reply = dbus_message_new_method_return(message);
        if (reply == NULL) {
            dbus_set_error(&derror, DBUS_ERROR_NO_MEMORY,
                           "No memory");
            goto out;
        }

        dbus_message_iter_init_append(reply, &out_arg_iter);
        dbus_message_iter_open_container(&out_arg_iter,
                                         DBUS_TYPE_ARRAY, "{sv}",
                                         &out_dict_iter);

        g_object_ref(g->gobj);
        (* method->sync_func) (connection, message,
                               &dict_iter, &out_dict_iter,
                               g->gobj,
                               &derror);
        g_object_unref(g->gobj);

        dbus_message_iter_close_container(&out_arg_iter, &out_dict_iter);

        if (!dbus_error_is_set(&derror)) {
            dbus_connection_send(connection, reply, NULL);
        }
        dbus_message_unref(reply);

    } else if (method->async_func != NULL) {
        g_object_ref(g->gobj);
        (* method->async_func) (connection, message,
                                &dict_iter,
                                g->gobj);
        g_object_unref(g->gobj);
    } else {
        g_warning("Method %s does not have any implementation", method->name);
    }

 out:
    if (dbus_error_is_set(&derror)) {
        DBusMessage *reply;

        reply = dbus_message_new_error(message,
                                       derror.name,
                                       derror.message);
        dbus_error_free(&derror);

        if (reply != NULL) {
            dbus_connection_send(connection, reply, NULL);

            dbus_message_unref(reply);
        } else {
            /* use g_printerr not g_warning since this is NOT a "can
             * never happen" just a "probably will never happen"
             */
            g_printerr("Could not send OOM error\n");
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusObjectPathVTable gobj_vtable = {
    gobj_path_unregistered,
    gobj_path_message,
    NULL,
};

/* Note that because of how this works, each object can be registered
 * at multiple paths but only once per path. Which is sort of bizarre,
 * but we'll fix it when we need it.
 */
void
gjs_dbus_register_g_object(DBusConnection *connection,
                           const char     *path,
                           GObject        *gobj,
                           const char     *iface_name)
{
    GjsDBusGObject *g;

    g = g_slice_new0(GjsDBusGObject);
    g->iface_name = g_strdup(iface_name);
    g->gobj = gobj;

    if (!dbus_connection_register_object_path(connection, path,
                                              &gobj_vtable, g)) {
        g_warning("Failed to register object path %s", path);
    }

    g_object_add_weak_pointer(g->gobj, &g->weak_ptr /* aka, gobj */);
}

void
gjs_dbus_unregister_g_object (DBusConnection *connection,
                              const char     *path)
{
    dbus_connection_unregister_object_path(connection, path);
}

static void
open_json_entry(DBusMessageIter *dict_iter,
                const char      *key,
                const char      *signature,
                DBusMessageIter *entry_iter,
                DBusMessageIter *variant_iter)
{
    dbus_message_iter_open_container(dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, entry_iter);

    dbus_message_iter_append_basic(entry_iter, DBUS_TYPE_STRING, &key);

    dbus_message_iter_open_container(entry_iter, DBUS_TYPE_VARIANT, signature, variant_iter);
}

static void
close_json_entry(DBusMessageIter *dict_iter,
                 DBusMessageIter *entry_iter,
                 DBusMessageIter *variant_iter)
{
    dbus_message_iter_close_container(entry_iter, variant_iter);

    dbus_message_iter_close_container(dict_iter, entry_iter);
}

static void
open_json_entry_array(DBusMessageIter *dict_iter,
                      const char      *key,
                      int              array_element_type,
                      DBusMessageIter *entry_iter,
                      DBusMessageIter *variant_iter,
                      DBusMessageIter *array_iter)
{
    char buf[3];
    buf[0] = 'a';
    buf[1] = array_element_type;
    buf[2] = '\0';

    open_json_entry(dict_iter, key, buf, entry_iter, variant_iter);

    dbus_message_iter_open_container(variant_iter, DBUS_TYPE_ARRAY, &buf[1], array_iter);
}

static void
close_json_entry_array(DBusMessageIter *dict_iter,
                       DBusMessageIter *entry_iter,
                       DBusMessageIter *variant_iter,
                       DBusMessageIter *array_iter)
{
    dbus_message_iter_close_container(variant_iter, array_iter);

    close_json_entry(dict_iter, entry_iter, variant_iter);
}

void
gjs_dbus_append_json_entry (DBusMessageIter *dict_iter,
                            const char      *key,
                            int              dbus_type,
                            void            *basic_value_p)
{
    DBusMessageIter entry_iter, variant_iter;
    char buf[2];

    buf[0] = dbus_type;
    buf[1] = '\0';

    open_json_entry(dict_iter, key, buf, &entry_iter, &variant_iter);

    dbus_message_iter_append_basic(&variant_iter, dbus_type, basic_value_p);

    close_json_entry(dict_iter, &entry_iter, &variant_iter);
}

void
gjs_dbus_append_json_entry_STRING (DBusMessageIter *dict_iter,
                                   const char      *key,
                                   const char      *value)
{
    gjs_dbus_append_json_entry(dict_iter, key, DBUS_TYPE_STRING, &value);
}

void
gjs_dbus_append_json_entry_INT32 (DBusMessageIter *dict_iter,
                                  const char      *key,
                                  dbus_int32_t     value)
{
    gjs_dbus_append_json_entry(dict_iter, key, DBUS_TYPE_INT32, &value);
}

void
gjs_dbus_append_json_entry_DOUBLE (DBusMessageIter *dict_iter,
                                   const char      *key,
                                   double           value)
{
    gjs_dbus_append_json_entry(dict_iter, key, DBUS_TYPE_DOUBLE, &value);
}

void
gjs_dbus_append_json_entry_BOOLEAN (DBusMessageIter *dict_iter,
                                  const char      *key,
                                  dbus_bool_t      value)
{
    gjs_dbus_append_json_entry(dict_iter, key, DBUS_TYPE_BOOLEAN, &value);
}

/* when coming from a dynamic language, we don't know what type of array '[]' is supposed to be */
void
gjs_dbus_append_json_entry_EMPTY_ARRAY (DBusMessageIter  *dict_iter,
                                        const char       *key)
{
    DBusMessageIter entry_iter, variant_iter, array_iter;

    /* so just say VARIANT even though there won't be any elements in the array */
    open_json_entry_array(dict_iter, key, DBUS_TYPE_VARIANT, &entry_iter, &variant_iter, &array_iter);

    close_json_entry_array(dict_iter, &entry_iter, &variant_iter, &array_iter);
}

void
gjs_dbus_append_json_entry_STRING_ARRAY (DBusMessageIter  *dict_iter,
                                         const char       *key,
                                         const char      **value)
{
    DBusMessageIter entry_iter, variant_iter, array_iter;
    int i;

    open_json_entry_array(dict_iter, key, DBUS_TYPE_STRING, &entry_iter, &variant_iter, &array_iter);

    for (i = 0; value[i] != NULL; ++i) {
        dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &value[i]);
    }

    close_json_entry_array(dict_iter, &entry_iter, &variant_iter, &array_iter);
}

gboolean
gjs_dbus_message_iter_get_gsize(DBusMessageIter  *iter,
                                gsize            *value_p)
{
    switch (dbus_message_iter_get_arg_type(iter)) {
    case DBUS_TYPE_INT32:
        {
            dbus_int32_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v < 0)
                return FALSE;
            *value_p = v;
        }
        break;
    case DBUS_TYPE_UINT32:
        {
            dbus_uint32_t v;
            dbus_message_iter_get_basic(iter, &v);
            *value_p = v;
        }
        break;
    case DBUS_TYPE_INT64:
        {
            dbus_int64_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v < 0)
                return FALSE;
            if (((guint64)v) > G_MAXSIZE)
                return FALSE;
            *value_p = v;
        }
        break;
    case DBUS_TYPE_UINT64:
        {
            dbus_uint64_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v > G_MAXSIZE)
                return FALSE;
            *value_p = v;
        }
        break;
    default:
        return FALSE;
    }

    return TRUE;
}

gboolean
gjs_dbus_message_iter_get_gssize(DBusMessageIter  *iter,
                                 gssize           *value_p)
{
    switch (dbus_message_iter_get_arg_type(iter)) {
    case DBUS_TYPE_INT32:
        {
            dbus_int32_t v;
            dbus_message_iter_get_basic(iter, &v);
            *value_p = v;
        }
        break;
    case DBUS_TYPE_UINT32:
        {
            dbus_uint32_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v > (guint32) G_MAXSSIZE)
                return FALSE;
            *value_p = v;
        }
        break;
    case DBUS_TYPE_INT64:
        {
            dbus_int64_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v > (gint64) G_MAXSSIZE)
                return FALSE;
            if (v < (gint64) G_MINSSIZE)
                return FALSE;
            *value_p = v;
        }
        break;
    case DBUS_TYPE_UINT64:
        {
            dbus_uint64_t v;
            dbus_message_iter_get_basic(iter, &v);
            if (v > (guint64) G_MAXSSIZE)
                return FALSE;
            *value_p = v;
        }
        break;
    default:
        return FALSE;
    }

    return TRUE;
}

#if GJS_BUILD_TESTS

#include "dbus-proxy.h"
#include "dbus-input-stream.h"
#include "dbus-output-stream.h"

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

static pid_t test_service_pid = 0;
static GjsDBusProxy *test_service_proxy = NULL;

static pid_t test_io_pid = 0;
static GjsDBusProxy *test_io_proxy = NULL;

static GMainLoop *client_loop = NULL;

static int n_running_children = 0;

static GjsDBusInputStream  *input_from_io_service;
static GjsDBusOutputStream *output_to_io_service;

static const char stream_data_to_io_service[] = "This is sent from the main test process to the IO service.";
static const char stream_data_from_io_service[] = "This is sent from the IO service to the main test process. The quick brown fox, etc.";

static void do_test_service_child (void);
static void do_test_io_child      (void);

/* quit when all children are gone */
static void
another_child_down(void)
{
    g_assert(n_running_children > 0);
    n_running_children -= 1;

    if (n_running_children == 0) {
        g_main_loop_quit(client_loop);
    }
}

static const char*
extract_string_arg(DBusMessageIter *in_iter,
                   const char      *prop_name,
                   DBusError       *error)
{
    const char *s;

    s = NULL;
    while (dbus_message_iter_get_arg_type(in_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry_iter, variant_iter;
        const char *key;

        dbus_message_iter_recurse(in_iter, &entry_iter);

        dbus_message_iter_get_basic(&entry_iter, &key);

        if (strcmp(key, prop_name) == 0) {
            dbus_message_iter_next(&entry_iter);

            dbus_message_iter_recurse(&entry_iter, &variant_iter);
            if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING) {
                dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
                               "Value of '%s' prop should be a string",
                               prop_name);
                return NULL;
            }

            dbus_message_iter_get_basic(&variant_iter, &s);

            return s;
        }
    }

    dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
                   "No '%s' prop provided", prop_name);
    return NULL;
}

static void
fork_child_test_service(void)
{
    pid_t child_pid;

    /* it would break to fork after we already connected */
    g_assert(session_bus_weak_ref == NULL);
    g_assert(system_bus_weak_ref == NULL);
    g_assert(test_service_pid == 0);

    child_pid = fork();

    if (child_pid == -1) {
        g_error("Failed to fork dbus service");
    } else if (child_pid > 0) {
        /* We are the parent */
        test_service_pid = child_pid;
        n_running_children += 1;

        return;
    }

    /* we are the child, set up a service for main test process to talk to */

    do_test_service_child();
}

/* This test function doesn't really test anything, just sets up
 * for the following one
 */
static void
fork_child_test_io(void)
{
    pid_t child_pid;

    /* it would break to fork after we already connected */
    g_assert(session_bus_weak_ref == NULL);
    g_assert(system_bus_weak_ref == NULL);
    g_assert(test_io_pid == 0);

    child_pid = fork();

    if (child_pid == -1) {
        g_error("Failed to fork dbus service");
    } else if (child_pid > 0) {
        /* We are the parent */
        test_io_pid = child_pid;
        n_running_children += 1;

        return;
    }

    /* we are the child, set up a service for main test process to talk to */

    do_test_io_child();
}

static void
on_expected_fnf_error_reply_kill_child(GjsDBusProxy    *proxy,
                                       const char      *error_name,
                                       const char      *error_message,
                                       void            *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "got expected error reply to alwaysErrorSync, killing child");

    /* We were expecting an error, good. */
    if (strcmp(error_name, DBUS_ERROR_FILE_NOT_FOUND) != 0) {
        g_error("Got error we did not expect %s: %s",
                error_name, error_message);
    }

    if (kill(test_service_pid, SIGTERM) < 0) {
        g_error("Test service was no longer around... it must have failed somehow (%s)",
                strerror(errno));
    }

    /* We will quit main loop when we see the child go away */
}

static void
on_unexpected_error_reply(GjsDBusProxy    *proxy,
                          const char      *error_name,
                          const char      *error_message,
                          void            *data)
{
    const char *context_text = data;

    g_error("Got error %s: '%s' context was: %s",
            error_name, error_message, context_text);
}

static void
on_get_always_error_reply(GjsDBusProxy    *proxy,
                          DBusMessage     *message,
                          DBusMessageIter *return_value_iter,
                          void            *data)
{
    g_error("alwaysError json method supposed to return an error always, not a valid reply");
}

static void
on_get_some_stuff_reply(GjsDBusProxy    *proxy,
                        DBusMessage     *message,
                        DBusMessageIter *return_value_iter,
                        void            *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "reply received to getSomeStuffSync");

    /* FIXME look at the return value to see if it's what
     * the test service sends
     */

    gjs_dbus_proxy_call_json_async(test_service_proxy,
                                   "alwaysErrorSync",
                                   on_get_always_error_reply,
                                   on_expected_fnf_error_reply_kill_child,
                                   NULL,
                                   NULL);
}

static void
on_test_service_appeared(DBusConnection *connection,
                         const char     *name,
                         const char     *new_owner_unique_name,
                         void           *data)
{
    dbus_int32_t v_INT32;

    gjs_debug(GJS_DEBUG_DBUS, "%s appeared",
            name);

    test_service_proxy =
        gjs_dbus_proxy_new(connection, new_owner_unique_name,
                           "/com/litl/test/object42",
                           "com.litl.TestIface");
    v_INT32 = 42;
    gjs_dbus_proxy_call_json_async(test_service_proxy,
                                   "getSomeStuffSync",
                                   on_get_some_stuff_reply,
                                   on_unexpected_error_reply,
                                   "getSomeStuffSync call from on_test_service_appeared",
                                   "yourNameIs", DBUS_TYPE_STRING, &name,
                                   "yourUniqueNameIs", DBUS_TYPE_STRING, &new_owner_unique_name,
                                   "anIntegerIs", DBUS_TYPE_INT32, &v_INT32,
                                   NULL);
}

static void
on_test_service_vanished(DBusConnection *connection,
                         const char     *name,
                         const char     *old_owner_unique_name,
                         void           *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "%s vanished", name);

    another_child_down();
}

static GjsDBusWatchNameFuncs watch_test_service_funcs = {
    on_test_service_appeared,
    on_test_service_vanished
};

static void
on_confirm_streams_reply(GjsDBusProxy    *proxy,
                         DBusMessage     *message,
                         DBusMessageIter *return_value_iter,
                         void            *data)
{
    const char *received;

    received = extract_string_arg(return_value_iter,
                                  "received",
                                  NULL);
    g_assert(received != NULL);

    if (strcmp(received, stream_data_to_io_service) != 0) {
        g_error("We sent the child process '%s' but it says it got '%s'",
                stream_data_to_io_service,
                received);
    }

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestIO says it got: '%s'", received);

    /* We've exchanged all our streams - time to kill the TestIO
     * child process
     */
    gjs_debug(GJS_DEBUG_DBUS, "Sending TERM to TestIO child");
    if (kill(test_io_pid, SIGTERM) < 0) {
        g_error("Test IO service was no longer around... it must have failed somehow (%s)",
                strerror(errno));
    }
}

static void
on_setup_streams_reply(GjsDBusProxy    *proxy,
                       DBusMessage     *message,
                       DBusMessageIter *return_value_iter,
                       void            *data)
{
    const char *stream_path;
    gsize total;
    gssize result;
    gsize read_size;
    GError *error;
    GString *str;
    char buf[10];

    gjs_debug(GJS_DEBUG_DBUS, "Got reply to setupStreams");

    stream_path = extract_string_arg(return_value_iter,
                                     "stream",
                                     NULL);
    g_assert(stream_path != NULL);

    output_to_io_service =
        gjs_dbus_output_stream_new(gjs_dbus_proxy_get_connection(proxy),
                                   dbus_message_get_sender(message),
                                   stream_path);

    g_assert(input_from_io_service && output_to_io_service);

    /* Write to the output stream */

    total = strlen(stream_data_to_io_service);

    error = NULL;
    result = g_output_stream_write(G_OUTPUT_STREAM(output_to_io_service),
                                   stream_data_to_io_service,
                                   10,
                                   NULL,
                                   &error);
    if (result < 0) {
        g_error("Error writing to output stream: %s", error->message);
        g_error_free(error);
    }

    if (result != 10) {
        g_error("Wrote %d instead of 10 bytes", (int) result);
    }

    if (!g_output_stream_write_all(G_OUTPUT_STREAM(output_to_io_service),
                                   stream_data_to_io_service + 10,
                                   total - 10,
                                   NULL, NULL, &error)) {
        g_error("Error writing all to output stream: %s", error->message);
        g_error_free(error);
    }

    /* flush should do nothing here, and is not needed, but
     * just calling it to test it
     */
    if (!g_output_stream_flush(G_OUTPUT_STREAM(output_to_io_service), NULL, &error)) {
        g_error("Error flushing output stream: %s", error->message);
        g_error_free(error);
    }

    if (!g_output_stream_close(G_OUTPUT_STREAM(output_to_io_service), NULL, &error)) {
        g_error("Error closing output stream: %s", error->message);
        g_error_free(error);
    }
    g_object_unref(output_to_io_service);
    output_to_io_service = NULL;

    /* Now read from the input stream - in an inefficient way to be sure
     * we test multiple, partial reads
     */

    read_size = 1;
    str = g_string_new(NULL);

    while (TRUE) {
        /* test get_received() */
        g_assert(gjs_dbus_input_stream_get_received(input_from_io_service) <= strlen(stream_data_from_io_service));

        /* This is a blocking read... in production code, you would
         * want to use the ready-to-read signal instead to avoid
         * blocking when there is nothing to read.
         */
        result = g_input_stream_read(G_INPUT_STREAM(input_from_io_service),
                                     buf,
                                     read_size,
                                     NULL, &error);
        if (result < 0) {
            g_error("Error reading %d bytes from input stream: %s",
                    (int) read_size, error->message);
            g_error_free(error);
        }

        if (result == 0) {
            /* EOF */
            break;
        }

        g_string_append_len(str, buf, result);

        if (read_size < sizeof(buf))
            read_size += 1;
    }

    if (!g_input_stream_close(G_INPUT_STREAM(input_from_io_service), NULL, &error)) {
        g_error("Error closing input stream: %s", error->message);
        g_error_free(error);
    }
    g_object_unref(input_from_io_service);
    input_from_io_service = NULL;

    /* Now make the confirmStreams call
     */
    gjs_debug(GJS_DEBUG_DBUS, "Confirming to com.litl.TestIO we got: '%s'", str->str);

    gjs_dbus_proxy_call_json_async(test_io_proxy,
                                   "confirmStreamsData",
                                   on_confirm_streams_reply,
                                   on_unexpected_error_reply,
                                   "confirmStreamsData call from on_setup_streams_reply",
                                   "received", DBUS_TYPE_STRING, &str->str,
                                   NULL);

    g_string_free(str, TRUE);
}

static void
on_test_io_appeared(DBusConnection *connection,
                    const char     *name,
                    const char     *new_owner_unique_name,
                    void           *data)
{
    const char *stream_path;

    gjs_debug(GJS_DEBUG_DBUS, "%s appeared",
            name);

    test_io_proxy =
        gjs_dbus_proxy_new(connection, new_owner_unique_name,
                           "/com/litl/test/object47",
                           "com.litl.TestIO");

    input_from_io_service =
        g_object_new(GJS_TYPE_DBUS_INPUT_STREAM, NULL);
    gjs_dbus_input_stream_attach(input_from_io_service, connection);

    stream_path = gjs_dbus_input_stream_get_path(input_from_io_service);

    gjs_dbus_proxy_call_json_async(test_io_proxy,
                                   "setupStreams",
                                   on_setup_streams_reply,
                                   on_unexpected_error_reply,
                                   "setupStreams call from on_test_io_appeared",
                                   "stream", DBUS_TYPE_STRING, &stream_path,
                                   NULL);
}

static void
on_test_io_vanished(DBusConnection *connection,
                    const char     *name,
                    const char     *old_owner_unique_name,
                    void           *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "%s vanished", name);

    another_child_down();
}

static GjsDBusWatchNameFuncs watch_test_io_funcs = {
    on_test_io_appeared,
    on_test_io_vanished
};

void
bigtest_test_func_util_dbus_client(void)
{
    pid_t result;
    int status;

    /* We have to fork() to avoid creating the DBusConnection*
     * and thus preventing other dbus-using tests from forking
     * children.  This dbus bug, when the fix makes it into Ubuntu,
     * should solve the problem:
     * https://bugs.freedesktop.org/show_bug.cgi?id=15570
     *
     * The symptom of that bug is failure to connect to the bus in
     * dbus-signals.c tests. The symptom of opening a connection
     * before forking children is the connection FD shared among
     * multiple processes, i.e. huge badness.
     */
    if (!g_test_trap_fork(0, 0)) {
        /* We are the parent */
        g_test_trap_assert_passed();
        return;
    }

    /* All this stuff runs in the forked child only */

    fork_child_test_service();
    fork_child_test_io();

    g_type_init();

    g_assert(test_service_pid != 0);
    g_assert(test_io_pid != 0);

    gjs_dbus_watch_name(DBUS_BUS_SESSION,
                        "com.litl.TestService",
                        0,
                        &watch_test_service_funcs,
                        NULL);

    gjs_dbus_watch_name(DBUS_BUS_SESSION,
                        "com.litl.TestIO",
                        0,
                        &watch_test_io_funcs,
                        NULL);

    client_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(client_loop);

    if (test_service_proxy != NULL)
        g_object_unref(test_service_proxy);

    if (test_io_proxy != NULL)
        g_object_unref(test_io_proxy);

    /* child was killed already, or should have been */

    gjs_debug(GJS_DEBUG_DBUS, "waitpid() for first child");

    result = waitpid(test_service_pid, &status, 0);
    if (result < 0) {
        g_error("Failed to waitpid() for forked child: %s", strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        g_error("Forked dbus service child exited with error code %d", WEXITSTATUS(status));
    }

    if (WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM) {
        g_error("Forked dbus service child exited on wrong signal number %d", WTERMSIG(status));
    }

    gjs_debug(GJS_DEBUG_DBUS, "waitpid() for second child");

    result = waitpid(test_io_pid, &status, 0);
    if (result < 0) {
        g_error("Failed to waitpid() for forked child: %s", strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        g_error("Forked dbus service child exited with error code %d", WEXITSTATUS(status));
    }

    if (WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM) {
        g_error("Forked dbus service child exited on wrong signal number %d", WTERMSIG(status));
    }

    gjs_debug(GJS_DEBUG_DBUS, "dbus client test completed");

    /* We want to kill dbus so the weak refs are NULL to start the
     * next dbus-related test, which allows those tests
     * to fork new child processes.
     */
    _gjs_dbus_dispose_info(gjs_dbus_get_weak_ref(DBUS_BUS_SESSION));
    dbus_shutdown();

    gjs_debug(GJS_DEBUG_DBUS, "dbus shut down");

    /* FIXME this is here only while we need g_test_trap_fork(),
     * see comment above.
     */
    exit(0);
}

/*
 * First child service we forked, tests general dbus API
 */

static gboolean currently_have_test_service = FALSE;
static GObject *test_service_object = NULL;

static void
test_service_get_some_stuff_sync(DBusConnection  *connection,
                                 DBusMessage     *message,
                                 DBusMessageIter *in_iter,
                                 DBusMessageIter *out_iter,
                                 void            *data,
                                 DBusError       *error)
{
    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService got getSomeStuffSync");

    g_assert(G_IS_OBJECT(data));

    gjs_dbus_append_json_entry_BOOLEAN(out_iter,
                                       "haveTestService",
                                       currently_have_test_service);
}

static void
test_service_always_error_sync(DBusConnection  *connection,
                               DBusMessage     *message,
                               DBusMessageIter *in_iter,
                               DBusMessageIter *out_iter,
                               void            *data,
                               DBusError       *error)
{
    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService got alwaysErrorSync");

    g_assert(G_IS_OBJECT(data));

    dbus_set_error(error, DBUS_ERROR_FILE_NOT_FOUND,
                   "Did not find some kind of file! Help!");
}

static GjsDBusJsonMethod test_service_methods[] = {
    { "getSomeStuffSync", test_service_get_some_stuff_sync, NULL },
    { "alwaysErrorSync",  test_service_always_error_sync, NULL }
};

static void
on_test_service_acquired(DBusConnection *connection,
                         const char     *name,
                         void           *data)
{
    g_assert(!currently_have_test_service);
    currently_have_test_service = TRUE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService acquired by child");

    gjs_dbus_register_json(connection,
                           "com.litl.TestIface",
                           test_service_methods,
                           G_N_ELEMENTS(test_service_methods));

    test_service_object = g_object_new(G_TYPE_OBJECT, NULL);

    gjs_dbus_register_g_object(connection,
                               "/com/litl/test/object42",
                               test_service_object,
                               "com.litl.TestIface");
}

static void
on_test_service_lost(DBusConnection *connection,
                     const char     *name,
                     void           *data)
{
    g_assert(currently_have_test_service);
    currently_have_test_service = FALSE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService lost by child");

    gjs_dbus_unregister_g_object(connection,
                                 "/com/litl/test/object42");

    gjs_dbus_unregister_json(connection,
                             "com.litl.TestIface");
}

static GjsDBusNameOwnerFuncs test_service_funcs = {
    "com.litl.TestService",
    GJS_DBUS_NAME_SINGLE_INSTANCE,
    on_test_service_acquired,
    on_test_service_lost
};

static void
do_test_service_child(void)
{
    GMainLoop *loop;

    g_type_init();

    loop = g_main_loop_new(NULL, FALSE);

    gjs_dbus_acquire_name(DBUS_BUS_SESSION,
                          &test_service_funcs,
                          NULL);

    g_main_loop_run(loop);

    /* Don't return to the test program main() */
    exit(0);
}

/*
 * Second child service we forked, tests IO streams
 */

static gboolean currently_have_test_io = FALSE;
static GObject *test_io_object = NULL;

static GjsDBusInputStream  *io_input_stream = NULL;
static GjsDBusOutputStream *io_output_stream = NULL;

static GString *input_buffer = NULL;

static void
test_io_confirm_streams_data(DBusConnection  *connection,
                             DBusMessage     *message,
                             DBusMessageIter *in_iter,
                             DBusMessageIter *out_iter,
                             void            *data,
                             DBusError       *error)
{
    const char *received;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestIO got confirmStreamsData");

    g_assert(G_IS_OBJECT(data));

    received = extract_string_arg(in_iter, "received", error);
    if (received == NULL) {
        g_assert(error == NULL || dbus_error_is_set(error));
        return;
    }

    if (strcmp(received, stream_data_from_io_service) != 0) {
        g_error("We sent the main process '%s' but it says it got '%s'",
                stream_data_from_io_service,
                received);
        return;
    }

    /* We were reading from the main process in the main loop.
     * As a hack, we'll block in the main loop here to test.
     * In a real app, never block in the main loop; you would
     * just plain block, e.g. in g_input_stream_read(), if
     * you wanted to block. But don't block.
     */
    while (io_input_stream != NULL) {
        g_main_iteration(TRUE);
    }

    gjs_dbus_append_json_entry_STRING(out_iter,
                                      "received",
                                      input_buffer->str);

    g_string_free(input_buffer, TRUE);
    input_buffer = NULL;
}

static void
on_input_ready(GjsDBusInputStream *dbus_stream,
               void               *data)
{
    GInputStream *stream;
    char buf[3];
    gssize result;
    GError *error;

    stream = G_INPUT_STREAM(dbus_stream);

    g_assert(dbus_stream == io_input_stream);

    /* test get_received() */
    g_assert(gjs_dbus_input_stream_get_received(dbus_stream) <= strlen(stream_data_to_io_service));

    /* Should not block, since we got the ready-to-read signal */
    error = NULL;
    result = g_input_stream_read(G_INPUT_STREAM(io_input_stream),
                                 buf,
                                 sizeof(buf),
                                 NULL,
                                 &error);
    if (result < 0) {
        g_error("Error reading bytes from input stream: %s",
                error->message);
        g_error_free(error);
    }

    if (result == 0) {
        /* EOF */
        if (!g_input_stream_close(G_INPUT_STREAM(io_input_stream), NULL, &error)) {
            g_error("Error closing input stream in child: %s", error->message);
            g_error_free(error);
        }
        g_object_unref(io_input_stream);
        io_input_stream = NULL;

        return;
    }

    g_string_append_len(input_buffer, buf, result);

    /* We should automatically get another callback if there's more data or EOF
     * was not yet reached.
     */
}

static void
test_io_setup_streams(DBusConnection  *connection,
                      DBusMessage     *message,
                      DBusMessageIter *in_iter,
                      DBusMessageIter *out_iter,
                      void            *data,
                      DBusError       *error)
{
    const char *stream_path;
    gsize total;
    gsize remaining;
    gssize result;
    GError *gerror;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestIO got setupStreams");

    g_assert(G_IS_OBJECT(data));

    stream_path = extract_string_arg(in_iter, "stream", error);

    if (stream_path == NULL) {
        g_assert(error == NULL || dbus_error_is_set(error));
        return;
    }

    /* Create output stream to write to caller's path */
    io_output_stream =
        gjs_dbus_output_stream_new(connection,
                                   dbus_message_get_sender(message),
                                   stream_path);

    /* Create input stream and return its path to caller */
    io_input_stream =
        g_object_new(GJS_TYPE_DBUS_INPUT_STREAM,
                     NULL);
    gjs_dbus_input_stream_attach(io_input_stream,
                                 connection);
    stream_path = gjs_dbus_input_stream_get_path(io_input_stream);

    gjs_dbus_append_json_entry_STRING(out_iter,
                                      "stream",
                                      stream_path);

    /* Set up callbacks to read input stream in an async way */
    input_buffer = g_string_new(NULL);

    g_signal_connect(io_input_stream,
                     "ready-to-read",
                     G_CALLBACK(on_input_ready),
                     NULL);

    /* Write to output stream */
    gerror = NULL;
    total = strlen(stream_data_from_io_service);
    remaining = total;
    while (remaining > 0) {
        /* One byte at a time, fun torture test, totally silly in real
         * code of course
         */
        result = g_output_stream_write(G_OUTPUT_STREAM(io_output_stream),
                                       stream_data_from_io_service + (total - remaining),
                                       1,
                                       NULL,
                                       &gerror);
        if (result < 0) {
            g_assert(gerror != NULL);
            g_error("Error writing to output stream: %s", gerror->message);
            g_error_free(gerror);
        }

        if (result != 1) {
            g_error("Wrote %d instead of 1 bytes", (int) result);
        }

        remaining -= 1;
    }

    /* flush should do nothing here, and is not needed, but
     * just calling it to test it
     */
    if (!g_output_stream_flush(G_OUTPUT_STREAM(io_output_stream), NULL, &gerror)) {
        g_assert(gerror != NULL);
        g_error("Error flushing output stream: %s", gerror->message);
        g_error_free(gerror);
    }

    if (!g_output_stream_close(G_OUTPUT_STREAM(io_output_stream), NULL, &gerror)) {
        g_assert(gerror != NULL);
        g_error("Error closing output stream: %s", gerror->message);
        g_error_free(gerror);
    }
    g_object_unref(io_output_stream);
    io_output_stream = NULL;


    /* Now return, and wait for our input stream data to come in from
     * the main process
     */
}

static GjsDBusJsonMethod test_io_methods[] = {
    { "setupStreams", test_io_setup_streams, NULL },
    { "confirmStreamsData", test_io_confirm_streams_data, NULL }
};

static void
on_test_io_acquired(DBusConnection *connection,
                    const char     *name,
                    void           *data)
{
    g_assert(!currently_have_test_io);
    currently_have_test_io = TRUE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestIO acquired by child");

    gjs_dbus_register_json(connection,
                           "com.litl.TestIO",
                           test_io_methods,
                           G_N_ELEMENTS(test_io_methods));

    test_io_object = g_object_new(G_TYPE_OBJECT, NULL);

    gjs_dbus_register_g_object(connection,
                               "/com/litl/test/object47",
                               test_io_object,
                               "com.litl.TestIO");
}

static void
on_test_io_lost(DBusConnection *connection,
                const char     *name,
                void           *data)
{
    g_assert(currently_have_test_io);
    currently_have_test_io = FALSE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestIO lost by child");

    gjs_dbus_unregister_g_object(connection,
                                 "/com/litl/test/object47");

    gjs_dbus_unregister_json(connection,
                             "com.litl.TestIO");
}

static GjsDBusNameOwnerFuncs test_io_funcs = {
    "com.litl.TestIO",
    GJS_DBUS_NAME_SINGLE_INSTANCE,
    on_test_io_acquired,
    on_test_io_lost
};

static void
do_test_io_child(void)
{
    GMainLoop *loop;

    g_type_init();

    loop = g_main_loop_new(NULL, FALSE);

    gjs_dbus_acquire_name(DBUS_BUS_SESSION,
                          &test_io_funcs,
                          NULL);

    g_main_loop_run(loop);

    /* Don't return to the test program main() */
    exit(0);
}

#endif /* GJS_BUILD_TESTS */
