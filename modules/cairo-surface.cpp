/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo.h>
#include <girepository/girepository.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/Object.h>              // for GetClass
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/cwrapper.h"
#include "gi/foreign.h"
#include "gjs/auto.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

/* Properties */
// clang-format off
const JSPropertySpec CairoSurface::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Surface", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/* Methods */
GJS_JSAPI_RETURN_CONVENTION
static bool
writeToPNG_func(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);
    Gjs::AutoChar filename;

    if (!gjs_parse_call_args(context, "writeToPNG", argv, "F",
                             "filename", &filename))
        return false;

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
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
bool flush_func(JSContext* cx,
                unsigned argc,
                JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, argv, obj);

    if (argc > 1) {
        gjs_throw(cx, "Surface.flush() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    cairo_surface_flush(surface);

    if (!gjs_cairo_check_status(cx, cairo_surface_status(surface), "surface"))
        return false;

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool finish_func(JSContext* cx,
                 unsigned argc,
                 JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, argv, obj);

    if (argc > 1) {
        gjs_throw(cx, "Surface.finish() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    cairo_surface_finish(surface);

    if (!gjs_cairo_check_status(cx, cairo_surface_status(surface), "surface"))
        return false;

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool CairoSurface::getType_func(JSContext* context, unsigned argc,
                                JS::Value* vp) {
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_surface_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Surface.getType() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(context, obj);
    if (!surface)
        return false;

    type = cairo_surface_get_type(surface);
    if (!gjs_cairo_check_status(context, cairo_surface_status(surface),
                                "surface"))
        return false;

    rec.rval().setInt32(type);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool setDeviceOffset_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, obj);
    double x_offset = 0.0, y_offset = 0.0;
    if (!gjs_parse_call_args(cx, "setDeviceOffset", args, "ff", "x_offset",
                             &x_offset, "y_offset", &y_offset))
        return false;

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    cairo_surface_set_device_offset(surface, x_offset, y_offset);
    if (!gjs_cairo_check_status(cx, cairo_surface_status(surface), "surface"))
        return false;

    args.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool getDeviceOffset_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, obj);

    if (argc > 0) {
        gjs_throw(cx, "Surface.getDeviceOffset() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    double x_offset, y_offset;
    cairo_surface_get_device_offset(surface, &x_offset, &y_offset);
    // cannot error

    JS::RootedValueArray<2> elements(cx);
    elements[0].setNumber(JS::CanonicalizeNaN(x_offset));
    elements[1].setNumber(JS::CanonicalizeNaN(y_offset));
    JS::RootedObject retval(cx, JS::NewArrayObject(cx, elements));
    if (!retval)
        return false;

    args.rval().setObject(*retval);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool setDeviceScale_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, obj);
    double x_scale = 1.0, y_scale = 1.0;

    if (!gjs_parse_call_args(cx, "setDeviceScale", args, "ff", "x_scale",
                             &x_scale, "y_scale", &y_scale))
        return false;

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    cairo_surface_set_device_scale(surface, x_scale, y_scale);
    if (!gjs_cairo_check_status(cx, cairo_surface_status(surface),
                                "surface"))
        return false;

    args.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool getDeviceScale_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, obj);

    if (argc > 0) {
        gjs_throw(cx, "Surface.getDeviceScale() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = CairoSurface::for_js(cx, obj);
    if (!surface)
        return false;

    double x_scale, y_scale;
    cairo_surface_get_device_scale(surface, &x_scale, &y_scale);
    // cannot error

    JS::RootedValueArray<2> elements(cx);
    elements[0].setNumber(JS::CanonicalizeNaN(x_scale));
    elements[1].setNumber(JS::CanonicalizeNaN(y_scale));
    JS::RootedObject retval(cx, JS::NewArrayObject(cx, elements));
    if (!retval)
        return false;

    args.rval().setObject(*retval);
    return true;
}

const JSFunctionSpec CairoSurface::proto_funcs[] = {
    JS_FN("flush", flush_func, 0, 0),
    JS_FN("finish", finish_func, 0, 0),
    // getContent
    // getFontOptions
    JS_FN("getType", getType_func, 0, 0),
    // markDirty
    // markDirtyRectangle
    JS_FN("setDeviceOffset", setDeviceOffset_func, 2, 0),
    JS_FN("getDeviceOffset", getDeviceOffset_func, 0, 0),
    JS_FN("setDeviceScale", setDeviceScale_func, 2, 0),
    JS_FN("getDeviceScale", getDeviceScale_func, 0, 0),
    // setFallbackResolution
    // getFallbackResolution
    // copyPage
    // showPage
    // hasShowTextGlyphs
    JS_FN("writeToPNG", writeToPNG_func, 0, 0), JS_FS_END};

/* Public API */

/**
 * CairoSurface::finalize_impl:
 * @surface: the pointer to finalize
 *
 * Destroys the resources associated with a surface wrapper.
 *
 * This is mainly used for subclasses.
 */
void CairoSurface::finalize_impl(JS::GCContext*, cairo_surface_t* surface) {
    if (!surface)
        return;
    cairo_surface_destroy(surface);
}

/**
 * CairoSurface::from_c_ptr:
 * @context: the context
 * @surface: cairo_surface to attach to the object
 *
 * Constructs a surface wrapper given cairo surface.
 * A reference to @surface will be taken.
 *
 */
JSObject* CairoSurface::from_c_ptr(JSContext* context,
                                   cairo_surface_t* surface) {
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(surface, nullptr);

    cairo_surface_type_t type = cairo_surface_get_type(surface);
    if (type == CAIRO_SURFACE_TYPE_IMAGE)
        return CairoImageSurface::from_c_ptr(context, surface);
    if (type == CAIRO_SURFACE_TYPE_PDF)
        return CairoPDFSurface::from_c_ptr(context, surface);
    if (type == CAIRO_SURFACE_TYPE_PS)
        return CairoPSSurface::from_c_ptr(context, surface);
    if (type == CAIRO_SURFACE_TYPE_SVG)
        return CairoSVGSurface::from_c_ptr(context, surface);
    return CairoSurface::CWrapper::from_c_ptr(context, surface);
}

/**
 * CairoSurface::for_js:
 * @cx: the context
 * @surface_wrapper: surface wrapper
 *
 * Overrides NativeObject::for_js().
 *
 * Returns: the surface attached to the wrapper.
 */
cairo_surface_t* CairoSurface::for_js(JSContext* cx,
                                      JS::HandleObject surface_wrapper) {
    g_return_val_if_fail(cx, nullptr);
    g_return_val_if_fail(surface_wrapper, nullptr);

    JS::RootedObject proto(cx, CairoSurface::prototype(cx));

    bool is_surface_subclass = false;
    if (!gjs_object_in_prototype_chain(cx, proto, surface_wrapper,
                                       &is_surface_subclass))
        return nullptr;
    if (!is_surface_subclass) {
        gjs_throw(cx, "Expected Cairo.Surface but got %s",
                  JS::GetClass(surface_wrapper)->name);
        return nullptr;
    }

    return JS::GetMaybePtrFromReservedSlot<cairo_surface_t>(
        surface_wrapper, CairoSurface::POINTER);
}

GJS_JSAPI_RETURN_CONVENTION static bool surface_to_gi_argument(
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

    if (!value.isObject()) {
        Gjs::AutoChar display_name{
            gjs_argument_display_name(arg_name, argument_type)};
        gjs_throw(context, "%s is not a Cairo.Surface", display_name.get());
        return false;
    }

    JS::RootedObject surface_wrapper(context, &value.toObject());
    cairo_surface_t* s = CairoSurface::for_js(context, surface_wrapper);
    if (!s)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_surface_reference(s);

    gjs_arg_set(arg, s);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool surface_from_gi_argument(JSContext* cx,
                                     JS::MutableHandleValue value_p,
                                     GIArgument* arg) {
    JSObject* obj =
        CairoSurface::from_c_ptr(cx, gjs_arg_get<cairo_surface_t*>(arg));
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool surface_release_argument(JSContext*, GITransfer transfer,
                                     GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_surface_destroy(gjs_arg_get<cairo_surface_t*>(arg));
    return true;
}

void gjs_cairo_surface_init(void) {
    static GjsForeignInfo foreign_info = {surface_to_gi_argument,
                                          surface_from_gi_argument,
                                          surface_release_argument};
    gjs_struct_foreign_register("cairo", "Surface", &foreign_info);
}
