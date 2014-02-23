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

GJS_DEFINE_PROTO_ABSTRACT("CairoSolidPattern", cairo_solid_pattern, JSCLASS_BACKGROUND_FINALIZE)

static void
gjs_cairo_solid_pattern_finalize(JSFreeOp *fop,
                                 JSObject *obj)
{
    gjs_cairo_pattern_finalize_pattern(fop, obj);
}

JSPropertySpec gjs_cairo_solid_pattern_proto_props[] = {
    { NULL }
};

static JSBool
createRGB_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    double red, green, blue;
    cairo_pattern_t *pattern;
    JSObject *pattern_wrapper;

    if (!gjs_parse_args(context, "createRGB", "fff", argc, argv,
                        "red", &red,
                        "green", &green,
                        "blue", &blue))
        return JS_FALSE;

    pattern = cairo_pattern_create_rgb(red, green, blue);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    pattern_wrapper = gjs_cairo_solid_pattern_from_pattern(context, pattern);
    cairo_pattern_destroy(pattern);

    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(pattern_wrapper));

    return JS_TRUE;
}

static JSBool
createRGBA_func(JSContext *context,
                unsigned   argc,
                jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    double red, green, blue, alpha;
    cairo_pattern_t *pattern;
    JSObject *pattern_wrapper;

    if (!gjs_parse_args(context, "createRGBA", "ffff", argc, argv,
                        "red", &red,
                        "green", &green,
                        "blue", &blue,
                        "alpha", &alpha))
        return JS_FALSE;

    pattern = cairo_pattern_create_rgba(red, green, blue, alpha);
    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    pattern_wrapper = gjs_cairo_solid_pattern_from_pattern(context, pattern);
    cairo_pattern_destroy(pattern);

    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(pattern_wrapper));

    return JS_TRUE;
}

JSFunctionSpec gjs_cairo_solid_pattern_proto_funcs[] = {
    { "createRGB", JSOP_WRAPPER((JSNative)createRGB_func), 0, 0 },
    { "createRGBA", JSOP_WRAPPER((JSNative)createRGBA_func), 0, 0 },
    { NULL }
};

JSObject *
gjs_cairo_solid_pattern_from_pattern(JSContext       *context,
                                     cairo_pattern_t *pattern)
{
    JSObject *object;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(pattern != NULL, NULL);
    g_return_val_if_fail(cairo_pattern_get_type(pattern) == CAIRO_PATTERN_TYPE_SOLID, NULL);

    object = JS_NewObject(context, &gjs_cairo_solid_pattern_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create solid pattern");
        return NULL;
    }

    gjs_cairo_pattern_construct(context, object, pattern);

    return object;
}

