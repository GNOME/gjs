/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2011 Giovanni Campagna
 */

#include <config.h>

#include <string.h>  // for strcmp

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "libgjs-private/gjs-gdbus-wrapper.h"

enum {
    PROP_0,
    PROP_G_INTERFACE_INFO,
    PROP_LAST
};

enum {
    SIGNAL_HANDLE_METHOD,
    SIGNAL_HANDLE_PROPERTY_GET,
    SIGNAL_HANDLE_PROPERTY_SET,
    SIGNAL_LAST,
};

static guint signals[SIGNAL_LAST];

struct _GjsDBusImplementationPrivate {
    GDBusInterfaceVTable  vtable;
    GDBusInterfaceInfo   *ifaceinfo;

    // from gchar* to GVariant*
    GHashTable           *outstanding_properties;
    guint                 idle_id;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsDBusImplementation, gjs_dbus_implementation,
                           G_TYPE_DBUS_INTERFACE_SKELETON);

static inline GVariant* _g_variant_ref_sink0(void* value) {
    if (value)
        g_variant_ref_sink(value);

    return value;
}

static inline void _g_variant_unref0(void* value) {
    if (value)
        g_variant_unref(value);
}

static gboolean gjs_dbus_implementation_check_interface(
    GjsDBusImplementation* self, GDBusConnection* connection,
    const char* object_path, const char* interface_name, GError** error) {
    const char* exported_object_path;

    if (!g_dbus_interface_skeleton_has_connection(
            G_DBUS_INTERFACE_SKELETON(self), connection)) {
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED,
                            "Wrong connection");
        return FALSE;
    }
    exported_object_path = g_dbus_interface_skeleton_get_object_path(
        G_DBUS_INTERFACE_SKELETON(self));
    if (!exported_object_path || strcmp(object_path, exported_object_path)) {
        g_set_error(
            error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT,
            "Wrong object path %s for %s", object_path,
            exported_object_path ? exported_object_path : "unexported object");
        return FALSE;
    }
    if (strcmp(interface_name, self->priv->ifaceinfo->name) != 0) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                    "Unknown interface %s on %s", interface_name,
                    self->priv->ifaceinfo->name);
        return FALSE;
    }
    return TRUE;
}

static gboolean gjs_dbus_implementation_check_property(
    GjsDBusImplementation* self, const char* interface_name,
    const char* property_name, GError** error) {
    if (!g_dbus_interface_info_lookup_property(self->priv->ifaceinfo,
                                               property_name)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                    "Unknown property %s on %s", property_name, interface_name);
        return FALSE;
    }
    return TRUE;
}

static void gjs_dbus_implementation_method_call(
    GDBusConnection* connection, const char* sender G_GNUC_UNUSED,
    const char* object_path, const char* interface_name,
    const char* method_name, GVariant* parameters,
    GDBusMethodInvocation* invocation, void* user_data) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (user_data);
    GError* error = NULL;

    if (!gjs_dbus_implementation_check_interface(self, connection, object_path,
                                                 interface_name, &error)) {
        g_dbus_method_invocation_take_error(invocation, error);
        return;
    }
    if (!g_dbus_interface_info_lookup_method(self->priv->ifaceinfo,
                                             method_name)) {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s on %s", method_name, interface_name);
        return;
    }

    g_signal_emit(self, signals[SIGNAL_HANDLE_METHOD], 0, method_name, parameters, invocation);
    g_object_unref (invocation);
}

static GVariant* gjs_dbus_implementation_property_get(
    GDBusConnection* connection, const char* sender G_GNUC_UNUSED,
    const char* object_path, const char* interface_name,
    const char* property_name, GError** error, void* user_data) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (user_data);
    GVariant *value;

    if (!gjs_dbus_implementation_check_interface(self, connection, object_path,
                                                 interface_name, error) ||
        !gjs_dbus_implementation_check_property(self, interface_name,
                                                property_name, error))
        return NULL;

    g_signal_emit(self, signals[SIGNAL_HANDLE_PROPERTY_GET], 0, property_name, &value);

    /* Marshaling GErrors is not supported, so this is the best we can do
       (GIO will assert if value is NULL and error is not set) */
    if (!value)
        g_set_error(error, g_quark_from_static_string("gjs-error-domain"), 0, "Property retrieval failed");

    return value;
}

static gboolean gjs_dbus_implementation_property_set(
    GDBusConnection* connection, const char* sender G_GNUC_UNUSED,
    const char* object_path, const char* interface_name,
    const char* property_name, GVariant* value, GError** error,
    void* user_data) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (user_data);

    if (!gjs_dbus_implementation_check_interface(self, connection, object_path,
                                                 interface_name, error) ||
        !gjs_dbus_implementation_check_property(self, interface_name,
                                                property_name, error))
        return FALSE;

    g_signal_emit(self, signals[SIGNAL_HANDLE_PROPERTY_SET], 0, property_name, value);

    return TRUE;
}

static void
gjs_dbus_implementation_init(GjsDBusImplementation *self) {
    GjsDBusImplementationPrivate* priv =
        gjs_dbus_implementation_get_instance_private(self);

    self->priv = priv;

    priv->vtable.method_call = gjs_dbus_implementation_method_call;
    priv->vtable.get_property = gjs_dbus_implementation_property_get;
    priv->vtable.set_property = gjs_dbus_implementation_property_set;

    priv->outstanding_properties = g_hash_table_new_full(g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         _g_variant_unref0);
}

static void gjs_dbus_implementation_dispose(GObject* object) {
    GjsDBusImplementation* self = GJS_DBUS_IMPLEMENTATION(object);

    g_clear_handle_id(&self->priv->idle_id, g_source_remove);

    G_OBJECT_CLASS(gjs_dbus_implementation_parent_class)->dispose(object);
}

static void
gjs_dbus_implementation_finalize(GObject *object) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (object);

    g_dbus_interface_info_unref (self->priv->ifaceinfo);
    g_hash_table_destroy(self->priv->outstanding_properties);

    G_OBJECT_CLASS(gjs_dbus_implementation_parent_class)->finalize(object);
}

static void
gjs_dbus_implementation_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (object);

    switch (property_id) {
    case PROP_G_INTERFACE_INFO:
        self->priv->ifaceinfo = (GDBusInterfaceInfo*) g_value_dup_boxed (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static GDBusInterfaceInfo *
gjs_dbus_implementation_get_info (GDBusInterfaceSkeleton *skeleton) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (skeleton);

    return self->priv->ifaceinfo;
}

static GDBusInterfaceVTable *
gjs_dbus_implementation_get_vtable (GDBusInterfaceSkeleton *skeleton) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (skeleton);

    return &(self->priv->vtable);
}

static GVariant *
gjs_dbus_implementation_get_properties (GDBusInterfaceSkeleton *skeleton) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION (skeleton);

    GDBusInterfaceInfo *info = self->priv->ifaceinfo;
    GDBusPropertyInfo **props;
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    for (props = info->properties; *props; ++props) {
        GDBusPropertyInfo *prop = *props;
        GVariant *value;

        /* If we have a cached value, we use that instead of querying again */
        if ((value = (GVariant*) g_hash_table_lookup(self->priv->outstanding_properties, prop->name))) {
            g_variant_builder_add(&builder, "{sv}", prop->name, value);
            continue;
        }

        g_signal_emit(self, signals[SIGNAL_HANDLE_PROPERTY_GET], 0, prop->name, &value);
        g_variant_builder_add(&builder, "{sv}", prop->name, value);
    }

    return g_variant_builder_end(&builder);
}

static void
gjs_dbus_implementation_flush (GDBusInterfaceSkeleton *skeleton) {
    GjsDBusImplementation *self = GJS_DBUS_IMPLEMENTATION(skeleton);

    GVariantBuilder changed_props;
    GVariantBuilder invalidated_props;
    GHashTableIter iter;
    GVariant *val;
    gchar *prop_name;

    g_variant_builder_init(&changed_props, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&invalidated_props, G_VARIANT_TYPE_STRING_ARRAY);

    g_hash_table_iter_init(&iter, self->priv->outstanding_properties);
    while (g_hash_table_iter_next(&iter, (void**) &prop_name, (void**) &val)) {
        if (val)
            g_variant_builder_add(&changed_props, "{sv}", prop_name, val);
        else
            g_variant_builder_add(&invalidated_props, "s", prop_name);
    }

    GList *connections = g_dbus_interface_skeleton_get_connections(skeleton);
    const char *object_path = g_dbus_interface_skeleton_get_object_path(skeleton);
    GVariant *properties = g_variant_new("(s@a{sv}@as)",
                                         self->priv->ifaceinfo->name,
                                         g_variant_builder_end(&changed_props),
                                         g_variant_builder_end(&invalidated_props));
    g_variant_ref_sink(properties);

    for (const GList *iter = connections; iter; iter = iter->next) {
        g_dbus_connection_emit_signal(G_DBUS_CONNECTION(iter->data),
                                      NULL, /* bus name */
                                      object_path,
                                      "org.freedesktop.DBus.Properties",
                                      "PropertiesChanged",
                                      properties,
                                      NULL /* error */);

        g_object_unref(iter->data);
    }
    g_variant_unref(properties);
    g_list_free(connections);

    g_hash_table_remove_all(self->priv->outstanding_properties);
    g_clear_handle_id(&self->priv->idle_id, g_source_remove);
}

void
gjs_dbus_implementation_class_init(GjsDBusImplementationClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS(klass);

    gobject_class->dispose = gjs_dbus_implementation_dispose;
    gobject_class->finalize = gjs_dbus_implementation_finalize;
    gobject_class->set_property = gjs_dbus_implementation_set_property;

    skeleton_class->get_info = gjs_dbus_implementation_get_info;
    skeleton_class->get_vtable = gjs_dbus_implementation_get_vtable;
    skeleton_class->get_properties = gjs_dbus_implementation_get_properties;
    skeleton_class->flush = gjs_dbus_implementation_flush;

    g_object_class_install_property(gobject_class, PROP_G_INTERFACE_INFO,
                                    g_param_spec_boxed("g-interface-info",
                                                       "Interface Info",
                                                       "A DBusInterfaceInfo representing the exported object",
                                                       G_TYPE_DBUS_INTERFACE_INFO,
                                                       (GParamFlags) (G_PARAM_STATIC_STRINGS | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)));

    signals[SIGNAL_HANDLE_METHOD] = g_signal_new("handle-method-call",
                                                 G_TYPE_FROM_CLASS(klass),
                                                 (GSignalFlags) 0, /* flags */
                                                 0, /* closure */
                                                 NULL, /* accumulator */
                                                 NULL, /* accumulator data */
                                                 NULL, /* C marshal */
                                                 G_TYPE_NONE,
                                                 3,
                                                 G_TYPE_STRING, /* method name */
                                                 G_TYPE_VARIANT, /* parameters */
                                                 G_TYPE_DBUS_METHOD_INVOCATION);

    signals[SIGNAL_HANDLE_PROPERTY_GET] = g_signal_new("handle-property-get",
                                                       G_TYPE_FROM_CLASS(klass),
                                                       (GSignalFlags) 0, /* flags */
                                                       0, /* closure */
                                                       g_signal_accumulator_first_wins,
                                                       NULL, /* accumulator data */
                                                       NULL, /* C marshal */
                                                       G_TYPE_VARIANT,
                                                       1,
                                                       G_TYPE_STRING /* property name */);


    signals[SIGNAL_HANDLE_PROPERTY_SET] = g_signal_new("handle-property-set",
                                                       G_TYPE_FROM_CLASS(klass),
                                                       (GSignalFlags) 0, /* flags */
                                                       0, /* closure */
                                                       NULL, /* accumulator */
                                                       NULL, /* accumulator data */
                                                       NULL, /* C marshal */
                                                       G_TYPE_NONE,
                                                       2,
                                                       G_TYPE_STRING, /* property name */
                                                       G_TYPE_VARIANT /* parameters */);
}

static gboolean
idle_cb (gpointer data) {
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (data);

    g_dbus_interface_skeleton_flush(skeleton);
    return G_SOURCE_REMOVE;
}

/**
 * gjs_dbus_implementation_emit_property_changed:
 * @self: a #GjsDBusImplementation
 * @property: the name of the property that changed
 * @newvalue: (allow-none): the new value, or %NULL to just invalidate it
 *
 * Queue a PropertyChanged signal for emission, or update the one queued
 * adding @property
 */
void
gjs_dbus_implementation_emit_property_changed (GjsDBusImplementation *self,
                                               gchar                 *property,
                                               GVariant              *newvalue)
{
    g_hash_table_replace(self->priv->outstanding_properties,
                         g_strdup(property),
                         _g_variant_ref_sink0(newvalue));

    if (!self->priv->idle_id)
        self->priv->idle_id = g_idle_add(idle_cb, self);
}

/**
 * gjs_dbus_implementation_emit_signal:
 * @self: a #GjsDBusImplementation
 * @signal_name: the name of the signal
 * @parameters: (allow-none): signal parameters, or %NULL for none
 *
 * Emits a signal named @signal_name from the object and interface represented
 * by @self. This signal has no destination.
 */
void
gjs_dbus_implementation_emit_signal (GjsDBusImplementation *self,
                                     gchar                 *signal_name,
                                     GVariant              *parameters)
{
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON(self);
    GList *connections = g_dbus_interface_skeleton_get_connections(skeleton);
    const char *object_path = g_dbus_interface_skeleton_get_object_path(skeleton);

    _g_variant_ref_sink0(parameters);

    for (const GList *iter = connections; iter; iter = iter->next) {
        g_dbus_connection_emit_signal(G_DBUS_CONNECTION(iter->data),
                                      NULL,
                                      object_path,
                                      self->priv->ifaceinfo->name,
                                      signal_name,
                                      parameters,
                                      NULL);

        g_object_unref(iter->data);
    }
    _g_variant_unref0(parameters);

    g_list_free(connections);
}

/**
 * gjs_dbus_implementation_unexport:
 * @self: a #GjsDBusImplementation
 *
 * Stops exporting @self on all connections it is exported on.
 *
 * To unexport @self from only a single connection, use
 * gjs_dbus_implementation_skeleton_unexport_from_connection()
 */
void
gjs_dbus_implementation_unexport(GjsDBusImplementation *self) {
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON(self);

    g_hash_table_remove_all(self->priv->outstanding_properties);
    g_clear_handle_id(&self->priv->idle_id, g_source_remove);

    g_dbus_interface_skeleton_unexport(skeleton);
}

/**
 * gjs_dbus_implementation_unexport_from_connection:
 * @self: a #GjsDBusImplementation
 * @connection: a #GDBusConnection
 *
 * Stops exporting @self on @connection.
 *
 * To stop exporting on all connections the interface is exported on,
 * use gjs_dbus_implementation_unexport().
 */
void
gjs_dbus_implementation_unexport_from_connection(GjsDBusImplementation *self,
                                                 GDBusConnection       *connection) {
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON(self);
    GList *connections = g_dbus_interface_skeleton_get_connections(skeleton);

    if (g_list_length(connections) <= 1) {
        g_hash_table_remove_all(self->priv->outstanding_properties);
        g_clear_handle_id(&self->priv->idle_id, g_source_remove);
    }

    g_list_free_full(connections, g_object_unref);

    g_dbus_interface_skeleton_unexport_from_connection(skeleton, connection);
}
