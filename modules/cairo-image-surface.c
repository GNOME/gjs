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

#ifdef CAIRO_HAS_IMAGE_SURFACE

GJS_DEFINE_PROTO("CairoImageSurface", gjs_cairo_image_surface)

static JSBool
gjs_cairo_image_surface_constructor(JSContext *context,
                                    JSObject  *obj,
                                    uintN      argc,
                                    jsval     *argv,
                                    jsval     *retval)
{
    int format, width, height;
    cairo_surface_t *surface;
    cairo_status_t status;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    // create_for_data optional parameter
    if (!gjs_parse_args(context, "ImageSurface", "iii", argc, argv,
                        "format", &format,
                        "width", &width,
                        "height", &height))
        return JS_FALSE;

    surface = cairo_image_surface_create(format, width, height);
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
gjs_cairo_image_surface_finalize(JSContext *context,
                                 JSObject  *obj)
{
    gjs_cairo_surface_finalize_surface(context, obj);
}

static JSPropertySpec gjs_cairo_image_surface_proto_props[] = {
    { NULL }
};

static JSBool
createFromPNG_func(JSContext *context,
                   JSObject  *obj,
                   uintN      argc,
                   jsval     *argv,
                   jsval     *retval)
{
    char *filename;
    cairo_surface_t *surface;
    JSObject *surface_wrapper;
    cairo_status_t status;

    if (!gjs_parse_args(context, "createFromPNG", "s", argc, argv,
                        "filename", &filename))
        return JS_FALSE;

    surface = cairo_image_surface_create_from_png(filename);
    status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(context, "failed to create surface: %s", cairo_status_to_string(status));
        return JS_FALSE;
    }
    surface_wrapper = JS_NewObject(context, &gjs_cairo_image_surface_class, NULL, NULL);
    if (!surface_wrapper) {
        gjs_throw(context, "failed to create surface");
        return JS_FALSE;
    }
    gjs_cairo_surface_construct(context, surface_wrapper, surface);

    *retval = OBJECT_TO_JSVAL(surface_wrapper);
    return JS_TRUE;
}

static JSFunctionSpec gjs_cairo_image_surface_proto_funcs[] = {
    { "createFromPNG", createFromPNG_func, 0, 0},
    // getData
    // getFormat
    // getWidth
    // getHeight
    // getStride
    { NULL }
};

void
gjs_cairo_image_surface_init(JSContext *context, JSObject *module_obj)
{

    if (!JS_DefineFunction(context, module_obj,
                           "createFromPNG",
                           createFromPNG_func,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;
}

#endif /* CAIRO_HAS_IMAGE_SURFACE */
