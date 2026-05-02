/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Igalia, S.L.

#include <config.h>

#include <stdint.h>  // for int32_t, INT32_MAX

#include <cairo-features.h>
#include <cairo.h>

#include <js/MemoryFunctions.h>  // for AddAssociatedMemory, RemoveAs...
#include <js/TypeDecls.h>
#include <mozilla/CheckedInt.h>

#include "gjs/mem-private.h"
#include "modules/cairo-memory.h"
#include "modules/cairo-xlib-facade.h"

#ifdef CAIRO_HAS_IMAGE_SURFACE
int32_t estimate_size_of_image_surface(cairo_surface_t* surface) {
    int stride = cairo_image_surface_get_stride(surface);
    int height = cairo_image_surface_get_height(surface);
    mozilla::CheckedInt32 result = mozilla::CheckedInt32{stride} * height;
    if (!result.isValid())
        return INT32_MAX;
    return result.value();
}
#endif

#ifdef CAIRO_HAS_XLIB_SURFACE
int32_t estimate_size_of_xlib_surface(cairo_surface_t* surface) {
    int width = cairo_xlib_surface_get_width(surface);
    int height = cairo_xlib_surface_get_height(surface);
    int depth = cairo_xlib_surface_get_depth(surface);
    mozilla::CheckedInt32 result =
        mozilla::CheckedInt32{width} * height * depth;
    if (!result.isValid())
        return INT32_MAX;
    return result.value();
}
#endif

int32_t add_associated_memory_for_surface(JSObject* obj,
                                          cairo_surface_t* surface) {
    // For now, only consider the size of the surface,
    // estimated from its width and height.
    int32_t estimated_size = 0;
    switch (cairo_surface_get_type(surface)) {
#ifdef CAIRO_HAS_IMAGE_SURFACE
        case CAIRO_SURFACE_TYPE_IMAGE: {
            estimated_size = estimate_size_of_image_surface(surface);
            JS::AddAssociatedMemory(obj, estimated_size, MemoryUse::Cairo);
            break;
        }
#endif
#ifdef CAIRO_HAS_XLIB_SURFACE
        case CAIRO_SURFACE_TYPE_XLIB: {
            estimated_size = estimate_size_of_xlib_surface(surface);
            JS::AddAssociatedMemory(obj, estimated_size, MemoryUse::Cairo);
            break;
        }
#endif
        // Other backends not handled yet
        default:
            break;
    }
    return estimated_size;
}

void remove_associated_memory_for_surface(JSObject* obj,
                                          cairo_surface_t* surface) {
    switch (cairo_surface_get_type(surface)) {
#ifdef CAIRO_HAS_IMAGE_SURFACE
        case CAIRO_SURFACE_TYPE_IMAGE: {
            int32_t estimated_size = estimate_size_of_image_surface(surface);
            JS::RemoveAssociatedMemory(obj, estimated_size, MemoryUse::Cairo);
            break;
        }
#endif
#ifdef CAIRO_HAS_XLIB_SURFACE
        case CAIRO_SURFACE_TYPE_XLIB: {
            int32_t estimated_size = estimate_size_of_xlib_surface(surface);
            JS::RemoveAssociatedMemory(obj, estimated_size, MemoryUse::Cairo);
            break;
        }
#endif
        // Other backends not handled yet
        default:
            break;
    }
}

void add_associated_memory_for_pattern(JSObject* obj,
                                       cairo_pattern_t* pattern) {
    cairo_surface_t* surface;
    cairo_status_t status = cairo_pattern_get_surface(pattern, &surface);
    if (status == CAIRO_STATUS_SUCCESS)
        add_associated_memory_for_surface(obj, surface);
}

void remove_associated_memory_for_pattern(JSObject* obj,
                                          cairo_pattern_t* pattern) {
    cairo_surface_t* surface;
    cairo_status_t status = cairo_pattern_get_surface(pattern, &surface);
    if (status == CAIRO_STATUS_SUCCESS)
        remove_associated_memory_for_surface(obj, surface);
}
