/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

class JSTracer;

class ObjectBox {
    static void destroy(ObjectBox*);

 public:
    using Ptr = GjsAutoPointer<ObjectBox, ObjectBox, ObjectBox::destroy>;

    [[nodiscard]] static GType gtype();

    GJS_JSAPI_RETURN_CONVENTION
    static ObjectBox::Ptr boxed(JSContext*, JSObject*);

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* object_for_c_ptr(JSContext*, ObjectBox*);

    void trace(JSTracer* trc);

 private:
    explicit ObjectBox(JSObject*);

    static void* boxed_copy(void*);
    static void boxed_free(void*);

    struct impl;
    static void destroy_impl(impl*);
    GjsAutoPointer<impl, impl, destroy_impl> m_impl;
};
