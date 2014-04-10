/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2014 Red Hat, Inc.
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
    JSContext *context;
    JSObject *object;
    cairo_region_t *region;
} GjsCairoRegion;

GJS_DEFINE_PROTO_WITH_GTYPE("CairoRegion", cairo_region, CAIRO_GOBJECT_TYPE_REGION, JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoRegion, gjs_cairo_region_class);

static cairo_region_t *
get_region(JSContext *context,
           JSObject *obj) {
    GjsCairoRegion *priv = priv_from_js(context, obj);
    if (priv == NULL)
        return NULL;
    else
        return priv->region;
}

static JSBool
fill_rectangle(JSContext *context, JSObject *obj,
               cairo_rectangle_int_t *rect);

#define PRELUDE                                                 \
    JSObject *obj = JS_THIS_OBJECT(context, vp);                \
    cairo_region_t *this_region = get_region(context, obj);

#define RETURN_STATUS                                           \
    return gjs_cairo_check_status(context, cairo_region_status(this_region), "region");

#define REGION_DEFINE_REGION_FUNC(method)                       \
    static JSBool                                               \
    method##_func(JSContext *context,                           \
                  unsigned argc,                                \
                  jsval *vp)                                    \
    {                                                           \
        PRELUDE;                                                \
        JSObject *other_obj;                                    \
        cairo_region_t *other_region;                           \
        if (!gjs_parse_args(context, #method, "o", argc,        \
                            JS_ARGV(context, vp),               \
                            "other_region", &other_obj))        \
            return JS_FALSE;                                    \
                                                                \
        this_region = get_region(context, obj);                 \
        other_region = get_region(context, other_obj);          \
                                                                \
        cairo_region_##method(this_region, other_region);       \
            JS_SET_RVAL(context, vp, JSVAL_VOID);               \
            RETURN_STATUS;                                      \
    }

#define REGION_DEFINE_RECT_FUNC(method)                         \
    static JSBool                                               \
    method##_rectangle_func(JSContext *context,                 \
                            unsigned argc,                      \
                            jsval *vp)                          \
    {                                                           \
        PRELUDE;                                                \
        JSObject *rect_obj;                                     \
        cairo_rectangle_int_t rect;                             \
        if (!gjs_parse_args(context, #method, "o", argc,        \
                            JS_ARGV(context, vp),               \
                            "rect", &rect_obj))                 \
            return JS_FALSE;                                    \
                                                                \
        if (!fill_rectangle(context, rect_obj, &rect))          \
            return JS_FALSE;                                    \
                                                                \
        cairo_region_##method##_rectangle(this_region, &rect);  \
            JS_SET_RVAL(context, vp, JSVAL_VOID);               \
            RETURN_STATUS;                                      \
    }

REGION_DEFINE_REGION_FUNC(union)
REGION_DEFINE_REGION_FUNC(subtract)
REGION_DEFINE_REGION_FUNC(intersect)
REGION_DEFINE_REGION_FUNC(xor)

REGION_DEFINE_RECT_FUNC(union)
REGION_DEFINE_RECT_FUNC(subtract)
REGION_DEFINE_RECT_FUNC(intersect)
REGION_DEFINE_RECT_FUNC(xor)

static JSBool
fill_rectangle(JSContext *context, JSObject *obj,
               cairo_rectangle_int_t *rect)
{
    jsval val;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_X, &val))
        return JS_FALSE;
    if (!JS_ValueToInt32(context, val, &rect->x))
        return JS_FALSE;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_Y, &val))
        return JS_FALSE;
    if (!JS_ValueToInt32(context, val, &rect->y))
        return JS_FALSE;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_WIDTH, &val))
        return JS_FALSE;
    if (!JS_ValueToInt32(context, val, &rect->width))
        return JS_FALSE;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_HEIGHT, &val))
        return JS_FALSE;
    if (!JS_ValueToInt32(context, val, &rect->height))
        return JS_FALSE;

    return JS_TRUE;
}

static JSObject *
make_rectangle(JSContext *context,
               cairo_rectangle_int_t *rect)
{
    JSObject *rect_obj = JS_NewObject(context, NULL, NULL, NULL);
    jsval val;

    val = INT_TO_JSVAL(rect->x);
    JS_SetProperty(context, rect_obj, "x", &val);

    val = INT_TO_JSVAL(rect->y);
    JS_SetProperty(context, rect_obj, "y", &val);

    val = INT_TO_JSVAL(rect->width);
    JS_SetProperty(context, rect_obj, "width", &val);

    val = INT_TO_JSVAL(rect->height);
    JS_SetProperty(context, rect_obj, "height", &val);

    return rect_obj;
}

static JSBool
num_rectangles_func(JSContext *context,
                    unsigned argc,
                    jsval *vp)
{
    PRELUDE;
    int n_rects;
    jsval retval;

    if (!gjs_parse_args(context, "num_rectangles", "", argc, JS_ARGV(context, vp)))
        return JS_FALSE;

    n_rects = cairo_region_num_rectangles(this_region);
    retval = INT_TO_JSVAL(n_rects);
    JS_SET_RVAL (context, vp, retval);
    RETURN_STATUS;
}

static JSBool
get_rectangle_func(JSContext *context,
                   unsigned argc,
                   jsval *vp)
{
    PRELUDE;
    int i;
    JSObject *rect_obj;
    cairo_rectangle_int_t rect;
    jsval retval;

    if (!gjs_parse_args(context, "get_rectangle", "i", argc, JS_ARGV(context, vp), "rect", &i))
        return JS_FALSE;

    cairo_region_get_rectangle(this_region, i, &rect);
    rect_obj = make_rectangle(context, &rect);

    retval = OBJECT_TO_JSVAL(rect_obj);
    JS_SET_RVAL (context, vp, retval);
    RETURN_STATUS;
}

JSPropertySpec gjs_cairo_region_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_cairo_region_proto_funcs[] = {
    { "union", (JSNative)union_func, 0, 0 },
    { "subtract", (JSNative)subtract_func, 0, 0 },
    { "intersect", (JSNative)intersect_func, 0, 0 },
    { "xor", (JSNative)xor_func, 0, 0 },

    { "unionRectangle", (JSNative)union_rectangle_func, 0, 0 },
    { "subtractRectangle", (JSNative)subtract_rectangle_func, 0, 0 },
    { "intersectRectangle", (JSNative)intersect_rectangle_func, 0, 0 },
    { "xorRectangle", (JSNative)xor_rectangle_func, 0, 0 },

    { "numRectangles", (JSNative)num_rectangles_func, 0, 0 },
    { "getRectangle", (JSNative)get_rectangle_func, 0, 0 },
    { NULL }
};

static void
_gjs_cairo_region_construct_internal(JSContext *context,
                                     JSObject *obj,
                                     cairo_region_t *region)
{
    GjsCairoRegion *priv;

    priv = g_slice_new0(GjsCairoRegion);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(obj, priv);

    priv->context = context;
    priv->object = obj;
    priv->region = cairo_region_reference(region);
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(cairo_region)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(cairo_region)
    cairo_region_t *region;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(cairo_region);

    if (!gjs_parse_args(context, "Region", "", argc, argv))
        return JS_FALSE;

    region = cairo_region_create();

    _gjs_cairo_region_construct_internal(context, object, region);
    cairo_region_destroy(region);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_region);

    return JS_TRUE;
}

static void
gjs_cairo_region_finalize(JSFreeOp *fop,
                          JSObject *obj)
{
    GjsCairoRegion *priv;
    priv = (GjsCairoRegion*) JS_GetPrivate(obj);
    if (priv == NULL)
        return;

    cairo_region_destroy(priv->region);
    g_slice_free(GjsCairoRegion, priv);
}

static JSObject *
gjs_cairo_region_from_region(JSContext *context,
                             cairo_region_t *region)
{
    JSObject *object;

    object = JS_NewObject(context, &gjs_cairo_region_class, NULL, NULL);
    if (!object)
        return NULL;

    _gjs_cairo_region_construct_internal(context, object, region);

    return object;
}

static JSBool
region_to_g_argument(JSContext      *context,
                     jsval           value,
                     const char     *arg_name,
                     GjsArgumentType argument_type,
                     GITransfer      transfer,
                     gboolean        may_be_null,
                     GArgument      *arg)
{
    JSObject *obj;
    cairo_region_t *region;

    obj = JSVAL_TO_OBJECT(value);
    region = get_region(context, obj);
    if (!region)
        return JS_FALSE;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_region_destroy(region);

    arg->v_pointer = region;
    return JS_TRUE;
}

static JSBool
region_from_g_argument(JSContext  *context,
                       jsval      *value_p,
                       GArgument  *arg)
{
    JSObject *obj;

    obj = gjs_cairo_region_from_region(context, (cairo_region_t*)arg->v_pointer);
    if (!obj)
        return JS_FALSE;

    *value_p = OBJECT_TO_JSVAL(obj);
    return JS_TRUE;
}

static JSBool
region_release_argument(JSContext  *context,
                        GITransfer  transfer,
                        GArgument  *arg)
{
    cairo_region_destroy((cairo_region_t*)arg->v_pointer);
    return JS_TRUE;
}

static GjsForeignInfo foreign_info = {
    region_to_g_argument,
    region_from_g_argument,
    region_release_argument
};

void
gjs_cairo_region_init(JSContext *context)
{
    gjs_struct_foreign_register("cairo", "Region", &foreign_info);
}
