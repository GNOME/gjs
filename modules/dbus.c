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
#include <config.h>

#include "dbus.h"
#include "dbus-exports.h"
#include "dbus-values.h"

#include <gjs/gjs-module.h>
#include "../gi/closure.h"
#include <gjs/compat.h>

#include <util/log.h>
#include <gjs-dbus/dbus.h>

static gboolean session_bus_weakref_added = FALSE;
static DBusConnection *session_bus = NULL;
static gboolean system_bus_weakref_added = FALSE;
static DBusConnection *system_bus = NULL;

/* A global not-threadsafe stack of DBusMessages
 * currently being processed when invoking a user callback.
 * Accessible via dbus.currentMessageContext.
 */
static GSList *_gjs_current_dbus_messages = NULL;

#define DBUS_CONNECTION_FROM_TYPE(type) ((type) == DBUS_BUS_SESSION ? session_bus : system_bus)

static JSBool
get_bus_type_from_object(JSContext   *context,
                         JSObject    *object,
                         DBusBusType *bus_type)
{
    jsval value;

    if (!gjs_object_get_property(context,
                                 object,
                                 "_dbusBusType",
                                 &value)) {
        gjs_throw(context, "Object has no _dbusBusType property, not a bus object?");
        return JS_FALSE;
    }

    *bus_type =  (DBusBusType)JSVAL_TO_INT(value);
    return JS_TRUE;
}

static JSBool
bus_check(JSContext *context, DBusBusType bus_type)
{
    gboolean bus_weakref_added;
    DBusConnection **bus_connection;

    bus_weakref_added = bus_type == DBUS_BUS_SESSION ? session_bus_weakref_added :
        system_bus_weakref_added;

    bus_connection = bus_type == DBUS_BUS_SESSION ? &session_bus : &system_bus;

    /* This is all done lazily only if a dbus-related method is actually invoked */

    if (!bus_weakref_added) {
        gjs_dbus_add_bus_weakref(bus_type, bus_connection);
    }

    if (*bus_connection == NULL)
        gjs_dbus_try_connecting_now(bus_type); /* force a synchronous connection attempt */

    /* Throw exception if connection attempt failed */
    if (*bus_connection == NULL) {
        const char *bus_type_name = bus_type == DBUS_BUS_SESSION ? "session" : "system";
        gjs_debug(GJS_DEBUG_DBUS, "Failed to connect to %s bus", bus_type_name);
        gjs_throw(context, "Not connected to %s message bus", bus_type_name);
        return JS_FALSE;
    }

    return JS_TRUE;
}

void
gjs_js_push_current_message(DBusMessage *message)
{
  /* Don't need to take a ref here, if someone forgets to unset the message,
   * that's a bug.
   */
  _gjs_current_dbus_messages = g_slist_prepend(_gjs_current_dbus_messages, message);
}

void
gjs_js_pop_current_message(void)
{
    g_assert(_gjs_current_dbus_messages != NULL);

    _gjs_current_dbus_messages = g_slist_delete_link(_gjs_current_dbus_messages,
                                                     _gjs_current_dbus_messages);
}

static DBusMessage*
prepare_call(JSContext   *context,
             JSObject    *obj,
             uintN        argc,
             jsval       *argv,
             DBusBusType  bus_type)
{
    DBusMessage *message = NULL;
    char *bus_name = NULL;
    char *path = NULL;
    char *interface = NULL;
    char *method = NULL;
    gboolean    auto_start;
    char *out_signature = NULL;
    char *in_signature = NULL;
    DBusMessageIter arg_iter;
    DBusSignatureIter sig_iter;

    if (!bus_check(context, bus_type))
        return NULL;

    bus_name = gjs_string_get_ascii(context, argv[0]);
    if (bus_name == NULL)
        return NULL;

    path = gjs_string_get_ascii(context, argv[1]);
    if (path == NULL)
        goto fail;

    if (JSVAL_IS_NULL(argv[2])) {
        interface = NULL;
    } else {
        interface = gjs_string_get_ascii(context, argv[2]);
        if (interface == NULL)
            goto fail; /* exception was set */
    }

    method = gjs_string_get_ascii(context, argv[3]);
    if (method == NULL)
        goto fail;

    out_signature = gjs_string_get_ascii(context, argv[4]);
    if (out_signature == NULL)
        goto fail;

    in_signature = gjs_string_get_ascii(context, argv[5]);
    if (in_signature == NULL)
        goto fail;

    g_assert(bus_name && path && method && in_signature && out_signature);

    if (!JSVAL_IS_BOOLEAN(argv[6])) {
        gjs_throw(context, "arg 7 must be boolean");
        goto fail;
    }
    auto_start = JSVAL_TO_BOOLEAN(argv[6]);

    /* FIXME should validate the bus_name, path, interface, method really, but
     * we should just not write buggy JS ;-)
     */

    message = dbus_message_new_method_call(bus_name,
                                           path,
                                           interface,
                                           method);
    if (message == NULL) {
        gjs_throw(context, "Out of memory (or invalid args to dbus_message_new_method_call)");
        goto fail;
    }

    dbus_message_set_auto_start(message, auto_start);

    dbus_message_iter_init_append(message, &arg_iter);

    if (in_signature)
        dbus_signature_iter_init(&sig_iter, in_signature);
    else
        dbus_signature_iter_init(&sig_iter, "a{sv}");

    if (!gjs_js_values_to_dbus(context, 0, argv[8], &arg_iter, &sig_iter)) {
        gjs_debug(GJS_DEBUG_DBUS, "Failed to marshal call from JS to dbus");
        dbus_message_unref(message);
        message = NULL;
    }

 fail:
    g_free(in_signature);
    g_free(out_signature);
    g_free(method);
    g_free(interface);
    g_free(path);
    g_free(bus_name);

    return message;
}

static JSBool
complete_call(JSContext   *context,
              jsval       *retval,
              DBusMessage *reply,
              DBusError   *derror)
{
    DBusMessageIter arg_iter;
    GjsRootedArray *ret_values;
    int array_length;

    if (dbus_error_is_set(derror)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Error sending call: %s: %s",
                  derror->name, derror->message);
        gjs_throw(context,
                  "DBus error: %s: %s",
                  derror->name,
                  derror->message);
        dbus_error_free(derror);
        return JS_FALSE;
    }

    if (reply == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "No reply received to call");
        return JS_FALSE;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        dbus_set_error_from_message(derror, reply);
        gjs_debug(GJS_DEBUG_DBUS,
                  "Error set by call: %s: %s",
                  derror->name, derror->message);

        gjs_throw(context,
                     "DBus error: %s: %s",
                     derror->name,
                     derror->message);
        dbus_error_free(derror);

        return JS_FALSE;
    }

    dbus_message_iter_init(reply, &arg_iter);
    if (!gjs_js_values_from_dbus(context, &arg_iter, &ret_values)) {
        gjs_debug(GJS_DEBUG_DBUS, "Failed to marshal dbus call reply back to JS");
        return JS_FALSE;
    }

    g_assert(ret_values != NULL);

    array_length = gjs_rooted_array_get_length(context, ret_values);
    if (array_length == 0) {
        /* the callback expects to be called with callback(undefined, null) */
        *retval = JSVAL_VOID;
    } else if (array_length == 1) {
        /* If the array only has one element return that element alone */
        *retval = gjs_rooted_array_get(context,
                                          ret_values,
                                          0);
    } else {
        /* Otherwise return an array with all the return values. The
         * funny assignment is to avoid creating a temporary JSObject
         * we'd need to root
         */
        *retval = OBJECT_TO_JSVAL(JS_NewArrayObject(context, array_length,
                                                    gjs_rooted_array_get_data(context, ret_values)));
    }

    /* We require the caller to have rooted retval or to have
     * called JS_EnterLocalRootScope()
     */
    gjs_rooted_array_free(context, ret_values, TRUE);

    return JS_TRUE;
}

static JSContext *
get_callback_context(GClosure *closure)
{
    JSRuntime *runtime;

    if (!gjs_closure_is_valid(closure))
        return NULL;

    runtime = gjs_closure_get_runtime(closure);
    return gjs_runtime_get_current_context(runtime);
}

static void
pending_notify(DBusPendingCall *pending,
               void            *user_data)
{
    GClosure *closure;
    JSContext *context;
    jsval argv[2];
    jsval discard;
    DBusMessage *reply;
    DBusError derror;

    closure = user_data;

    context = get_callback_context(closure);

    gjs_debug(GJS_DEBUG_DBUS,
              "Notified of reply to async call closure %p context %p",
              closure, context);

    if (context == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure destroyed before we could complete pending call");
        return;
    }

    JS_BeginRequest(context);

    /* reply may be NULL if none received? I think it may never be if
     * we've already been notified, but be safe here.
     */
    reply = dbus_pending_call_steal_reply(pending);

    dbus_error_init(&derror);
    /* argv[0] will be the return value if any, argv[1] we fill with exception if any */
    gjs_set_values(context, argv, 2, JSVAL_NULL);
    gjs_root_value_locations(context, argv, 2);
    gjs_js_push_current_message(reply);
    complete_call(context, &argv[0], reply, &derror);
    gjs_js_pop_current_message();
    g_assert(!dbus_error_is_set(&derror)); /* not supposed to be left set by complete_call() */

    /* If completing the call failed, we still call the callback, but with null for the reply
     * and an extra arg that is the exception. Kind of odd maybe.
     */
    if (JS_IsExceptionPending(context)) {
        JS_GetPendingException(context, &argv[1]);
        JS_ClearPendingException(context);
    }

    gjs_js_push_current_message(reply);
    gjs_closure_invoke(closure, 2, &argv[0], &discard);
    gjs_js_pop_current_message();

    if (reply)
        dbus_message_unref(reply);

    gjs_unroot_value_locations(context, argv, 2);

    JS_EndRequest(context);
}

static void
pending_free_closure(void *data)
{
    g_closure_invalidate(data);
    g_closure_unref(data);
}

/* Args are bus_name, object_path, iface, method, out signature, in signature, args, and callback to get returned value */
static JSBool
gjs_js_dbus_call_async(JSContext  *context,
                       uintN       argc,
                       jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    GClosure *closure;
    DBusMessage *message;
    DBusPendingCall *pending;
    DBusConnection  *bus_connection;
    DBusBusType bus_type;
    int timeout;

    if (argc < 10) {
        gjs_throw(context, "Not enough args, need bus name, object path, interface, method, out signature, in signature, autostart flag, timeout limit, args, and callback");
        return JS_FALSE;
    }

    /* No way to tell if the object is in fact a callable function, other than trying to
     * call it later on.
     */
    if (!JSVAL_IS_OBJECT(argv[9])) {
        gjs_throw(context, "arg 10 must be a callback to invoke when call completes");
        return JS_FALSE;
    }

    if (!JSVAL_IS_INT(argv[7])) {
        gjs_throw(context, "arg 8 must be int");
        return JS_FALSE;
    }
    timeout = JSVAL_TO_INT(argv[7]);

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    message = prepare_call(context, obj, argc, argv, bus_type);

    if (message == NULL)
        return JS_FALSE;

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    pending = NULL;
    if (!dbus_connection_send_with_reply(bus_connection, message, &pending, timeout) ||
        pending == NULL) {
        gjs_debug(GJS_DEBUG_DBUS, "Failed to send async dbus message connected %d pending %p",
                  dbus_connection_get_is_connected(bus_connection), pending);
        gjs_throw(context, "Failed to send dbus message, connected %d pending %p",
                  dbus_connection_get_is_connected(bus_connection), pending);
        dbus_message_unref(message);
        return JS_FALSE;
    }

    g_assert(pending != NULL);

    dbus_message_unref(message);

    /* We cheat a bit here and use a closure to store a JavaScript function
     * and deal with the GC root and other issues, even though we
     * won't ever marshal via GValue
     */
    closure = gjs_closure_new(context, JSVAL_TO_OBJECT(argv[9]), "async call", TRUE);
    if (closure == NULL) {
        dbus_pending_call_unref(pending);
        return JS_FALSE;
    }

    g_closure_ref(closure);
    g_closure_sink(closure);
    dbus_pending_call_set_notify(pending, pending_notify, closure,
                                 pending_free_closure);

    dbus_pending_call_unref(pending); /* DBusConnection should still hold a ref until it's completed */

    return JS_TRUE;
}

static JSBool
fill_with_null_or_string(JSContext *context, char **string_p, jsval value)
{
    if (JSVAL_IS_NULL(value))
        *string_p = NULL;
    else {
        *string_p = gjs_string_get_ascii(context, value);
        if (!*string_p)
            return JS_FALSE;
    }

    return JS_TRUE;
}

typedef struct {
    int refcount;
    DBusBusType bus_type;
    int connection_id;
    GClosure *closure;
} SignalHandler;
/* allow removal by passing in the callable
 * FIXME don't think we ever end up using this,
 * could get rid of it, it predates having an ID
 * to remove by
 */
static GHashTable *signal_handlers_by_callable = NULL;

static void signal_on_closure_invalidated (void          *data,
                                           GClosure      *closure);
static void signal_handler_ref            (SignalHandler *handler);
static void signal_handler_unref          (SignalHandler *handler);


static SignalHandler*
signal_handler_new(JSContext *context,
                   jsval      callable)
{
    SignalHandler *handler;

    if (signal_handlers_by_callable &&
        g_hash_table_lookup(signal_handlers_by_callable,
                            JSVAL_TO_OBJECT(callable)) != NULL) {
        /* To fix this, get rid of signal_handlers_by_callable
         * and just require removal by id. Not sure we ever use
         * removal by callable anyway.
         */
        gjs_throw(context,
                  "For now, same callback cannot be the handler for two dbus signal connections");
        return NULL;
    }

    handler = g_slice_new0(SignalHandler);
    handler->refcount = 1;

    /* We cheat a bit here and use a closure to store a JavaScript function
     * and deal with the GC root and other issues, even though we
     * won't ever marshal via GValue
     */
    handler->closure = gjs_closure_new(context,
                                       JSVAL_TO_OBJECT(callable),
                                       "signal watch",
                                       TRUE);
    if (handler->closure == NULL) {
        g_free(handler);
        return NULL;
    }

    g_closure_ref(handler->closure);
    g_closure_sink(handler->closure);

    g_closure_add_invalidate_notifier(handler->closure, handler,
                                      signal_on_closure_invalidated);

    if (!signal_handlers_by_callable) {
        signal_handlers_by_callable =
            g_hash_table_new_full(g_direct_hash,
                                  g_direct_equal,
                                  NULL,
                                  NULL);
    }

    /* We keep a weak reference on the closure in a table indexed
     * by the object, so we can retrieve it when removing the signal
     * watch. The signal_handlers_by_callable owns one ref to the SignalHandler.
     */
    signal_handler_ref(handler);
    g_hash_table_replace(signal_handlers_by_callable,
                         JSVAL_TO_OBJECT(callable),
                         handler);

    return handler;
}

static void
signal_handler_ref(SignalHandler *handler)
{
    g_assert(handler->refcount > 0);
    handler->refcount += 1;
}

static void
signal_handler_dispose(SignalHandler *handler)
{
    g_assert(handler->refcount > 0);

    signal_handler_ref(handler);

    if (handler->closure) {
        /* invalidating closure could dispose
         * re-entrantly, so set handler->closure
         * NULL before we invalidate
         */
        GClosure *closure = handler->closure;
        handler->closure = NULL;

        g_hash_table_remove(signal_handlers_by_callable,
                            gjs_closure_get_callable(closure));
        if (g_hash_table_size(signal_handlers_by_callable) == 0) {
            g_hash_table_destroy(signal_handlers_by_callable);
            signal_handlers_by_callable = NULL;
        }
        /* the hash table owned 1 ref */
        signal_handler_unref(handler);

        g_closure_invalidate(closure);
        g_closure_unref(closure);
    }

    /* remove signal if it hasn't been */
    if (handler->connection_id != 0) {
        int id = handler->connection_id;
        handler->connection_id = 0;

        /* this should clear another ref off the
         * handler by calling signal_on_watch_removed
         */
        gjs_dbus_unwatch_signal_by_id(handler->bus_type,
                                      id);
    }

    signal_handler_unref(handler);
}

static void
signal_handler_unref(SignalHandler *handler)
{
    g_assert(handler->refcount > 0);

    if (handler->refcount == 1) {
        signal_handler_dispose(handler);
    }

    handler->refcount -= 1;
    if (handler->refcount == 0) {
        g_assert(handler->closure == NULL);
        g_assert(handler->connection_id == 0);
        g_slice_free(SignalHandler, handler);
    }
}

static void
signal_on_watch_removed(void *data)
{
    SignalHandler *handler = data;

    handler->connection_id = 0; /* don't re-remove it */

    /* The watch owns a ref; removing it
     * also forces dispose, which invalidates
     * the closure if that hasn't been done.
     */
    signal_handler_dispose(handler);
    signal_handler_unref(handler);
}

static void
signal_on_closure_invalidated(void     *data,
                              GClosure *closure)
{
    SignalHandler *handler;

    handler = data;

    /* this removes the watch if it has not been */
    signal_handler_dispose(handler);
}

static void
signal_handler_callback(DBusConnection *connection,
                        DBusMessage    *message,
                        void           *data)
{
    JSContext *context;
    SignalHandler *handler;
    jsval ret_val;
    DBusMessageIter arg_iter;
    GjsRootedArray *arguments;

    gjs_debug(GJS_DEBUG_DBUS,
              "Signal handler called");

    handler = data;

    if (handler->closure == NULL) {
        gjs_debug(GJS_DEBUG_DBUS, "dbus signal handler invalidated, ignoring");
        return;
    }

    context = get_callback_context(handler->closure);

    if (!context) {
        /* The runtime is gone */
        return;
    }

    JS_BeginRequest(context);

    dbus_message_iter_init(message, &arg_iter);
    if (!gjs_js_values_from_dbus(context, &arg_iter, &arguments)) {
        gjs_debug(GJS_DEBUG_DBUS, "Failed to marshal dbus signal to JS");
        JS_EndRequest(context);
        return;
    }

    signal_handler_ref(handler); /* for safety, in case handler removes itself */

    g_assert(arguments != NULL);

    ret_val = JSVAL_VOID;

    gjs_debug(GJS_DEBUG_DBUS,
              "Invoking closure on signal received, %d args",
              gjs_rooted_array_get_length(context, arguments));

    gjs_js_push_current_message(message);
    gjs_closure_invoke(handler->closure,
                       gjs_rooted_array_get_length(context, arguments),
                       gjs_rooted_array_get_data(context, arguments),
                       &ret_val);
    gjs_js_pop_current_message();

    gjs_rooted_array_free(context, arguments, TRUE);

    signal_handler_unref(handler); /* for safety */

    JS_EndRequest(context);
}

/* Args are bus_name, object_path, iface, signal, and callback */
static JSBool
gjs_js_dbus_watch_signal(JSContext  *context,
                         uintN       argc,
                         jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    char *bus_name = NULL;
    char *object_path = NULL;
    char *iface = NULL;
    char *signal = NULL;
    SignalHandler *handler;
    int id;
    DBusBusType bus_type;
    JSBool ret = JS_FALSE;

    if (argc < 5) {
        gjs_throw(context, "Not enough args, need bus name, object path, interface, signal and callback");
        return JS_FALSE;
    }

    /* No way to tell if the object is in fact a callable function, other than trying to
     * call it later on.
     */
    if (!JSVAL_IS_OBJECT(argv[4])) {
        gjs_throw(context, "arg 5 must be a callback to invoke when call completes");
        return JS_FALSE;
    }

    if (!fill_with_null_or_string(context, &bus_name, argv[0]))
        return JS_FALSE;
    if (!fill_with_null_or_string(context, &object_path, argv[1]))
        goto fail;
    if (!fill_with_null_or_string(context, &iface, argv[2]))
        goto fail;
    if (!fill_with_null_or_string(context, &signal, argv[3]))
        goto fail;

    if (!get_bus_type_from_object(context, obj, &bus_type))
        goto fail;

    handler = signal_handler_new(context, argv[4]);
    if (handler == NULL)
        goto fail;

    id = gjs_dbus_watch_signal(bus_type,
                               bus_name,
                               object_path,
                               iface,
                               signal,
                               signal_handler_callback,
                               handler,
                               signal_on_watch_removed);
    handler->bus_type = bus_type;
    handler->connection_id = id;

    /* signal_on_watch_removed() takes ownership of our
     * ref to the SignalHandler
     */

    JS_SET_RVAL(context, vp, INT_TO_JSVAL(id));

    ret = JS_TRUE;

 fail:
    g_free(signal);
    g_free(iface);
    g_free(object_path);
    g_free(bus_name);

    return ret;
}

/* Args are handler id */
static JSBool
gjs_js_dbus_unwatch_signal_by_id(JSContext  *context,
                                 uintN       argc,
                                 jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    int id;
    DBusBusType bus_type;

    if (argc < 1) {
        gjs_throw(context, "Not enough args, need handler id");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    id = JSVAL_TO_INT(argv[0]);

    gjs_dbus_unwatch_signal_by_id(bus_type,
                                  id);
    return JS_TRUE;
}

/* Args are bus_name, object_path, iface, signal, and callback */
static JSBool
gjs_js_dbus_unwatch_signal(JSContext  *context,
                           uintN       argc,
                           jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    char *bus_name = NULL;
    char *object_path = NULL;
    char *iface = NULL;
    char *signal = NULL;
    SignalHandler *handler;
    DBusBusType bus_type;
    JSBool ret = JS_FALSE;

    if (argc < 5) {
        gjs_throw(context, "Not enough args, need bus name, object path, interface, signal and callback");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    /* No way to tell if the object is in fact a callable function, other than trying to
     * call it later on.
     */
    if (!JSVAL_IS_OBJECT(argv[4])) {
        gjs_throw(context, "arg 5 must be a callback to invoke when call completes");
        return JS_FALSE;
    }

    if (!fill_with_null_or_string(context, &bus_name, argv[0]))
        return JS_FALSE;
    if (!fill_with_null_or_string(context, &object_path, argv[1]))
        goto fail;
    if (!fill_with_null_or_string(context, &iface, argv[2]))
        goto fail;
    if (!fill_with_null_or_string(context, &signal, argv[3]))
        goto fail;

    /* we don't complain if the signal seems to have been already removed
     * or to never have been watched, to match g_signal_handler_disconnect
     */
    if (!signal_handlers_by_callable) {
        ret = JS_TRUE;
        goto fail;
    }

    handler = g_hash_table_lookup(signal_handlers_by_callable, JSVAL_TO_OBJECT(argv[4]));

    if (!handler) {
        ret = JS_TRUE;
        goto fail;
    }

    /* This should dispose the handler which should in turn
     * remove it from the handler table
     */
    gjs_dbus_unwatch_signal(bus_type,
                            bus_name,
                            object_path,
                            iface,
                            signal,
                            signal_handler_callback,
                            handler);

    g_assert(g_hash_table_lookup(signal_handlers_by_callable,
                                 JSVAL_TO_OBJECT(argv[4])) == NULL);

    ret = JS_TRUE;

 fail:
    g_free(signal);
    g_free(iface);
    g_free(object_path);
    g_free(bus_name);

    return ret;
}

/* Args are object_path, iface, signal, arguments signature, arguments */
static JSBool
gjs_js_dbus_emit_signal(JSContext  *context,
                        uintN       argc,
                        jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    DBusConnection *bus_connection;
    DBusMessage *message;
    DBusMessageIter arg_iter;
    DBusSignatureIter sig_iter;
    char *object_path = NULL;
    char *iface = NULL;
    char *signal = NULL;
    char *in_signature = NULL;
    DBusBusType bus_type;
    JSBool ret = JS_FALSE;

    if (argc < 4) {
        gjs_throw(context, "Not enough args, need object path, interface and signal and the arguments");
        return JS_FALSE;
    }

    if (!JSVAL_IS_OBJECT(argv[4])) {
        gjs_throw(context, "5th argument should be an array of arguments");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    object_path = gjs_string_get_ascii(context, argv[0]);
    if (!object_path)
        return JS_FALSE;
    iface = gjs_string_get_ascii(context, argv[1]);
    if (!iface)
        goto fail;
    signal = gjs_string_get_ascii(context, argv[2]);
    if (!signal)
        goto fail;
    in_signature = gjs_string_get_ascii(context, argv[3]);
    if (!in_signature)
        goto fail;

    if (!bus_check(context, bus_type))
        goto fail;

    gjs_debug(GJS_DEBUG_DBUS,
              "Emitting signal %s %s %s",
              object_path,
              iface,
              signal);

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    message = dbus_message_new_signal(object_path,
                                      iface,
                                      signal);

    dbus_message_iter_init_append(message, &arg_iter);

    dbus_signature_iter_init(&sig_iter, in_signature);

    if (!gjs_js_values_to_dbus(context, 0, argv[4], &arg_iter, &sig_iter)) {
        dbus_message_unref(message);
        goto fail;
    }

    dbus_connection_send(bus_connection, message, NULL);

    dbus_message_unref(message);

    ret = JS_TRUE;

 fail:
    g_free(in_signature);
    g_free(signal);
    g_free(iface);
    g_free(object_path);

    return ret;
}

/* Blocks until dbus outgoing message queue is empty.  This is the only way
 * to ensure that a signal has been sent before proceeding. */
static JSBool
gjs_js_dbus_flush(JSContext  *context,
                  uintN       argc,
                  jsval      *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    DBusConnection *bus_connection;
    DBusBusType bus_type;

    if (argc != 0) {
        gjs_throw(context, "Does not take any arguments.");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    if (!bus_check(context, bus_type))
        return JS_FALSE;

    gjs_debug(GJS_DEBUG_DBUS, "Flushing bus");

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    dbus_connection_flush(bus_connection);

    return JS_TRUE;
}

/* Args are bus_name, object_path, iface, method, out signature, in signature, args */
static JSBool
gjs_js_dbus_call(JSContext  *context,
                 uintN       argc,
                 jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    DBusMessage *message;
    DBusError derror;
    DBusMessage *reply;
    JSBool result;
    DBusConnection *bus_connection;
    DBusBusType bus_type;
    jsval retval;

    if (argc < 8) {
        gjs_throw(context, "Not enough args, need bus name, object path, interface, method, out signature, in signature, autostart flag, and args");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    message = prepare_call(context, obj, argc, argv, bus_type);

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    /* send_with_reply_and_block() returns NULL if error was set. */
    dbus_error_init(&derror);
    reply = dbus_connection_send_with_reply_and_block(bus_connection, message, -1, &derror);

    dbus_message_unref(message);

    /* this consumes the DBusError leaving it freed */
    /* The retval is (we hope) rooted by jsapi when it invokes the
     * native function
     */
    retval = JSVAL_NULL;
    JS_AddValueRoot(context, &retval);
    result = complete_call(context, &retval, reply, &derror);
    if (result)
        JS_SET_RVAL(context, vp, retval);

    if (reply)
        dbus_message_unref(reply);

    JS_RemoveValueRoot(context, &retval);

    return result;
}

typedef struct {
    GjsDBusNameOwnerFuncs funcs;
    GClosure *acquired_closure;
    GClosure *lost_closure;
    DBusBusType bus_type;
} GjsJSDBusNameOwner;

static void
on_name_acquired(DBusConnection *connection,
                 const char     *name,
                 void           *data)
{
    int argc;
    jsval argv[1];
    jsval rval;
    JSContext *context;
    GjsJSDBusNameOwner *owner;

    owner = data;

    context = get_callback_context(owner->acquired_closure);
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure destroyed before we could notify name acquired");
        return;
    }

    JS_BeginRequest(context);

    argc = 1;

    argv[0] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, name));
    JS_AddValueRoot(context, &argv[0]);

    rval = JSVAL_VOID;
    JS_AddValueRoot(context, &rval);

    gjs_closure_invoke(owner->acquired_closure,
                       argc, argv, &rval);

    JS_RemoveValueRoot(context, &argv[0]);
    JS_RemoveValueRoot(context, &rval);

    JS_EndRequest(context);
}

static void
on_name_lost(DBusConnection *connection,
             const char     *name,
             void           *data)
{
    int argc;
    jsval argv[1];
    jsval rval;
    JSContext *context;
    GjsJSDBusNameOwner *owner;

    owner = data;

    context = get_callback_context(owner->lost_closure);
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure destroyed before we could notify name lost");
        return;
    }

    JS_BeginRequest(context);

    argc = 1;

    argv[0] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, name));
    JS_AddValueRoot(context, &argv[0]);

    rval = JSVAL_VOID;
    JS_AddValueRoot(context, &rval);

    gjs_closure_invoke(owner->lost_closure,
                          argc, argv, &rval);

    JS_RemoveValueRoot(context, &argv[0]);
    JS_RemoveValueRoot(context, &rval);

    JS_EndRequest(context);
}

static void
owner_closure_invalidated(gpointer  data,
                          GClosure *closure)
{
    GjsJSDBusNameOwner *owner;

    owner = (GjsJSDBusNameOwner*)data;

    if (owner) {
        gjs_dbus_release_name(owner->bus_type,
                              &owner->funcs,
                              owner);

        g_closure_unref(owner->acquired_closure);
        g_closure_unref(owner->lost_closure);

        g_free(owner->funcs.name);

        g_slice_free(GjsJSDBusNameOwner, owner);
    }

}

/* Args are bus_name, name type, acquired_func, lost_func */
static JSBool
gjs_js_dbus_acquire_name(JSContext  *context,
                         uintN       argc,
                         jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    char *bus_name;
    JSObject *acquire_func;
    JSObject *lost_func;
    GjsJSDBusNameOwner *owner;
    DBusBusType bus_type;
    GjsDBusNameType name_type;
    unsigned int id;
    jsval retval = JSVAL_VOID;

    if (argc < 4) {
        gjs_throw(context, "Not enough args, need bus name, name type, acquired_func, lost_func");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    bus_name = gjs_string_get_ascii(context, argv[0]);
    if (bus_name == NULL)
        return JS_FALSE;

    if (!JSVAL_IS_INT(argv[1])) {
        gjs_throw(context, "Second arg is an integer representing the name type (single or multiple instances)\n"
                     "Use the constants DBus.SINGLE_INSTANCE and DBus.MANY_INSTANCES, defined in the DBus module");
        goto fail;
    }

    name_type = (GjsDBusNameType)JSVAL_TO_INT(argv[1]);

    if (!JSVAL_IS_OBJECT(argv[2])) {
        gjs_throw(context, "Third arg is a callback to invoke on acquiring the name");
        goto fail;
    }

    acquire_func = JSVAL_TO_OBJECT(argv[2]);

    if (!JSVAL_IS_OBJECT(argv[3])) {
        gjs_throw(context, "Fourth arg is a callback to invoke on losing the name");
        goto fail;
    }

    lost_func = JSVAL_TO_OBJECT(argv[3]);

    owner = g_slice_new0(GjsJSDBusNameOwner);

    owner->funcs.name = bus_name;
    owner->funcs.type = name_type;
    owner->funcs.acquired = on_name_acquired;
    owner->funcs.lost = on_name_lost;
    owner->bus_type = bus_type;

    owner->acquired_closure =
        gjs_closure_new(context, acquire_func, "acquired bus name", TRUE);

    g_closure_ref(owner->acquired_closure);
    g_closure_sink(owner->acquired_closure);

    owner->lost_closure =
        gjs_closure_new(context, lost_func, "lost bus name", TRUE);

    g_closure_ref(owner->lost_closure);
    g_closure_sink(owner->lost_closure);

    /* Only add the invalidate notifier to one of the closures, should
     * be enough */
    g_closure_add_invalidate_notifier(owner->acquired_closure, owner,
                                      owner_closure_invalidated);

    id = gjs_dbus_acquire_name(bus_type,
                               &owner->funcs,
                               owner);

    if (!JS_NewNumberValue(context, (jsdouble)id, &retval)) {
        gjs_throw(context, "Could not convert name owner id to jsval");
        goto fail;
    }
    JS_SET_RVAL(context, vp, retval);

    return JS_TRUE;

 fail:
    g_free(bus_name);
    return JS_FALSE;
}

/* Args are name owner monitor id */
static JSBool
gjs_js_dbus_release_name_by_id (JSContext  *context,
                                uintN       argc,
                                jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    DBusBusType bus_type;
    unsigned int id;

    if (argc < 1) {
        gjs_throw(context, "Not enough args, need name owner monitor id");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    id = JSVAL_TO_INT(argv[0]);

    gjs_dbus_release_name_by_id(bus_type,
                                id);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

typedef struct {
    GClosure *appeared_closure;
    GClosure *vanished_closure;
    char *bus_name;
    DBusBusType bus_type;
} GjsJSDBusNameWatcher;

static void
on_name_appeared(DBusConnection *connection,
                 const char     *name,
                 const char     *owner_unique_name,
                 void           *data)
{
    int argc;
    jsval argv[2];
    jsval rval;
    JSContext *context;
    GjsJSDBusNameWatcher *watcher;

    watcher = data;

    context = get_callback_context(watcher->appeared_closure);
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure destroyed before we could notify name appeared");
        return;
    }

    JS_BeginRequest(context);

    argc = 2;

    gjs_set_values(context, argv, argc, JSVAL_VOID);
    gjs_root_value_locations(context, argv, argc);

    argv[0] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, name));
    argv[1] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, owner_unique_name));

    rval = JSVAL_VOID;
    JS_AddValueRoot(context, &rval);

    gjs_closure_invoke(watcher->appeared_closure,
                          argc, argv, &rval);

    JS_RemoveValueRoot(context, &rval);
    gjs_unroot_value_locations(context, argv, argc);

    JS_EndRequest(context);
}

static void
on_name_vanished(DBusConnection *connection,
                 const char     *name,
                 const char     *owner_unique_name,
                 void           *data)
{
    int argc;
    jsval argv[2];
    jsval rval;
    JSContext *context;
    GjsJSDBusNameWatcher *watcher;

    watcher = data;

    context = get_callback_context(watcher->vanished_closure);
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure destroyed before we could notify name vanished");
        return;
    }

    JS_BeginRequest(context);

    argc = 2;

    gjs_set_values(context, argv, argc, JSVAL_VOID);
    gjs_root_value_locations(context, argv, argc);

    argv[0] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, name));
    argv[1] = STRING_TO_JSVAL(JS_NewStringCopyZ(context, owner_unique_name));

    rval = JSVAL_VOID;
    JS_AddValueRoot(context, &rval);

    gjs_closure_invoke(watcher->vanished_closure,
                          argc, argv, &rval);

    JS_RemoveValueRoot(context, &rval);
    gjs_unroot_value_locations(context, argv, argc);

    JS_EndRequest(context);
}

static const GjsDBusWatchNameFuncs watch_name_funcs = {
    on_name_appeared,
    on_name_vanished
};

static void
watch_closure_invalidated(gpointer  data,
                          GClosure *closure)
{
    GjsJSDBusNameWatcher *watcher;

    watcher = (GjsJSDBusNameWatcher*)data;

    if (watcher) {
        gjs_dbus_unwatch_name(watcher->bus_type,
                              watcher->bus_name,
                              &watch_name_funcs,
                              watcher);

        g_free(watcher->bus_name);
        g_closure_unref(watcher->appeared_closure);
        g_closure_unref(watcher->vanished_closure);

        g_slice_free(GjsJSDBusNameWatcher, watcher);
    }

}

/* Args are bus_name, start_if_not_found, appeared_func, vanished_func */
static JSBool
gjs_js_dbus_watch_name(JSContext  *context,
                       uintN       argc,
                       jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    char *bus_name;
    JSBool start_if_not_found;
    JSObject *appeared_func;
    JSObject *vanished_func;
    GjsJSDBusNameWatcher *watcher;
    DBusBusType bus_type;

    if (argc < 4) {
        gjs_throw(context, "Not enough args, need bus name, acquired_func, lost_func");
        return JS_FALSE;
    }

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    bus_name = gjs_string_get_ascii(context, argv[0]);
    if (bus_name == NULL)
        return JS_FALSE;

    start_if_not_found = JS_FALSE;
    if (!JS_ValueToBoolean(context, argv[1], &start_if_not_found)) {
        if (!JS_IsExceptionPending(context))
            gjs_throw(context, "Second arg is a bool for whether to start the name if not found");
        goto fail;
    }

    if (!JSVAL_IS_OBJECT(argv[2])) {
        gjs_throw(context, "Third arg is a callback to invoke on seeing the name");
        goto fail;
    }

    appeared_func = JSVAL_TO_OBJECT(argv[2]);

    if (!JSVAL_IS_OBJECT(argv[3])) {
        gjs_throw(context, "Fourth arg is a callback to invoke when the name vanishes");
        goto fail;
    }

    vanished_func = JSVAL_TO_OBJECT(argv[3]);

    watcher = g_slice_new0(GjsJSDBusNameWatcher);

    watcher->appeared_closure =
        gjs_closure_new(context, appeared_func, "service appeared", TRUE);

    g_closure_ref(watcher->appeared_closure);
    g_closure_sink(watcher->appeared_closure);

    watcher->vanished_closure =
        gjs_closure_new(context, vanished_func, "service vanished", TRUE);

    g_closure_ref(watcher->vanished_closure);
    g_closure_sink(watcher->vanished_closure);

    watcher->bus_type = bus_type;
    watcher->bus_name = bus_name;

    /* Only add the invalidate notifier to one of the closures, should
     * be enough */
    g_closure_add_invalidate_notifier(watcher->appeared_closure, watcher,
                                      watch_closure_invalidated);

    gjs_dbus_watch_name(bus_type,
                        bus_name,
                        start_if_not_found ?
                        GJS_DBUS_NAME_START_IF_NOT_FOUND : 0,
                        &watch_name_funcs,
                        watcher);

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;

 fail:
    g_free(bus_name);
    return JS_FALSE;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
unique_name_getter(JSContext  *context,
                   JSObject   *obj,
                   jsid        id,
                   jsval      *value_p)
{
    char *name;
    DBusConnection *bus_connection;
    DBusBusType bus_type;

    if (!get_bus_type_from_object(context, obj, &bus_type))
        return JS_FALSE;

    if (!gjs_get_string_id(context, id, &name))
        return JS_FALSE;

    gjs_debug_jsprop(GJS_DEBUG_DBUS, "Get prop '%s' on dbus object", name);
    g_free(name);

    bus_check(context, bus_type);

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    if (bus_connection == NULL) {
        *value_p = JSVAL_NULL;
    } else {
        const char *unique_name;
        JSString *s;

        unique_name = dbus_bus_get_unique_name(bus_connection);

        s = JS_NewStringCopyZ(context, unique_name);
        *value_p = STRING_TO_JSVAL(s);
    }

    return JS_TRUE;
}

static JSBool
gjs_js_dbus_signature_length(JSContext  *context,
                             uintN       argc,
                             jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *signature;
    DBusSignatureIter iter;
    int length = 0;

    if (argc < 1) {
        gjs_throw(context, "Not enough args, need a dbus signature");
        return JS_FALSE;
    }

    signature = gjs_string_get_ascii(context, argv[0]);
    if (signature == NULL)
        return JS_FALSE;

    if (!dbus_signature_validate(signature, NULL)) {
        gjs_throw(context, "Invalid signature");
        g_free(signature);
        return JS_FALSE;
    }

    /* Empty sig is special 0-length case */
    if (*signature == '\0')
        goto out;

    dbus_signature_iter_init(&iter, signature);

    do {
        length++;
    } while (dbus_signature_iter_next(&iter));

 out:
    g_free(signature);
    JS_SET_RVAL(context, vp, INT_TO_JSVAL(length));

    return JS_TRUE;
}

static JSBool
gjs_js_dbus_start_service(JSContext  *context,
                          uintN       argc,
                          jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    char     *name;
    DBusBusType     bus_type;
    DBusConnection *bus_connection;
    JSBool ret = JS_FALSE;

    if (argc != 1) {
        gjs_throw(context, "Wrong number of arguments, expected service name");
        return JS_FALSE;
    }

    name = gjs_string_get_ascii(context, argv[0]);
    if (!name)
        return JS_FALSE;

    if (!get_bus_type_from_object(context, obj, &bus_type))
        goto out;

    if (!bus_check(context, bus_type))
        goto out;

    bus_connection = DBUS_CONNECTION_FROM_TYPE(bus_type);

    gjs_dbus_start_service(bus_connection, name);

    ret = JS_TRUE;

 out:
    g_free(name);
    return ret;
}

static JSBool
gjs_js_dbus_get_machine_id(JSContext *context,
                           JSObject  *obj,
                           jsid       key,
                           jsval     *value)
{
    char *machine_id;
    JSString *machine_id_string;

    if (value)
        *value = JSVAL_VOID;

    machine_id = dbus_get_local_machine_id();
    machine_id_string = JS_NewStringCopyZ(context, machine_id);
    dbus_free(machine_id);

    if (!machine_id_string)
        return JS_FALSE;

    if (value)
        *value = STRING_TO_JSVAL(machine_id_string);

    return JS_TRUE;
}

static JSBool
gjs_js_dbus_get_current_message_context(JSContext  *context,
                                        uintN       argc,
                                        jsval      *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    const char *sender;
    JSString *sender_str;
    JSObject *context_obj;
    jsval context_val;
    JSBool result = JS_FALSE;
    DBusMessage *current_message;

    if (!gjs_parse_args(context, "getCurrentMessageContext", "", argc, argv))
        return JS_FALSE;

    if (!_gjs_current_dbus_messages) {
        JS_SET_RVAL(context, vp, JSVAL_NULL);
        return JS_TRUE;
    }

    current_message = _gjs_current_dbus_messages->data;

    context_obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (context_obj == NULL)
        return JS_FALSE;

    context_val = OBJECT_TO_JSVAL(context_obj);
    JS_AddValueRoot(context, &context_val);

    sender = dbus_message_get_sender(current_message);
    if (sender)
        sender_str = JS_NewStringCopyZ(context, sender);
    else
        sender_str = NULL;

    if (!JS_DefineProperty(context, context_obj,
                           "sender",
                           sender_str ? STRING_TO_JSVAL(sender_str) : JSVAL_NULL,
                           NULL, NULL,
                           JSPROP_ENUMERATE))
        goto out;

    if (!JS_DefineProperty(context, context_obj,
                           "serial", INT_TO_JSVAL(dbus_message_get_serial(current_message)),
                           NULL, NULL,
                           JSPROP_ENUMERATE))
        goto out;

    result = JS_TRUE;
    JS_SET_RVAL(context, vp, context_val);

out:
    JS_RemoveValueRoot(context, &context_val);
    return result;
}


static JSBool
define_bus_proto(JSContext *context,
                 JSObject *module_obj,
                 JSObject **bus_proto_obj_out)
{
    JSObject *bus_proto_obj;
    jsval bus_proto_val;
    JSBool result;

    result = JS_FALSE;

    bus_proto_val = JSVAL_VOID;
    JS_AddValueRoot(context, &bus_proto_val);

    bus_proto_obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (bus_proto_obj == NULL)
        goto out;

    bus_proto_val = OBJECT_TO_JSVAL(bus_proto_obj);

    if (!JS_DefineProperty(context, bus_proto_obj,
                           "unique_name",
                           JSVAL_VOID,
                           unique_name_getter,
                           NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "call",
                           (JSNative)gjs_js_dbus_call,
                           8, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "call_async",
                           (JSNative)gjs_js_dbus_call_async,
                           9, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "acquire_name",
                           (JSNative)gjs_js_dbus_acquire_name,
                           3, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "release_name_by_id",
                           (JSNative)gjs_js_dbus_release_name_by_id,
                           1, GJS_MODULE_PROP_FLAGS))
        goto out;


    if (!JS_DefineFunction(context, bus_proto_obj,
                           "watch_name",
                           (JSNative)gjs_js_dbus_watch_name,
                           4, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "watch_signal",
                           (JSNative)gjs_js_dbus_watch_signal,
                           5, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "unwatch_signal_by_id",
                           (JSNative)gjs_js_dbus_unwatch_signal_by_id,
                           1, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "unwatch_signal",
                           (JSNative)gjs_js_dbus_unwatch_signal,
                           5, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "emit_signal",
                           (JSNative)gjs_js_dbus_emit_signal,
                           3, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "flush",
                           (JSNative)gjs_js_dbus_flush,
                           0, GJS_MODULE_PROP_FLAGS))
        goto out;

    if (!JS_DefineFunction(context, bus_proto_obj,
                           "start_service",
                           (JSNative)gjs_js_dbus_start_service,
                           1, GJS_MODULE_PROP_FLAGS))
        goto out;

    /* Add the bus proto object inside the passed in module object */
    if (!JS_DefineProperty(context, module_obj,
                           "_busProto", OBJECT_TO_JSVAL(bus_proto_obj),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto out;

    *bus_proto_obj_out = bus_proto_obj;
    result = JS_TRUE;

 out:
    JS_RemoveValueRoot(context, &bus_proto_val);

    return result;
}

static JSBool
define_bus_object(JSContext  *context,
                  JSObject   *module_obj,
                  JSObject   *proto_obj,
                  DBusBusType which_bus)
{
    JSObject *bus_obj;
    jsval bus_val;
    jsval bus_type_val;
    const char *bus_name;
    JSBool result;

    bus_name = GJS_DBUS_NAME_FROM_TYPE(which_bus);

    if (gjs_object_has_property(context, module_obj, bus_name))
        return JS_TRUE;

    result = JS_FALSE;

    bus_val = JSVAL_VOID;
    JS_AddValueRoot(context, &bus_val);

    bus_obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (bus_obj == NULL)
        goto out;
    /* We need to use a separate call to SetPrototype to work
     * around a SpiderMonkey bug where with clasp=NULL, the
     * parent and proto arguments to JS_ConstructObject are
     * lost.
     * https://bugzilla.mozilla.org/show_bug.cgi?id=599651
     */
    JS_SetPrototype(context, bus_obj, proto_obj);

    bus_val = OBJECT_TO_JSVAL(bus_obj);

    /* Store the bus type as a property of the object. This way
     * we can have generic methods that will get the bus type
     * from the object they are defined in
     */
    bus_type_val = INT_TO_JSVAL((int)which_bus);
    if (!JS_DefineProperty(context, bus_obj, "_dbusBusType",
                           bus_type_val,
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto out;


    /* When we get an incoming dbus call to path /com/litl/foo/bar
     * then we map that to the object dbus.session.exports.com.litl.foo.bar
     */
    if (!gjs_js_define_dbus_exports(context, bus_obj, which_bus))
        goto out;

    /* Add the bus object inside the passed in module object */
    if (!JS_DefineProperty(context, module_obj,
                           bus_name, OBJECT_TO_JSVAL(bus_obj),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto out;

    result = JS_TRUE;

 out:
    JS_RemoveValueRoot(context, &bus_val);

    return result;
}

JSBool
gjs_js_define_dbus_stuff(JSContext      *context,
                         JSObject       *module_obj)
{
    JSObject *bus_proto_obj;
    /* Note that we don't actually connect to dbus in here, since the
     * JS program may not even be using it.
     */

    if (!JS_DefineFunction(context, module_obj,
                           "signatureLength",
                           (JSNative)gjs_js_dbus_signature_length,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineProperty(context, module_obj,
                           "BUS_SESSION",
                           INT_TO_JSVAL(DBUS_BUS_SESSION),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineProperty(context, module_obj,
                           "BUS_SYSTEM",
                           INT_TO_JSVAL(DBUS_BUS_SYSTEM),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineProperty(context, module_obj,
                           "BUS_STARTER",
                           INT_TO_JSVAL(DBUS_BUS_STARTER),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineProperty(context, module_obj,
                           "localMachineID",
                           JSVAL_VOID,
                           gjs_js_dbus_get_machine_id, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "getCurrentMessageContext",
                           (JSNative)gjs_js_dbus_get_current_message_context,
                           0, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    /* Define both the session and system objects */
    if (!define_bus_proto(context, module_obj, &bus_proto_obj))
        return JS_FALSE;

    if (!define_bus_object(context, module_obj, bus_proto_obj, DBUS_BUS_SESSION))
        return JS_FALSE;

    if (!define_bus_object(context, module_obj, bus_proto_obj, DBUS_BUS_SYSTEM))
        return JS_FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("dbusNative", gjs_js_define_dbus_stuff)
