/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

bool             gjs_cairo_check_status(JSContext       *context,
                                        cairo_status_t   status,
                                        const char      *name);

bool gjs_cairo_region_define_proto(JSContext              *cx,
                                   JS::HandleObject        module,
                                   JS::MutableHandleObject proto);

void             gjs_cairo_region_init(JSContext       *context);

bool gjs_cairo_context_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

cairo_t *        gjs_cairo_context_get_context(JSContext       *context,
                                               JS::HandleObject object);
JSObject *       gjs_cairo_context_from_context(JSContext       *context,
                                                cairo_t         *cr);
void             gjs_cairo_context_init(JSContext       *context);
void             gjs_cairo_surface_init(JSContext       *context);

/* path */
bool gjs_cairo_path_define_proto(JSContext              *cx,
                                 JS::HandleObject        module,
                                 JS::MutableHandleObject proto);

JSObject *       gjs_cairo_path_from_path(JSContext       *context,
                                          cairo_path_t    *path);
cairo_path_t *   gjs_cairo_path_get_path(JSContext       *context,
                                         JSObject        *path_wrapper);

/* surface */
JSObject *gjs_cairo_surface_get_proto(JSContext *cx);

bool gjs_cairo_surface_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void             gjs_cairo_surface_construct(JSContext       *context,
                                             JS::HandleObject object,
                                             cairo_surface_t *surface);
void             gjs_cairo_surface_finalize_surface(JSFreeOp        *fop,
                                                    JSObject        *object);
JSObject *       gjs_cairo_surface_from_surface(JSContext       *context,
                                                cairo_surface_t *surface);
cairo_surface_t* gjs_cairo_surface_get_surface(JSContext       *context,
                                               JSObject        *object);

/* image surface */
bool gjs_cairo_image_surface_define_proto(JSContext              *cx,
                                          JS::HandleObject        module,
                                          JS::MutableHandleObject proto);

void             gjs_cairo_image_surface_init(JSContext       *context,
                                              JS::HandleObject proto);

JSObject *       gjs_cairo_image_surface_from_surface(JSContext       *context,
                                                      cairo_surface_t *surface);

/* postscript surface */
#ifdef CAIRO_HAS_PS_SURFACE
bool gjs_cairo_ps_surface_define_proto(JSContext              *cx,
                                       JS::HandleObject        module,
                                       JS::MutableHandleObject proto);
#endif
JSObject *       gjs_cairo_ps_surface_from_surface(JSContext       *context,
                                                   cairo_surface_t *surface);

/* pdf surface */
#ifdef CAIRO_HAS_PDF_SURFACE
bool gjs_cairo_pdf_surface_define_proto(JSContext              *cx,
                                        JS::HandleObject        module,
                                        JS::MutableHandleObject proto);
#endif
JSObject *       gjs_cairo_pdf_surface_from_surface(JSContext       *context,
                                                    cairo_surface_t *surface);

/* svg surface */
#ifdef CAIRO_HAS_SVG_SURFACE
bool gjs_cairo_svg_surface_define_proto(JSContext              *cx,
                                        JS::HandleObject        module,
                                        JS::MutableHandleObject proto);
#endif
JSObject *       gjs_cairo_svg_surface_from_surface(JSContext       *context,
                                                    cairo_surface_t *surface);

/* pattern */
JSObject *gjs_cairo_pattern_get_proto(JSContext *cx);

bool gjs_cairo_pattern_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void             gjs_cairo_pattern_construct(JSContext       *context,
                                             JS::HandleObject object,
                                             cairo_pattern_t *pattern);
void             gjs_cairo_pattern_finalize_pattern(JSFreeOp        *fop,
                                                    JSObject        *object);
JSObject*        gjs_cairo_pattern_from_pattern(JSContext       *context,
                                                cairo_pattern_t *pattern);
cairo_pattern_t* gjs_cairo_pattern_get_pattern(JSContext       *context,
                                               JSObject        *object);

/* gradient */
JSObject *gjs_cairo_gradient_get_proto(JSContext *cx);

bool gjs_cairo_gradient_define_proto(JSContext              *cx,
                                     JS::HandleObject        module,
                                     JS::MutableHandleObject proto);

/* linear gradient */
bool gjs_cairo_linear_gradient_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

JSObject *       gjs_cairo_linear_gradient_from_pattern(JSContext       *context,
                                                        cairo_pattern_t *pattern);

/* radial gradient */
bool gjs_cairo_radial_gradient_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

JSObject *       gjs_cairo_radial_gradient_from_pattern(JSContext       *context,
                                                        cairo_pattern_t *pattern);

/* surface pattern */
bool gjs_cairo_surface_pattern_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

JSObject *       gjs_cairo_surface_pattern_from_pattern(JSContext       *context,
                                                        cairo_pattern_t *pattern);

/* solid pattern */
bool gjs_cairo_solid_pattern_define_proto(JSContext              *cx,
                                          JS::HandleObject        module,
                                          JS::MutableHandleObject proto);

JSObject *       gjs_cairo_solid_pattern_from_pattern(JSContext       *context,
                                                      cairo_pattern_t *pattern);

#endif /* __CAIRO_PRIVATE_H__ */

