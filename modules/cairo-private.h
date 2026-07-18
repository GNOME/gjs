/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#pragma once

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
bool gjs_cairo_check_status(JSContext*, cairo_status_t, const char* name);

class CairoRegion : public CWrapper<CairoRegion, cairo_region_t> {
    friend CWrapperPointerOps<CairoRegion, cairo_region_t>;
    friend CWrapper<CairoRegion, cairo_region_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_region;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 0;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_REGION; }

    static cairo_region_t* copy_ptr(cairo_region_t* region) {
        return cairo_region_reference(region);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_region_t* constructor_impl(JSContext*, const JS::CallArgs&);

    static void finalize_impl(JS::GCContext*, cairo_region_t*);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .prototypeFunctions = CairoRegion::proto_funcs,
        .prototypeProperties = CairoRegion::proto_props,
        .finishInit = CairoRegion::define_gtype_prop};
    // COMPAT: Switch to background finalize after https://bugzilla.mozilla.org/show_bug.cgi?id=2035230
    // (The same for other instances of JS_CLASS_FOREGROUND_FINALIZE in this file)
    static constexpr JSClass klass = {
        .name = "Region",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoRegion::class_ops,
        .spec = &CairoRegion::class_spec};

 public:
    CairoRegion() = delete;
    CairoRegion(CairoRegion&) = delete;
    CairoRegion(CairoRegion&&) = delete;
};

void gjs_cairo_region_init();

class CairoContext : public CWrapper<CairoContext, cairo_t> {
    friend CWrapperPointerOps<CairoContext, cairo_t>;
    friend CWrapper<CairoContext, cairo_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_context;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 1;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_CONTEXT; }

    static cairo_t* copy_ptr(cairo_t* cr) { return cairo_reference(cr); }

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_t* constructor_impl(JSContext*, const JS::CallArgs&);

    static void finalize_impl(JS::GCContext*, cairo_t*);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .prototypeFunctions = CairoContext::proto_funcs,
        .prototypeProperties = CairoContext::proto_props,
        .finishInit = CairoContext::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "Context",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoContext::class_ops,
        .spec = &CairoContext::class_spec};

    GJS_JSAPI_RETURN_CONVENTION
    static bool dispose(JSContext*, unsigned, JS::Value*);

    static void add_associated_memory(JSObject*, cairo_t*);
    static void remove_associated_memory(JSObject*, cairo_t*);

 public:
    CairoContext() = delete;
    CairoContext(CairoContext&) = delete;
    CairoContext(CairoContext&&) = delete;
};

void gjs_cairo_context_init();
void gjs_cairo_surface_init();

// path
void gjs_cairo_path_init();
class CairoPath : public CWrapper<CairoPath, cairo_path_t> {
    friend CWrapperPointerOps<CairoPath, cairo_path_t>;
    friend CWrapper<CairoPath, cairo_path_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_path;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static void finalize_impl(JS::GCContext*, cairo_path_t*);

    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createConstructor = CairoPath::create_abstract_constructor,
        .prototypeProperties = CairoPath::proto_props};
    static constexpr JSClass klass = {
        .name = "Path",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPath::class_ops,
        .spec = &CairoPath::class_spec};

 public:
    static cairo_path_t* copy_ptr(cairo_path_t*);
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* take_c_ptr(JSContext*, cairo_path_t*);

    CairoPath() = delete;
    CairoPath(CairoPath&) = delete;
    CairoPath(CairoPath&&) = delete;
};

// surface

class CairoSurface : public CWrapper<CairoSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoSurface, cairo_surface_t>;
    friend CWrapper<CairoSurface, cairo_surface_t>;
    friend class CairoImageSurface;  // "inherits" from CairoSurface
    friend class CairoPSSurface;
    friend class CairoPDFSurface;
    friend class CairoSVGSurface;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static GType gtype() { return CAIRO_GOBJECT_TYPE_SURFACE; }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createConstructor = &CairoSurface::create_abstract_constructor,
        .prototypeFunctions = CairoSurface::proto_funcs,
        .prototypeProperties = CairoSurface::proto_props,
        .finishInit = &CairoSurface::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "Surface",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoSurface::class_ops,
        .spec = &CairoSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void add_associated_memory(JSObject*, cairo_surface_t*);
    static void remove_associated_memory(JSObject*, cairo_surface_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static bool getType_func(JSContext*, unsigned, JS::Value*);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext*, cairo_surface_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* for_js(JSContext*,
                                   JS::HandleObject surface_wrapper);

    CairoSurface() = delete;
    CairoSurface(CairoSurface&) = delete;
    CairoSurface(CairoSurface&&) = delete;
};

class CairoImageSurface : public CWrapper<CairoImageSurface, cairo_surface_t> {
    friend CWrapperPointerOps<CairoImageSurface, cairo_surface_t>;
    friend CWrapper<CairoImageSurface, cairo_surface_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_image_surface;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;
    static constexpr unsigned constructor_nargs = 3;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec static_funcs[];
    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoImageSurface::new_proto,
        .constructorFunctions = CairoImageSurface::static_funcs,
        .prototypeFunctions = CairoImageSurface::proto_funcs,
        .prototypeProperties = CairoImageSurface::proto_props,
        .finishInit = &CairoSurface::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "ImageSurface",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoSurface::class_ops,
        .spec = &CairoImageSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void add_associated_memory(JSObject*, cairo_surface_t*);

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext*, const JS::CallArgs&);
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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoPSSurface::new_proto,
        .prototypeFunctions = CairoPSSurface::proto_funcs,
        .prototypeProperties = CairoPSSurface::proto_props,
        .finishInit = &CairoSurface::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "PSSurface",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoSurface::class_ops,
        .spec = &CairoPSSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext*, const JS::CallArgs&);
};
#else
class CairoPSSurface {
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext*, cairo_surface_t*);
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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoPDFSurface::new_proto,
        .prototypeFunctions = CairoSurface::proto_funcs,
        .prototypeProperties = CairoSurface::proto_props,
        .finishInit = &CairoSurface::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "PDFSurface",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoSurface::class_ops,
        .spec = &CairoPDFSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext*, const JS::CallArgs&);
};
#else
class CairoPDFSurface {
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext*, cairo_surface_t*);
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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoSVGSurface::new_proto,
        .prototypeProperties = CairoSVGSurface::proto_props,
        .finishInit = &CairoSurface::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "SVGSurface",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoSurface::class_ops,
        .spec = &CairoSVGSurface::class_spec};

    static cairo_surface_t* copy_ptr(cairo_surface_t* surface) {
        return cairo_surface_reference(surface);
    }

    static void finalize_impl(JS::GCContext*, cairo_surface_t*) {}

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_surface_t* constructor_impl(JSContext*, const JS::CallArgs&);
};
#else
class CairoSVGSurface {
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext*, cairo_surface_t*);
};
#endif  // CAIRO_HAS_SVG_SURFACE

// pattern
void gjs_cairo_pattern_init();

class CairoPattern : public CWrapper<CairoPattern, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoPattern, cairo_pattern_t>;
    friend CWrapper<CairoPattern, cairo_pattern_t>;
    friend class CairoGradient;  // "inherits" from CairoPattern
    friend class CairoLinearGradient;
    friend class CairoRadialGradient;
    friend class CairoSurfacePattern;
    friend class CairoSolidPattern;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_pattern;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createConstructor = &CairoPattern::create_abstract_constructor,
        .prototypeFunctions = CairoPattern::proto_funcs,
        .prototypeProperties = CairoPattern::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "Pattern",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoPattern::class_spec};

    static GType gtype() { return CAIRO_GOBJECT_TYPE_PATTERN; }

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void add_associated_memory(JSObject*, cairo_pattern_t*);
    static void remove_associated_memory(JSObject*, cairo_pattern_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static bool getType_func(JSContext*, unsigned, JS::Value*);

 protected:
    static void finalize_impl(JS::GCContext*, cairo_pattern_t*);

 public:
    static cairo_pattern_t* for_js(JSContext*,
                                   JS::HandleObject pattern_wrapper);

    CairoPattern() = delete;
    CairoPattern(CairoPattern&) = delete;
    CairoPattern(CairoPattern&&) = delete;
};

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_cairo_pattern_from_pattern(JSContext*, cairo_pattern_t*);

// Note: for classes that use `CairoPattern::class_ops`,
// only add_associated_memory needs to be defined,
// because `CairoPattern`'s finalizer is called, which calls
// `CairoPattern::remove_associated_memory`.

class CairoGradient : public CWrapper<CairoGradient, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoGradient, cairo_pattern_t>;
    friend CWrapper<CairoGradient, cairo_pattern_t>;
    friend class CairoLinearGradient;  // "inherits" from CairoGradient
    friend class CairoRadialGradient;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_gradient;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createConstructor = &CairoGradient::create_abstract_constructor,
        .createPrototype = &CairoGradient::new_proto,
        .prototypeFunctions = CairoGradient::proto_funcs,
        .prototypeProperties = CairoGradient::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "Gradient",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoGradient::class_spec};

    static void add_associated_memory(JSObject*, cairo_pattern_t*);

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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoLinearGradient::new_proto,
        .prototypeFunctions = CairoLinearGradient::proto_funcs,
        .prototypeProperties = CairoLinearGradient::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "LinearGradient",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoLinearGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void add_associated_memory(JSObject*, cairo_pattern_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext*, const JS::CallArgs&);

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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoRadialGradient::new_proto,
        .prototypeFunctions = CairoRadialGradient::proto_funcs,
        .prototypeProperties = CairoRadialGradient::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "RadialGradient",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoRadialGradient::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void add_associated_memory(JSObject*, cairo_pattern_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext*, const JS::CallArgs&);

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
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec proto_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createPrototype = &CairoSurfacePattern::new_proto,
        .prototypeFunctions = CairoSurfacePattern::proto_funcs,
        .prototypeProperties = CairoSurfacePattern::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "SurfacePattern",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoSurfacePattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void add_associated_memory(JSObject*, cairo_pattern_t*);

    GJS_JSAPI_RETURN_CONVENTION
    static cairo_pattern_t* constructor_impl(JSContext*, const JS::CallArgs&);

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
};

class CairoSolidPattern : public CWrapper<CairoSolidPattern, cairo_pattern_t> {
    friend CWrapperPointerOps<CairoSolidPattern, cairo_pattern_t>;
    friend CWrapper<CairoSolidPattern, cairo_pattern_t>;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_cairo_solid_pattern;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CAIRO;

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_proto(JSContext*, JSProtoKey);

    static const JSFunctionSpec static_funcs[];
    static const JSPropertySpec proto_props[];
    static constexpr js::ClassSpec class_spec = {
        .createConstructor = &CairoSolidPattern::create_abstract_constructor,
        .createPrototype = &CairoSolidPattern::new_proto,
        .constructorFunctions = CairoSolidPattern::static_funcs,
        .prototypeProperties = CairoSolidPattern::proto_props,
        .finishInit = &CairoPattern::define_gtype_prop};
    static constexpr JSClass klass = {
        .name = "SolidPattern",
        .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
        .cOps = &CairoPattern::class_ops,
        .spec = &CairoSolidPattern::class_spec};

    static cairo_pattern_t* copy_ptr(cairo_pattern_t* pattern) {
        return cairo_pattern_reference(pattern);
    }

    static void add_associated_memory(JSObject*, cairo_pattern_t*);

    static void finalize_impl(JS::GCContext*, cairo_pattern_t*) {}
};
