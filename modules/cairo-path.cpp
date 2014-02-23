/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 Red Hat, Inc.
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

typedef struct {
    JSContext       *context;
    JSObject        *object;
    cairo_path_t    *path;
} GjsCairoPath;

GJS_DEFINE_PROTO_ABSTRACT("CairoPath", cairo_path, JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoPath, gjs_cairo_path_class)

static void
gjs_cairo_path_finalize(JSFreeOp *fop,
                        JSObject *obj)
{
    GjsCairoPath *priv;
    priv = (GjsCairoPath*) JS_GetPrivate(obj);
    if (priv == NULL)
        return;
    cairo_path_destroy(priv->path);
    g_slice_free(GjsCairoPath, priv);
}

/* Properties */
JSPropertySpec gjs_cairo_path_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_cairo_path_proto_funcs[] = {
    { NULL }
};

/**
 * gjs_cairo_path_from_path:
 * @context: the context
 * @path: cairo_path_t to attach to the object
 *
 * Constructs a pattern wrapper given cairo pattern.
 * NOTE: This function takes ownership of the path.
 */
JSObject *
gjs_cairo_path_from_path(JSContext    *context,
                         cairo_path_t *path)
{
    JSObject *object;
    GjsCairoPath *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(path != NULL, NULL);

    object = JS_NewObject(context, &gjs_cairo_path_class, NULL, NULL);
    if (!object) {
        gjs_throw(context, "failed to create path");
        return NULL;
    }

    priv = g_slice_new0(GjsCairoPath);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    priv->context = context;
    priv->object = object;
    priv->path = path;

    return object;
}


/**
 * gjs_cairo_path_get_path:
 * @context: the context
 * @object: path wrapper
 *
 * Returns: the path attached to the wrapper.
 *
 */
cairo_path_t *
gjs_cairo_path_get_path(JSContext *context,
                        JSObject  *object)
{
    GjsCairoPath *priv;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(object != NULL, NULL);

    priv = (GjsCairoPath*) JS_GetPrivate(object);
    if (priv == NULL)
        return NULL;
    return priv->path;
}
