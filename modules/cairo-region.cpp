/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Red Hat, Inc.

#include <config.h>

#include <cairo.h>
#include <girepository/girepository.h>

#include <js/CallArgs.h>
#include <js/Conversions.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

GJS_JSAPI_RETURN_CONVENTION
static bool
fill_rectangle(JSContext             *context,
               JS::HandleObject       obj,
               cairo_rectangle_int_t *rect);

#define PRELUDE                                                            \
    GJS_GET_THIS(context, argc, vp, argv, obj);                            \
    cairo_region_t* this_region;                                           \
    if (!CairoRegion::for_js_typecheck(context, obj, &this_region, &argv)) \
        return false;

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
        cairo_region_t* other_region =                                        \
            CairoRegion::for_js(context, other_obj);                          \
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

// clang-format off
const JSPropertySpec CairoRegion::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Region", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

const JSFunctionSpec CairoRegion::proto_funcs[] = {
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

cairo_region_t* CairoRegion::constructor_impl(JSContext* context,
                                              const JS::CallArgs& argv) {
    if (!gjs_parse_call_args(context, "Region", argv, ""))
        return nullptr;

    return cairo_region_create();
}

void CairoRegion::finalize_impl(JS::GCContext*, cairo_region_t* region) {
    if (!region)
        return;

    cairo_region_destroy(region);
}

GJS_JSAPI_RETURN_CONVENTION static bool region_to_gi_argument(
    JSContext* context, JS::Value value, const char* arg_name,
    GjsArgumentType argument_type, GITransfer transfer, GjsArgumentFlags flags,
    GIArgument* arg) {
    if (value.isNull()) {
        if (!(flags & GjsArgumentFlags::MAY_BE_NULL)) {
            Gjs::AutoChar display_name{
                gjs_argument_display_name(arg_name, argument_type)};
            gjs_throw(context, "%s may not be null", display_name.get());
            return false;
        }

        gjs_arg_unset(arg);
        return true;
    }

    JS::RootedObject obj(context, &value.toObject());
    cairo_region_t *region;

    if (!CairoRegion::for_js_typecheck(context, obj, &region))
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_region_reference(region);

    gjs_arg_set(arg, region);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool region_from_gi_argument(JSContext* context,
                                    JS::MutableHandleValue value_p,
                                    GIArgument* arg) {
    JSObject* obj =
        CairoRegion::from_c_ptr(context, gjs_arg_get<cairo_region_t*>(arg));
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool region_release_argument(JSContext*, GITransfer transfer,
                                    GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_region_destroy(gjs_arg_get<cairo_region_t*>(arg));
    return true;
}


void gjs_cairo_region_init(void) {
    static GjsForeignInfo foreign_info = {region_to_gi_argument,
                                          region_from_gi_argument,
                                          region_release_argument};

    gjs_struct_foreign_register("cairo", "Region", &foreign_info);
}
