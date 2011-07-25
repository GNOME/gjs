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

#ifndef __GJS_UTIL_DBUS_H__
#define __GJS_UTIL_DBUS_H__

#include <glib-object.h>
#include <dbus/dbus.h>

G_BEGIN_DECLS

/* Convenience macro */

#define GJS_DBUS_NAME_FROM_TYPE(type) ((type) == DBUS_BUS_SESSION ? "session" : "system")

/* Error names */
#define GJS_DBUS_ERROR_STREAM_RECEIVER_CLOSED "com.litl.Error.Stream.ReceiverClosed"

/*
 * Monitor whether we are connected / not-connected to the bus
 */

typedef void (* GjsDBusConnectionOpenedFunc)   (DBusConnection *connection,
                                                void           *data);
typedef void (* GjsDBusConnectionClosedFunc)   (DBusConnection *connection,
                                                void           *data);

typedef struct {
    DBusBusType which_bus;
    GjsDBusConnectionOpenedFunc opened;
    GjsDBusConnectionClosedFunc closed;
} GjsDBusConnectFuncs;

void gjs_dbus_add_connect_funcs             (const GjsDBusConnectFuncs *funcs,
                                             void                      *data);
void gjs_dbus_remove_connect_funcs          (const GjsDBusConnectFuncs *funcs,
                                             void                      *data);
void gjs_dbus_add_connect_funcs_sync_notify (const GjsDBusConnectFuncs *funcs,
                                             void                      *data);


void gjs_dbus_add_bus_weakref    (DBusBusType      bus_type,
                                  DBusConnection **connection_p);
void gjs_dbus_remove_bus_weakref (DBusBusType      bus_type,
                                  DBusConnection **connection_p);

DBusConnection* gjs_dbus_get_weak_ref       (DBusBusType     which_bus);


void gjs_dbus_try_connecting_now (DBusBusType which_bus);

/*
 * Own a bus name
 *
 */

typedef enum {
    GJS_DBUS_NAME_SINGLE_INSTANCE,
    GJS_DBUS_NAME_MANY_INSTANCES
} GjsDBusNameType;

typedef void (* GjsDBusNameAcquiredFunc) (DBusConnection *connection,
                                          const char     *name,
                                          void           *data);
typedef void (* GjsDBusNameLostFunc)     (DBusConnection *connection,
                                          const char     *name,
                                          void           *data);

typedef struct {
    char *name;
    GjsDBusNameType type;
    GjsDBusNameAcquiredFunc acquired;
    GjsDBusNameLostFunc lost;
} GjsDBusNameOwnerFuncs;

guint        gjs_dbus_acquire_name       (DBusBusType                  bus_type,
                                          const GjsDBusNameOwnerFuncs *funcs,
                                          void                        *data);
void         gjs_dbus_release_name       (DBusBusType                  bus_type,
                                          const GjsDBusNameOwnerFuncs *funcs,
                                          void                        *data);
void         gjs_dbus_release_name_by_id (DBusBusType                  bus_type,
                                          guint                        id);

/*
 * Keep track of someone else's bus name
 *
 */

typedef enum {
    GJS_DBUS_NAME_START_IF_NOT_FOUND = 0x1
} GjsDBusWatchNameFlags;

typedef void (* GjsDBusNameAppearedFunc) (DBusConnection *connection,
                                          const char     *name,
                                          const char     *new_owner_unique_name,
                                          void           *data);
typedef void (* GjsDBusNameVanishedFunc) (DBusConnection *connection,
                                          const char     *name,
                                          const char     *old_owner_unique_name,
                                          void           *data);

typedef struct {
    GjsDBusNameAppearedFunc appeared;
    GjsDBusNameVanishedFunc vanished;
} GjsDBusWatchNameFuncs;

void        gjs_dbus_watch_name             (DBusBusType                  bus_type,
                                             const char                  *name,
                                             GjsDBusWatchNameFlags        flags,
                                             const GjsDBusWatchNameFuncs *funcs,
                                             void                        *data);
void        gjs_dbus_unwatch_name           (DBusBusType                  bus_type,
                                             const char                  *name,
                                             const GjsDBusWatchNameFuncs *funcs,
                                             void                        *data);
const char* gjs_dbus_get_watched_name_owner (DBusBusType                  bus_type,
                                             const char                  *name);


typedef void (* GjsDBusSignalHandler) (DBusConnection *connection,
                                       DBusMessage    *message,
                                       void           *data);
int gjs_dbus_watch_signal          (DBusBusType           bus_type,
                                    const char           *sender,
                                    const char           *path,
                                    const char           *iface,
                                    const char           *name,
                                    GjsDBusSignalHandler  handler,
                                    void                 *data,
                                    GDestroyNotify        data_dnotify);
void gjs_dbus_unwatch_signal       (DBusBusType           bus_type,
                                    const char           *sender,
                                    const char           *path,
                                    const char           *iface,
                                    const char           *name,
                                    GjsDBusSignalHandler  handler,
                                    void                 *data);
void gjs_dbus_unwatch_signal_by_id (DBusBusType           bus_type,
                                    int                   id);

/* A "json method" is a D-Bus method with signature
 *      DICT jsonMethodName(DICT)
 * with the idea that it both takes and returns
 * a JavaScript-style dictionary. This makes
 * our JavaScript-to-dbus bindings really simple,
 * and avoids a lot of futzing with dbus IDL.
 *
 * Of course it's completely annoying for someone
 * using D-Bus in a "normal" way but the idea is just
 * to use this to communicate within our own app
 * that happens to consist of multiple processes
 * and have bits written in JS.
 */
typedef void (* GjsDBusJsonSyncMethodFunc)  (DBusConnection  *connection,
                                             DBusMessage     *message,
                                             DBusMessageIter *in_iter,
                                             DBusMessageIter *out_iter,
                                             void            *data,
                                             DBusError       *error);

typedef void (* GjsDBusJsonAsyncMethodFunc) (DBusConnection  *connection,
                                             DBusMessage     *message,
                                             DBusMessageIter *in_iter,
                                             void            *data);

typedef struct {
    const char *name;
    /* one of these two but not both should be non-NULL */
    GjsDBusJsonSyncMethodFunc sync_func;
    GjsDBusJsonAsyncMethodFunc async_func;
} GjsDBusJsonMethod;

void gjs_dbus_register_json       (DBusConnection          *connection,
                                   const char              *iface_name,
                                   const GjsDBusJsonMethod *methods,
                                   int                      n_methods);
void gjs_dbus_unregister_json     (DBusConnection          *connection,
                                   const char              *iface_name);
void gjs_dbus_register_g_object   (DBusConnection          *connection,
                                   const char              *path,
                                   GObject                 *gobj,
                                   const char              *iface_name);
void gjs_dbus_unregister_g_object (DBusConnection          *connection,
                                   const char              *path);

void gjs_dbus_append_json_entry              (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              int               dbus_type,
                                              void             *basic_value_p);
void gjs_dbus_append_json_entry_STRING       (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              const char       *value);
void gjs_dbus_append_json_entry_INT32        (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              dbus_int32_t      value);
void gjs_dbus_append_json_entry_DOUBLE       (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              double            value);
void gjs_dbus_append_json_entry_BOOLEAN      (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              dbus_bool_t       value);
void gjs_dbus_append_json_entry_EMPTY_ARRAY  (DBusMessageIter  *dict_iter,
                                              const char       *key);
void gjs_dbus_append_json_entry_STRING_ARRAY (DBusMessageIter  *dict_iter,
                                              const char       *key,
                                              const char      **value);

gboolean gjs_dbus_message_iter_get_gsize  (DBusMessageIter  *iter,
                                           gsize            *value_p);
gboolean gjs_dbus_message_iter_get_gssize (DBusMessageIter  *iter,
                                           gssize           *value_p);

void gjs_dbus_start_service(DBusConnection *connection,
                            const char     *name);

G_END_DECLS

#endif  /* __GJS_UTIL_DBUS_H__ */
