/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2009 Red Hat, Inc.
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

#include <stddef.h>  // for size_t

#include <glib.h>

#include "gjs/jsapi-wrapper.h"
#include "js/CompilationAndEvaluation.h"
#include "js/SourceText.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <girepository.h>
#include <libsoup/soup.h>

#include <codecvt>
#include <locale>
#include <string>  // for u16string

#include <gjs/gjs.h>
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/engine.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/native.h"

using AutoURI = GjsAutoPointer<SoupURI, SoupURI, soup_uri_free>;
using AutoGHashTable =
    GjsAutoPointer<GHashTable, GHashTable, g_hash_table_destroy>;

bool gjs_load_internal_module(JSContext* js_context, unsigned argc,
                              JS::Value* vp) {
    GjsModuleGlobal* gjs = GjsModuleGlobal::from_cx(js_context);
    return gjs->loader()->load_internal_module(js_context, argc, vp);
}

bool gjs_load_gi_module(JSContext* js_context, unsigned argc, JS::Value* vp) {
    GjsModuleGlobal* gjs = GjsModuleGlobal::from_cx(js_context);
    return gjs->loader()->load_gi_module(js_context, argc, vp);
}

static constexpr JSFunctionSpec internal_functions[] = {
    JS_FN("require", gjs_load_internal_module, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS_END,
};

GJS_JSAPI_RETURN_CONVENTION
static bool run_module_bootstrap(JSContext* cx, const char* bootstrap_script,
                                 JS::HandleObject global) {
    JSAutoRealm ac(cx, global);
    GjsAutoChar uri = g_strdup_printf(
        "resource:///org/gnome/gjs/modules/esm/_bootstrap/%s.js",
        bootstrap_script);

    char* module_text_raw;
    gsize module_len;
    GError* err = NULL;

    auto gfile = g_file_new_for_uri(uri);

    if (!g_file_load_contents(gfile, NULL, &module_text_raw, &module_len, NULL,
                              &err)) {
        return false;
    }

    GjsAutoChar mod_text(module_text_raw);
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(mod_text);

    JS::SourceText<char16_t> buf;
    if (!buf.init(cx, utf16_string.c_str(), utf16_string.size(),
                  JS::SourceOwnership::Borrowed)) {
        return false;
    }

    JS::RootedObject bootstrap_module(cx);
    JS::CompileOptions options(cx);
    // UTF-8 = true?
    options.setFileAndLine(uri, 1).setSourceIsLazy(true);
    if (!JS::CompileModule(cx, options, buf, &bootstrap_module)) {
        return false;
    }

    auto mod = new GjsModule();
    mod->set_module_uri(uri.get());
    mod->set_module_record(bootstrap_module);
    JS::SetModulePrivate(bootstrap_module, JS::PrivateValue(mod));

    if (!JS::ModuleInstantiate(cx, bootstrap_module)) {
        g_warning("Failed to instantiate module: %s", uri.get());
        gjs_log_exception(cx);
        return false;
    }

    if (!JS::ModuleEvaluate(cx, bootstrap_module)) {
        gjs_log_exception(cx);
        return false;
    }
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool run_legacy_bootstrap(JSContext* cx, const char* bootstrap_script,
                                 JS::HandleObject global) {
    GjsAutoChar uri = g_strdup_printf(
        "resource:///org/gnome/gjs/modules/legacy/_bootstrap/%s.js",
        bootstrap_script);

    JSAutoRealm ar(cx, global);

    JS::CompileOptions options(cx);
    options.setFileAndLine(uri, 1).setSourceIsLazy(true);

    char* script;
    size_t script_len;
    if (!gjs_load_internal_source(cx, uri, &script, &script_len))
        return false;

    JS::SourceText<mozilla::Utf8Unit> source;
    if (!source.init(cx, script, script_len,
                     JS::SourceOwnership::TakeOwnership))
        return false;

    JS::RootedScript compiled_script(cx, JS::Compile(cx, options, source));
    if (!compiled_script)
        return false;

    JS::RootedValue ignored(cx);
    return JS::CloneAndExecuteScript(cx, compiled_script, &ignored);
}




std::string GjsModuleGlobal::global_type() {
    return GjsModuleGlobal::type();
}

GJS_JSAPI_RETURN_CONVENTION
bool GjsModuleGlobal::define_properties(JSContext* cx,

                                                const char* bootstrap_script) {
    JSAutoRealm ac(cx, m_global);
    JS::RootedObject global(cx, m_global);
    // const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    GjsContextPrivate* gjs_cx = GjsContextPrivate::from_cx(cx);
    const GjsAtoms& atoms = gjs_cx->atoms();

    if (!JS_DefinePropertyById(cx, global, atoms.window(), global,
                               JSPROP_READONLY | JSPROP_PERMANENT))
        return false;

    if (!JS_DefineFunctions(cx, global, internal_functions))
        return false;

    if (bootstrap_script) {
        if (!run_module_bootstrap(cx, bootstrap_script, global)) {
            return false;
        }
    }

    return true;
}

std::string GjsLegacyGlobal::global_type() {
    return GjsLegacyGlobal::type();
}

JSObject* GjsModuleGlobal::lookup_module(const char* identifier) {
    return m_module_loader->lookup_module(identifier);
}

GJS_JSAPI_RETURN_CONVENTION
bool GjsLegacyGlobal::define_properties(JSContext* cx,
                                                const char* bootstrap_script) {
    JSAutoRealm ac(cx, m_global);
    JS::RootedObject global(cx, m_global);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_DefinePropertyById(cx, global, atoms.window(), global,
                               JSPROP_READONLY | JSPROP_PERMANENT))
        return false;

    JS::Value v_importer =
        gjs_get_global_slot(cx, global, GJS_GLOBAL_SLOT_IMPORTS);
    // TODO A temporary fix for the debugging global?
    // TODO Figure out if pre-ESM this code lived here
    if (!v_importer.isNullOrUndefined()) {
        g_assert(((void)"importer should be defined before passing null "
                        "importer to GjsGlobal::define_properties",
                  v_importer.isObject()));
        JS::RootedObject root_importer(cx, &v_importer.toObject());

        /* Wrapping is a no-op if the importer is already in the same
         * compartment. */
        if (!JS_WrapObject(cx, &root_importer) ||
            !JS_DefinePropertyById(cx, global, atoms.imports(), root_importer,
                                   GJS_MODULE_PROP_FLAGS))
            return false;
    }

    if (bootstrap_script) {
        JS::RootedObject obj(cx, global);
        if (!run_legacy_bootstrap(cx, bootstrap_script, obj))
            return false;
    }

    return true;
}

/**
 * gjs_create_global_object:
 * @cx: a #JSContext
 *
 * Creates a global object, and initializes it with the default API.
 *
 * Returns: the created global object on success, nullptr otherwise, in which
 * case an exception is pending on @cx
 */
JSObject* gjs_create_global_object(JSContext* cx, GjsGlobalType global_type) {
    switch (global_type) {
        case GjsGlobalType::LEGACY:
            return GjsLegacyGlobal::create(cx);
        case GjsGlobalType::MODULE:
            return GjsModuleGlobal::create(cx);
        default:
            return nullptr;
    }
}

/**
 * gjs_define_global_properties:
 * @cx: a #JSContext
 * @global: a JS global object that has not yet been passed to this function
 * @bootstrap_script: (nullable): name of a bootstrap script (found at
 * resource://org/gnome/gjs/modules/legacy/_bootstrap/@bootstrap_script) or
 * %NULL for none
 *
 * Defines properties on the global object such as 'window' and 'imports', and
 * runs a bootstrap JS script on the global object to define any properties
 * that can be defined from JS.
 * This function completes the initialization of a new global object, but it
 * is separate from gjs_create_global_object() because all globals share the
 * same root importer.
 * The code creating the main global for the JS context needs to create the
 * root importer in between calling gjs_create_global_object() and
 * gjs_define_global_properties().
 *
 * The caller of this function should be in the realm for @global.
 * If the root importer object belongs to a different realm, this function will
 * create a wrapper for it.
 *
 * Returns: true on success, false otherwise, in which case an exception is
 * pending on @cx
 */
bool gjs_define_global_properties(JSContext* cx, JS::HandleObject global,
                                  const char* bootstrap_script) {
    GjsGlobal* priv = (GjsGlobal*)JS_GetPrivate(global);

    JSAutoRealm ar(cx, global);

    return priv->define_properties(cx, bootstrap_script);
}

void gjs_set_global_slot(JSContext* cx, JSObject* global, GjsGlobalSlot slot,
                         JS::Value value) {
    JS_SetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot, value);
}

JS::Value gjs_get_global_slot(JSContext* cx, JSObject* global,
                              GjsGlobalSlot slot) {
    return JS_GetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot);
}

GjsModuleLoader* GjsModuleGlobal::loader() { return m_module_loader; }

const struct JSClassOps GjsGlobal::class_ops = {nullptr,  // addProperty
                                                nullptr,  // deleteProperty
                                                nullptr,  // enumerate
                                                JS_NewEnumerateStandardClasses,
                                                JS_ResolveStandardClass,
                                                JS_MayResolveStandardClass,
                                                nullptr,  // finalize
                                                nullptr,  // call
                                                nullptr,  // hasInstance
                                                nullptr,  // construct
                                                JS_GlobalObjectTraceHook};
// clang-format on

const struct JSClass GjsGlobal::klass = {
    "GjsGlobal",
    JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(GJS_GLOBAL_SLOT_LAST) | JSCLASS_HAS_PRIVATE,
    &class_ops,
};