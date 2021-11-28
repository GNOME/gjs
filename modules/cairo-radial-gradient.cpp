/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo.h>

#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto
#include <jspubtd.h>  // for JSProtoKey

#include "gjs/jsapi-util-args.h"
#include "modules/cairo-private.h"

namespace JS {
class CallArgs;
}

JSObject* CairoRadialGradient::new_proto(JSContext* cx, JSProtoKey) {
    JS::RootedObject parent_proto(cx, CairoGradient::prototype(cx));
    return JS_NewObjectWithGivenProto(cx, nullptr, parent_proto);
}

cairo_pattern_t* CairoRadialGradient::constructor_impl(
    JSContext* context, const JS::CallArgs& argv) {
    double cx0, cy0, radius0, cx1, cy1, radius1;
    cairo_pattern_t* pattern;
    if (!gjs_parse_call_args(context, "RadialGradient", argv, "ffffff",
                             "cx0", &cx0,
                             "cy0", &cy0,
                             "radius0", &radius0,
                             "cx1", &cx1,
                             "cy1", &cy1,
                             "radius1", &radius1))
        return nullptr;

    pattern = cairo_pattern_create_radial(cx0, cy0, radius0, cx1, cy1, radius1);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return nullptr;

    return pattern;
}

const JSPropertySpec CairoRadialGradient::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "RadialGradient", JSPROP_READONLY),
    JS_PS_END};

const JSFunctionSpec CairoRadialGradient::proto_funcs[] = {
    // getRadialCircles
    JS_FS_END};
