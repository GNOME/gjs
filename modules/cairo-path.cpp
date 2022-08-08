/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 Red Hat, Inc.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cairo.h>

#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto

#include "modules/cairo-private.h"

// clang-format off
const JSPropertySpec CairoPath::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Path", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/*
 * CairoPath::take_c_ptr():
 * Same as CWrapper::from_c_ptr(), but always takes ownership of the pointer
 * rather than copying it. It's not possible to copy a cairo_path_t*.
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
