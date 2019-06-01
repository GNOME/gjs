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

#include "gi/foreign.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-wrapper.h"
#include <cairo.h>
#include <cairo-gobject.h>
#include "cairo-private.h"

typedef struct {
    void            *dummy;
    JSContext       *context;
    JSObject        *object;
    cairo_surface_t *surface;
} GjsCairoSurface;

GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE("Surface", cairo_surface,
                                     CAIRO_GOBJECT_TYPE_SURFACE,
                                     JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoSurface, gjs_cairo_surface_class)

static void gjs_cairo_surface_finalize(JSFreeOp*, JSObject* obj) {
    GjsCairoSurface *priv;
    priv = (GjsCairoSurface*) JS_GetPrivate(obj);
    if (!priv)
        return;
    cairo_surface_destroy(priv->surface);
    g_slice_free(GjsCairoSurface, priv);
}

/* Properties */
JSPropertySpec gjs_cairo_surface_proto_props[] = {
    JS_PS_END
};

/* Methods */
GJS_JSAPI_RETURN_CONVENTION
static bool
writeToPNG_func(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);
    GjsAutoChar filename;
    cairo_surface_t *surface;

    if (!gjs_parse_call_args(context, "writeToPNG", argv, "F",
                             "filename", &filename))
        return false;

    surface = gjs_cairo_surface_get_surface(context, obj);
    if (!surface)
        return false;

    cairo_surface_write_to_png(surface, filename);
    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return false;
    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getType_func(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_t *surface;
    cairo_surface_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Surface.getType() takes no arguments");
        return false;
    }

    surface = gjs_cairo_surface_get_surface(context, obj);
    type = cairo_surface_get_type(surface);
    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return false;

    rec.rval().setInt32(type);
    return true;
}

JSFunctionSpec gjs_cairo_surface_proto_funcs[] = {
    // flush
    // getContent
    // getFontOptions
    JS_FN("getType", getType_func, 0, 0),
    // markDirty
    // markDirtyRectangle
    // setDeviceOffset
    // getDeviceOffset
    // setFallbackResolution
    // getFallbackResolution
    // copyPage
    // showPage
    // hasShowTextGlyphs
    JS_FN("writeToPNG", writeToPNG_func, 0, 0),
    JS_FS_END};

JSFunctionSpec gjs_cairo_surface_static_funcs[] = { JS_FS_END };

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
                            JS::HandleObject object,
                            cairo_surface_t *surface)
{
    GjsCairoSurface *priv;

    g_return_if_fail(context);
    g_return_if_fail(object);
    g_return_if_fail(surface);

    priv = g_slice_new0(GjsCairoSurface);

    g_assert(!priv_from_js(context, object));
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
    g_return_if_fail(fop);
    g_return_if_fail(object);

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
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(surface, nullptr);

    cairo_surface_type_t type = cairo_surface_get_type(surface);
    if (type == CAIRO_SURFACE_TYPE_IMAGE)
        return gjs_cairo_image_surface_from_surface(context, surface);
    if (type == CAIRO_SURFACE_TYPE_PDF)
        return gjs_cairo_pdf_surface_from_surface(context, surface);
    if (type == CAIRO_SURFACE_TYPE_PS)
        return gjs_cairo_ps_surface_from_surface(context, surface);
    if (type == CAIRO_SURFACE_TYPE_SVG)
        return gjs_cairo_svg_surface_from_surface(context, surface);

    JS::RootedObject proto(context, gjs_cairo_surface_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_surface_class, proto));
    if (!object) {
        gjs_throw(context, "failed to create surface");
        return nullptr;
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

    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(object, nullptr);

    priv = (GjsCairoSurface*) JS_GetPrivate(object);
    if (!priv)
        return nullptr;
    return priv->surface;
}

GJS_USE
static bool
surface_to_g_argument(JSContext      *context,
                      JS::Value       value,
                      const char     *arg_name,
                      GjsArgumentType argument_type,
                      GITransfer      transfer,
                      bool            may_be_null,
                      GArgument      *arg)
{
    if (value.isNull()) {
        if (!may_be_null) {
            GjsAutoChar display_name =
                gjs_argument_display_name(arg_name, argument_type);
            gjs_throw(context, "%s may not be null", display_name.get());
            return false;
        }

        arg->v_pointer = nullptr;
        return true;
    }

    JSObject *obj;
    cairo_surface_t *s;

    obj = &value.toObject();
    s = gjs_cairo_surface_get_surface(context, obj);
    if (!s)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_surface_destroy(s);

    arg->v_pointer = s;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
surface_from_g_argument(JSContext             *context,
                        JS::MutableHandleValue value_p,
                        GIArgument            *arg)
{
    JSObject *obj;

    obj = gjs_cairo_surface_from_surface(context, (cairo_surface_t*)arg->v_pointer);
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool surface_release_argument(JSContext*, GITransfer transfer,
                                     GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_surface_destroy(static_cast<cairo_surface_t*>(arg->v_pointer));
    return true;
}

static GjsForeignInfo foreign_info = {
    surface_to_g_argument,
    surface_from_g_argument,
    surface_release_argument
};

void gjs_cairo_surface_init(void) {
    gjs_struct_foreign_register("cairo", "Surface", &foreign_info);
}
