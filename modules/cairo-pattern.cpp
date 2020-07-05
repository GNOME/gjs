/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 litl, LLC.
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
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_GetPrivate, JS_GetClass, ...

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE("Pattern", cairo_pattern,
                                     CAIRO_GOBJECT_TYPE_PATTERN,
                                     JSCLASS_BACKGROUND_FINALIZE)

static void gjs_cairo_pattern_finalize(JSFreeOp*, JSObject* obj) {
    using AutoPattern =
        GjsAutoPointer<cairo_pattern_t, cairo_pattern_t, cairo_pattern_destroy>;
    AutoPattern pattern = static_cast<cairo_pattern_t*>(JS_GetPrivate(obj));
    JS_SetPrivate(obj, nullptr);
}

/* Properties */
JSPropertySpec gjs_cairo_pattern_proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Pattern", JSPROP_READONLY), JS_PS_END};

/* Methods */

GJS_JSAPI_RETURN_CONVENTION
static bool
getType_func(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, obj);
    cairo_pattern_type_t type;

    if (argc > 1) {
        gjs_throw(context, "Pattern.getType() takes no arguments");
        return false;
    }

    cairo_pattern_t* pattern = gjs_cairo_pattern_get_pattern(context, obj);
    if (!pattern)
        return false;

    type = cairo_pattern_get_type(pattern);

    if (!gjs_cairo_check_status(context, cairo_pattern_status(pattern), "pattern"))
        return false;

    rec.rval().setInt32(type);
    return true;
}

JSFunctionSpec gjs_cairo_pattern_proto_funcs[] = {
    // getMatrix
    JS_FN("getType", getType_func, 0, 0),
    // setMatrix
    JS_FS_END};

JSFunctionSpec gjs_cairo_pattern_static_funcs[] = { JS_FS_END };

/* Public API */

/**
 * gjs_cairo_pattern_construct:
 * @object: object to construct
 * @pattern: cairo_pattern to attach to the object
 *
 * Constructs a pattern wrapper giving an empty JSObject and a
 * cairo pattern. A reference to @pattern will be taken.
 *
 * This is mainly used for subclasses where object is already created.
 */
void gjs_cairo_pattern_construct(JSObject* object, cairo_pattern_t* pattern) {
    g_return_if_fail(object);
    g_return_if_fail(pattern);

    g_assert(!JS_GetPrivate(object));
    JS_SetPrivate(object, cairo_pattern_reference(pattern));
}

/**
 * gjs_cairo_pattern_finalize:
 * @fop: the free op
 * @object: object to finalize
 *
 * Destroys the resources associated with a pattern wrapper.
 *
 * This is mainly used for subclasses.
 */

void
gjs_cairo_pattern_finalize_pattern(JSFreeOp *fop,
                                   JSObject *object)
{
    g_return_if_fail(fop);
    g_return_if_fail(object);

    gjs_cairo_pattern_finalize(fop, object);
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
            return gjs_cairo_solid_pattern_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_SURFACE:
            return gjs_cairo_surface_pattern_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_LINEAR:
            return gjs_cairo_linear_gradient_from_pattern(context, pattern);
        case CAIRO_PATTERN_TYPE_RADIAL:
            return gjs_cairo_radial_gradient_from_pattern(context, pattern);
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
 * gjs_cairo_pattern_get_pattern:
 * @cx: the context
 * @pattern_wrapper: pattern wrapper
 *
 * Returns: the pattern attached to the wrapper.
 */
cairo_pattern_t* gjs_cairo_pattern_get_pattern(
    JSContext* cx, JS::HandleObject pattern_wrapper) {
    g_return_val_if_fail(cx, nullptr);
    g_return_val_if_fail(pattern_wrapper, nullptr);

    JS::RootedObject proto(cx, gjs_cairo_pattern_get_proto(cx));

    bool is_pattern_subclass = false;
    if (!gjs_object_in_prototype_chain(cx, proto, pattern_wrapper,
                                       &is_pattern_subclass))
        return nullptr;
    if (!is_pattern_subclass) {
        gjs_throw(cx, "Expected Cairo.Pattern but got %s",
                  JS_GetClass(pattern_wrapper)->name);
        return nullptr;
    }

    return static_cast<cairo_pattern_t*>(JS_GetPrivate(pattern_wrapper));
}
