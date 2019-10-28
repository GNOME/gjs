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

#include "gjs/jsapi-wrapper.h"

#include "gjs/macros.h"
#include "gjs/module.h"

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

enum class GjsGlobalType { MODULE, LEGACY };

using ModuleTable = JS::GCHashMap<std::string, JS::Heap<JSObject*>,
                                  CppStringHashPolicy, js::SystemAllocPolicy>;

class GjsGlobal {
    static const struct JSClassOps class_ops;
    static const struct JSClass klass;

 protected:
    JSObject* js_global(JSContext* cx) { return JS::CurrentGlobalOrNull(cx); }

 public:
    GjsGlobal() {}

    virtual ~GjsGlobal() {}

    GJS_USE
    static JSObject* create(JSContext* cx) {
        JS::RealmCreationOptions creation;
        creation.setFieldsEnabled(true);
        auto cur = JS::CurrentGlobalOrNull(cx);
        if (cur)
            creation.setExistingCompartment(cur);
        else
            creation.setNewCompartmentInSystemZone();
        JS::RealmBehaviors behaviors;
        JS::RealmOptions compartment_options(creation, behaviors);
        JS::RootedObject global(
            cx, JS_NewGlobalObject(cx, &klass, nullptr, JS::FireOnNewGlobalHook,
                                   compartment_options));

        if (!global)
            return nullptr;

        JSAutoRealm ac(cx, global);

        if (!JS_InitReflectParse(cx, global) ||
            !JS_DefineDebuggerObject(cx, global))
            return nullptr;

        return global;
    }

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool define_properties(JSContext* cx,
                                   const char* bootstrap_script) = 0;

    virtual GjsGlobalType global_type() = 0;
};

class GjsModuleGlobal : public GjsGlobal {
    JSObject* m_global;
    GjsModuleLoader* m_module_loader;

 public:
    GjsModuleGlobal(JSObject* global) : GjsGlobal() {
        m_global = global;
        m_module_loader = new GjsModuleLoader();
    }

    ~GjsModuleGlobal() { delete m_module_loader; }

    static GjsModuleGlobal* from_cx(JSContext* cx) {
        JSObject* glob = JS::CurrentGlobalOrNull(cx);

        GjsGlobal* priv = (GjsGlobal*)JS_GetPrivate(glob);

        g_assert_true(priv->global_type() == GjsModuleGlobal::type());

        GjsModuleGlobal* module_global = (GjsModuleGlobal*)priv;

        return module_global;
    }

    static GjsModuleGlobal* from_global(JSObject* global) {
        GjsGlobal* priv = (GjsGlobal*)JS_GetPrivate(global);

        g_assert_true(priv->global_type() == GjsModuleGlobal::type());

        GjsModuleGlobal* module_global = (GjsModuleGlobal*)priv;

        return module_global;
    }

    // TODO
    GJS_USE
    static JSObject* create(JSContext* cx) {
        auto global = GjsGlobal::create(cx);

        {
            JSAutoRealm ac(cx, global);
            JS_SetPrivate(global, new GjsModuleGlobal(global));
            JSRuntime* rt = JS_GetRuntime(cx);

            JS::SetModuleResolveHook(rt, gjs_module_resolve);
            JS::SetModuleMetadataHook(rt, gjs_populate_module_meta);
        }

        return global;
    }

    GjsModuleLoader* loader();

    JSObject* lookup_module(const char* identifier);

    static GjsGlobalType type() { return GjsGlobalType::MODULE; }

    virtual GjsGlobalType global_type();

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool define_properties(JSContext* cx, const char* bootstrap_script);
};

class GjsLegacyGlobal : public GjsGlobal {
    JSObject* m_global;

 public:
    GjsLegacyGlobal(JSObject* global) : GjsGlobal() { m_global = global; }

    static GjsGlobalType type() { return GjsGlobalType::LEGACY; }

    virtual GjsGlobalType global_type();

    GJS_USE
    static JSObject* create(JSContext* cx) {
        auto global = GjsGlobal::create(cx);
        JSAutoRealm ac(cx, global);
        JS_SetPrivate(global, new GjsLegacyGlobal(global));

        return global;
    }

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool define_properties(JSContext* cx, const char* bootstrap_script);
};

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_global_object(JSContext* cx, GjsGlobalType global_type);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_global_properties(JSContext* cx, JS::HandleObject global,
                                  const char* bootstrap_script);

void gjs_set_global_slot(JSContext* context, JSObject* global,
                         GjsGlobalSlot slot, JS::Value value);

JS::Value gjs_get_global_slot(JSContext* cx, JSObject* global,
                              GjsGlobalSlot slot);

#endif  // GJS_GLOBAL_H_
