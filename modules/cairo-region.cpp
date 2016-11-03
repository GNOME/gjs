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

#include "gi/foreign.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-wrapper.h"

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
get_region(JSContext       *context,
           JS::HandleObject obj)
{
    GjsCairoRegion *priv = priv_from_js(context, obj);
    if (priv == NULL)
        return NULL;
    else
        return priv->region;
}

static bool
fill_rectangle(JSContext             *context,
               JS::HandleObject       obj,
               cairo_rectangle_int_t *rect);

#define PRELUDE                                                       \
    GJS_GET_PRIV(context, argc, vp, argv, obj, GjsCairoRegion, priv); \
    cairo_region_t *this_region = priv ? priv->region : NULL;

#define RETURN_STATUS                                           \
    return gjs_cairo_check_status(context, cairo_region_status(this_region), "region");

#define REGION_DEFINE_REGION_FUNC(method)                       \
    static JSBool                                               \
    method##_func(JSContext *context,                           \
                  unsigned argc,                                \
                  JS::Value *vp)                                \
    {                                                           \
        PRELUDE;                                                \
        JS::RootedObject other_obj(context);                    \
        cairo_region_t *other_region;                           \
        if (!gjs_parse_call_args(context, #method, argv, "o",   \
                                 "other_region", &other_obj))   \
            return false;                                       \
                                                                \
        other_region = get_region(context, other_obj);          \
                                                                \
        cairo_region_##method(this_region, other_region);       \
            argv.rval().setUndefined();                         \
            RETURN_STATUS;                                      \
    }

#define REGION_DEFINE_RECT_FUNC(method)                         \
    static JSBool                                               \
    method##_rectangle_func(JSContext *context,                 \
                            unsigned argc,                      \
                            JS::Value *vp)                      \
    {                                                           \
        PRELUDE;                                                \
        JS::RootedObject rect_obj(context);                     \
        cairo_rectangle_int_t rect;                             \
        if (!gjs_parse_call_args(context, #method, argv, "o",   \
                                 "rect", &rect_obj))            \
            return false;                                       \
                                                                \
        if (!fill_rectangle(context, rect_obj, &rect))          \
            return false;                                       \
                                                                \
        cairo_region_##method##_rectangle(this_region, &rect);  \
            argv.rval().setUndefined();                         \
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

static bool
fill_rectangle(JSContext             *context,
               JS::HandleObject       obj,
               cairo_rectangle_int_t *rect)
{
    JS::RootedValue val(context);

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_X, &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->x))
        return false;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_Y, &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->y))
        return false;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_WIDTH, &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->width))
        return false;

    if (!gjs_object_get_property_const(context, obj, GJS_STRING_HEIGHT, &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->height))
        return false;

    return true;
}

static JSObject *
make_rectangle(JSContext *context,
               cairo_rectangle_int_t *rect)
{
    JS::RootedObject rect_obj(context,
        JS_NewObject(context, NULL, NULL, NULL));
    JS::RootedValue val(context);

    val = JS::Int32Value(rect->x);
    JS_SetProperty(context, rect_obj, "x", val.address());

    val = JS::Int32Value(rect->y);
    JS_SetProperty(context, rect_obj, "y", val.address());

    val = JS::Int32Value(rect->width);
    JS_SetProperty(context, rect_obj, "width", val.address());

    val = JS::Int32Value(rect->height);
    JS_SetProperty(context, rect_obj, "height", val.address());

    return rect_obj;
}

static JSBool
num_rectangles_func(JSContext *context,
                    unsigned argc,
                    JS::Value *vp)
{
    PRELUDE;
    int n_rects;

    if (!gjs_parse_call_args(context, "num_rectangles", argv, ""))
        return false;

    n_rects = cairo_region_num_rectangles(this_region);
    argv.rval().setInt32(n_rects);
    RETURN_STATUS;
}

static JSBool
get_rectangle_func(JSContext *context,
                   unsigned argc,
                   JS::Value *vp)
{
    PRELUDE;
    int i;
    JSObject *rect_obj;
    cairo_rectangle_int_t rect;

    if (!gjs_parse_call_args(context, "get_rectangle", argv, "i",
                             "rect", &i))
        return false;

    cairo_region_get_rectangle(this_region, i, &rect);
    rect_obj = make_rectangle(context, &rect);

    argv.rval().setObjectOrNull(rect_obj);
    RETURN_STATUS;
}

JSPropertySpec gjs_cairo_region_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_cairo_region_proto_funcs[] = {
    JS_FS("union", union_func, 0, 0),
    JS_FS("subtract", subtract_func, 0, 0),
    JS_FS("intersect", intersect_func, 0, 0),
    JS_FS("xor", xor_func, 0, 0),

    JS_FS("unionRectangle", union_rectangle_func, 0, 0),
    JS_FS("subtractRectangle", subtract_rectangle_func, 0, 0),
    JS_FS("intersectRectangle", intersect_rectangle_func, 0, 0),
    JS_FS("xorRectangle", xor_rectangle_func, 0, 0),

    JS_FS("numRectangles", num_rectangles_func, 0, 0),
    JS_FS("getRectangle", get_rectangle_func, 0, 0),
    JS_FS_END
};

static void
_gjs_cairo_region_construct_internal(JSContext       *context,
                                     JS::HandleObject obj,
                                     cairo_region_t  *region)
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

    if (!gjs_parse_call_args(context, "Region", argv, ""))
        return false;

    region = cairo_region_create();

    _gjs_cairo_region_construct_internal(context, object, region);
    cairo_region_destroy(region);

    GJS_NATIVE_CONSTRUCTOR_FINISH(cairo_region);

    return true;
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
    JS::RootedObject object(context,
                            JS_NewObject(context, &gjs_cairo_region_class, NULL, NULL));
    if (!object)
        return NULL;

    _gjs_cairo_region_construct_internal(context, object, region);

    return object;
}

static bool
region_to_g_argument(JSContext      *context,
                     JS::Value       value,
                     const char     *arg_name,
                     GjsArgumentType argument_type,
                     GITransfer      transfer,
                     bool            may_be_null,
                     GArgument      *arg)
{
    JS::RootedObject obj(context, &value.toObject());
    cairo_region_t *region;

    region = get_region(context, obj);
    if (!region)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_region_destroy(region);

    arg->v_pointer = region;
    return true;
}

static bool
region_from_g_argument(JSContext             *context,
                       JS::MutableHandleValue value_p,
                       GIArgument            *arg)
{
    JSObject *obj;

    obj = gjs_cairo_region_from_region(context, (cairo_region_t*)arg->v_pointer);
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool
region_release_argument(JSContext  *context,
                        GITransfer  transfer,
                        GArgument  *arg)
{
    cairo_region_destroy((cairo_region_t*)arg->v_pointer);
    return true;
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
