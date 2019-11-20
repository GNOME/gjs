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

#ifndef MODULES_CAIRO_PRIVATE_H_
#define MODULES_CAIRO_PRIVATE_H_

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFACE
#include <cairo.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool             gjs_cairo_check_status                 (JSContext       *context,
                                                         cairo_status_t   status,
                                                         const char      *name);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_region_define_proto(JSContext              *cx,
                                   JS::HandleObject        module,
                                   JS::MutableHandleObject proto);

void gjs_cairo_region_init(void);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_context_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

GJS_USE
cairo_t *        gjs_cairo_context_get_context          (JSContext       *context,
                                                         JS::HandleObject object);
GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_context_from_context         (JSContext       *context,
                                                         cairo_t         *cr);
void gjs_cairo_context_init(void);
void gjs_cairo_surface_init(void);

/* path */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_path_define_proto(JSContext              *cx,
                                 JS::HandleObject        module,
                                 JS::MutableHandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_path_from_path               (JSContext       *context,
                                                         cairo_path_t    *path);
GJS_JSAPI_RETURN_CONVENTION
cairo_path_t* gjs_cairo_path_get_path(JSContext* cx,
                                      JS::HandleObject path_wrapper);

/* surface */
GJS_USE
JSObject *gjs_cairo_surface_get_proto(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_surface_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void             gjs_cairo_surface_construct            (JSContext       *context,
                                                         JS::HandleObject object,
                                                         cairo_surface_t *surface);
void             gjs_cairo_surface_finalize_surface     (JSFreeOp        *fop,
                                                         JSObject        *object);
GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_surface_from_surface         (JSContext       *context,
                                                         cairo_surface_t *surface);
GJS_JSAPI_RETURN_CONVENTION
cairo_surface_t* gjs_cairo_surface_get_surface(
    JSContext* cx, JS::HandleObject surface_wrapper);

/* image surface */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_image_surface_define_proto(JSContext              *cx,
                                          JS::HandleObject        module,
                                          JS::MutableHandleObject proto);

void             gjs_cairo_image_surface_init           (JSContext       *context,
                                                         JS::HandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_image_surface_from_surface   (JSContext       *context,
                                                         cairo_surface_t *surface);

/* postscript surface */
#ifdef CAIRO_HAS_PS_SURFACE
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_ps_surface_define_proto(JSContext              *cx,
                                       JS::HandleObject        module,
                                       JS::MutableHandleObject proto);
#endif
GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_ps_surface_from_surface       (JSContext       *context,
                                                          cairo_surface_t *surface);

/* pdf surface */
#ifdef CAIRO_HAS_PDF_SURFACE
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_pdf_surface_define_proto(JSContext              *cx,
                                        JS::HandleObject        module,
                                        JS::MutableHandleObject proto);
#endif
GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_pdf_surface_from_surface     (JSContext       *context,
                                                         cairo_surface_t *surface);

/* svg surface */
#ifdef CAIRO_HAS_SVG_SURFACE
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_svg_surface_define_proto(JSContext              *cx,
                                        JS::HandleObject        module,
                                        JS::MutableHandleObject proto);
#endif
GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_svg_surface_from_surface     (JSContext       *context,
                                                         cairo_surface_t *surface);

/* pattern */
GJS_USE
JSObject *gjs_cairo_pattern_get_proto(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_pattern_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void             gjs_cairo_pattern_construct            (JSContext       *context,
                                                         JS::HandleObject object,
                                                         cairo_pattern_t *pattern);
void             gjs_cairo_pattern_finalize_pattern     (JSFreeOp        *fop,
                                                         JSObject        *object);
GJS_JSAPI_RETURN_CONVENTION
JSObject*        gjs_cairo_pattern_from_pattern         (JSContext       *context,
                                                         cairo_pattern_t *pattern);
GJS_JSAPI_RETURN_CONVENTION
cairo_pattern_t* gjs_cairo_pattern_get_pattern(
    JSContext* cx, JS::HandleObject pattern_wrapper);

/* gradient */
GJS_USE
JSObject *gjs_cairo_gradient_get_proto(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_gradient_define_proto(JSContext              *cx,
                                     JS::HandleObject        module,
                                     JS::MutableHandleObject proto);

/* linear gradient */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_linear_gradient_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_linear_gradient_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* radial gradient */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_radial_gradient_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_radial_gradient_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* surface pattern */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_surface_pattern_define_proto(JSContext              *cx,
                                            JS::HandleObject        module,
                                            JS::MutableHandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_surface_pattern_from_pattern (JSContext       *context,
                                                         cairo_pattern_t *pattern);

/* solid pattern */
GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_solid_pattern_define_proto(JSContext              *cx,
                                          JS::HandleObject        module,
                                          JS::MutableHandleObject proto);

GJS_JSAPI_RETURN_CONVENTION
JSObject *       gjs_cairo_solid_pattern_from_pattern   (JSContext       *context,
                                                         cairo_pattern_t *pattern);

#endif  // MODULES_CAIRO_PRIVATE_H_
