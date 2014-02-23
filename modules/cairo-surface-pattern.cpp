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

GJS_DEFINE_PROTO("CairoSurfacePattern", cairo_surface_pattern, JSCLASS_BACKGROUND_FINALIZE)

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_surface_pattern)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_surface_pattern)
    JSObject *surface_wrapper;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_surface_pattern);

    if (!gjs_parse_args(context, "SurfacePattern", "o", argc, argv,
                        "surface", &surface_wrapper))
        return JS_FALSE;

    surface = gjs_cairo_surface_get_surface(context, surface_wrapper);
    if (!surface) {
        gjs_throw(context, "first argument to SurfacePattern() should be a surface");
        return JS_FALSE;
    }

    pattern = cairo_pattern_create_for_surface(surface);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    gjs_cairo_pattern_construct(context, object, pattern);
    cairo_pattern_destroy(pattern);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_surface_pattern);

    return JS_TRUE;
}


static void
gjs_cairo_surface_pattern_finalize(JSFreeOp *fop,
                                   JSObject *obj)
{
    gjs_cairo_pattern_finalize_pattern(fop, obj);
}

JSPropertySpec gjs_cairo_surface_pattern_proto_props[] = {
    { NULL }
};


static JSBool
setExtend_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    cairo_extend_t extend;
    cairo_pattern_t *pattern;

    if (!gjs_parse_args(context, "setExtend", "i", argc, argv,
                        "extend", &extend))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    cairo_pattern_set_extend(pattern, extend);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
getExtend_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    cairo_extend_t extend;
    cairo_pattern_t *pattern;

    if (argc > 0) {
        gjs_throw(context, "SurfacePattern.getExtend() requires no arguments");
        return JS_FALSE;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    extend = cairo_pattern_get_extend(pattern);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    JS_SET_RVAL(context, vp, INT_TO_JSVAL(extend));

    return JS_TRUE;
}

static JSBool
setFilter_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    cairo_filter_t filter;
    cairo_pattern_t *pattern;

    if (!gjs_parse_args(context, "setFilter", "i", argc, argv,
                        "filter", &filter))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    cairo_pattern_set_filter(pattern, filter);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
getFilter_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    cairo_filter_t filter;
    cairo_pattern_t *pattern;

    if (argc > 0) {
        gjs_throw(context, "SurfacePattern.getFilter() requires no arguments");
        return JS_FALSE;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    filter = cairo_pattern_get_filter(pattern);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return JS_FALSE;

    JS_SET_RVAL(context, vp, INT_TO_JSVAL(filter));

    return JS_TRUE;
}

JSFunctionSpec gjs_cairo_surface_pattern_proto_funcs[] = {
    { "setExtend", JSOP_WRAPPER((JSNative)setExtend_func), 0, 0 },
    { "getExtend", JSOP_WRAPPER((JSNative)getExtend_func), 0, 0 },
    { "setFilter", JSOP_WRAPPER((JSNative)setFilter_func), 0, 0 },
    { "getFilter", JSOP_WRAPPER((JSNative)getFilter_func), 0, 0 },
    { NULL }
};

JSObject *
gjs_cairo_surface_pattern_from_pattern(JSContext       *context,
                                       cairo_pattern_t *pattern)
{
    JSObject *object;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(pattern != NULL, NULL);
    g_return_val_if_fail(cairo_pattern_get_type(pattern) == CAIRO_PATTERN_TYPE_SURFACE, NULL);

    object = JS_NewObject(context, &gjs_cairo_surface_pattern_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create surface pattern");
        return NULL;
    }

    gjs_cairo_pattern_construct(context, object, pattern);

    return object;
}

