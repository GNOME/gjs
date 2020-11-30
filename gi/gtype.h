/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#ifndef GI_GTYPE_H_
#define GI_GTYPE_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
JSObject * gjs_gtype_create_gtype_wrapper (JSContext *context,
                                           GType      gtype);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_gtype_get_actual_gtype(JSContext* context, JS::HandleObject object,
                                GType* gtype_out);

#endif  // GI_GTYPE_H_
