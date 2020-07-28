/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017  Philip Chimento <philip.chimento@gmail.com>
 * Copyright (c) 2020  Evan Welsh <contact@evanwelsh.com>
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

#ifndef GJS_GLOBAL_H_
#define GJS_GLOBAL_H_

#include <config.h>

#include <js/RootingAPI.h>  // for Handle
#include <js/TypeDecls.h>
#include <js/Value.h>

#include <stdint.h>

#include "gjs/macros.h"

enum class GjsGlobalType {
    DEFAULT,
    DEBUGGER,
};

enum class GjsBaseGlobalSlot : uint32_t {
    GLOBAL_TYPE = 0,
    LAST,
};

enum class GjsDebuggerGlobalSlot : uint32_t {
    LAST = static_cast<uint32_t>(GjsBaseGlobalSlot::LAST),
};

enum class GjsGlobalSlot : uint32_t {
    IMPORTS = static_cast<uint32_t>(GjsBaseGlobalSlot::LAST),
    PROTOTYPE_gtype,
    PROTOTYPE_importer,
    PROTOTYPE_function,
    PROTOTYPE_ns,
    PROTOTYPE_repo,
    PROTOTYPE_cairo_context,
    PROTOTYPE_cairo_gradient,
    PROTOTYPE_cairo_image_surface,
    PROTOTYPE_cairo_linear_gradient,
    PROTOTYPE_cairo_path,
    PROTOTYPE_cairo_pattern,
    PROTOTYPE_cairo_pdf_surface,
    PROTOTYPE_cairo_ps_surface,
    PROTOTYPE_cairo_radial_gradient,
    PROTOTYPE_cairo_region,
    PROTOTYPE_cairo_solid_pattern,
    PROTOTYPE_cairo_surface,
    PROTOTYPE_cairo_surface_pattern,
    PROTOTYPE_cairo_svg_surface,
    LAST,
};

GjsGlobalType gjs_global_get_type(JSContext* cx);
GjsGlobalType gjs_global_get_type(JSObject* global);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_global_object(JSContext* cx, GjsGlobalType global_type,
                                   JS::HandleObject existing_global = nullptr);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_global_properties(JSContext* cx, JS::HandleObject global,
                                  GjsGlobalType global_type,
                                  const char* realm_name,
                                  const char* bootstrap_script);

namespace detail {
void set_global_slot(JSObject* global, uint32_t slot, JS::Value value);
JS::Value get_global_slot(JSObject* global, uint32_t slot);
}  // namespace detail

template <typename Slot>
inline void gjs_set_global_slot(JSObject* global, Slot slot, JS::Value value) {
    static_assert(std::is_same_v<GjsBaseGlobalSlot, Slot> ||
                      std::is_same_v<GjsGlobalSlot, Slot> ||
                      std::is_same_v<GjsDebuggerGlobalSlot, Slot>,
                  "Must use a GJS global slot enum");
    detail::set_global_slot(global, static_cast<uint32_t>(slot), value);
}

template <typename Slot>
inline JS::Value gjs_get_global_slot(JSObject* global, Slot slot) {
    static_assert(std::is_same_v<GjsBaseGlobalSlot, Slot> ||
                      std::is_same_v<GjsGlobalSlot, Slot> ||
                      std::is_same_v<GjsDebuggerGlobalSlot, Slot>,
                  "Must use a GJS global slot enum");
    return detail::get_global_slot(global, static_cast<uint32_t>(slot));
}

#endif  // GJS_GLOBAL_H_
