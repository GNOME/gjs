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
#include <js/TypeDecls.h>
#include <jspubtd.h>  // for JSProtoKey

#include "gi/cwrapper.h"
#include "gjs/global.h"
#include "gjs/macros.h"
#include "util/log.h"

struct JSFunctionSpec;
struct JSPropertySpec;
namespace JS {
class CallArgs;
}

GJS_JSAPI_RETURN_CONVENTION
bool             gjs_cairo_check_status                 (JSContext       *context,
                                                         cairo_status_t   status,
                                                         const char      *name);

class CairoRegion : public CWrapper<CairoRegion, cairo_region_t> {
    friend CWrapperPointerOps<CairoRegion, cairo_region_t>;
    friend CWrapper<CairoRegion, cairo_region_t>;

    CairoRegion() = delete;
    CairoRegion(CairoRegion&) = delete;
    CairoRegion(CairoRegion&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_region;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 0;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_REGION; }

    static cairo_region_t* copy_ptr(cairo_region_t* region) {
        return cairo_region_reference(region);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_region_t* constructor_impl(JSContext* cx,
                                            const JS::CallArgs& args);

    static void finalize_impl(JS::GCContext*, cairo_region_t* cr);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoRegion::proto_funcs,
        CairoRegion::proto_props,
        CairoRegion::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "Region", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoRegion::class_ops, &CairoRegion::class_spec};
};

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

    static void finalize_impl(JS::GCContext*, cairo_t* cr);

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
        "Context", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoContext::class_ops, &CairoContext::class_spec};

    GJS_JSAPI_RETURN_CONVENTION
    static bool dispose(JSContext* cx, unsigned argc, JS::Value* vp);
};

void gjs_cairo_context_init(void);
void gjs_cairo_surface_init(void);

/* path */
void gjs_cairo_path_init();
class CairoPath : public CWrapper<CairoPath, cairo_path_t> {
    friend CWrapperPointerOps<CairoPath, cairo_path_t>;
    friend CWrapper<CairoPath, cairo_path_t>;

    CairoPath() = delete;
    CairoPath(CairoPath&) = delete;
    CairoPath(CairoPath&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_path;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static void finalize_impl(JS::GCContext*, cairo_path_t* path);

    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        CairoPath::create_abstract_constructor,
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        nullptr,  // prototypeFunctions
        CairoPath::proto_props,
        nullptr,  // finishInit
    };
    static constexpr JSClass klass = {
        "Path", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPath::class_ops, &CairoPath::class_spec};

 public:
    static cairo_path_t* copy_ptr(cairo_path_t* path);
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* take_c_ptr(JSContext* cx, cairo_path_t* ptr);
};

/* surface */

class CairoSurface : public CWrapper<CairoSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoSurface, cairo_surface_t>;
    friend CWrapper<CairoSurface, cairo_surface_t>;
    friend class CairoImageSurface;  // "inherits" from CairoSurface
    friend class CairoPSSurface;
    friend class CairoPDFSurface;
    friend class CairoSVGSurface;

    CairoSurface() = delete;
    CairoSurface(CairoSurface&) = delete;
    CairoSurface(CairoSurface&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_SURFACE; }

    static void finalize_impl(JS::GCContext*, cairo_surface_t* surface);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        &CairoSurface::create_abstract_constructor,
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoSurface::proto_funcs,
        CairoSurface::proto_props,
        &CairoSurface::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "Surface", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoSurface::class_ops, &CairoSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool getType_func(JSContext* context, unsigned argc, JS::Value* vp);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext* cx, cairo_surface_t* surface);

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* for_js(JSContext* cx,
                                   JS::HandleObject surface_wrapper);
};

class CairoImageSurface : public CWrapper<CairoImageSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoImageSurface, cairo_surface_t>;
    friend CWrapper<CairoImageSurface, cairo_surface_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_image_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 3;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec static_funcs[];
    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor,
        &CairoImageSurface::new_proto,
        CairoImageSurface::static_funcs,
        nullptr,  // constructorProperties
        CairoImageSurface::proto_funcs,
        CairoImageSurface::proto_props,
        &CairoSurface::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "ImageSurface",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoSurface::class_ops, &CairoImageSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);
};

#ifdef CAIRO_HAS_PS_SURFACE
class CairoPSSurface : public CWrapper<CairoPSSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoPSSurface, cairo_surface_t>;
    friend CWrapper<CairoPSSurface, cairo_surface_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_ps_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 3;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor,
        &CairoPSSurface::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoPSSurface::proto_funcs,
        CairoPSSurface::proto_props,
        &CairoSurface::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "PSSurface",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoSurface::class_ops, &CairoPSSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);
};
#else
class CairoPSSurface {
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext* cx, cairo_surface_t* surface);
};
#endif  // CAIRO_HAS_PS_SURFACE

#ifdef CAIRO_HAS_PDF_SURFACE
class CairoPDFSurface : public CWrapper<CairoPDFSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoPDFSurface, cairo_surface_t>;
    friend CWrapper<CairoPDFSurface, cairo_surface_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_pdf_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 3;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor,
        &CairoPDFSurface::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        CairoSurface::proto_funcs,
        CairoSurface::proto_props,
        &CairoSurface::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "PDFSurface",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoSurface::class_ops, &CairoPDFSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);
};
#else
class CairoPDFSurface {
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext* cx, cairo_surface_t* surface);
};
#endif  // CAIRO_HAS_PDF_SURFACE

#ifdef CAIRO_HAS_SVG_SURFACE
class CairoSVGSurface : public CWrapper<CairoSVGSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoSVGSurface, cairo_surface_t>;
    friend CWrapper<CairoSVGSurface, cairo_surface_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_svg_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 3;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext* cx, JSProtoKey);

    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor,
        &CairoSVGSurface::new_proto,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        nullptr,  // prototypeFunctions
        CairoSVGSurface::proto_props,
        &CairoSurface::define_gtype_prop,
    };
    static constexpr JSClass klass = {
        "SVGSurface",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoSurface::class_ops, &CairoSVGSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);
};
#else
class CairoSVGSurface {
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext* cx, cairo_surface_t* surface);
};
#endif  // CAIRO_HAS_SVG_SURFACE

/* pattern */
void gjs_cairo_pattern_init();

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
        "Pattern", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoPattern::class_spec};

    static GType gtype() { return CAIRO_GOBJECT_TYPE_PATTERN; }

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool getType_func(JSContext* context, unsigned argc, JS::Value* vp);

 protected:
    static void finalize_impl(JS::GCContext*, cairo_pattern_t* pattern);

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
        "Gradient", JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoGradient::class_spec};

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
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
        "LinearGradient",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoLinearGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
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
        "RadialGradient",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoRadialGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
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
        "SurfacePattern",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoSurfacePattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext* cx,
                                             const JS::CallArgs& args);

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
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
        "SolidPattern",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &CairoPattern::class_ops, &CairoSolidPattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
};

#endif  // MODULES_CAIRO_PRIVATE_H_
