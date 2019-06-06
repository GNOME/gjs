/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <memory>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

struct ObjectBox {
    using Ptr = std::unique_ptr<ObjectBox, void (*)(ObjectBox*)>;

    [[nodiscard]] static GType gtype();
    [[nodiscard]] static ObjectBox::Ptr boxed(JSContext*, JSObject*);

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* object_for_c_ptr(JSContext*, ObjectBox*);

 private:
    ObjectBox(JSContext*, JSObject*);

    struct impl;
    std::unique_ptr<impl> m_impl;
};
