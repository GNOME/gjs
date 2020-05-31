/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 Red Hat, Inc.
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

#include <cairo.h>
#include <glib.h>

#include <js/Class.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_GetClass, JS_GetInstancePrivate

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

GJS_USE
static JSObject *gjs_cairo_path_get_proto(JSContext *);

GJS_DEFINE_PROTO_ABSTRACT("Path", cairo_path, JSCLASS_BACKGROUND_FINALIZE)

static void gjs_cairo_path_finalize(JSFreeOp*, JSObject* obj) {
    using AutoCairoPath =
        GjsAutoPointer<cairo_path_t, cairo_path_t, cairo_path_destroy>;
    AutoCairoPath path = static_cast<cairo_path_t*>(JS_GetPrivate(obj));
    JS_SetPrivate(obj, nullptr);
}

/* Properties */
JSPropertySpec gjs_cairo_path_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_cairo_path_proto_funcs[] = {
    JS_FS_END
};

JSFunctionSpec gjs_cairo_path_static_funcs[] = { JS_FS_END };

/**
 * gjs_cairo_path_from_path:
 * @context: the context
 * @path: cairo_path_t to attach to the object
 *
 * Constructs a pattern wrapper given cairo pattern.
 * NOTE: This function takes ownership of the path.
 */
JSObject *
gjs_cairo_path_from_path(JSContext    *context,
                         cairo_path_t *path)
{
    g_return_val_if_fail(context, nullptr);
    g_return_val_if_fail(path, nullptr);

    JS::RootedObject proto(context, gjs_cairo_path_get_proto(context));
    JS::RootedObject object(context,
        JS_NewObjectWithGivenProto(context, &gjs_cairo_path_class, proto));
    if (!object) {
        gjs_throw(context, "failed to create path");
        return nullptr;
    }

    g_assert(!JS_GetPrivate(object));
    JS_SetPrivate(object, path);

    return object;
}

/**
 * gjs_cairo_path_get_path:
 * @cx: the context
 * @path_wrapper: path wrapper
 *
 * Returns: the path attached to the wrapper.
 */
cairo_path_t* gjs_cairo_path_get_path(JSContext* cx,
                                      JS::HandleObject path_wrapper) {
    g_return_val_if_fail(cx, nullptr);
    g_return_val_if_fail(path_wrapper, nullptr);

    auto* path = static_cast<cairo_path_t*>(JS_GetInstancePrivate(
        cx, path_wrapper, &gjs_cairo_path_class, nullptr));
    if (!path) {
        gjs_throw(cx, "Expected Cairo.Path but got %s",
                  JS_GetClass(path_wrapper)->name);
        return nullptr;
    }

    return path;
}
