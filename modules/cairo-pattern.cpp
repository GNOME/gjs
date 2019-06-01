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

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"
#include <cairo.h>
#include <cairo-gobject.h>
#include "cairo-private.h"

typedef struct {
    void            *dummy;
    JSContext       *context;
    JSObject        *object;
    cairo_pattern_t *pattern;
} GjsCairoPattern;

GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE("Pattern", cairo_pattern,
                                     CAIRO_GOBJECT_TYPE_PATTERN,
                                     JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoPattern, gjs_cairo_pattern_class)

static void gjs_cairo_pattern_finalize(JSFreeOp*, JSObject* obj) {
    GjsCairoPattern *priv;
    priv = (GjsCairoPattern*) JS_GetPrivate(obj);
    if (!priv)
        return;
    cairo_pattern_destroy(priv->pattern);
    g_slice_free(GjsCairoPattern, priv);
}

/* Properties */
JSPropertySpec gjs_cairo_pattern_proto_props[] = {
    JS_PS_END
};

/* Methods */

GJS_JSAPI_RETURN_CONVENTION
static bool
getType_func(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_pattern_t *pattern;
    cairo_pattern_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Pattern.getType() takes no arguments");
        return false;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, obj);
    type = cairo_pattern_get_type(pattern);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    rec.rval().setInt32(type);
    return true;
}

JSFunctionSpec gjs_cairo_pattern_proto_funcs[] = {
    // getMatrix
    JS_FN("getType", getType_func, 0, 0),
    // setMatrix
    JS_FS_END};

JSFunctionSpec gjs_cairo_pattern_static_funcs[] = { JS_FS_END };

/* Public API */

/**
 * gjs_cairo_pattern_construct:
 * @context: the context
 * @object: object to construct
 * @pattern: cairo_pattern to attach to the object
 *
 * Constructs a pattern wrapper giving an empty JSObject and a
 * cairo pattern. A reference to @pattern will be taken.
 *
 * This is mainly used for subclasses where object is already created.
 */
void
gjs_cairo_pattern_construct(JSContext       *context,
                            JS::HandleObject object,
                            cairo_pattern_t *pattern)
{
    GjsCairoPattern *priv;

    g_return_if_fail(context);
    g_return_if_fail(object);
    g_return_if_fail(pattern);

    priv = g_slice_new0(GjsCairoPattern);

    g_assert(!priv_from_js(context, object));
    JS_SetPrivate(object, priv);

    priv->context = context;
    priv->object = object;
    priv->pattern = cairo_pattern_reference(pattern);
}

/**
 * gjs_cairo_pattern_finalize:
 * @fop: the free op
 * @object: object to finalize
 *
 * Destroys the resources associated with a pattern wrapper.
 *
 * This is mainly used for subclasses.
 */

void
gjs_cairo_pattern_finalize_pattern(JSFreeOp *fop,
                                   JSObject *object)
{
    g_return_if_fail(fop);
    g_return_if_fail(object);

    gjs_cairo_pattern_finalize(fop, object);
}

/**
 * gjs_cairo_pattern_from_pattern:
 * @context: the context
 * @pattern: cairo_pattern to attach to the object
 *
 * Constructs a pattern wrapper given cairo pattern.
 * A reference to @pattern will be taken.
 *
 */
JSObject *
gjs_cairo_pattern_from_pattern(JSContext       *context,
                               cairo_pattern_t *pattern)
{
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(pattern, nullptr);

    switch (cairo_pattern_get_type(pattern)) {
        case CAIRO_PATTERN_TYPE_SOLID:
            return gjs_cairo_solid_pattern_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_SURFACE:
            return gjs_cairo_surface_pattern_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_LINEAR:
            return gjs_cairo_linear_gradient_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_RADIAL:
            return gjs_cairo_radial_gradient_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_MESH:
        case CAIRO_PATTERN_TYPE_RASTER_SOURCE:
        default:
            gjs_throw(context,
                      "failed to create pattern, unsupported pattern type %d",
                      cairo_pattern_get_type(pattern));
            return nullptr;
    }
}

/**
 * gjs_cairo_pattern_get_pattern:
 * @context: the context
 * @object: pattern wrapper
 *
 * Returns: the pattern attaches to the wrapper.
 *
 */
cairo_pattern_t *
gjs_cairo_pattern_get_pattern(JSContext *context,
                              JSObject  *object)
{
    GjsCairoPattern *priv;

    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(object, nullptr);

    priv = (GjsCairoPattern*) JS_GetPrivate(object);
    if (!priv)
        return nullptr;

    return priv->pattern;
}

