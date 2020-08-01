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

#include <cairo-features.h>  // for CAIRO_HAS_SVG_SURFACE
#include <cairo.h>

#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"

#if CAIRO_HAS_SVG_SURFACE
#    include <cairo-svg.h>
#    include <glib.h>

#    include <js/Class.h>
#    include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#    include <js/PropertySpec.h>
#    include <js/RootingAPI.h>
#    include <jsapi.h>  // for JS_NewObjectWithGivenProto

#    include "gjs/jsapi-class.h"
#    include "gjs/jsapi-util-args.h"
#    include "gjs/macros.h"
#    include "modules/cairo-private.h"

[[nodiscard]] static JSObject* gjs_cairo_svg_surface_get_proto(JSContext*);

GJS_DEFINE_PROTO_WITH_PARENT("SVGSurface", cairo_svg_surface,
                             cairo_surface, JSCLASS_BACKGROUND_FINALIZE)

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_svg_surface)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_svg_surface)
    GjsAutoChar filename;
    double width, height;
    cairo_surface_t *surface;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_svg_surface);

    if (!gjs_parse_call_args(context, "SVGSurface", argv, "Fff",
                             "filename", &filename,
                             "width", &width,
                             "height", &height))
        return false;

    surface = cairo_svg_surface_create(filename, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return false;

    gjs_cairo_surface_construct(object, surface);
    cairo_surface_destroy(surface);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_svg_surface);

    return true;
}

static void
gjs_cairo_svg_surface_finalize(JSFreeOp *fop,
                               JSObject *obj)
{
    gjs_cairo_surface_finalize_surface(fop, obj);
}

// clang-format off
JSPropertySpec gjs_cairo_svg_surface_proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "SVGSurface", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

JSFunctionSpec gjs_cairo_svg_surface_proto_funcs[] = {
    JS_FS_END
};

JSFunctionSpec gjs_cairo_svg_surface_static_funcs[] = { JS_FS_END };

JSObject *
gjs_cairo_svg_surface_from_surface(JSContext       *context,
                                   cairo_surface_t *surface)
{
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(surface, nullptr);
    g_return_val_if_fail(
        cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_SVG, nullptr);

    JS::RootedObject proto(context, gjs_cairo_svg_surface_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_svg_surface_class,
                                   proto));
    if (!object) {
        gjs_throw(context, "failed to create svg surface");
        return nullptr;
    }

    gjs_cairo_surface_construct(object, surface);

    return object;
}
#else
JSObject *
gjs_cairo_svg_surface_from_surface(JSContext       *context,
                                   cairo_surface_t *surface)
{
    gjs_throw(context,
        "could not create SVG surface, recompile cairo and gjs with "
        "SVG support.");
    return nullptr;
}
#endif /* CAIRO_HAS_SVG_SURFACE */
