/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFA...
#include <cairo.h>

#ifdef CAIRO_HAS_XLIB_SURFACE
#    include <cairo-xlib.h>
#    undef None
// X11 defines a global None macro. Rude! This conflicts with None used as an
// enum member in SpiderMonkey headers, e.g. JS::ExceptionStatus::None.
#endif

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/jsapi-util.h"
#include "modules/cairo-private.h"  // IWYU pragma: associated

#ifdef CAIRO_HAS_XLIB_SURFACE
class XLibConstructor {
 public:
    XLibConstructor() {
        XInitThreads();
    }
};

static XLibConstructor constructor;
#endif

bool gjs_cairo_check_status(JSContext* cx, cairo_status_t status,
                            const char* name) {
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(cx, "cairo error on %s: \"%s\" (%d)", name,
                  cairo_status_to_string(status), status);
        return false;
    }

    return true;
}

bool gjs_js_define_cairo_stuff(JSContext* cx, JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));

    if (!CairoRegion::create_prototype(cx, module))
        return false;
    gjs_cairo_region_init();

    if (!CairoContext::create_prototype(cx, module))
        return false;
    gjs_cairo_context_init();

    if (!CairoSurface::create_prototype(cx, module))
        return false;
    gjs_cairo_surface_init();

    if (!CairoPattern::create_prototype(cx, module))
        return false;
    gjs_cairo_pattern_init();

    if (!CairoPath::create_prototype(cx, module))
        return false;
    gjs_cairo_path_init();

    return CairoImageSurface::create_prototype(cx, module) &&
#if CAIRO_HAS_PS_SURFACE
           CairoPSSurface::create_prototype(cx, module) &&
#endif
#if CAIRO_HAS_PDF_SURFACE
           CairoPDFSurface::create_prototype(cx, module) &&
#endif
#if CAIRO_HAS_SVG_SURFACE
           CairoSVGSurface::create_prototype(cx, module) &&
#endif
           CairoGradient::create_prototype(cx, module) &&
           CairoLinearGradient::create_prototype(cx, module) &&
           CairoRadialGradient::create_prototype(cx, module) &&
           CairoSurfacePattern::create_prototype(cx, module) &&
           CairoSolidPattern::create_prototype(cx, module);
}
