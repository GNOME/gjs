/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#ifndef GJS_GLOBAL_H_
#define GJS_GLOBAL_H_

#include <config.h>

#include <stdint.h>

#include <type_traits>

#include <js/RootingAPI.h>  // for Handle
#include <js/TypeDecls.h>
#include <js/Value.h>

#include "gjs/macros.h"

namespace JS {
struct PropertyKey;
}

enum class GjsGlobalType {
    DEFAULT,
    DEBUGGER,
    INTERNAL,
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
    // Stores an object with methods to resolve and load modules
    MODULE_LOADER,
    // Stores the module registry (a Map object)
    MODULE_REGISTRY,
    // Stores the source map registry (a Map object)
    SOURCE_MAP_REGISTRY,
    NATIVE_REGISTRY,
    // prettyPrint() function defined in JS but used internally in C++
    PRETTY_PRINT_FUNC,
    PROTOTYPE_gtype,
    PROTOTYPE_importer,
    PROTOTYPE_function,
    PROTOTYPE_ns,
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

enum class GjsInternalGlobalSlot : uint32_t {
    LAST = static_cast<uint32_t>(GjsGlobalSlot::LAST),
};

bool gjs_global_is_type(JSContext* cx, GjsGlobalType type);
GjsGlobalType gjs_global_get_type(JSContext* cx);
GjsGlobalType gjs_global_get_type(JSObject* global);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_global_registry_set(JSContext* cx, JS::HandleObject registry,
                             JS::PropertyKey key, JS::HandleObject value);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_global_registry_get(JSContext* cx, JS::HandleObject registry,
                             JS::PropertyKey key,
                             JS::MutableHandleObject value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_global_source_map_get(JSContext* cx, JS::HandleObject registry,
                               JS::HandleString key,
                               JS::MutableHandleObject value);

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
                      std::is_same_v<GjsInternalGlobalSlot, Slot> ||
                      std::is_same_v<GjsDebuggerGlobalSlot, Slot>,
                  "Must use a GJS global slot enum");
    detail::set_global_slot(global, static_cast<uint32_t>(slot), value);
}

template <typename Slot>
inline JS::Value gjs_get_global_slot(JSObject* global, Slot slot) {
    static_assert(std::is_same_v<GjsBaseGlobalSlot, Slot> ||
                      std::is_same_v<GjsGlobalSlot, Slot> ||
                      std::is_same_v<GjsInternalGlobalSlot, Slot> ||
                      std::is_same_v<GjsDebuggerGlobalSlot, Slot>,
                  "Must use a GJS global slot enum");
    return detail::get_global_slot(global, static_cast<uint32_t>(slot));
}

#endif  // GJS_GLOBAL_H_
