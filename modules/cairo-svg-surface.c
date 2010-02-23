/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 litl, LLC. All Rights Reserved.
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

#include <gjs/gjs.h>
#include <cairo.h>
#include "cairo-private.h"

#if CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>

GJS_DEFINE_PROTO("CairoSVGSurface", gjs_cairo_svg_surface)

static JSBool
gjs_cairo_svg_surface_constructor(JSContext *context,
                                  JSObject  *obj,
                                  uintN      argc,
                                  jsval     *argv,
                                  jsval     *retval)
{
    char *filename;
    double width, height;
    cairo_surface_t *surface;
    cairo_status_t status;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    if (!gjs_parse_args(context, "SVGSurface", "sff", argc, argv,
                        "filename", &filename,
                        "width", &width,
                        "height", &height))
        return JS_FALSE;

    surface = cairo_svg_surface_create(filename, width, height);
    status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(context, "Failed to create cairo surface: %s",
                  cairo_status_to_string(status));
        return JS_FALSE;
    }
    gjs_cairo_surface_construct(context, obj, surface);

    return JS_TRUE;
}

static void
gjs_cairo_svg_surface_finalize(JSContext *context,
                               JSObject  *obj)
{
    gjs_cairo_surface_finalize_surface(context, obj);
}

static JSPropertySpec gjs_cairo_svg_surface_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_cairo_svg_surface_proto_funcs[] = {
    { NULL }
};

#endif /* CAIRO_HAS_SVG_SURFACE */
