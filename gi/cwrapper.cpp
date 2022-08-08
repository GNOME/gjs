/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>

#include <glib-object.h>

#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/cwrapper.h"
#include "gi/gtype.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"

bool gjs_wrapper_define_gtype_prop(JSContext* cx, JS::HandleObject constructor,
                                   GType gtype) {
    JS::RootedObject gtype_obj(cx, gjs_gtype_create_gtype_wrapper(cx, gtype));
    if (!gtype_obj)
        return false;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    return JS_DefinePropertyById(cx, constructor, atoms.gtype(), gtype_obj,
                                 JSPROP_PERMANENT);
}
