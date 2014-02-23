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
#include <gi/foreign.h>
#include <cairo.h>
#include <cairo-gobject.h>
#include "cairo-private.h"

typedef struct {
    void            *dummy;
    JSContext       *context;
    JSObject        *object;
    cairo_surface_t *surface;
} GjsCairoSurface;

GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE("CairoSurface", cairo_surface, CAIRO_GOBJECT_TYPE_SURFACE, JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoSurface, gjs_cairo_surface_class)

static void
gjs_cairo_surface_finalize(JSFreeOp *fop,
                           JSObject *obj)
{
    GjsCairoSurface *priv;
    priv = (GjsCairoSurface*) JS_GetPrivate(obj);
    if (priv == NULL)
        return;
    cairo_surface_destroy(priv->surface);
    g_slice_free(GjsCairoSurface, priv);
}

/* Properties */
JSPropertySpec gjs_cairo_surface_proto_props[] = {
    { NULL }
};

/* Methods */
static JSBool
writeToPNG_func(JSContext *context,
                unsigned   argc,
                jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
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
    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return JS_FALSE;
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
getType_func(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    cairo_surface_t *surface;
    cairo_surface_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Surface.getType() takes no arguments");
        return JS_FALSE;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    type = cairo_surface_get_type(surface);
    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return JS_FALSE;

    JS_SET_RVAL(context, vp, INT_TO_JSVAL(type));
    return JS_TRUE;
}

JSFunctionSpec gjs_cairo_surface_proto_funcs[] = {
    // flush
    // getContent
    // getFontOptions
    { "getType", JSOP_WRAPPER((JSNative)getType_func), 0, 0},
    // markDirty
    // markDirtyRectangle
    // setDeviceOffset
    // getDeviceOffset
    // setFallbackResolution
    // getFallbackResolution
    // copyPage
    // showPage
    // hasShowTextGlyphs
    { "writeToPNG", JSOP_WRAPPER((JSNative)writeToPNG_func), 0, 0 },
    { NULL }
};

/* Public API */

/**
 * gjs_cairo_surface_construct:
 * @context: the context
 * @object: object to construct
 * @surface: cairo_surface to attach to the object
 *
 * Constructs a surface wrapper giving an empty JSObject and a
 * cairo surface. A reference to @surface will be taken.
 *
 * This is mainly used for subclasses where object is already created.
 */
void
gjs_cairo_surface_construct(JSContext       *context,
                            JSObject        *object,
                            cairo_surface_t *surface)
{
    GjsCairoSurface *priv;

    g_return_if_fail(context != NULL);
    g_return_if_fail(object != NULL);
    g_return_if_fail(surface != NULL);

    priv = g_slice_new0(GjsCairoSurface);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    priv->context = context;
    priv->object = object;
    priv->surface = cairo_surface_reference(surface);
}

/**
 * gjs_cairo_surface_finalize:
 * @fop: the free op
 * @object: object to finalize
 *
 * Destroys the resources associated with a surface wrapper.
 *
 * This is mainly used for subclasses.
 */
void
gjs_cairo_surface_finalize_surface(JSFreeOp *fop,
                                   JSObject *object)
{
    g_return_if_fail(fop != NULL);
    g_return_if_fail(object != NULL);

    gjs_cairo_surface_finalize(fop, object);
}

/**
 * gjs_cairo_surface_from_surface:
 * @context: the context
 * @surface: cairo_surface to attach to the object
 *
 * Constructs a surface wrapper given cairo surface.
 * A reference to @surface will be taken.
 *
 */
JSObject *
gjs_cairo_surface_from_surface(JSContext       *context,
                               cairo_surface_t *surface)
{
    JSObject *object;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(surface != NULL, NULL);

    switch (cairo_surface_get_type(surface)) {
        case CAIRO_SURFACE_TYPE_IMAGE:
            return gjs_cairo_image_surface_from_surface(context, surface);
        case CAIRO_SURFACE_TYPE_PDF:
            return gjs_cairo_pdf_surface_from_surface(context, surface);
        case CAIRO_SURFACE_TYPE_PS:
            return gjs_cairo_ps_surface_from_surface(context, surface);
        case CAIRO_SURFACE_TYPE_SVG:
            return gjs_cairo_svg_surface_from_surface(context, surface);
        default:
            break;
    }

    object = JS_NewObject(context, &gjs_cairo_surface_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create surface");
        return NULL;
    }

    gjs_cairo_surface_construct(context, object, surface);

    return object;
}

/**
 * gjs_cairo_surface_get_surface:
 * @context: the context
 * @object: surface wrapper
 *
 * Returns: the surface attaches to the wrapper.
 *
 */
cairo_surface_t *
gjs_cairo_surface_get_surface(JSContext *context,
                              JSObject *object)
{
    GjsCairoSurface *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(object != NULL, NULL);

    priv = (GjsCairoSurface*) JS_GetPrivate(object);
    if (priv == NULL)
        return NULL;
    return priv->surface;
}

static JSBool
surface_to_g_argument(JSContext      *context,
                      jsval           value,
                      const char     *arg_name,
                      GjsArgumentType argument_type,
                      GITransfer      transfer,
                      gboolean        may_be_null,
                      GArgument      *arg)
{
    JSObject *obj;
    cairo_surface_t *s;

    obj = JSVAL_TO_OBJECT(value);
    s = gjs_cairo_surface_get_surface(context, obj);
    if (!s)
        return JS_FALSE;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_surface_destroy(s);

    arg->v_pointer = s;
    return JS_TRUE;
}

static JSBool
surface_from_g_argument(JSContext  *context,
                        jsval      *value_p,
                        GArgument  *arg)
{
    JSObject *obj;

    obj = gjs_cairo_surface_from_surface(context, (cairo_surface_t*)arg->v_pointer);
    if (!obj)
        return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);
    return JS_TRUE;
}

static JSBool
surface_release_argument(JSContext  *context,
                         GITransfer  transfer,
                         GArgument  *arg)
{
    cairo_surface_destroy((cairo_surface_t*)arg->v_pointer);
    return JS_TRUE;
}

static GjsForeignInfo foreign_info = {
    surface_to_g_argument,
    surface_from_g_argument,
    surface_release_argument
};

void
gjs_cairo_surface_init(JSContext *context)
{
    gjs_struct_foreign_register("cairo", "Surface", &foreign_info);
}
