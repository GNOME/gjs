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

#include <cairo.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

[[nodiscard]] static JSObject* gjs_cairo_solid_pattern_get_proto(JSContext*);

GJS_DEFINE_PROTO_ABSTRACT_WITH_PARENT("SolidPattern", cairo_solid_pattern,
                                      cairo_pattern,
                                      JSCLASS_BACKGROUND_FINALIZE)

static void
gjs_cairo_solid_pattern_finalize(JSFreeOp *fop,
                                 JSObject *obj)
{
    gjs_cairo_pattern_finalize_pattern(fop, obj);
}

// clang-format off
JSPropertySpec gjs_cairo_solid_pattern_proto_props[] = {
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
    JSObject *pattern_wrapper;

    if (!gjs_parse_call_args(context, "createRGB", argv, "fff",
                             "red", &red,
                             "green", &green,
                             "blue", &blue))
        return false;

    pattern = cairo_pattern_create_rgb(red, green, blue);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    pattern_wrapper = gjs_cairo_solid_pattern_from_pattern(context, pattern);
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
    JSObject *pattern_wrapper;

    if (!gjs_parse_call_args(context, "createRGBA", argv, "ffff",
                             "red", &red,
                             "green", &green,
                             "blue", &blue,
                             "alpha", &alpha))
        return false;

    pattern = cairo_pattern_create_rgba(red, green, blue, alpha);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    pattern_wrapper = gjs_cairo_solid_pattern_from_pattern(context, pattern);
    cairo_pattern_destroy(pattern);

    argv.rval().setObjectOrNull(pattern_wrapper);

    return true;
}

JSFunctionSpec gjs_cairo_solid_pattern_proto_funcs[] = {
    JS_FN("createRGB", createRGB_func, 0, 0),
    JS_FN("createRGBA", createRGBA_func, 0, 0),
    JS_FS_END};

JSFunctionSpec gjs_cairo_solid_pattern_static_funcs[] = { JS_FS_END };

JSObject *
gjs_cairo_solid_pattern_from_pattern(JSContext       *context,
                                     cairo_pattern_t *pattern)
{
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(pattern, nullptr);
    g_return_val_if_fail(
        cairo_pattern_get_type(pattern) == CAIRO_PATTERN_TYPE_SOLID, nullptr);

    JS::RootedObject proto(context, gjs_cairo_solid_pattern_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_solid_pattern_class,
                                   proto));
    if (!object) {
        gjs_throw(context, "failed to create solid pattern");
        return nullptr;
    }

    gjs_cairo_pattern_construct(object, pattern);

    return object;
}

