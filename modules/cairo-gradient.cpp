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

GJS_DEFINE_PROTO_ABSTRACT("CairoGradient", cairo_gradient, JSCLASS_BACKGROUND_FINALIZE)

static void
gjs_cairo_gradient_finalize(JSFreeOp *fop,
                            JSObject *obj)
{
    gjs_cairo_pattern_finalize_pattern(fop, obj);
}

/* Properties */
JSPropertySpec gjs_cairo_gradient_proto_props[] = {
    { NULL }
};

/* Methods */

static JSBool
addColorStopRGB_func(JSContext *context,
                     unsigned   argc,
                     jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JSObject *obj = JSVAL_TO_OBJECT(argv.thisv());

    double offset, red, green, blue;
    cairo_pattern_t *pattern;

    if (!gjs_parse_call_args(context, "addColorStopRGB", "ffff", argv,
                        "offset", &offset,
                        "red", &red,
                        "green", &green,
                        "blue", &blue))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, obj);

    cairo_pattern_add_color_stop_rgb(pattern, offset, red, green, blue);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    argv.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
addColorStopRGBA_func(JSContext *context,
                      unsigned   argc,
                      jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JSObject *obj = JSVAL_TO_OBJECT(argv.thisv());

    double offset, red, green, blue, alpha;
    cairo_pattern_t *pattern;

    if (!gjs_parse_call_args(context, "addColorStopRGBA", "fffff", argv,
                        "offset", &offset,
                        "red", &red,
                        "green", &green,
                        "blue", &blue,
                        "alpha", &alpha))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    cairo_pattern_add_color_stop_rgba(pattern, offset, red, green, blue, alpha);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    argv.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

JSFunctionSpec gjs_cairo_gradient_proto_funcs[] = {
    { "addColorStopRGB", JSOP_WRAPPER((JSNative)addColorStopRGB_func), 0, 0 },
    { "addColorStopRGBA", JSOP_WRAPPER((JSNative)addColorStopRGBA_func), 0, 0 },
    // getColorStopRGB
    // getColorStopRGBA
    { NULL }
};
