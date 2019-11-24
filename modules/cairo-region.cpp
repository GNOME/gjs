/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <cairo-gobject.h>
#include <cairo.h>
#include <girepository.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/Conversions.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_GetPropertyById, JS_SetPropert...

#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

typedef struct {
    cairo_region_t *region;
} GjsCairoRegion;

GJS_USE
static JSObject *gjs_cairo_region_get_proto(JSContext *);

GJS_DEFINE_PROTO_WITH_GTYPE("Region", cairo_region,
                            CAIRO_GOBJECT_TYPE_REGION,
                            JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoRegion, gjs_cairo_region_class);

static cairo_region_t *
get_region(JSContext       *context,
           JS::HandleObject obj)
{
    GjsCairoRegion *priv = priv_from_js(context, obj);
    if (!priv)
        return nullptr;
    else
        return priv->region;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
fill_rectangle(JSContext             *context,
               JS::HandleObject       obj,
               cairo_rectangle_int_t *rect);

#define PRELUDE                                                       \
    GJS_GET_PRIV(context, argc, vp, argv, obj, GjsCairoRegion, priv); \
    cairo_region_t* this_region = priv ? priv->region : nullptr;

#define RETURN_STATUS                                           \
    return gjs_cairo_check_status(context, cairo_region_status(this_region), "region");

#define REGION_DEFINE_REGION_FUNC(method)                                     \
    GJS_JSAPI_RETURN_CONVENTION                                               \
    static bool method##_func(JSContext* context, unsigned argc,              \
                              JS::Value* vp) {                                \
        PRELUDE;                                                              \
        JS::RootedObject other_obj(context);                                  \
        if (!gjs_parse_call_args(context, #method, argv, "o", "other_region", \
                                 &other_obj))                                 \
            return false;                                                     \
                                                                              \
        cairo_region_t* other_region = get_region(context, other_obj);        \
                                                                              \
        cairo_region_##method(this_region, other_region);                     \
        argv.rval().setUndefined();                                           \
        RETURN_STATUS;                                                        \
    }

#define REGION_DEFINE_RECT_FUNC(method)                                    \
    GJS_JSAPI_RETURN_CONVENTION                                            \
    static bool method##_rectangle_func(JSContext* context, unsigned argc, \
                                        JS::Value* vp) {                   \
        PRELUDE;                                                           \
        JS::RootedObject rect_obj(context);                                \
        if (!gjs_parse_call_args(context, #method, argv, "o", "rect",      \
                                 &rect_obj))                               \
            return false;                                                  \
                                                                           \
        cairo_rectangle_int_t rect;                                        \
        if (!fill_rectangle(context, rect_obj, &rect))                     \
            return false;                                                  \
                                                                           \
        cairo_region_##method##_rectangle(this_region, &rect);             \
        argv.rval().setUndefined();                                        \
        RETURN_STATUS;                                                     \
    }

REGION_DEFINE_REGION_FUNC(union)
REGION_DEFINE_REGION_FUNC(subtract)
REGION_DEFINE_REGION_FUNC(intersect)
REGION_DEFINE_REGION_FUNC(xor)

REGION_DEFINE_RECT_FUNC(union)
REGION_DEFINE_RECT_FUNC(subtract)
REGION_DEFINE_RECT_FUNC(intersect)
REGION_DEFINE_RECT_FUNC(xor)

GJS_JSAPI_RETURN_CONVENTION
static bool
fill_rectangle(JSContext             *context,
               JS::HandleObject       obj,
               cairo_rectangle_int_t *rect)
{
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedValue val(context);

    if (!JS_GetPropertyById(context, obj, atoms.x(), &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->x))
        return false;

    if (!JS_GetPropertyById(context, obj, atoms.y(), &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->y))
        return false;

    if (!JS_GetPropertyById(context, obj, atoms.width(), &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->width))
        return false;

    if (!JS_GetPropertyById(context, obj, atoms.height(), &val))
        return false;
    if (!JS::ToInt32(context, val, &rect->height))
        return false;

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
make_rectangle(JSContext *context,
               cairo_rectangle_int_t *rect)
{
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject rect_obj(context, JS_NewPlainObject(context));
    if (!rect_obj)
        return nullptr;
    JS::RootedValue val(context);

    val = JS::Int32Value(rect->x);
    if (!JS_SetPropertyById(context, rect_obj, atoms.x(), val))
        return nullptr;

    val = JS::Int32Value(rect->y);
    if (!JS_SetPropertyById(context, rect_obj, atoms.y(), val))
        return nullptr;

    val = JS::Int32Value(rect->width);
    if (!JS_SetPropertyById(context, rect_obj, atoms.width(), val))
        return nullptr;

    val = JS::Int32Value(rect->height);
    if (!JS_SetPropertyById(context, rect_obj, atoms.height(), val))
        return nullptr;

    return rect_obj;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
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

GJS_JSAPI_RETURN_CONVENTION
static bool
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
    JS_FN("union", union_func, 0, 0),
    JS_FN("subtract", subtract_func, 0, 0),
    JS_FN("intersect", intersect_func, 0, 0),
    JS_FN("xor", xor_func, 0, 0),

    JS_FN("unionRectangle", union_rectangle_func, 0, 0),
    JS_FN("subtractRectangle", subtract_rectangle_func, 0, 0),
    JS_FN("intersectRectangle", intersect_rectangle_func, 0, 0),
    JS_FN("xorRectangle", xor_rectangle_func, 0, 0),

    JS_FN("numRectangles", num_rectangles_func, 0, 0),
    JS_FN("getRectangle", get_rectangle_func, 0, 0),
    JS_FS_END};

JSFunctionSpec gjs_cairo_region_static_funcs[] = { JS_FS_END };

static void
_gjs_cairo_region_construct_internal(JSContext       *context,
                                     JS::HandleObject obj,
                                     cairo_region_t  *region)
{
    GjsCairoRegion *priv;

    priv = g_slice_new0(GjsCairoRegion);

    g_assert(!priv_from_js(context, obj));
    JS_SetPrivate(obj, priv);

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

static void gjs_cairo_region_finalize(JSFreeOp*, JSObject* obj) {
    GjsCairoRegion *priv;
    priv = (GjsCairoRegion*) JS_GetPrivate(obj);
    if (!priv)
        return;

    cairo_region_destroy(priv->region);
    g_slice_free(GjsCairoRegion, priv);
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
gjs_cairo_region_from_region(JSContext *context,
                             cairo_region_t *region)
{
    JS::RootedObject proto(context, gjs_cairo_region_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_region_class, proto));
    if (!object)
        return nullptr;

    _gjs_cairo_region_construct_internal(context, object, region);

    return object;
}

GJS_USE
static bool
region_to_g_argument(JSContext      *context,
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

GJS_JSAPI_RETURN_CONVENTION
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

static bool region_release_argument(JSContext*, GITransfer transfer,
                                    GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_region_destroy(static_cast<cairo_region_t*>(arg->v_pointer));
    return true;
}

static GjsForeignInfo foreign_info = {
    region_to_g_argument,
    region_from_g_argument,
    region_release_argument
};

void gjs_cairo_region_init(void) {
    gjs_struct_foreign_register("cairo", "Region", &foreign_info);
}
