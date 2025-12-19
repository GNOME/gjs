/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_SVG_SURFACE
#include <cairo.h>

#if CAIRO_HAS_SVG_SURFACE
#    include <cairo-svg.h>
#endif

#include <js/TypeDecls.h>

#if CAIRO_HAS_SVG_SURFACE
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

JSObject* CairoSVGSurface::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoSurface::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

cairo_surface_t* CairoSVGSurface::constructor_impl(JSContext* cx,
                                                   const JS::CallArgs& args) {
    Gjs::AutoChar filename;
    double width, height;
    if (!gjs_parse_call_args(cx, "SVGSurface", args, "Fff", "filename",
                             &filename, "width", &width, "height", &height))
        return nullptr;

    cairo_surface_t* surface =
        cairo_svg_surface_create(filename, width, height);

    if (!gjs_cairo_check_status(cx, cairo_surface_status(surface), "surface"))
        return nullptr;

    return surface;
}

// clang-format off
const JSPropertySpec CairoSVGSurface::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "SVGSurface", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

#else
JSObject* CairoSVGSurface::from_c_ptr(JSContext* cx, cairo_surface_t* surface) {
    gjs_throw(cx,
              "could not create SVG surface, recompile cairo and gjs with SVG "
              "support.");
    return nullptr;
}
#endif  // CAIRO_HAS_SVG_SURFACE
