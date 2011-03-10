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

#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <cairo.h>
#include "cairo-private.h"

GJS_DEFINE_PROTO("CairoImageSurface", cairo_image_surface)

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_image_surface)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_image_surface)
    int format, width, height;
    cairo_surface_t *surface;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_image_surface);

    // create_for_data optional parameter
    if (!gjs_parse_args(context, "ImageSurface", "iii", argc, argv,
                        "format", &format,
                        "width", &width,
                        "height", &height))
        return JS_FALSE;

    surface = cairo_image_surface_create(format, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return JS_FALSE;

    gjs_cairo_surface_construct(context, object, surface);
    cairo_surface_destroy(surface);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_image_surface);

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
                   uintN      argc,
                   jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *filename;
    cairo_surface_t *surface;
    JSObject *surface_wrapper;

    if (!gjs_parse_args(context, "createFromPNG", "s", argc, argv,
                        "filename", &filename))
        return JS_FALSE;

    surface = cairo_image_surface_create_from_png(filename);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return JS_FALSE;

    surface_wrapper = JS_NewObject(context, &gjs_cairo_image_surface_class, NULL, NULL);
    if (!surface_wrapper) {
        gjs_throw(context, "failed to create surface");
        return JS_FALSE;
    }
    gjs_cairo_surface_construct(context, surface_wrapper, surface);
    cairo_surface_destroy(surface);

    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(surface_wrapper));
    return JS_TRUE;
}

static JSFunctionSpec gjs_cairo_image_surface_proto_funcs[] = {
    { "createFromPNG", (JSNative)createFromPNG_func, 0, JSFUN_FAST_NATIVE},
    // getData
    // getFormat
    // getWidth
    // getHeight
    // getStride
    { NULL }
};

JSObject *
gjs_cairo_image_surface_from_surface(JSContext       *context,
                                     cairo_surface_t *surface)
{
    JSObject *object;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(surface != NULL, NULL);
    g_return_val_if_fail(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE, NULL);

    object = JS_NewObject(context, &gjs_cairo_image_surface_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create image surface");
        return NULL;
    }

    gjs_cairo_surface_construct(context, object, surface);

    return object;
}

void
gjs_cairo_image_surface_init(JSContext *context, JSObject *module_obj)
{

    if (!JS_DefineFunction(context, module_obj,
                           "createFromPNG",
                           (JSNative)createFromPNG_func,
                           1, GJS_MODULE_PROP_FLAGS | JSFUN_FAST_NATIVE))
        return;
}
