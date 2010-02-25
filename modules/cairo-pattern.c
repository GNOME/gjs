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

GJS_DEFINE_PROTO_ABSTRACT("CairoPattern", gjs_cairo_pattern)

GJS_DEFINE_PRIV_FROM_JS(GjsCairoPattern, gjs_cairo_pattern_class)

static void
gjs_cairo_pattern_finalize(JSContext *context,
                           JSObject  *obj)
{
    GjsCairoPattern *priv;
    priv = JS_GetPrivate(context, obj);
    if (priv == NULL)
        return;
    cairo_pattern_destroy(priv->pattern);
    g_slice_free(GjsCairoPattern, priv);
}

void
gjs_cairo_pattern_construct(JSContext       *context,
                            JSObject        *obj,
                            cairo_pattern_t *pattern)
{
    GjsCairoPattern *priv;

    priv = g_slice_new0(GjsCairoPattern);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    priv->context = context;
    priv->object = obj;
    priv->pattern = pattern;
}

/* Properties */
static JSPropertySpec gjs_cairo_pattern_proto_props[] = {
    { NULL }
};

/* Methods */

static JSBool
getType_func(JSContext *context,
             JSObject  *object,
             uintN      argc,
             jsval     *argv,
             jsval     *retval)
{
    cairo_pattern_t *pattern;
    cairo_pattern_type_t type;

    if (argc > 1) {
        gjs_throw(context, "FIXME");
        return JS_FALSE;
    }

    pattern = gjs_cairo_pattern_get_pattern(context, object);
    type = cairo_pattern_get_type(pattern);

    *retval = INT_TO_JSVAL(type);
    return JS_TRUE;
}

static JSFunctionSpec gjs_cairo_pattern_proto_funcs[] = {
    // getMatrix
    { "getType", getType_func, 0, 0 },
    // setMatrix
    { NULL }
};

/* Public API */
void
gjs_cairo_pattern_finalize_pattern(JSContext *context,
                                   JSObject  *obj)
{
    g_return_if_fail(context != NULL);
    g_return_if_fail(obj != NULL);

    gjs_cairo_pattern_finalize(context, obj);
}

cairo_pattern_t *
gjs_cairo_pattern_get_pattern(JSContext *context,
                              JSObject *object)
{
    GjsCairoPattern *priv;
    priv = JS_GetPrivate(context, object);
    if (priv == NULL)
        return NULL;
    return priv->pattern;
}

