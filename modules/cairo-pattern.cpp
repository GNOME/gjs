/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <cairo.h>
#include <girepository/girepository.h>  // for GIArgument, GITransfer, ...
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/Object.h>              // for GetClass
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/auto.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

/* Properties */

// clang-format off
const JSPropertySpec CairoPattern::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Pattern", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/* Methods */

GJS_JSAPI_RETURN_CONVENTION
bool CairoPattern::getType_func(JSContext* context, unsigned argc,
                                JS::Value* vp) {
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_pattern_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Pattern.getType() takes no arguments");
        return false;
    }

    cairo_pattern_t* pattern = CairoPattern::for_js(context, obj);
    if (!pattern)
        return false;

    type = cairo_pattern_get_type(pattern);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    rec.rval().setInt32(type);
    return true;
}

const JSFunctionSpec CairoPattern::proto_funcs[] = {
    // getMatrix
    JS_FN("getType", getType_func, 0, 0),
    // setMatrix
    JS_FS_END};

/* Public API */

/**
 * CairoPattern::finalize_impl:
 * @pattern: pointer to free
 *
 * Destroys the resources associated with a pattern wrapper.
 *
 * This is mainly used for subclasses.
 */
void CairoPattern::finalize_impl(JS::GCContext*, cairo_pattern_t* pattern) {
    if (!pattern)
        return;
    cairo_pattern_destroy(pattern);
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
            return CairoSolidPattern::from_c_ptr(context, pattern);
        case CAIRO_PATTERN_TYPE_SURFACE:
            return CairoSurfacePattern::from_c_ptr(context, pattern);
        case CAIRO_PATTERN_TYPE_LINEAR:
            return CairoLinearGradient::from_c_ptr(context, pattern);
        case CAIRO_PATTERN_TYPE_RADIAL:
            return CairoRadialGradient::from_c_ptr(context, pattern);
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
 * CairoPattern::for_js:
 * @cx: the context
 * @pattern_wrapper: pattern wrapper
 *
 * Returns: the pattern attached to the wrapper.
 */
cairo_pattern_t* CairoPattern::for_js(JSContext* cx,
                                      JS::HandleObject pattern_wrapper) {
    g_return_val_if_fail(cx, nullptr);
    g_return_val_if_fail(pattern_wrapper, nullptr);

    JS::RootedObject proto(cx, CairoPattern::prototype(cx));

    bool is_pattern_subclass = false;
    if (!gjs_object_in_prototype_chain(cx, proto, pattern_wrapper,
                                       &is_pattern_subclass))
        return nullptr;
    if (!is_pattern_subclass) {
        gjs_throw(cx, "Expected Cairo.Pattern but got %s",
                  JS::GetClass(pattern_wrapper)->name);
        return nullptr;
    }

    return JS::GetMaybePtrFromReservedSlot<cairo_pattern_t>(
        pattern_wrapper, CairoPattern::POINTER);
}

GJS_JSAPI_RETURN_CONVENTION static bool pattern_to_gi_argument(
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
        gjs_throw(context, "%s is not a Cairo.Pattern", display_name.get());
        return false;
    }

    JS::RootedObject pattern_wrapper{context, &value.toObject()};
    cairo_pattern_t* s = CairoPattern::for_js(context, pattern_wrapper);
    if (!s)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_pattern_reference(s);

    gjs_arg_set(arg, s);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool pattern_from_gi_argument(JSContext* cx,
                                     JS::MutableHandleValue value_p,
                                     GIArgument* arg) {
    JSObject* obj =
        CairoPattern::from_c_ptr(cx, gjs_arg_get<cairo_pattern_t*>(arg));
    if (!obj)
        return false;

    value_p.setObject(*obj);
    return true;
}

static bool pattern_release_argument(JSContext*, GITransfer transfer,
                                     GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_pattern_destroy(gjs_arg_get<cairo_pattern_t*>(arg));
    return true;
}

void gjs_cairo_pattern_init(void) {
    static GjsForeignInfo foreign_info = {pattern_to_gi_argument,
                                          pattern_from_gi_argument,
                                          pattern_release_argument};
    gjs_struct_foreign_register("cairo", "Pattern", &foreign_info);
}
