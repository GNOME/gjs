/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <cairo.h>
#include "cairo-private.h"

#if CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>

GJS_DEFINE_PROTO("CairoSVGSurface", cairo_svg_surface, JSCLASS_BACKGROUND_FINALIZE)

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_svg_surface)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_svg_surface)
    char *filename;
    double width, height;
    cairo_surface_t *surface;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_svg_surface);

    if (!gjs_parse_args(context, "SVGSurface", "sff", argc, argv,
                        "filename", &filename,
                        "width", &width,
                        "height", &height))
        return JS_FALSE;

    surface = cairo_svg_surface_create(filename, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface")) {
        g_free(filename);
        return JS_FALSE;
    }

    gjs_cairo_surface_construct(context, object, surface);
    cairo_surface_destroy(surface);
    g_free(filename);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_svg_surface);

    return JS_TRUE;
}

static void
gjs_cairo_svg_surface_finalize(JSFreeOp *fop,
                               JSObject *obj)
{
    gjs_cairo_surface_finalize_surface(fop, obj);
}

JSPropertySpec gjs_cairo_svg_surface_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_cairo_svg_surface_proto_funcs[] = {
    { NULL }
};

JSObject *
gjs_cairo_svg_surface_from_surface(JSContext       *context,
                                   cairo_surface_t *surface)
{
    JSObject *object;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(surface != NULL, NULL);
    g_return_val_if_fail(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_SVG, NULL);

    object = JS_NewObject(context, &gjs_cairo_svg_surface_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create svg surface");
        return NULL;
    }

    gjs_cairo_surface_construct(context, object, surface);

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
    return NULL;
}
#endif /* CAIRO_HAS_SVG_SURFACE */
