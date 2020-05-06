/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#ifndef MODULES_CAIRO_PRIVATE_H_
#define MODULES_CAIRO_PRIVATE_H_

#include <config.h>

#include <cairo-features.h>  // for CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFACE
#include <cairo-gobject.h>
#include <cairo.h>
#include <glib-object.h>

#include <js/PropertySpec.h>
#include <js/TypeDecls.h>

#include "gi/cwrapper.h"
#include "gi/wrapperutils.h"
#include "gjs/global.h"
#include "gjs/macros.h"
#include "util/log.h"

namespace JS {
class CallArgs;
}
namespace js {
struct ClassSpec;
}
struct JSClass;

GJS_JSAPI_RETURN_CONVENTION
bool             gjs_cairo_check_status                 (JSContext       *context,
                                                         cairo_status_t   status,
                                                         const char      *name);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_region_define_proto(JSContext              *cx,
                                   JS::HandleObject        module,
                                   JS::MutableHandleObject proto);

void gjs_cairo_region_init(void);

class CairoContext : public CWrapper<CairoContext, cairo_t> {
    friend CWrapperPointerOps<CairoContext, cairo_t>;
    friend CWrapper<CairoContext, cairo_t>;

    CairoContext() = delete;
    CairoContext(CairoContext&) = delete;
    CairoContext(CairoContext&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_context;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 1;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_CONTEXT; }

    static cairo_t* copy_ptr(cairo_t* cr) { return cairo_reference(cr); }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_t* constructor_impl(JSContext* cx, const JS::CallArgs& args);

    static void finalize_impl(JSFreeOp* fop, cairo_t* cr);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoContext::proto_funcs,
        CairoContext::proto_props,
        CairoContext::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "Context", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoContext::class_ops, &CairoContext::class_spec};
};

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
[[nodiscard]] JSObject* gjs_cairo_surface_get_proto(JSContext* cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_surface_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void gjs_cairo_surface_construct(JSObject* object, cairo_surface_t* surface);
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
[[nodiscard]] JSObject* gjs_cairo_pattern_get_proto(JSContext* cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_cairo_pattern_define_proto(JSContext              *cx,
                                    JS::HandleObject        module,
                                    JS::MutableHandleObject proto);

void gjs_cairo_pattern_construct(JSObject* object, cairo_pattern_t* pattern);
void             gjs_cairo_pattern_finalize_pattern     (JSFreeOp        *fop,
                                                         JSObject        *object);
GJS_JSAPI_RETURN_CONVENTION
JSObject*        gjs_cairo_pattern_from_pattern         (JSContext       *context,
                                                         cairo_pattern_t *pattern);
GJS_JSAPI_RETURN_CONVENTION
cairo_pattern_t* gjs_cairo_pattern_get_pattern(
    JSContext* cx, JS::HandleObject pattern_wrapper);

/* gradient */
[[nodiscard]] JSObject* gjs_cairo_gradient_get_proto(JSContext* cx);

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
