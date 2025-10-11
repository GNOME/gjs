/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 Red Hat, Inc.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cairo.h>
#include <girepository/girepository.h>  // for GIArgument, GITransfer, ...

#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/auto.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

// clang-format off
const JSPropertySpec CairoPath::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Path", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/*
 * CairoPath::take_c_ptr():
 * Same as CWrapper::from_c_ptr(), but always takes ownership of the pointer
 * rather than copying it.
 */
JSObject* CairoPath::take_c_ptr(JSContext* cx, cairo_path_t* ptr) {
    JS::RootedObject proto(cx, CairoPath::prototype(cx));
    if (!proto)
        return nullptr;

    JS::RootedObject wrapper(
        cx, JS_NewObjectWithGivenProto(cx, &CairoPath::klass, proto));
    if (!wrapper)
        return nullptr;

    CairoPath::init_private(wrapper, ptr);

    debug_lifecycle(ptr, wrapper, "take_c_ptr");

    return wrapper;
}

void CairoPath::finalize_impl(JS::GCContext*, cairo_path_t* path) {
    if (!path)
        return;
    cairo_path_destroy(path);
}

GJS_JSAPI_RETURN_CONVENTION static bool path_to_gi_argument(
    JSContext* cx, JS::Value value, const char* arg_name,
    GjsArgumentType argument_type, GITransfer transfer, GjsArgumentFlags flags,
    GIArgument* arg) {
    if (value.isNull()) {
        if (!(flags & GjsArgumentFlags::MAY_BE_NULL)) {
            Gjs::AutoChar display_name{
                gjs_argument_display_name(arg_name, argument_type)};
            gjs_throw(cx, "%s may not be null", display_name.get());
            return false;
        }

        gjs_arg_unset(arg);
        return true;
    }

    if (!value.isObject()) {
        Gjs::AutoChar display_name{
            gjs_argument_display_name(arg_name, argument_type)};
        gjs_throw(cx, "%s is not a Cairo.Path", display_name.get());
        return false;
    }

    JS::RootedObject path_wrapper{cx, &value.toObject()};
    cairo_path_t* s = CairoPath::for_js(cx, path_wrapper);
    if (!s)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        s = CairoPath::copy_ptr(s);

    gjs_arg_set(arg, s);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool path_from_gi_argument(JSContext* cx, JS::MutableHandleValue value_p,
                                  GIArgument* arg) {
    JSObject* obj = CairoPath::from_c_ptr(cx, gjs_arg_get<cairo_path_t*>(arg));
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool path_release_argument(JSContext*, GITransfer transfer,
                                  GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_path_destroy(gjs_arg_get<cairo_path_t*>(arg));
    return true;
}

void gjs_cairo_path_init(void) {
    static GjsForeignInfo foreign_info = {
        path_to_gi_argument, path_from_gi_argument, path_release_argument};
    gjs_struct_foreign_register("cairo", "Path", &foreign_info);
}

// Adapted from PyGObject cairo code
cairo_path_t* CairoPath::copy_ptr(cairo_path_t* path) {
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 0, 0);
    cairo_t* cr = cairo_create(surface);
    cairo_append_path(cr, path);
    cairo_path_t* copy = cairo_copy_path(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return copy;
}
