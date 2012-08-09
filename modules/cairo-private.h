/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 litl, LLC.
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

#ifndef __CAIRO_PRIVATE_H__
#define __CAIRO_PRIVATE_H__

#include "cairo-module.h"
#include <cairo.h>

JSBool           gjs_cairo_check_status                 (JSContext       *context,
                                                         cairo_status_t   status,
                                                         const char      *name);

jsval            gjs_cairo_region_create_proto          (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
void             gjs_cairo_region_init                  (JSContext       *context);


jsval            gjs_cairo_context_create_proto         (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
cairo_t *        gjs_cairo_context_get_context          (JSContext       *context,
                                                         JSObject        *object);
JSObject *       gjs_cairo_context_from_context         (JSContext       *context,
                                                         cairo_t         *cr);
void             gjs_cairo_context_init                 (JSContext       *context);
void             gjs_cairo_surface_init                 (JSContext       *context);


/* cairo_path_t */
jsval            gjs_cairo_path_create_proto            (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
JSObject *       gjs_cairo_path_from_path               (JSContext       *context,
                                                         cairo_path_t    *path);
cairo_path_t *   gjs_cairo_path_get_path                (JSContext       *context,
                                                         JSObject        *path_wrapper);

/* surface */
jsval            gjs_cairo_surface_create_proto         (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
void             gjs_cairo_surface_construct            (JSContext       *context,
                                                         JSObject        *object,
                                                         cairo_surface_t *surface);
void             gjs_cairo_surface_finalize_surface     (JSFreeOp        *fop,
                                                         JSObject        *object);
JSObject *       gjs_cairo_surface_from_surface         (JSContext       *context,
                                                         cairo_surface_t *surface);
cairo_surface_t* gjs_cairo_surface_get_surface          (JSContext       *context,
                                                         JSObject        *object);

/* image surface */
jsval            gjs_cairo_image_surface_create_proto   (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
void             gjs_cairo_image_surface_init           (JSContext       *context,
                                                         JSObject        *object);
JSObject *       gjs_cairo_image_surface_from_surface   (JSContext       *context,
                                                         cairo_surface_t *surface);

/* postscript surface */
#ifdef CAIRO_HAS_PS_SURFACE
jsval            gjs_cairo_ps_surface_create_proto      (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
#endif
JSObject *       gjs_cairo_ps_surface_from_surface       (JSContext       *context,
                                                          cairo_surface_t *surface);

/* pdf surface */
#ifdef CAIRO_HAS_PDF_SURFACE
jsval            gjs_cairo_pdf_surface_create_proto     (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
#endif
JSObject *       gjs_cairo_pdf_surface_from_surface     (JSContext       *context,
                                                         cairo_surface_t *surface);

/* svg surface */
#ifdef CAIRO_HAS_SVG_SURFACE
jsval            gjs_cairo_svg_surface_create_proto     (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
#endif
JSObject *       gjs_cairo_svg_surface_from_surface     (JSContext       *context,
                                                         cairo_surface_t *surface);

/* pattern */
jsval            gjs_cairo_pattern_create_proto         (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
void             gjs_cairo_pattern_construct            (JSContext       *context,
                                                         JSObject        *object,
                                                         cairo_pattern_t *pattern);
void             gjs_cairo_pattern_finalize_pattern     (JSFreeOp        *fop,
                                                         JSObject        *object);
JSObject*        gjs_cairo_pattern_from_pattern         (JSContext       *context,
                                                         cairo_pattern_t *pattern);
cairo_pattern_t* gjs_cairo_pattern_get_pattern          (JSContext       *context,
                                                         JSObject        *object);

/* gradient */
jsval            gjs_cairo_gradient_create_proto        (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);

/* linear gradient */
jsval            gjs_cairo_linear_gradient_create_proto (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
JSObject *       gjs_cairo_linear_gradient_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* radial gradient */
jsval            gjs_cairo_radial_gradient_create_proto (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
JSObject *       gjs_cairo_radial_gradient_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* surface pattern */
jsval            gjs_cairo_surface_pattern_create_proto (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
JSObject *       gjs_cairo_surface_pattern_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* solid pattern */
jsval            gjs_cairo_solid_pattern_create_proto   (JSContext       *context,
                                                         JSObject        *module,
                                                         const char      *proto_name,
                                                         JSObject        *parent);
JSObject *       gjs_cairo_solid_pattern_from_pattern   (JSContext       *context,
                                                         cairo_pattern_t *pattern);

#endif /* __CAIRO_PRIVATE_H__ */

