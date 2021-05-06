/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFA...
#include <cairo.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/jsapi-util.h"
#include "modules/cairo-private.h"

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

    if (!CairoRegion::create_prototype(context, module))
        return false;
    gjs_cairo_region_init();

    if (!CairoContext::create_prototype(context, module))
        return false;
    gjs_cairo_context_init();

    if (!CairoSurface::create_prototype(context, module))
        return false;
    gjs_cairo_surface_init();

    return CairoImageSurface::create_prototype(context, module) &&
           CairoPath::create_prototype(context, module) &&
#if CAIRO_HAS_PS_SURFACE
           CairoPSSurface::create_prototype(context, module) &&
#endif
#if CAIRO_HAS_PDF_SURFACE
           CairoPDFSurface::create_prototype(context, module) &&
#endif
#if CAIRO_HAS_SVG_SURFACE
           CairoSVGSurface::create_prototype(context, module) &&
#endif
           CairoPattern::create_prototype(context, module) &&
           CairoGradient::create_prototype(context, module) &&
           CairoLinearGradient::create_prototype(context, module) &&
           CairoRadialGradient::create_prototype(context, module) &&
           CairoSurfacePattern::create_prototype(context, module) &&
           CairoSolidPattern::create_prototype(context, module);
}
