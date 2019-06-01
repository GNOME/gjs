/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017  Philip Chimento <philip.chimento@gmail.com>
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

#include <glib.h>

#include "gjs/macros.h"
#include "jsapi-wrapper.h"

G_BEGIN_DECLS

typedef enum {
    GJS_GLOBAL_SLOT_IMPORTS,
    GJS_GLOBAL_SLOT_PROTOTYPE_gtype,
    GJS_GLOBAL_SLOT_PROTOTYPE_function,
    GJS_GLOBAL_SLOT_PROTOTYPE_ns,
    GJS_GLOBAL_SLOT_PROTOTYPE_repo,
    GJS_GLOBAL_SLOT_PROTOTYPE_byte_array,
    GJS_GLOBAL_SLOT_PROTOTYPE_importer,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_context,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_gradient,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_image_surface,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_linear_gradient,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_path,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_pattern,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_pdf_surface,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_ps_surface,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_radial_gradient,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_region,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_solid_pattern,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_surface,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_surface_pattern,
    GJS_GLOBAL_SLOT_PROTOTYPE_cairo_svg_surface,
    GJS_GLOBAL_SLOT_LAST,
} GjsGlobalSlot;

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_create_global_object(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_global_properties(JSContext       *cx,
                                  JS::HandleObject global,
                                  const char      *bootstrap_script);

void gjs_set_global_slot(JSContext    *context,
                         GjsGlobalSlot slot,
                         JS::Value     value);

G_END_DECLS

JS::Value gjs_get_global_slot(JSContext* cx, GjsGlobalSlot slot);

#endif  // GJS_GLOBAL_H_
