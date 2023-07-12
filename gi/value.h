/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_VALUE_H_
#define GI_VALUE_H_

#include <config.h>

#include <utility>  // for move, swap
#include <vector>   // for vector

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

namespace Gjs {
struct AutoGValue : GValue {
    AutoGValue() : GValue(G_VALUE_INIT) {
        static_assert(sizeof(AutoGValue) == sizeof(GValue));
    }
    explicit AutoGValue(GType gtype) : AutoGValue() {
        g_value_init(this, gtype);
    }
    AutoGValue(AutoGValue const& src) : AutoGValue(G_VALUE_TYPE(&src)) {
        g_value_copy(&src, this);
    }
    AutoGValue& operator=(AutoGValue other) {
        // We need to cast to GValue here not to make swap to recurse here
        std::swap(*static_cast<GValue*>(this), *static_cast<GValue*>(&other));
        return *this;
    }
    AutoGValue(AutoGValue&& src) {
        switch (G_VALUE_TYPE(&src)) {
            case G_TYPE_NONE:
            case G_TYPE_CHAR:
            case G_TYPE_UCHAR:
            case G_TYPE_BOOLEAN:
            case G_TYPE_INT:
            case G_TYPE_UINT:
            case G_TYPE_LONG:
            case G_TYPE_ULONG:
            case G_TYPE_INT64:
            case G_TYPE_UINT64:
            case G_TYPE_FLOAT:
            case G_TYPE_DOUBLE:
                *static_cast<GValue*>(this) =
                    std::move(static_cast<GValue const&&>(src));
                break;
            default:
                // We can't safely move in complex cases, so let's just copy
                this->steal();
                *this = src;
                g_value_unset(&src);
        }
    }
    void steal() { *static_cast<GValue*>(this) = G_VALUE_INIT; }
    ~AutoGValue() { g_value_unset(this); }
};
}  // namespace Gjs

using AutoGValueVector = std::vector<Gjs::AutoGValue>;

GJS_JSAPI_RETURN_CONVENTION
bool       gjs_value_to_g_value         (JSContext      *context,
                                         JS::HandleValue value,
                                         GValue         *gvalue);
GJS_JSAPI_RETURN_CONVENTION
bool       gjs_value_to_g_value_no_copy (JSContext      *context,
                                         JS::HandleValue value,
                                         GValue         *gvalue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_g_value(JSContext             *context,
                            JS::MutableHandleValue value_p,
                            const GValue          *gvalue);


#endif  // GI_VALUE_H_
