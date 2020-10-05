/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo-gobject.h>
#include <cairo.h>
#include <girepository.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_GetPrivate, JS_GetClass, ...

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE("Surface", cairo_surface,
                                     CAIRO_GOBJECT_TYPE_SURFACE,
                                     JSCLASS_BACKGROUND_FINALIZE)

static void gjs_cairo_surface_finalize(JSFreeOp*, JSObject* obj) {
    using AutoSurface =
        GjsAutoPointer<cairo_surface_t, cairo_surface_t, cairo_surface_destroy>;
    AutoSurface surface = static_cast<cairo_surface_t*>(JS_GetPrivate(obj));
    JS_SetPrivate(obj, nullptr);
}

/* Properties */
// clang-format off
JSPropertySpec gjs_cairo_surface_proto_props[] = {
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
    GjsAutoChar filename;

    if (!gjs_parse_call_args(context, "writeToPNG", argv, "F",
                             "filename", &filename))
        return false;

    cairo_surface_t* surface = gjs_cairo_surface_get_surface(context, obj);
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
    cairo_surface_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Surface.getType() takes no arguments");
        return false;
    }

    cairo_surface_t* surface = gjs_cairo_surface_get_surface(context, obj);
    if (!surface)
        return false;

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
 * @object: object to construct
 * @surface: cairo_surface to attach to the object
 *
 * Constructs a surface wrapper giving an empty JSObject and a
 * cairo surface. A reference to @surface will be taken.
 *
 * This is mainly used for subclasses where object is already created.
 */
void gjs_cairo_surface_construct(JSObject* object, cairo_surface_t* surface) {
    g_return_if_fail(object);
    g_return_if_fail(surface);

    g_assert(!JS_GetPrivate(object));
    JS_SetPrivate(object, cairo_surface_reference(surface));
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

    gjs_cairo_surface_construct(object, surface);

    return object;
}

/**
 * gjs_cairo_surface_get_surface:
 * @cx: the context
 * @surface_wrapper: surface wrapper
 *
 * Returns: the surface attached to the wrapper.
 */
cairo_surface_t* gjs_cairo_surface_get_surface(
    JSContext* cx, JS::HandleObject surface_wrapper) {
    g_return_val_if_fail(cx, nullptr);
    g_return_val_if_fail(surface_wrapper, nullptr);

    JS::RootedObject proto(cx, gjs_cairo_surface_get_proto(cx));

    bool is_surface_subclass = false;
    if (!gjs_object_in_prototype_chain(cx, proto, surface_wrapper,
                                       &is_surface_subclass))
        return nullptr;
    if (!is_surface_subclass) {
        gjs_throw(cx, "Expected Cairo.Surface but got %s",
                  JS_GetClass(surface_wrapper)->name);
        return nullptr;
    }

    return static_cast<cairo_surface_t*>(JS_GetPrivate(surface_wrapper));
}

[[nodiscard]] static bool surface_to_g_argument(
    JSContext* context, JS::Value value, const char* arg_name,
    GjsArgumentType argument_type, GITransfer transfer, bool may_be_null,
    GIArgument* arg) {
    if (value.isNull()) {
        if (!may_be_null) {
            GjsAutoChar display_name =
                gjs_argument_display_name(arg_name, argument_type);
            gjs_throw(context, "%s may not be null", display_name.get());
            return false;
        }

        gjs_arg_unset<void*>(arg);
        return true;
    }

    if (!value.isObject()) {
        GjsAutoChar display_name =
            gjs_argument_display_name(arg_name, argument_type);
        gjs_throw(context, "%s is not a Cairo.Surface", display_name.get());
        return false;
    }

    JS::RootedObject surface_wrapper(context, &value.toObject());
    cairo_surface_t* s =
        gjs_cairo_surface_get_surface(context, surface_wrapper);
    if (!s)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_surface_destroy(s);

    gjs_arg_set(arg, s);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
surface_from_g_argument(JSContext             *context,
                        JS::MutableHandleValue value_p,
                        GIArgument            *arg)
{
    JSObject* obj = gjs_cairo_surface_from_surface(
        context, gjs_arg_get<cairo_surface_t*>(arg));
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

static GjsForeignInfo foreign_info = {
    surface_to_g_argument,
    surface_from_g_argument,
    surface_release_argument
};

void gjs_cairo_surface_init(void) {
    gjs_struct_foreign_register("cairo", "Surface", &foreign_info);
}
