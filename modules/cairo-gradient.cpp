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
#include <jsapi.h>    // for JS_NewObjectWithGivenProto
#include <jspubtd.h>  // for JSProtoKey

#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

JSObject* CairoGradient::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoPattern::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

/* Properties */
// clang-format off
const JSPropertySpec CairoGradient::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Gradient", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/* Methods */

GJS_JSAPI_RETURN_CONVENTION
static bool
addColorStopRGB_func(JSContext *context,
                     unsigned   argc,
                     JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);
    double offset, red, green, blue;

    if (!gjs_parse_call_args(context, "addColorStopRGB", argv, "ffff",
                             "offset", &offset,
                             "red", &red,
                             "green", &green,
                             "blue", &blue))
        return false;

    cairo_pattern_t* pattern = CairoPattern::for_js(context, obj);
    if (!pattern)
        return false;

    cairo_pattern_add_color_stop_rgb(pattern, offset, red, green, blue);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
addColorStopRGBA_func(JSContext *context,
                      unsigned   argc,
                      JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);
    double offset, red, green, blue, alpha;

    if (!gjs_parse_call_args(context, "addColorStopRGBA", argv, "fffff",
                             "offset", &offset,
                             "red", &red,
                             "green", &green,
                             "blue", &blue,
                             "alpha", &alpha))
        return false;

    cairo_pattern_t* pattern = CairoPattern::for_js(context, obj);
    if (!pattern)
        return false;

    cairo_pattern_add_color_stop_rgba(pattern, offset, red, green, blue, alpha);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    argv.rval().setUndefined();
    return true;
}

const JSFunctionSpec CairoGradient::proto_funcs[] = {
    JS_FN("addColorStopRGB", addColorStopRGB_func, 0, 0),
    JS_FN("addColorStopRGBA", addColorStopRGBA_func, 0, 0),
    // getColorStopRGB
    // getColorStopRGBA
    JS_FS_END};
