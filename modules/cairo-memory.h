/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Igalia, S.L.

#pragma once

#include <config.h>

#include <stddef.h>

#include <cairo.h>

#include <js/TypeDecls.h>

#ifdef CAIRO_HAS_IMAGE_SURFACE
extern int32_t estimate_size_of_image_surface(cairo_surface_t* surface);
#endif

#ifdef CAIRO_HAS_XLIB_SURFACE
extern int32_t estimate_size_of_xlib_surface(cairo_surface_t* surface);
#endif

extern int32_t add_associated_memory_for_surface(JSObject* obj,
                                                 cairo_surface_t* surface);
extern void remove_associated_memory_for_surface(JSObject* obj,
                                                 cairo_surface_t* surface);
extern void add_associated_memory_for_pattern(JSObject* obj,
                                              cairo_pattern_t* pattern);
extern void remove_associated_memory_for_pattern(JSObject* obj,
                                                 cairo_pattern_t* pattern);
