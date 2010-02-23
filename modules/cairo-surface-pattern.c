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

#include <gjs/gjs.h>
#include <cairo.h>
#include "cairo-private.h"

GJS_DEFINE_PROTO("CairoSurfacePattern", gjs_cairo_surface_pattern)

static JSBool
gjs_cairo_surface_pattern_constructor(JSContext *context,
                                      JSObject  *obj,
                                      uintN      argc,
                                      jsval     *argv,
                                      jsval     *retval)
{
    JSObject *surface_wrapper;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    cairo_status_t status;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    if (!gjs_parse_args(context, "SurfacePattern", "o", argc, argv,
                        "surface", &surface_wrapper))
        return JS_FALSE;

    surface = gjs_cairo_surface_get_surface(context, surface_wrapper);
    if (!surface) {
        gjs_throw(context, "first argument to SurfacePattern() should be a surface");
        return JS_FALSE;
    }

    pattern = cairo_pattern_create_for_surface(surface);
    status = cairo_pattern_status(pattern);
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(context, "Failed to create cairo pattern: %s",
                  cairo_status_to_string(status));
        return JS_FALSE;
    }
    gjs_cairo_pattern_construct(context, obj, pattern);

    return JS_TRUE;
}


static void
gjs_cairo_surface_pattern_finalize(JSContext *context,
                                   JSObject  *obj)
{
    gjs_cairo_pattern_finalize_pattern(context, obj);
}

static JSPropertySpec gjs_cairo_surface_pattern_proto_props[] = {
    { NULL }
};


static JSBool
setExtend_func(JSContext *context,
               JSObject  *object,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    cairo_extend_t extend;
    cairo_pattern_t *pattern;

    if (!gjs_parse_args(context, "setExtend", "i", argc, argv,
                        "extend", &extend))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, object);
    cairo_pattern_set_extend(pattern, extend);

    return JS_TRUE;
}

static JSBool
getExtend_func(JSContext *context,
               JSObject  *object,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    cairo_extend_t extend;
    cairo_pattern_t *pattern;

    if (argc > 0) {
        gjs_throw(context, "Context.getExtend() requires no arguments");
        return JS_FALSE;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, object);
    extend = cairo_pattern_get_extend(pattern);
    *retval = INT_TO_JSVAL(extend);

    return JS_TRUE;
}

static JSBool
setFilter_func(JSContext *context,
               JSObject  *object,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    cairo_filter_t filter;
    cairo_pattern_t *pattern;

    if (!gjs_parse_args(context, "setFilter", "i", argc, argv,
                        "filter", &filter))
        return JS_FALSE;

    pattern = gjs_cairo_pattern_get_pattern(context, object);
    cairo_pattern_set_filter(pattern, filter);

    return JS_TRUE;
}

static JSBool
getFilter_func(JSContext *context,
               JSObject  *object,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    cairo_filter_t filter;
    cairo_pattern_t *pattern;

    if (argc > 0) {
        gjs_throw(context, "Context.getFilter() requires no arguments");
        return JS_FALSE;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, object);
    filter = cairo_pattern_get_filter(pattern);
    *retval = INT_TO_JSVAL(filter);

    return JS_TRUE;
}

static JSFunctionSpec gjs_cairo_surface_pattern_proto_funcs[] = {
    { "setExtend", setExtend_func, 0, 0 },
    { "getExtend", getExtend_func, 0, 0 },
    { "setFilter", setFilter_func, 0, 0 },
    { "getFilter", getFilter_func, 0, 0 },
    { NULL }
};
