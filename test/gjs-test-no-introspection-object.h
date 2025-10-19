/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Endless Mobile Inc.

#pragma once

#include <config.h>

#include <glib-object.h>

#define GJSTEST_TYPE_NO_INTROSPECTION_OBJECT \
    gjstest_no_introspection_object_get_type()
G_DECLARE_FINAL_TYPE(GjsTestNoIntrospectionObject,
                     gjstest_no_introspection_object, GJSTEST,
                     NO_INTROSPECTION_OBJECT, GObject)

GjsTestNoIntrospectionObject* gjstest_no_introspection_object_peek();
