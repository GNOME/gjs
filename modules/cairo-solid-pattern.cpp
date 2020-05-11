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

#include "gjs/jsapi-util-args.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

JSObject* CairoSolidPattern::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoPattern::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

// clang-format off
const JSPropertySpec CairoSolidPattern::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "SolidPattern", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

GJS_JSAPI_RETURN_CONVENTION
static bool
createRGB_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    double red, green, blue;
    cairo_pattern_t *pattern;

    if (!gjs_parse_call_args(context, "createRGB", argv, "fff",
                             "red", &red,
                             "green", &green,
                             "blue", &blue))
        return false;

    pattern = cairo_pattern_create_rgb(red, green, blue);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    JSObject* pattern_wrapper = CairoSolidPattern::from_c_ptr(context, pattern);
    if (!pattern_wrapper)
        return false;
    cairo_pattern_destroy(pattern);

    argv.rval().setObjectOrNull(pattern_wrapper);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
createRGBA_func(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    double red, green, blue, alpha;
    cairo_pattern_t *pattern;

    if (!gjs_parse_call_args(context, "createRGBA", argv, "ffff",
                             "red", &red,
                             "green", &green,
                             "blue", &blue,
                             "alpha", &alpha))
        return false;

    pattern = cairo_pattern_create_rgba(red, green, blue, alpha);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    JSObject* pattern_wrapper = CairoSolidPattern::from_c_ptr(context, pattern);
    if (!pattern_wrapper)
        return false;
    cairo_pattern_destroy(pattern);

    argv.rval().setObjectOrNull(pattern_wrapper);

    return true;
}

// clang-format off
const JSFunctionSpec CairoSolidPattern::static_funcs[] = {
    JS_FN("createRGB", createRGB_func, 0, 0),
    JS_FN("createRGBA", createRGBA_func, 0, 0),
    JS_FS_END};
// clang-format on
