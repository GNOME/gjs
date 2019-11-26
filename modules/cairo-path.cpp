/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 Red Hat, Inc.

#include <config.h>

#include <cairo.h>
#include <glib.h>

#include <js/Class.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_GetClass, JS_GetInstancePrivate

#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"

[[nodiscard]] static JSObject* gjs_cairo_path_get_proto(JSContext*);

struct GjsCairoPath
    : GjsAutoPointer<cairo_path_t, cairo_path_t, cairo_path_destroy> {
    explicit GjsCairoPath(cairo_path_t* path) : GjsAutoPointer(path) {}
};

GJS_DEFINE_PROTO_ABSTRACT("Path", cairo_path, JSCLASS_BACKGROUND_FINALIZE)
GJS_DEFINE_PRIV_FROM_JS(GjsCairoPath, gjs_cairo_path_class);

static void gjs_cairo_path_finalize(JSFreeOp*, JSObject* obj) {
    delete static_cast<GjsCairoPath*>(JS_GetPrivate(obj));
    JS_SetPrivate(obj, nullptr);
}

/* Properties */
// clang-format off
JSPropertySpec gjs_cairo_path_proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Path", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

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

    g_assert(!priv_from_js(context, object));
    JS_SetPrivate(object, new GjsCairoPath(path));

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

    GjsCairoPath* priv;
    if (!priv_from_js_with_typecheck(cx, path_wrapper, &priv)) {
        gjs_throw(cx, "Expected Cairo.Path but got %s",
                  JS_GetClass(path_wrapper)->name);
        return nullptr;
    }

    return priv->get();
}
