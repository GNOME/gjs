/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Endless Mobile Inc.

#include <config.h>

#include "test/gjs-test-no-introspection-object.h"

struct _GjsTestNoIntrospectionObject {
    GObject parent_instance;

    int a_int;
};

G_DEFINE_TYPE(GjsTestNoIntrospectionObject, gjstest_no_introspection_object,
              G_TYPE_OBJECT)

static GjsTestNoIntrospectionObject* last_object = nullptr;

static void gjstest_no_introspection_object_init(
    GjsTestNoIntrospectionObject* self) {
    self->a_int = 0;
    last_object = self;
}

static void gjstest_no_introspection_object_set_property(GObject* object,
                                                         unsigned prop_id,
                                                         const GValue* value,
                                                         GParamSpec* pspec) {
    GjsTestNoIntrospectionObject* self =
        GJSTEST_NO_INTROSPECTION_OBJECT(object);

    switch (prop_id) {
        case 1:
            self->a_int = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gjstest_no_introspection_object_get_property(GObject* object,
                                                         unsigned prop_id,
                                                         GValue* value,
                                                         GParamSpec* pspec) {
    GjsTestNoIntrospectionObject* self =
        GJSTEST_NO_INTROSPECTION_OBJECT(object);

    switch (prop_id) {
        case 1:
            g_value_set_int(value, self->a_int);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gjstest_no_introspection_object_class_init(
    GjsTestNoIntrospectionObjectClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = gjstest_no_introspection_object_set_property;
    object_class->get_property = gjstest_no_introspection_object_get_property;

    g_object_class_install_property(
        object_class, 1,
        g_param_spec_int("a-int", "An integer", "An integer property", 0,
                         100000000, 0, G_PARAM_READWRITE));
}

GjsTestNoIntrospectionObject* gjstest_no_introspection_object_peek() {
    return last_object;
}
