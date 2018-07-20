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

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-wrapper.h"
#include <cairo.h>
#include "cairo-private.h"

static JSObject *gjs_cairo_image_surface_get_proto(JSContext *);

GJS_DEFINE_PROTO_WITH_PARENT("ImageSurface", cairo_image_surface,
                             cairo_surface, JSCLASS_BACKGROUND_FINALIZE)

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_image_surface)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_image_surface)
    int format, width, height;
    cairo_surface_t *surface;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_image_surface);

    // create_for_data optional parameter
    if (!gjs_parse_call_args(context, "ImageSurface", argv, "iii",
                             "format", &format,
                             "width", &width,
                             "height", &height))
        return false;

    surface = cairo_image_surface_create((cairo_format_t) format, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    gjs_cairo_surface_construct(context, object, surface);
    cairo_surface_destroy(surface);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_image_surface);

    return true;
}

static void
gjs_cairo_image_surface_finalize(JSFreeOp *fop,
                                 JSObject *obj)
{
    gjs_cairo_surface_finalize_surface(fop, obj);
}

JSPropertySpec gjs_cairo_image_surface_proto_props[] = {
    JS_PS_END
};

static bool
createFromPNG_func(JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoChar filename;
    cairo_surface_t *surface;

    if (!gjs_parse_call_args(context, "createFromPNG", argv, "F",
                             "filename", &filename))
        return false;

    surface = cairo_image_surface_create_from_png(filename);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    JS::RootedObject proto(context, gjs_cairo_image_surface_get_proto(context));
    JS::RootedObject surface_wrapper(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_image_surface_class,
                                   proto));
    if (!surface_wrapper) {
        gjs_throw(context, "failed to create surface");
        return false;
    }
    gjs_cairo_surface_construct(context, surface_wrapper, surface);
    cairo_surface_destroy(surface);

    argv.rval().setObject(*surface_wrapper);
    return true;
}

static bool
getFormat_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_t *surface;
    cairo_format_t format;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getFormat() takes no arguments");
        return false;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    format = cairo_image_surface_get_format(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(format);
    return true;
}

static bool
getWidth_func(JSContext *context,
              unsigned   argc,
              JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_t *surface;
    int width;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getWidth() takes no arguments");
        return false;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    width = cairo_image_surface_get_width(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(width);
    return true;
}

static bool
getHeight_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_t *surface;
    int height;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getHeight() takes no arguments");
        return false;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    height = cairo_image_surface_get_height(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(height);
    return true;
}

static bool
getStride_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_t *surface;
    int stride;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getStride() takes no arguments");
        return false;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    stride = cairo_image_surface_get_stride(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(stride);
    return true;
}

JSFunctionSpec gjs_cairo_image_surface_proto_funcs[] = {
    JS_FN("createFromPNG", createFromPNG_func, 0, 0),
    // getData
    JS_FN("getFormat", getFormat_func, 0, 0),
    JS_FN("getWidth", getWidth_func, 0, 0),
    JS_FN("getHeight", getHeight_func, 0, 0),
    JS_FN("getStride", getStride_func, 0, 0),
    JS_FS_END};

JSFunctionSpec gjs_cairo_image_surface_static_funcs[] = {
    JS_FN("createFromPNG", createFromPNG_func, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

JSObject *
gjs_cairo_image_surface_from_surface(JSContext       *context,
                                     cairo_surface_t *surface)
{
    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(surface != NULL, NULL);
    g_return_val_if_fail(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE, NULL);

    JS::RootedObject proto(context, gjs_cairo_image_surface_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_image_surface_class,
                                   proto));
    if (!object) {
        gjs_throw(context, "failed to create image surface");
        return NULL;
    }

    gjs_cairo_surface_construct(context, object, surface);

    return object;
}

