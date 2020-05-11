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

#include <js/Class.h>
#include <js/PropertySpec.h>
#include <js/TypeDecls.h>
#include <jspubtd.h>  // for JSProtoKey

#include "gi/cwrapper.h"
#include "gjs/global.h"
#include "gjs/macros.h"
#include "util/log.h"

namespace JS {
class CallArgs;
}

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

class CairoPattern : public CWrapper<CairoPattern, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoPattern, cairo_pattern_t>;
    friend CWrapper<CairoPattern, cairo_pattern_t>;
    friend class CairoGradient;  // "inherits" from CairoPattern
    friend class CairoLinearGradient;
    friend class CairoRadialGradient;
    friend class CairoSurfacePattern;
    friend class CairoSolidPattern;

    CairoPattern() = delete;
    CairoPattern(CairoPattern&) = delete;
    CairoPattern(CairoPattern&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_pattern;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        &CairoPattern::create_abstract_constructor,
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoPattern::proto_funcs,
        CairoPattern::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "Pattern", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoPattern::class_spec};

    static GType gtype() { return CAIRO_GOBJECT_TYPE_PATTERN; }

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

 protected:
    static void finalize_impl(JSFreeOp* fop, cairo_pattern_t* pattern);

 public:
    static cairo_pattern_t* for_js(JSContext* cx,
                                   JS::HandleObject pattern_wrapper);
};

GJS_JSAPI_RETURN_CONVENTION
JSObject*        gjs_cairo_pattern_from_pattern         (JSContext       *context,
                                                         cairo_pattern_t *pattern);

class CairoGradient : public CWrapper<CairoGradient, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoGradient, cairo_pattern_t>;
    friend CWrapper<CairoGradient, cairo_pattern_t>;
    friend class CairoLinearGradient;  // "inherits" from CairoGradient
    friend class CairoRadialGradient;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_gradient;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        &CairoGradient::create_abstract_constructor,
        &CairoGradient::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoGradient::proto_funcs,
        CairoGradient::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "Gradient", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoGradient::class_spec};

    static void finalize_impl(JSFreeOp*, cairo_pattern_t*) {}
};

class CairoLinearGradient
    : public CWrapper<CairoLinearGradient, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoLinearGradient, cairo_pattern_t>;
    friend CWrapper<CairoLinearGradient, cairo_pattern_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_linear_gradient;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 4;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        &CairoLinearGradient::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoLinearGradient::proto_funcs,
        CairoLinearGradient::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "LinearGradient", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoLinearGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JSFreeOp*, cairo_pattern_t*) {}
};

class CairoRadialGradient
    : public CWrapper<CairoRadialGradient, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoRadialGradient, cairo_pattern_t>;
    friend CWrapper<CairoRadialGradient, cairo_pattern_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_radial_gradient;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 6;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        &CairoRadialGradient::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoRadialGradient::proto_funcs,
        CairoRadialGradient::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "RadialGradient", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoRadialGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JSFreeOp*, cairo_pattern_t*) {}
};

class CairoSurfacePattern
    : public CWrapper<CairoSurfacePattern, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoSurfacePattern, cairo_pattern_t>;
    friend CWrapper<CairoSurfacePattern, cairo_pattern_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_surface_pattern;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 1;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        &CairoSurfacePattern::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoSurfacePattern::proto_funcs,
        CairoSurfacePattern::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "SurfacePattern", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoSurfacePattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JSFreeOp*, cairo_pattern_t*) {}
};

class CairoSolidPattern : public CWrapper<CairoSolidPattern, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoSolidPattern, cairo_pattern_t>;
    friend CWrapper<CairoSolidPattern, cairo_pattern_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_solid_pattern;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec static_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        &CairoSolidPattern::create_abstract_constructor,
        &CairoSolidPattern::new_proto,
        CairoSolidPattern::static_funcs,
        nullptr,  // constructorProperties
        nullptr,  // prototypeFunctions
        CairoSolidPattern::proto_props,
        &CairoPattern::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "SolidPattern", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoSolidPattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void finalize_impl(JSFreeOp*, cairo_pattern_t*) {}
};

#endif  // MODULES_CAIRO_PRIVATE_H_
