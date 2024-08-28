/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PS_SURFACE
#include <cairo.h>

#if CAIRO_HAS_PS_SURFACE
#    include <cairo-ps.h>
#endif

#include <js/TypeDecls.h>

#if CAIRO_HAS_PS_SURFACE
#    include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#    include <js/PropertySpec.h>
#    include <js/RootingAPI.h>
#    include <jsapi.h>    // for JS_NewObjectWithGivenProto
#    include <jspubtd.h>  // for JSProtoKey

#    include "gjs/auto.h"
#    include "gjs/jsapi-util-args.h"
#    include "modules/cairo-private.h"

namespace JS {
class CallArgs;
}

JSObject* CairoPSSurface::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoSurface::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

cairo_surface_t* CairoPSSurface::constructor_impl(JSContext* context,
                                                  const JS::CallArgs& argv) {
    Gjs::AutoChar filename;
    double width, height;
    cairo_surface_t *surface;
    if (!gjs_parse_call_args(context, "PSSurface", argv, "Fff",
                             "filename", &filename,
                             "width", &width,
                             "height", &height))
        return nullptr;

    surface = cairo_ps_surface_create(filename, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return nullptr;

    return surface;
}

// clang-format off
const JSPropertySpec CairoPSSurface::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "PSSurface", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

const JSFunctionSpec CairoPSSurface::proto_funcs[] = {
    // restrictToLevel
    // getLevels
    // levelToString
    // setEPS
    // getEPS
    // setSize
    // dscBeginSetup
    // dscBeginPageSetup
    // dscComment
    JS_FS_END};

#else
JSObject* CairoPSSurface::from_c_ptr(JSContext* context,
                                     cairo_surface_t* surface) {
    gjs_throw(context,
        "could not create PS surface, recompile cairo and gjs with "
        "PS support.");
    return nullptr;
}
#endif /* CAIRO_HAS_PS_SURFACE */
