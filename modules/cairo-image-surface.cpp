/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo.h>

#include <js/CallArgs.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto
#include <jspubtd.h>  // for JSProtoKey

#include "gjs/auto.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

JSObject* CairoImageSurface::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoSurface::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

cairo_surface_t* CairoImageSurface::constructor_impl(JSContext* context,
                                                     const JS::CallArgs& argv) {
    int format, width, height;
    cairo_surface_t *surface;

    // create_for_data optional parameter
    if (!gjs_parse_call_args(context, "ImageSurface", argv, "iii",
                             "format", &format,
                             "width", &width,
                             "height", &height))
        return nullptr;

    surface = cairo_image_surface_create((cairo_format_t) format, width, height);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return nullptr;

    return surface;
}

// clang-format off
const JSPropertySpec CairoImageSurface::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "ImageSurface", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

GJS_JSAPI_RETURN_CONVENTION
static bool
createFromPNG_func(JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    Gjs::AutoChar filename;
    cairo_surface_t *surface;

    if (!gjs_parse_call_args(context, "createFromPNG", argv, "F",
                             "filename", &filename))
        return false;

    surface = cairo_image_surface_create_from_png(filename);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    JSObject* surface_wrapper = CairoImageSurface::from_c_ptr(context, surface);
    if (!surface_wrapper)
        return false;

    cairo_surface_destroy(surface);

    argv.rval().setObject(*surface_wrapper);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getFormat_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_format_t format;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getFormat() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
    if (!surface)
        return false;

    format = cairo_image_surface_get_format(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(format);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getWidth_func(JSContext *context,
              unsigned   argc,
              JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    int width;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getWidth() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
    if (!surface)
        return false;

    width = cairo_image_surface_get_width(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(width);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getHeight_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    int height;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getHeight() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
    if (!surface)
        return false;

    height = cairo_image_surface_get_height(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(height);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getStride_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    int stride;

    if (argc > 1) {
        gjs_throw(context, "ImageSurface.getStride() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
    if (!surface)
        return false;

    stride = cairo_image_surface_get_stride(surface);

    if (!gjs_cairo_check_status(context, cairo_surface_status(surface), "surface"))
        return false;

    rec.rval().setInt32(stride);
    return true;
}

const JSFunctionSpec CairoImageSurface::proto_funcs[] = {
    JS_FN("createFromPNG", createFromPNG_func, 0, 0),
    // getData
    JS_FN("getFormat", getFormat_func, 0, 0),
    JS_FN("getWidth", getWidth_func, 0, 0),
    JS_FN("getHeight", getHeight_func, 0, 0),
    JS_FN("getStride", getStride_func, 0, 0), JS_FS_END};

const JSFunctionSpec CairoImageSurface::static_funcs[] = {
    JS_FN("createFromPNG", createFromPNG_func, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};
