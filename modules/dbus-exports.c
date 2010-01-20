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

#include "dbus-exports.h"
#include "dbus-values.h"

#include "gjs-dbus/dbus.h"

#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

#include <string.h>

typedef struct {
    char *name;
    char *signature;
    gboolean readable;
    gboolean writable;
} PropertyDetails;

/*
 * The dbus.exports object contains objects whose methods are exported
 * over dbus. So e.g. if you create dbus.exports.foo.bar then
 * dbus method calls to path /foo/bar would go to this object.
 */

typedef struct {
    void *dummy;

    /* Back-pointers to ourselves.
     *
     * The JSObject* may not be safe if a copying GC can move them
     * around.  However, the alternatives are complicated, and
     * SpiderMonkey currently uses mark-and-sweep, so I think this
     * should be fine. We'll see I guess.
     */
    JSRuntime *runtime;
    JSObject   *object;

    DBusBusType which_bus;
    DBusConnection *connection_weak_ref;
    gboolean filter_was_registered;
} Exports;

static struct JSClass gjs_js_exports_class;

GJS_DEFINE_PRIV_FROM_JS(Exports, gjs_js_exports_class);

static void              property_details_init  (PropertyDetails *details);
static void              property_details_clear (PropertyDetails *details);

static void              on_bus_opened        (DBusConnection *connection,
                                               void           *data);
static void              on_bus_closed        (DBusConnection *connection,
                                               void           *data);
static DBusHandlerResult on_message           (DBusConnection *connection,
                                               DBusMessage    *message,
                                               void           *user_data);

static const GjsDBusConnectFuncs system_connect_funcs = {
    DBUS_BUS_SYSTEM,
    on_bus_opened,
    on_bus_closed
};

static const GjsDBusConnectFuncs session_connect_funcs = {
    DBUS_BUS_SESSION,
    on_bus_opened,
    on_bus_closed
};

static void
on_bus_opened(DBusConnection *connection,
              void           *data)
{
    Exports *priv = data;

    g_assert(priv->connection_weak_ref == NULL);

    priv->connection_weak_ref = connection;

    gjs_debug(GJS_DEBUG_DBUS, "%s bus opened, exporting JS dbus methods", GJS_DBUS_NAME_FROM_TYPE(priv->which_bus));

    if (priv->filter_was_registered)
        return;

    if (!dbus_connection_add_filter(connection,
                                    on_message, priv,
                                    NULL)) {
        gjs_debug(GJS_DEBUG_DBUS, "Failed to add message filter");
        return;
    }

    priv->filter_was_registered = TRUE;
}

static void
on_bus_closed(DBusConnection *connection,
              void           *data)
{
    Exports *priv = data;

    g_assert(priv->connection_weak_ref != NULL);

    priv->connection_weak_ref = NULL;

    gjs_debug(GJS_DEBUG_DBUS, "%s bus closed, unexporting JS dbus methods", GJS_DBUS_NAME_FROM_TYPE(priv->which_bus));

    if (priv->filter_was_registered) {
        dbus_connection_remove_filter(connection,
                                      on_message, priv);
        priv->filter_was_registered = FALSE;
    }
}

#define dbus_reply_from_exception(context, message, reply_p)            \
    (dbus_reply_from_exception_and_sender((context),                    \
                                          dbus_message_get_sender(message), \
                                          dbus_message_get_serial(message), \
                                          (reply_p)))
static JSBool
dbus_reply_from_exception_and_sender(JSContext    *context,
                                     const char   *sender,
                                     dbus_uint32_t serial,
                                     DBusMessage **reply_p)
{
    char *s;
    jsval exc;
    char *name = NULL;
    jsval nameval;

    *reply_p = NULL;

    if (!JS_GetPendingException(context, &exc))
        return JS_FALSE;

    if (JSVAL_IS_OBJECT(exc) &&
        gjs_object_get_property(context, JSVAL_TO_OBJECT(exc),
                                "dbusErrorName", &nameval))
        name = gjs_string_get_ascii(context, nameval);

    if (!gjs_log_exception(context, &s)) {
        g_free(name);
        return JS_FALSE;
    }

    gjs_debug(GJS_DEBUG_DBUS,
              "JS exception we will send as dbus reply to %s: %s",
              sender,
              s);

    *reply_p = dbus_message_new(DBUS_MESSAGE_TYPE_ERROR);
    dbus_message_set_destination(*reply_p, sender);
    dbus_message_set_reply_serial(*reply_p, serial);
    dbus_message_set_no_reply(*reply_p, TRUE);
    dbus_message_set_error_name(*reply_p, name ? name : DBUS_ERROR_FAILED);
    g_free(name);
    if (s != NULL) {
        DBusMessageIter iter;

        dbus_message_iter_init_append(*reply_p, &iter);

        if (!dbus_message_iter_append_basic(&iter,
                                            DBUS_TYPE_STRING,
                                            &s)) {
            dbus_message_unref(*reply_p);
            g_free(s);
            return JS_FALSE;
        }
        g_free(s);
    }

    return JS_TRUE;
}

static JSBool
signature_from_method(JSContext   *context,
                      JSObject    *method_obj,
                      char       **signature)
{
    jsval signature_value;

    if (gjs_object_get_property(context,
                                method_obj, "outSignature",
                                &signature_value)) {
        *signature = gjs_string_get_ascii(context,
                                                  signature_value);
        if (*signature == NULL) {
            return JS_FALSE;
        }
    } else {
        /* We default to a{sv} */
        *signature = g_strdup("a{sv}");
    }

    return JS_TRUE;
}

static gboolean
signature_has_one_element(const char *signature)
{
    DBusSignatureIter iter;

    if (!signature)
        return FALSE;

    dbus_signature_iter_init(&iter, signature);

    return !dbus_signature_iter_next(&iter);
}

static DBusMessage *
build_reply_from_jsval(JSContext     *context,
                       const char    *signature,
                       const char    *sender,
                       dbus_uint32_t  serial,
                       jsval          rval)
{
    DBusMessage *reply;
    DBusMessageIter arg_iter;
    DBusSignatureIter sig_iter;
    JSBool marshalled = JS_FALSE;

    reply = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_message_set_destination(reply, sender);
    dbus_message_set_reply_serial(reply, serial);
    dbus_message_set_no_reply(reply, TRUE);

    dbus_message_iter_init_append(reply, &arg_iter);

    if (JSVAL_IS_VOID(rval) || g_str_equal(signature, "")) {
        /* We don't want to send anything in these cases so skip the
         * marshalling altogether.
         */
        return reply;
    }

    dbus_signature_iter_init(&sig_iter, signature);

    if (signature_has_one_element(signature)) {
        marshalled = gjs_js_one_value_to_dbus(context, rval, &arg_iter, &sig_iter);
    } else {
        if (!JS_IsArrayObject(context, JSVAL_TO_OBJECT(rval))) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Signature has multiple items but return value is not an array");
            return reply;
        }
        marshalled = gjs_js_values_to_dbus(context, 0, rval, &arg_iter, &sig_iter);
    }

    if (!marshalled) {
        /* replace our planned reply with an error */
        dbus_message_unref(reply);
        if (!dbus_reply_from_exception_and_sender(context, sender, serial, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "conversion of dbus return value failed but no exception was set?");
    }

    return reply;
}

static DBusMessage*
invoke_js_from_dbus(JSContext   *context,
                    DBusMessage *method_call,
                    JSObject    *this_obj,
                    JSObject    *method_obj)
{
    DBusMessage *reply;
    int argc;
    jsval *argv;
    jsval rval;
    DBusMessageIter arg_iter;
    GjsRootedArray *values;
    char *signature;

    if (JS_IsExceptionPending(context)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Exception was pending before invoking JS method??? Not expected");
        gjs_log_exception(context, NULL);
    }

    reply = NULL;

    dbus_message_iter_init(method_call, &arg_iter);

    if (!gjs_js_values_from_dbus(context, &arg_iter, &values)) {
        if (!dbus_reply_from_exception(context, method_call, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "conversion of dbus method arg failed but no exception was set?");
        return reply;
    }

    argc = gjs_rooted_array_get_length(context, values);
    argv = gjs_rooted_array_get_data(context, values);

    rval = JSVAL_VOID;
    JS_AddValueRoot(context, &rval);

    gjs_js_push_current_message(method_call);
    if (!gjs_call_function_value(context,
                                 this_obj,
                                 OBJECT_TO_JSVAL(method_obj),
                                 argc,
                                 argv,
                                 &rval)) {
        /* Exception thrown... */
        gjs_debug(GJS_DEBUG_DBUS,
                  "dbus method invocation failed");

        if (!dbus_reply_from_exception(context, method_call, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "dbus method invocation failed but no exception was set?");

        goto out;
    }

    if (dbus_reply_from_exception(context, method_call, &reply)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Closure invocation succeeded but an exception was set?");
        goto out;
    }

    if (!signature_from_method(context,
                               method_obj,
                               &signature)) {
        if (!dbus_reply_from_exception(context, method_call, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "dbus method invocation failed but no exception was set?");

        goto out;
    }

    reply = build_reply_from_jsval(context,
                                   signature,
                                   dbus_message_get_sender(method_call),
                                   dbus_message_get_serial(method_call),
                                   rval);

    g_free(signature);

 out:
    gjs_rooted_array_free(context, values, TRUE);
    JS_RemoveValueRoot(context, &rval);

    gjs_js_pop_current_message();

    if (reply)
        gjs_debug(GJS_DEBUG_DBUS, "Sending %s reply to dbus method %s",
                  dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN ?
                  "normal" : "error",
                  dbus_message_get_member(method_call));
    else
        gjs_debug(GJS_DEBUG_DBUS,
                  "Failed to create reply to dbus method %s",
                  dbus_message_get_member(method_call));

    return reply;
}

static JSBool
async_call_callback(JSContext *context,
                    uintN      argc,
                    jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    DBusConnection *connection;
    DBusBusType which_bus;
    DBusMessage *reply;
    JSObject *callback_object;
    char *sender;
    dbus_uint32_t serial;
    jsval prop_value;
    char *signature = NULL;
    gboolean thrown;

    callback_object = JSVAL_TO_OBJECT(JS_CALLEE(context, vp));
    reply = NULL;
    thrown = FALSE;

    if (!gjs_object_require_property(context,
                                     callback_object,
                                     "DBus async call callback",
                                     "_dbusSender",
                                     &prop_value)) {
        /* we are a little screwed because we can't send the
         * error back. This should never happen though */
        gjs_log_and_keep_exception(context, NULL);
        return JS_FALSE;
    }
    sender = gjs_string_get_ascii(context, prop_value);
    if (!sender)
        return JS_FALSE;

    if (!gjs_object_require_property(context,
                                     callback_object,
                                     "DBus async call callback",
                                     "_dbusSerial",
                                     &prop_value)) {
        gjs_log_and_keep_exception(context, NULL);
        goto fail;
    }
    if (!JS_ValueToECMAUint32(context, prop_value, &serial))
        goto fail;

    if (!gjs_object_require_property(context,
                                     callback_object,
                                     "DBus async call callback",
                                     "_dbusBusType",
                                     &prop_value)) {
        gjs_log_and_keep_exception(context, NULL);
        goto fail;
    }
    which_bus = JSVAL_TO_INT(prop_value);

    /* From now we have enough information to
     * send the exception back to the callee so we'll do so
     */
    if (!gjs_object_require_property(context,
                                     callback_object,
                                     "DBus async call callback",
                                     "_dbusOutSignature",
                                     &prop_value)) {
        thrown = TRUE;
        goto out;
    }
    signature = gjs_string_get_ascii(context, prop_value);
    if (!signature)
        goto fail;

    if ((argc == 0 && !g_str_equal(signature, ""))
        || argc > 1) {
        gjs_throw(context, "The callback to async DBus calls takes one argument, "
                  "the return value or array of return values");
        thrown = TRUE;
        goto out;
    }

    reply = build_reply_from_jsval(context,
                                   signature,
                                   sender,
                                   serial,
                                   argv[0]);

 out:
    if (!reply && thrown) {
        if (!dbus_reply_from_exception_and_sender(context, sender, serial, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "dbus method invocation failed but no exception was set?");
    }

    g_free(sender);
    g_free(signature);

    if (reply) {
        gjs_dbus_add_bus_weakref(which_bus, &connection);
        if (!connection) {
            gjs_throw(context, "We were disconnected from the bus before the callback "
                      "to some async remote call was called");
            dbus_message_unref(reply);
            gjs_dbus_remove_bus_weakref(which_bus, &connection);
            return JS_FALSE;
        }
        dbus_connection_send(connection, reply, NULL);
        gjs_dbus_remove_bus_weakref(which_bus, &connection);
        dbus_message_unref(reply);
    }
    if (!thrown)
        JS_SET_RVAL(context, vp, JSVAL_VOID);

    return (thrown == FALSE);

 fail:
    g_free(sender);
    return JS_FALSE;
}

/* returns an error message or NULL */
static DBusMessage *
invoke_js_async_from_dbus(JSContext   *context,
                          DBusBusType  bus_type,
                          DBusMessage *method_call,
                          JSObject    *this_obj,
                          JSObject    *method_obj)
{
    DBusMessage *reply;
    int argc;
    jsval *argv;
    DBusMessageIter arg_iter;
    GjsRootedArray *values;
    JSFunction *callback;
    JSObject *callback_object;
    JSString *sender_string;
    jsval serial_value;
    gboolean thrown;
    jsval ignored;
    char *signature;
    JSString *signature_string;

    reply = NULL;
    thrown = FALSE;
    argc = 0;
    argv = NULL;

    if (JS_IsExceptionPending(context)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Exception was pending before invoking JS method??? Not expected");
        gjs_log_exception(context, NULL);
    }

    dbus_message_iter_init(method_call, &arg_iter);

    if (!gjs_js_values_from_dbus(context, &arg_iter, &values)) {
        if (!dbus_reply_from_exception(context, method_call, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "conversion of dbus method arg failed but no exception was set?");
        return reply;
    }

    /* we will add an argument, the callback */
    callback = JS_NewFunction(context,
                              (JSNative)async_call_callback,
                              1, 0,
                              NULL,
                              "" /* anonymous */);

    if (!callback) {
        thrown = TRUE;
        goto out;
    }

    callback_object = JS_GetFunctionObject(callback);
    g_assert(callback_object);

    gjs_rooted_array_append(context, values, OBJECT_TO_JSVAL(callback_object));

    /* We attach the DBus sender and serial as properties to
     * callback, so we don't need to bother with memory managing them
     * if the callback is never called and just discarded.*/
    sender_string = JS_NewStringCopyZ(context, dbus_message_get_sender(method_call));
    if (!sender_string) {
        thrown = TRUE;
        goto out;
    }

    if (!JS_DefineProperty(context,
                           callback_object,
                           "_dbusSender",
                           STRING_TO_JSVAL(sender_string),
                           NULL, NULL,
                           0)) {
        thrown = TRUE;
        goto out;
    }

    if (!JS_NewNumberValue(context,
                           (double)dbus_message_get_serial(method_call),
                           &serial_value)) {
        thrown = TRUE;
        goto out;
    }

    if (!JS_DefineProperty(context,
                           callback_object,
                           "_dbusSerial",
                           serial_value,
                           NULL, NULL,
                           0)) {
        thrown = TRUE;
        goto out;
    }

    if (!JS_DefineProperty(context,
                           callback_object,
                           "_dbusBusType",
                           INT_TO_JSVAL(bus_type),
                           NULL, NULL,
                           0)) {
        thrown = TRUE;
        goto out;
    }

    if (!signature_from_method(context,
                               method_obj,
                               &signature)) {
        thrown = TRUE;
        goto out;
    }

    signature_string = JS_NewStringCopyZ(context, signature);
    g_free(signature);
    if (!signature_string) {
        thrown = TRUE;
        goto out;
    }

    if (!JS_DefineProperty(context,
                           callback_object,
                           "_dbusOutSignature",
                           STRING_TO_JSVAL(signature_string),
                           NULL, NULL,
                           0)) {
        thrown = TRUE;
        goto out;
    }

    argc = gjs_rooted_array_get_length(context, values);
    argv = gjs_rooted_array_get_data(context, values);

    if (!gjs_call_function_value(context,
                                 this_obj,
                                 OBJECT_TO_JSVAL(method_obj),
                                 argc,
                                 argv,
                                 &ignored)) {
        thrown = TRUE;
        goto out;
    }

 out:
    if (thrown) {
        if (!dbus_reply_from_exception(context, method_call, &reply))
            gjs_debug(GJS_DEBUG_DBUS,
                      "conversion of dbus method arg failed but no exception was set?");
    }

    if (argv) {
        gjs_unroot_value_locations(context, argv, argc);
    }

    return reply;
}

static JSObject*
find_js_property_by_path(JSContext  *context,
                         JSObject   *root_obj,
                         const char *path,
                         JSObject **dir_obj_p)
{
    char **elements;
    int i;
    JSObject *obj;
    jsval value;

    elements = g_strsplit(path, "/", -1);
    obj = root_obj;

    /* g_strsplit returns empty string for the first
     * '/' and if you split just "/" it returns two
     * empty strings. We just skip all empty strings,
     * and start with element[1] since the first is
     * always an empty string.
     */
    for (i = 1; elements[i] != NULL; ++i) {

        if (*(elements[i]) == '\0')
            continue;

        gjs_object_get_property(context, obj, elements[i], &value);

        if (JSVAL_IS_VOID(value) ||
            JSVAL_IS_NULL(value) ||
            !JSVAL_IS_OBJECT(value)) {
            obj = NULL;
            break;
        }

        obj = JSVAL_TO_OBJECT(value);
    }

    g_strfreev(elements);

    // this is the directory object; see if there's an actual
    // implementation object hanging off it.
    if (dir_obj_p)
        *dir_obj_p = obj;

    if (obj != NULL) {
        gjs_object_get_property(context, obj, "-impl-", &value);

        if (JSVAL_IS_VOID(value) ||
            JSVAL_IS_NULL(value) ||
            !JSVAL_IS_OBJECT(value)) {
            obj = NULL;
        } else {
            obj = JSVAL_TO_OBJECT(value);
        }
    }

    return obj;
}

static gboolean
find_method(JSContext  *context,
            JSObject   *obj,
            const char *method_name,
            jsval      *method_value)
{
    gjs_object_get_property(context,
                            obj,
                            method_name,
                            method_value);

    if (JSVAL_IS_VOID(*method_value) ||
        JSVAL_IS_NULL(*method_value) ||
        !JSVAL_IS_OBJECT(*method_value)) {
        return JS_FALSE;
    }

    return JS_TRUE;
}

/* FALSE on exception only; if no array, sets its val to void */
static gboolean
find_properties_array(JSContext       *context,
                      JSObject        *obj,
                      const char      *iface,
                      jsval           *array_p,
                      unsigned int    *array_length_p)
{
    /* We are looking for obj._dbusInterfaces[iface].properties */

    jsval ifaces_val;
    jsval iface_val;

    *array_p = JSVAL_VOID;
    *array_length_p = 0;

    ifaces_val = JSVAL_VOID;
    if (!gjs_object_get_property(context,
                                 obj,
                                 "_dbusInterfaces",
                                 &ifaces_val)) {
        /* NOT an exception ... object simply has no properties */
        return JS_TRUE;
    }

    iface_val = JSVAL_VOID;
    if (!gjs_object_get_property(context,
                                 JSVAL_TO_OBJECT(ifaces_val),
                                 iface,
                                 &iface_val)) {
        /* NOT an exception ... object simply lacks the interface */
    }

    /* http://bugzilla.gnome.org/show_bug.cgi?id=569933
     * libnm is screwed up and passes wrong interface.
     * Fortunately the properties interface does not
     * have any properties so there's no case where
     * we actually want to use it.
     */
    if (JSVAL_IS_VOID(iface_val) &&
        strcmp(iface, DBUS_INTERFACE_PROPERTIES) == 0) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Changing interface to work around GNOME bug 569933");

        if (!gjs_object_get_property(context,
                                     JSVAL_TO_OBJECT(ifaces_val),
                                     "org.freedesktop.NetworkManager",
                                     &iface_val)) {
            /* NOT an exception ... object simply lacks the interface */
        }
    }

    if (JSVAL_IS_VOID(iface_val)) {
        /* NOT an exception ... object simply lacks the interface */
        return JS_TRUE;
    }

    if (!gjs_object_get_property(context,
                                 JSVAL_TO_OBJECT(iface_val),
                                 "properties",
                                 array_p)) {
        /* NOT an exception ... interface simply has no properties */
        return JS_TRUE;
    }

    if (!JS_GetArrayLength(context, JSVAL_TO_OBJECT(*array_p),
                           array_length_p)) {
        gjs_throw(context, "Error retrieving length property of properties array");
        return JS_FALSE;
    }

    return JS_TRUE;
}

static void
property_details_init(PropertyDetails *details)
{
    details->name = NULL;
    details->signature = NULL;
    details->readable = FALSE;
    details->writable = FALSE;
}

static void
property_details_clear(PropertyDetails *details)
{
    g_free(details->name);
    g_free(details->signature);
    property_details_init(details);
}

/* FALSE on exception; throws if details are bad */
static gboolean
unpack_property_details(JSContext       *context,
                        JSObject        *prop_description,
                        PropertyDetails *details)
{
    jsval name_val;
    jsval signature_val;
    jsval access_val;
    char *name = NULL;
    char *signature = NULL;
    char *access = NULL;

    if (!gjs_object_get_property(context,
                                 prop_description,
                                 "name",
                                 &name_val)) {
        gjs_throw(context,
                  "Property has no name");
        return JS_FALSE;
    }

    name = gjs_string_get_ascii(context,
                                        name_val);
    if (name == NULL) {
        return JS_FALSE;
    }

    if (!gjs_object_get_property(context,
                                 prop_description,
                                 "signature",
                                 &signature_val)) {
        gjs_throw(context,
                  "Property %s has no signature",
                  name);
        goto fail;
    }

    signature = gjs_string_get_ascii(context,
                                             signature_val);
    if (signature == NULL)
        goto fail;

    if (!gjs_object_get_property(context,
                                 prop_description,
                                 "access",
                                 &access_val)) {
        gjs_throw(context,
                  "Property %s has no access",
                  name);
        goto fail;
    }

    access = gjs_string_get_ascii(context,
                                          access_val);
    if (access == NULL)
        goto fail;

    g_assert(name && signature && access);

    if (strcmp(access, "readwrite") == 0) {
        details->readable = TRUE;
        details->writable = TRUE;
    } else if (strcmp(access, "read") == 0) {
        details->readable = TRUE;
    } else if (strcmp(access, "write") == 0) {
        details->writable = TRUE;
    } else {
        gjs_throw(context, "Unknown access on property, should be readwrite read or write");
        goto fail;
    }

    details->name = name;
    details->signature = signature;

    g_free(access);
    return JS_TRUE;

 fail:
    g_free(access);
    g_free(signature);
    g_free(name);
    return JS_FALSE;
}

/* FALSE on exception, NULL property name in details if no such
 * property. Exceptions here are only for malformed introspection data
 * or out of memory or something, not a missing property.
 */
static gboolean
find_property_details(JSContext       *context,
                      JSObject        *obj,
                      const char      *iface,
                      const char      *prop_name,
                      PropertyDetails *details)
{
    /* We are looking for obj._dbusInterfaces[iface].properties array
     * member where property.name == prop_name
     */
    jsval properties_array_val;
    unsigned int length, i;

    g_assert(details->name == NULL);

    properties_array_val = JSVAL_VOID;
    if (!find_properties_array(context, obj,
                               iface, &properties_array_val,
                               &length)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "No properties found on interface %s",
                  iface);
        return JS_FALSE;
    }

    if (JSVAL_IS_VOID(properties_array_val)) {
        /* NOT an exception ... interface simply has no properties */
        return JS_TRUE;
    }

    for (i = 0; i < length; ++i) {
        jsval property_val;

        property_val = JSVAL_VOID;
        if (!JS_GetElement(context, JSVAL_TO_OBJECT(properties_array_val),
                           i, &property_val) ||
            JSVAL_IS_VOID(property_val)) {
            gjs_throw(context,
                      "Error accessing element %d of properties array",
                      i);
            return JS_FALSE;
        }

        if (!unpack_property_details(context,
                                     JSVAL_TO_OBJECT(property_val),
                                     details)) {
            return JS_FALSE;
        }

        if (strcmp(prop_name, details->name) != 0) {
            property_details_clear(details);
            continue;
        }

        return JS_TRUE;
    }

    /* Property was not found on the object, not an exception */
    return JS_TRUE;
}

static DBusMessage*
handle_get_property(JSContext      *context,
                    JSObject       *obj,
                    DBusMessage    *message,
                    DBusError      *derror)
{
    const char *iface = NULL;
    const char *prop_name = NULL;
    PropertyDetails details;
    jsval value;
    DBusMessageIter iter;
    DBusMessageIter variant_iter;
    DBusSignatureIter sig_iter;
    DBusMessage *reply;

    reply = NULL;

    if (!dbus_message_get_args(message,
                               derror,
                               DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_STRING, &prop_name,
                               DBUS_TYPE_INVALID)) {
        return NULL;
    }

    property_details_init(&details);
    if (!find_property_details(context, obj, iface, prop_name,
                               &details)) {

        /* Should mean an exception was set */

        if (dbus_reply_from_exception(context, message,
                                      &reply)) {
            return reply;
        } else {
            dbus_set_error(derror,
                           DBUS_ERROR_INVALID_ARGS,
                           "Getting property %s.%s an exception should have been set",
                           iface, prop_name);
            return NULL;
        }
    }

    /* http://bugzilla.gnome.org/show_bug.cgi?id=570031
     * org.freedesktop.NetworkManager used rather than
     * org.freedesktop.NetworkManager.Connection.Active
     * on property Devices
     */
    if (details.name == NULL &&
        strcmp(prop_name, "Devices") == 0 &&
        strcmp(iface, "org.freedesktop.NetworkManager") == 0) {
        if (!find_property_details(context, obj,
                                   "org.freedesktop.NetworkManager.Connection.Active",
                                   prop_name,
                                   &details)) {
            /* Should mean an exception was set */

            if (dbus_reply_from_exception(context, message,
                                          &reply)) {
                return reply;
            } else {
                dbus_set_error(derror,
                               DBUS_ERROR_INVALID_ARGS,
                               "Getting property %s.%s an exception should have been set",
                               iface, prop_name);
                return NULL;
            }
        }
    }

    if (details.name == NULL) {
        dbus_set_error(derror,
                       DBUS_ERROR_INVALID_ARGS,
                       "No such property %s.%s",
                       iface, prop_name);
        return NULL;
    }

    g_assert(details.name != NULL);
    g_assert(details.signature != NULL);

    if (!details.readable) {

        property_details_clear(&details);

        dbus_set_error(derror,
                       DBUS_ERROR_INVALID_ARGS,
                       "Property %s.%s not readable",
                       iface, prop_name);
        return NULL;
    }

    value = JSVAL_VOID;
    JS_AddValueRoot(context, &value);
    if (!gjs_object_require_property(context, obj,
                                     "DBus GetProperty callee",
                                     prop_name, &value)) {

        JS_RemoveValueRoot(context, &value);
        property_details_clear(&details);

        dbus_reply_from_exception(context, message,
                                  &reply);
        g_assert(reply != NULL);
        return reply;
    }

    reply = dbus_message_new_method_return(message);
    g_assert(reply != NULL); /* assert not oom */

    dbus_message_iter_init_append(reply, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
                                     details.signature,
                                     &variant_iter);

    dbus_signature_iter_init(&sig_iter, details.signature);
    if (!gjs_js_one_value_to_dbus(context, value,
                                  &variant_iter, &sig_iter)) {

        property_details_clear(&details);
        JS_RemoveValueRoot(context, &value);

        dbus_message_unref(reply);
        reply = NULL;
        dbus_reply_from_exception(context, message,
                                  &reply);

        return reply;
    }

    dbus_message_iter_close_container(&iter, &variant_iter);

    JS_RemoveValueRoot(context, &value);

    property_details_clear(&details);

    return reply;
}

static DBusMessage*
handle_get_all_properties(JSContext      *context,
                          JSObject       *obj,
                          DBusMessage    *message,
                          DBusError      *derror)
{
    const char *iface;
    jsval properties_array_val;
    unsigned int length, i;
    DBusMessage *reply;
    DBusMessageIter iter;
    DBusMessageIter dict_iter;

    reply = NULL;
    iface = NULL;

    if (!dbus_message_get_args(message,
                               derror,
                               DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_INVALID)) {
        return NULL;
    }

    properties_array_val = JSVAL_VOID;
    if (!find_properties_array(context, obj,
                               iface, &properties_array_val,
                               &length)) {
        goto js_exception;
    }

    /* Open return dictionary */
    reply = dbus_message_new_method_return(message);
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                     "{sv}", &dict_iter);

    if (!JSVAL_IS_VOID(properties_array_val)) {
        for (i = 0; i < length; ++i) {
            jsval property_val;
            PropertyDetails details;
            DBusMessageIter entry_iter;
            DBusMessageIter entry_value_iter;
            DBusSignatureIter sig_iter;
            jsval value;

            property_val = JSVAL_VOID;
            if (!JS_GetElement(context, JSVAL_TO_OBJECT(properties_array_val),
                               i, &property_val) ||
                JSVAL_IS_VOID(property_val)) {
                gjs_throw(context,
                          "Error accessing element %d of properties array",
                          i);
                goto js_exception;
            }

            property_details_init(&details);
            if (!unpack_property_details(context,
                                         JSVAL_TO_OBJECT(property_val),
                                         &details)) {
                goto js_exception;
            }

            g_assert(details.name != NULL);
            g_assert(details.signature != NULL);

            if (!details.readable) {
                property_details_clear(&details);

                continue;
            }

            value = JSVAL_VOID;
            JS_AddValueRoot(context, &value);
            if (!gjs_object_require_property(context, obj,
                                             "DBus GetAllProperties callee",
                                             details.name, &value)) {

                property_details_clear(&details);
                JS_RemoveValueRoot(context, &value);

                goto js_exception;
            }

            dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY,
                                             NULL, &entry_iter);

            dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING,
                                           &details.name);


            dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT,
                                             details.signature, &entry_value_iter);

            dbus_signature_iter_init(&sig_iter, details.signature);
            if (!gjs_js_one_value_to_dbus(context, value, &entry_value_iter,
                                          &sig_iter)) {
#ifdef HAVE_DBUS_MESSAGE_ITER_ABANDON_CONTAINER
                dbus_message_iter_abandon_container(&entry_iter, &entry_value_iter);
#endif
                JS_RemoveValueRoot(context, &value);
                property_details_clear(&details);
                goto js_exception;
            }
            dbus_message_iter_close_container(&entry_iter, &entry_value_iter);

            JS_RemoveValueRoot(context, &value);

            dbus_message_iter_close_container(&dict_iter, &entry_iter);
            property_details_clear(&details);
        }
    }

    /* close return dictionary */
    dbus_message_iter_close_container(&iter, &dict_iter);

    return reply;

 js_exception:

    if (reply)
        dbus_message_unref(reply);

    dbus_reply_from_exception(context, message, &reply);

    g_assert(reply != NULL);

    return reply;
}

static DBusMessage*
handle_set_property(JSContext      *context,
                    JSObject       *obj,
                    DBusMessage    *message,
                    DBusError      *derror)
{
    const char *iface = NULL;
    const char *prop_name = NULL;
    DBusMessageIter iter;
    PropertyDetails details;
    jsval value;
    DBusMessage *reply;

    reply = NULL;

    if (!dbus_message_has_signature(message,
                                    "ssv")) {
        dbus_set_error(derror,
                       DBUS_ERROR_INVALID_ARGS,
                       DBUS_INTERFACE_PROPERTIES ".Set signature is not '%s'",
                       dbus_message_get_signature(message));
        return NULL;
    }

    dbus_message_iter_init(message, &iter);

    dbus_message_iter_get_basic(&iter, &iface);
    dbus_message_iter_next(&iter);

    dbus_message_iter_get_basic(&iter, &prop_name);
    dbus_message_iter_next(&iter);

    property_details_init(&details);
    if (!find_property_details(context, obj, iface, prop_name,
                               &details)) {

        /* Should mean exception set */

        if (dbus_reply_from_exception(context, message,
                                      &reply)) {
            return reply;
        } else {
            dbus_set_error(derror,
                           DBUS_ERROR_INVALID_ARGS,
                           "Getting property %s.%s an exception should have been set",
                           iface, prop_name);
            return NULL;
        }
    }

    if (details.name == NULL) {
        dbus_set_error(derror,
                       DBUS_ERROR_INVALID_ARGS,
                       "No such property %s.%s",
                       iface, prop_name);
        return NULL;
    }

    g_assert(details.name != NULL);
    g_assert(details.signature != NULL);

    /* FIXME At the moment we don't use the signature when setting,
     * though really to be fully paranoid we probably ought to.
     */

    if (!details.writable) {
        property_details_clear(&details);

        dbus_set_error(derror,
                       DBUS_ERROR_INVALID_ARGS,
                       "Property %s.%s not writable",
                       iface, prop_name);
        return NULL;
    }

    property_details_clear(&details);

    value = JSVAL_VOID;
    JS_AddValueRoot(context, &value);
    gjs_js_one_value_from_dbus(context, &iter, &value);

    if (dbus_reply_from_exception(context, message, &reply)) {
        JS_RemoveValueRoot(context, &value);
        return reply;
    }

    /* this throws on oom or if prop is read-only for example */
    JS_SetProperty(context, obj, prop_name, &value);

    JS_RemoveValueRoot(context, &value);

    if (!dbus_reply_from_exception(context, message, &reply)) {
        g_assert(reply == NULL);
        reply = dbus_message_new_method_return(message);
    }

    g_assert(reply != NULL);
    return reply;
}

static DBusHandlerResult
handle_properties(JSContext      *context,
                  DBusConnection *connection,
                  JSObject       *obj,
                  DBusMessage    *message,
                  const char     *method_name)
{
    DBusError derror;
    DBusMessage *reply;

    reply = NULL;
    dbus_error_init(&derror);

    if (strcmp(method_name, "Get") == 0) {
        reply = handle_get_property(context, obj, message, &derror);
    } else if (strcmp(method_name, "Set") == 0) {
        reply = handle_set_property(context, obj, message, &derror);
    } else if (strcmp(method_name, "GetAll") == 0) {
        reply = handle_get_all_properties(context, obj, message, &derror);
    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_error_is_set(&derror)) {
        g_assert(reply == NULL);
        reply = dbus_message_new_error(message,
                                       derror.name,
                                       derror.message);
    }
    g_assert(reply != NULL); /* note: fails on OOM */

    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_introspect(JSContext      *context,
                  DBusConnection *connection,
                  JSObject       *dir_obj,
                  JSObject       *obj,
                  DBusMessage    *message)
{
    DBusMessage *reply;
    char **children;
    char *interfaceXML;
    GString *doc;
    int i;
    JSObject *props_iter = NULL;
    JSString *key_str = NULL;
    jsid prop_id;

    reply = NULL;

    if (!dbus_connection_list_registered (connection,
                                          dbus_message_get_path (message),
                                          &children)) {
        g_error("No memory");
    }

    doc = g_string_new(NULL);

    g_string_append(doc, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
    g_string_append(doc, "<node>\n");

    for (i = 0; children[i] != NULL; ++i) {
        g_string_append_printf(doc, "  <node name=\"%s\"/>\n",
                               children[i]);
    }

    JS_AddObjectRoot(context, &props_iter);
    JS_AddStringRoot(context, &key_str);
    props_iter = JS_NewPropertyIterator(context, dir_obj);

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, props_iter, &prop_id)) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Failed to get next property iterating dbus directory");
        goto out;
    }

    while (!JSID_IS_VOID(prop_id)) {
        char *key;
        jsval keyval;
        jsval valueval = JSVAL_VOID;

        if (!JS_IdToValue(context, prop_id, &keyval)) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Failed to convert dbus object id to value");
            goto out;
        }

        // Note that keyval can be integer. For example, the path
        //  /org/freedesktop/NetworkManagerSettings/0
        // has an object with an JSVAL_IS_INT key at the end.  See
        // the note at the end of
        //   https://developer.mozilla.org/en/SpiderMonkey/JSAPI_Reference/jsid
        // for a bit more info.  At any rate, force to string.
        key_str = JS_ValueToString(context, keyval);
        if (!key_str) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Failed to convert dbus object value to string");
            goto out;
        }

        if (!gjs_string_to_utf8(context, keyval, &key))
            goto out;

        if (!gjs_object_require_property(context, dir_obj,
                                         "dbus directory",
                                         key, &valueval)) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Somehow failed to get property of dbus object");
            g_free(key);
            goto out;
        }

        /* ignore non-object values and the '-impl-' node. */
        if (JSVAL_IS_OBJECT(valueval) && strcmp(key, "-impl-") != 0) {
            g_string_append_printf(doc, "  <node name=\"%s\"/>\n",
                                   key);
        }
        g_free(key);

        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, props_iter, &prop_id)) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Failed to get next property iterating dbus object");
            goto out;
        }
    }

    // add interface description for this node
    if (obj != NULL) {
        jsval valueval;

        if (!JS_CallFunctionName(context, obj, "getDBusInterfaceXML", 0, NULL,
                                 &valueval)) {
            gjs_debug(GJS_DEBUG_DBUS, "Error calling getDBusInterfaceXML (did you forget to call conformExport?)");
            gjs_log_exception(context, NULL);
        } else if (!gjs_string_to_utf8(context, valueval, &interfaceXML)) {
            gjs_debug(GJS_DEBUG_DBUS,
                      "Couldn't stringify getDBusInterfaceXML() retval");
            JS_ClearPendingException(context);
        } else {
            g_string_append(doc, interfaceXML);
            g_free(interfaceXML);
        }

    }

    g_string_append_printf(doc, "</node>\n");

    reply = dbus_message_new_method_return(message);
    if (reply == NULL)
        g_error("No memory");

    dbus_message_append_args(reply,
                             DBUS_TYPE_STRING, &doc->str,
                             DBUS_TYPE_INVALID);

    dbus_connection_send(connection, reply, NULL);

 out:
    JS_RemoveStringRoot(context, &key_str);
    JS_RemoveObjectRoot(context, &props_iter);

    if (reply != NULL)
        dbus_message_unref(reply);
    else
        gjs_debug(GJS_DEBUG_DBUS,
                  "Error introspecting dbus exports object; shouldn't happen, apparently it did, figure it out...");

    g_string_free(doc, TRUE);
    dbus_free_string_array(children);

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
on_message(DBusConnection *connection,
           DBusMessage    *message,
           void           *user_data)
{
    const char *path;
    DBusHandlerResult result;
    JSContext *context;
    JSObject *obj, *dir_obj = NULL;
    const char *method_name;
    char *async_method_name;
    jsval method_value;
    DBusMessage *reply;
    Exports *priv;

    priv = user_data;
    async_method_name = NULL;
    reply = NULL;

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    context = gjs_runtime_get_current_context(priv->runtime);

    JS_BeginRequest(context);
    method_value = JSVAL_VOID;
    JS_AddValueRoot(context, &method_value);

    result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    path = dbus_message_get_path(message);

    obj = find_js_property_by_path(context,
                                   priv->object,
                                   path, &dir_obj);

    method_name = dbus_message_get_member(message);

    /* we implement most of Introspect for all exported objects, although
     * they can provide their own interface descriptions if they like.
     * (Note that obj may == NULL here, which just means this is a "bare"
     * directory node, with no implementation attached.
     */
    if (dbus_message_is_method_call (message,
                                     DBUS_INTERFACE_INTROSPECTABLE,
                                     "Introspect")) {
        gjs_debug(GJS_DEBUG_DBUS,
                  "Default-introspecting JS obj at dbus path %s",
                  path);

        if (dir_obj != NULL)
            result = handle_introspect(context,
                                       connection,
                                       dir_obj, obj,
                                       message);
        goto out;
    }

    if (obj == NULL) {
        /* No JS object at the path, no need to be noisy. If there's another
         * handler it can handle the message, otherwise the caller should
         * receive (and log) NoSuchMethod error. */
        goto out;
    }

    if (dbus_message_has_interface(message,
                                   DBUS_INTERFACE_PROPERTIES)) {
        const char *iface;
        iface = NULL;
        dbus_message_get_args(message, NULL,
                              DBUS_TYPE_STRING, &iface,
                              DBUS_TYPE_INVALID);
        gjs_debug(GJS_DEBUG_DBUS,
                  "Properties request %s on %s",
                  method_name,
                  iface ? iface : "MISSING INTERFACE");
        result = handle_properties(context, connection,
                                   obj, message, method_name);
        goto out;
    }

    async_method_name = g_strdup_printf("%sAsync", method_name);

    /* try first if an async version exists */
    if (find_method(context,
                    obj,
                    async_method_name,
                    &method_value)) {

        gjs_debug(GJS_DEBUG_DBUS,
                  "Invoking async method %s on JS obj at dbus path %s",
                  async_method_name, path);

        reply = invoke_js_async_from_dbus(context,
                                          priv->which_bus,
                                          message,
                                          obj,
                                          JSVAL_TO_OBJECT(method_value));

        result = DBUS_HANDLER_RESULT_HANDLED;

        /* otherwise try the sync version */
    } else if (find_method(context,
                           obj,
                           method_name,
                           &method_value)) {

        gjs_debug(GJS_DEBUG_DBUS,
                  "Invoking method %s on JS obj at dbus path %s",
                  method_name, path);

        reply = invoke_js_from_dbus(context,
                                    message,
                                    obj,
                                    JSVAL_TO_OBJECT(method_value));

        result = DBUS_HANDLER_RESULT_HANDLED;

        /* otherwise do nothing, method not found */
    } else {
        gjs_debug(GJS_DEBUG_DBUS,
                  "There is a JS object at %s but it has no method %s",
                  path, method_name);
    }

    if (reply != NULL) {
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);
    }

 out:
    if (async_method_name)
        g_free(async_method_name);
    JS_RemoveValueRoot(context, &method_value);
    JS_EndRequest(context);
    return result;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or exports prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
exports_new_resolve(JSContext *context,
                    JSObject  *obj,
                    jsid       id,
                    uintN      flags,
                    JSObject **objp)
{
    Exports *priv;
    char *name;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_DBUS, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);
    g_free(name);

    if (priv == NULL)
        return JS_TRUE; /* we are the prototype, or have the wrong class */

    return JS_TRUE;
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(js_exports)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(js_exports)
    Exports *priv;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(js_exports);

    priv = g_slice_new0(Exports);

    GJS_INC_COUNTER(dbus_exports);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_DBUS,
                        "exports constructor, obj %p priv %p", object, priv);

    priv->runtime = JS_GetRuntime(context);
    priv->object = object;

    GJS_NATIVE_CONSTRUCTOR_FINISH(js_exports);

    return JS_TRUE;
}

static JSBool
add_connect_funcs(JSContext  *context,
                  JSObject   *obj,
                  DBusBusType which_bus)
{
    Exports *priv;
    GjsDBusConnectFuncs const *connect_funcs;

    priv = priv_from_js(context, obj);
    if (priv == NULL)
        return JS_FALSE;

    if (which_bus == DBUS_BUS_SESSION) {
        connect_funcs = &session_connect_funcs;
    } else if (which_bus == DBUS_BUS_SYSTEM) {
        connect_funcs = &system_connect_funcs;
    } else
        g_assert_not_reached();

    priv->which_bus = which_bus;
    gjs_dbus_add_connect_funcs_sync_notify(connect_funcs, priv);

    return JS_TRUE;
}

static void
exports_finalize(JSContext *context,
                 JSObject  *obj)
{
    Exports *priv;
    GjsDBusConnectFuncs const *connect_funcs;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_DBUS,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    if (priv->which_bus == DBUS_BUS_SESSION) {
        connect_funcs = &session_connect_funcs;
    } else if (priv->which_bus == DBUS_BUS_SYSTEM) {
        connect_funcs = &system_connect_funcs;
    } else
        g_assert_not_reached();

    gjs_dbus_remove_connect_funcs(connect_funcs, priv);

    if (priv->connection_weak_ref != NULL) {
        on_bus_closed(priv->connection_weak_ref, priv);
    }

    GJS_DEC_COUNTER(dbus_exports);
    g_slice_free(Exports, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_js_exports_class = {
    "DBusExports", /* means "new DBusExports()" works */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) exports_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    exports_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};

static JSPropertySpec gjs_js_exports_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_js_exports_proto_funcs[] = {
    { NULL }
};

static JSObject*
exports_new(JSContext  *context,
            DBusBusType which_bus)
{
    JSObject *exports;
    JSObject *global;

    /* put constructor for DBusExports() in the global namespace */
    global = gjs_get_import_global(context);

    if (!gjs_object_has_property(context, global, gjs_js_exports_class.name)) {
        JSObject *prototype;

        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 NULL,
                                 &gjs_js_exports_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 gjs_js_exports_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_js_exports_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_js_exports_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            return JS_FALSE;

        g_assert(gjs_object_has_property(context, global, gjs_js_exports_class.name));

        gjs_debug(GJS_DEBUG_DBUS, "Initialized class %s prototype %p",
                  gjs_js_exports_class.name, prototype);
    }

    exports = JS_ConstructObject(context, &gjs_js_exports_class, NULL, global);
    /* may be NULL */

    return exports;
}

JSBool
gjs_js_define_dbus_exports(JSContext      *context,
                           JSObject       *in_object,
                           DBusBusType     which_bus)
{
    JSObject *exports;
    JSBool success;

    success = JS_FALSE;
    JS_BeginRequest(context);

    exports = exports_new(context, which_bus);
    if (exports == NULL) {
        gjs_move_exception(context, context);
        goto fail;
    }

    if (!add_connect_funcs(context, exports, which_bus))
        goto fail;

    if (!JS_DefineProperty(context, in_object,
                           "exports",
                           OBJECT_TO_JSVAL(exports),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto fail;

    success = JS_TRUE;
 fail:
    JS_EndRequest(context);
    return success;
}
