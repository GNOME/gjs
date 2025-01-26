/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_VALUE_H_
#define GI_VALUE_H_

#include <config.h>

#include <stdint.h>

#include <sstream>  // for ostringstream
#include <string>   // for string
#include <utility>  // for move, swap
#include <vector>   // for vector

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>    // for FALSE, g_clear_pointer, g_free, g_variant_...

#include <js/TypeDecls.h>

#include "gi/utils-inl.h"
#include "gjs/auto.h"
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

/* This is based on what GMarshalling does, it is an unsupported API but
 * gjs can be considered a glib implementation for JS, so it is fine
 * to do this, but we need to be in sync with gmarshal.c in GLib.
 * https://gitlab.gnome.org/GNOME/glib/-/blob/main/gobject/gmarshal.c
 */

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
inline constexpr T gvalue_get(const GValue* gvalue) {
    if constexpr (std::is_same_v<T, gboolean> && TAG == GI_TYPE_TAG_BOOLEAN)
        return gvalue->data[0].v_int != FALSE;
    else if constexpr (std::is_same_v<T, bool>)
        return gvalue->data[0].v_int != FALSE;
    else if constexpr (std::is_same_v<T, char>)
        return gvalue->data[0].v_int;
    else if constexpr (std::is_same_v<T, signed char>)
        return gvalue->data[0].v_int;
    else if constexpr (std::is_same_v<T, guchar>)
        return gvalue->data[0].v_uint;
    else if constexpr (std::is_same_v<T, int>)
        return gvalue->data[0].v_int;
    else if constexpr (std::is_same_v<T, unsigned int>)
        return gvalue->data[0].v_uint;
    else if constexpr (std::is_same_v<T, long>)  // NOLINT(runtime/int)
        return gvalue->data[0].v_long;
    else if constexpr (std::is_same_v<T, unsigned long>)  // NOLINT(runtime/int)
        return gvalue->data[0].v_ulong;
    else if constexpr (std::is_same_v<T, int64_t>)
        return gvalue->data[0].v_int64;
    else if constexpr (std::is_same_v<T, uint64_t>)
        return gvalue->data[0].v_uint64;
    else if constexpr (std::is_enum_v<T> && TAG == GI_TYPE_TAG_INT32)
        return gvalue->data[0].v_long;
    else if constexpr (std::is_enum_v<T> && TAG == GI_TYPE_TAG_UINT64)
        return gvalue->data[0].v_ulong;
    else if constexpr (std::is_same_v<T, float>)
        return gvalue->data[0].v_float;
    else if constexpr (std::is_same_v<T, double>)
        return gvalue->data[0].v_double;
    else if constexpr (std::is_same_v<T, GType> && TAG == GI_TYPE_TAG_GTYPE)
        return gjs_pointer_to_int<GType>(gvalue->data[0].v_pointer);
    else if constexpr (!std::is_pointer_v<T>)
        static_assert(std::is_pointer_v<T>, "Scalar type not properly handled");
    else
        return static_cast<T>(gvalue->data[0].v_pointer);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
void gvalue_set(GValue* gvalue, T value) {
    if constexpr (std::is_same_v<T, gboolean> && TAG == GI_TYPE_TAG_BOOLEAN)
        gvalue->data[0].v_int = value != FALSE;
    else if constexpr (std::is_same_v<T, bool>)
        gvalue->data[0].v_int = value != false;
    else if constexpr (std::is_same_v<T, char>)
        gvalue->data[0].v_int = value;
    else if constexpr (std::is_same_v<T, signed char>)
        gvalue->data[0].v_int = value;
    else if constexpr (std::is_same_v<T, guchar>)
        gvalue->data[0].v_uint = value;
    else if constexpr (std::is_same_v<T, int>)
        gvalue->data[0].v_int = value;
    else if constexpr (std::is_same_v<T, unsigned int>)
        gvalue->data[0].v_uint = value;
    else if constexpr (std::is_same_v<T, long>)  // NOLINT(runtime/int)
        gvalue->data[0].v_long = value;
    else if constexpr (std::is_same_v<T, unsigned long>)  // NOLINT(runtime/int)
        gvalue->data[0].v_ulong = value;
    else if constexpr (std::is_same_v<T, int64_t>)
        gvalue->data[0].v_int64 = value;
    else if constexpr (std::is_same_v<T, uint64_t>)
        gvalue->data[0].v_uint64 = value;
    else if constexpr (std::is_enum_v<T> && TAG == GI_TYPE_TAG_INT32)
        gvalue->data[0].v_long = value;
    else if constexpr (std::is_enum_v<T> && TAG == GI_TYPE_TAG_UINT64)
        gvalue->data[0].v_ulong = value;
    else if constexpr (std::is_same_v<T, float>)
        gvalue->data[0].v_float = value;
    else if constexpr (std::is_same_v<T, double>)
        gvalue->data[0].v_double = value;
    else if constexpr (std::is_same_v<T, GType> && TAG == GI_TYPE_TAG_GTYPE)
        gvalue->data[0].v_pointer = gjs_int_to_pointer(value);
    else
        static_assert(std::is_pointer_v<T>, "Scalar type not properly handled");

    if constexpr (std::is_same_v<T, char*>) {
        g_clear_pointer(&gvalue->data[0].v_pointer, g_free);
        gvalue->data[0].v_pointer = g_strdup(value);
    } else if constexpr (std::is_same_v<T, GObject*>) {
        g_set_object(&gvalue->data[0].v_pointer, value);
    } else if constexpr (std::is_same_v<T, GVariant*>) {
        g_clear_pointer(reinterpret_cast<T*>(&gvalue->data[0].v_pointer),
                        g_variant_unref);
        gvalue->data[0].v_pointer = value ? g_variant_ref(value) : nullptr;
    } else {
        static_assert(!std::is_pointer_v<T>, "Not a known pointer type");
    }
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
void gvalue_take(GValue* gvalue, T value) {
    if constexpr (!std::is_pointer_v<T>) {
        return gvalue_set<T, TAG>(gvalue, value);
    }

    if constexpr (std::is_same_v<T, char*>) {
        g_clear_pointer(&gvalue->data[0].v_pointer, g_free);
        gvalue->data[0].v_pointer = g_steal_pointer(&value);
    } else if constexpr (std::is_same_v<T, GObject*>) {
        g_clear_object(&gvalue->data[0].v_pointer);
        gvalue->data[0].v_pointer = g_steal_pointer(&value);
    } else if constexpr (std::is_same_v<T, GVariant*>) {
        g_clear_pointer(reinterpret_cast<T*>(&gvalue->data[0].v_pointer),
                        g_variant_unref);
        gvalue->data[0].v_pointer = value;
    } else {
        static_assert(!std::is_pointer_v<T>, "Not a known pointer type");
    }
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
std::string gvalue_to_string(GValue* gvalue) {
    auto str =
        std::string("GValue of type ") + G_VALUE_TYPE_NAME(gvalue) + ": ";

    if constexpr (std::is_same_v<T, char*>) {
        str += std::string("\"") + Gjs::gvalue_get<T, TAG>(gvalue) + '"';
    } else if constexpr (std::is_same_v<T, GVariant*>) {
        AutoChar variant{
            g_variant_print(Gjs::gvalue_get<T, TAG>(gvalue), true)};
        str += std::string("<") + variant.get() + '>';
    } else if constexpr (std::is_arithmetic_v<T>) {
        str += std::to_string(Gjs::gvalue_get<T, TAG>(gvalue));
    } else {
        std::ostringstream out;
        out << Gjs::gvalue_get<T, TAG>(gvalue);
        str += out.str();
    }
    return str;
}

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
