/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFA...
#include <cairo.h>

#include <js/GCPolicyAPI.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/jsapi-util.h"
#include "modules/cairo-private.h"

// Avoid static_assert in MSVC builds
namespace JS {
template <>
struct GCPolicy<void*> : public IgnoreGCPolicy<void*> {};
}

#ifdef CAIRO_HAS_XLIB_SURFACE
#    include <cairo-xlib.h>

class XLibConstructor {
 public:
    XLibConstructor() {
        XInitThreads();
    }
};

static XLibConstructor constructor;
#endif

bool
gjs_cairo_check_status(JSContext      *context,
                       cairo_status_t  status,
                       const char     *name)
{
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(context, "cairo error on %s: \"%s\" (%d)",
                  name,
                  cairo_status_to_string(status),
                  status);
        return false;
    }

    return true;
}

bool
gjs_js_define_cairo_stuff(JSContext              *context,
                          JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));
    JS::RootedObject proto(context);  /* not used */

    if (!gjs_cairo_region_define_proto(context, module, &proto))
        return false;
    gjs_cairo_region_init();

    if (!gjs_cairo_context_define_proto(context, module, &proto))
        return false;
    gjs_cairo_context_init();

    if (!gjs_cairo_surface_define_proto(context, module, &proto))
        return false;
    gjs_cairo_surface_init();

    return
        gjs_cairo_image_surface_define_proto(context, module, &proto) &&
        gjs_cairo_path_define_proto(context, module, &proto) &&
#if CAIRO_HAS_PS_SURFACE
        gjs_cairo_ps_surface_define_proto(context, module, &proto) &&
#endif
#if CAIRO_HAS_PDF_SURFACE
        gjs_cairo_pdf_surface_define_proto(context, module, &proto) &&
#endif
#if CAIRO_HAS_SVG_SURFACE
        gjs_cairo_svg_surface_define_proto(context, module, &proto) &&
#endif
        gjs_cairo_pattern_define_proto(context, module, &proto) &&
        gjs_cairo_gradient_define_proto(context, module, &proto) &&
        gjs_cairo_linear_gradient_define_proto(context, module, &proto) &&
        gjs_cairo_radial_gradient_define_proto(context, module, &proto) &&
        gjs_cairo_surface_pattern_define_proto(context, module, &proto) &&
        gjs_cairo_solid_pattern_define_proto(context, module, &proto);
}
