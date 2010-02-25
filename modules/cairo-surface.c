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

GJS_DEFINE_PROTO_ABSTRACT("CairoSurface", gjs_cairo_surface)

GJS_DEFINE_PRIV_FROM_JS(GjsCairoSurface, gjs_cairo_surface_class)

static void
gjs_cairo_surface_finalize(JSContext *context,
                           JSObject  *obj)
{
    GjsCairoSurface *priv;
    priv = JS_GetPrivate(context, obj);
    if (priv == NULL)
        return;
    cairo_surface_destroy(priv->surface);
    g_slice_free(GjsCairoSurface, priv);
}

void
gjs_cairo_surface_construct(JSContext       *context,
                            JSObject        *obj,
                            cairo_surface_t *surface)
{
    GjsCairoSurface *priv;

    priv = g_slice_new0(GjsCairoSurface);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    priv->context = context;
    priv->object = obj;
    priv->surface = surface;
}

/* Properties */
static JSPropertySpec gjs_cairo_surface_proto_props[] = {
    { NULL }
};

/* Methods */
static JSBool
writeToPNG_func(JSContext *context,
                JSObject  *obj,
                uintN      argc,
                jsval     *argv,
                jsval     *retval)
{
    char *filename;
    cairo_surface_t *surface;

    if (!gjs_parse_args(context, "writeToPNG", "s", argc, argv,
                        "filename", &filename))
        return JS_FALSE;

    surface = gjs_cairo_surface_get_surface(context, obj);
    if (!surface) {
        g_free(filename);
        return JS_FALSE;
    }
    cairo_surface_write_to_png(surface, filename);
    g_free(filename);
    return JS_TRUE;
}

static JSBool
getType_func(JSContext *context,
             JSObject  *object,
             uintN      argc,
             jsval     *argv,
             jsval     *retval)
{
    cairo_surface_t *surface;
    cairo_surface_type_t type;

    if (argc > 1) {
        gjs_throw(context, "FIXME");
        return JS_FALSE;
    }

    surface = gjs_cairo_surface_get_surface(context, object);
    type = cairo_surface_get_type(surface);

    *retval = INT_TO_JSVAL(type);
    return JS_TRUE;
}

static JSFunctionSpec gjs_cairo_surface_proto_funcs[] = {
    // flush
    // getContent
    // getFontOptions
    { "getType", getType_func, 0, 0 },
    // markDirty
    // markDirtyRectangle
    // setDeviceOffset
    // getDeviceOffset
    // setFallbackResolution
    // getFallbackResolution
    // copyPage
    // showPage
    // hasShowTextGlyphs
    { "writeToPNG", writeToPNG_func, 0, 0 },
    { NULL }
};

/* Public API */
void
gjs_cairo_surface_finalize_surface(JSContext *context,
                                   JSObject  *obj)
{
    g_return_if_fail(context != NULL);
    g_return_if_fail(obj != NULL);

    gjs_cairo_surface_finalize(context, obj);
}

cairo_surface_t *
gjs_cairo_surface_get_surface(JSContext *context,
                              JSObject *object)
{
    GjsCairoSurface *priv;
    priv = JS_GetPrivate(context, object);
    if (priv == NULL)
        return NULL;
    return priv->surface;
}

