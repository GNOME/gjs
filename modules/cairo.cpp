/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 litl, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFA...
#include <cairo.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/jsapi-util.h"
#include "modules/cairo-private.h"

// Avoid static_assert in MSVC builds
namespace JS {
template <typename T> struct GCPolicy;

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
