/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC. */

#ifndef __GJS_UTIL_DBUS_PROXY_H__
#define __GJS_UTIL_DBUS_PROXY_H__

#include <gio/gio.h>
#include <dbus/dbus.h>

G_BEGIN_DECLS


typedef struct _GjsDBusProxy      GjsDBusProxy;
typedef struct _GjsDBusProxyClass GjsDBusProxyClass;

typedef void (* GjsDBusProxyReplyFunc)      (GjsDBusProxy    *proxy,
                                             DBusMessage     *message,
                                             void            *data);
typedef void (* GjsDBusProxyJsonReplyFunc)  (GjsDBusProxy    *proxy,
                                             DBusMessage     *message,
                                             DBusMessageIter *return_value_iter,
                                             void            *data);
typedef void (* GjsDBusProxyErrorReplyFunc) (GjsDBusProxy    *proxy,
                                             const char      *error_name,
                                             const char      *error_message,
                                             void            *data);

#define GJS_TYPE_DBUS_PROXY              (gjs_dbus_proxy_get_type ())
#define GJS_DBUS_PROXY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GJS_TYPE_DBUS_PROXY, GjsDBusProxy))
#define GJS_DBUS_PROXY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GJS_TYPE_DBUS_PROXY, GjsDBusProxyClass))
#define GJS_IS_DBUS_PROXY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GJS_TYPE_DBUS_PROXY))
#define GJS_IS_DBUS_PROXY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GJS_TYPE_DBUS_PROXY))
#define GJS_DBUS_PROXY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GJS_TYPE_DBUS_PROXY, GjsDBusProxyClass))

GType           gjs_dbus_proxy_get_type      (void) G_GNUC_CONST;


GjsDBusProxy*   gjs_dbus_proxy_new             (DBusConnection             *connection,
                                                const char                 *bus_name,
                                                const char                 *object_path,
                                                const char                 *iface);
DBusConnection* gjs_dbus_proxy_get_connection  (GjsDBusProxy               *proxy);
const char*     gjs_dbus_proxy_get_bus_name    (GjsDBusProxy               *proxy);
DBusMessage*    gjs_dbus_proxy_new_method_call (GjsDBusProxy               *proxy,
                                                const char                 *method_name);
DBusMessage*    gjs_dbus_proxy_new_json_call   (GjsDBusProxy               *proxy,
                                                const char                 *method_name,
                                                DBusMessageIter            *arg_iter,
                                                DBusMessageIter            *dict_iter);
void            gjs_dbus_proxy_send            (GjsDBusProxy               *proxy,
                                                DBusMessage                *message,
                                                GjsDBusProxyReplyFunc       reply_func,
                                                GjsDBusProxyErrorReplyFunc  error_func,
                                                void                       *data);
void           gjs_dbus_proxy_send_full        (GjsDBusProxy              *proxy,
                                                DBusMessage               *message,
                                                GjsDBusProxyReplyFunc      plain_func,
                                                GjsDBusProxyJsonReplyFunc  json_func,
                                                GjsDBusProxyErrorReplyFunc error_func,
                                                void                      *data);

/* varargs are like:
 *
 *   key1, dbus_type_1, &value_1,
 *   key2, dbus_type_2, &value_2,
 *   NULL
 *
 * Basic types only (no arrays)
 */
void          gjs_dbus_proxy_call_json_async (GjsDBusProxy              *proxy,
                                              const char                *method_name,
                                              GjsDBusProxyJsonReplyFunc  reply_func,
                                              GjsDBusProxyErrorReplyFunc error_func,
                                              void                      *data,
                                              const char                *first_key,
                                              ...);

G_END_DECLS

#endif  /* __GJS_UTIL_DBUS_PROXY_H__ */
