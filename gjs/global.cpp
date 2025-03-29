/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/CallArgs.h>           // for CallArgs, CallArgsFromVp
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/Class.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/Debug.h>         // for JS_DefineDebuggerObject
#include <js/GlobalObject.h>  // for CurrentGlobalOrNull, JS_NewGlobalObject
#include <js/Id.h>
#include <js/MapAndSet.h>
#include <js/Object.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_RE...
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetObjectRealmOrNull, SetRealmPrivate
#include <js/RealmOptions.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>       // for JS_IdToValue, JS_InitReflectParse

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/engine.h"
#include "gjs/global.h"
#include "gjs/internal.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/native.h"

namespace mozilla {
union Utf8Unit;
}

class GjsBaseGlobal {
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* base(JSContext* cx, const JSClass* clasp,
                          JS::RealmCreationOptions options,
                          JSPrincipals* principals = nullptr) {
        JS::RealmBehaviors behaviors;
        JS::RealmOptions compartment_options(options, behaviors);

        JS::RootedObject global{cx, JS_NewGlobalObject(cx, clasp, principals,
                                                       JS::FireOnNewGlobalHook,
                                                       compartment_options)};
        if (!global)
            return nullptr;

        JSAutoRealm ac(cx, global);

        if (!JS_InitReflectParse(cx, global) ||
            !JS_DefineDebuggerObject(cx, global))
            return nullptr;

        return global;
    }

 protected:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(
        JSContext* cx, const JSClass* clasp,
        JS::RealmCreationOptions options = JS::RealmCreationOptions(),
        JSPrincipals* principals = nullptr) {
        options.setNewCompartmentAndZone();
        return base(cx, clasp, options, principals);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_with_compartment(
        JSContext* cx, JS::HandleObject existing, const JSClass* clasp,
        JS::RealmCreationOptions options = JS::RealmCreationOptions(),
        JSPrincipals* principals = nullptr) {
        options.setExistingCompartment(existing);
        return base(cx, clasp, options, principals);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool run_bootstrap(JSContext* cx, const char* bootstrap_script,
                              JS::HandleObject global) {
        Gjs::AutoChar uri{g_strdup_printf(
            "resource:///org/gnome/gjs/modules/script/_bootstrap/%s.js",
            bootstrap_script)};

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

        JS::RootedValue ignored(cx);
        return JS::Evaluate(cx, options, source, &ignored);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool load_native_module(JSContext* m_cx, unsigned argc,
                                   JS::Value* vp) {
        JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

        // This function should never be directly exposed to user code, so we
        // can be strict.
        g_assert(argc == 1);
        g_assert(argv[0].isString());

        JS::RootedString str(m_cx, argv[0].toString());
        JS::UniqueChars id(JS_EncodeStringToUTF8(m_cx, str));

        if (!id)
            return false;

        JS::RootedObject native_obj(m_cx);

        if (!Gjs::NativeModuleDefineFuncs::get().define(m_cx, id.get(),
                                                        &native_obj)) {
            gjs_throw(m_cx, "Failed to load native module: %s", id.get());
            return false;
        }

        argv.rval().setObject(*native_obj);
        return true;
    }
};

const JSClassOps defaultclassops = JS::DefaultGlobalClassOps;

class GjsGlobal : GjsBaseGlobal {
    static constexpr JSClass klass = {
        // Jasmine depends on the class name "GjsGlobal" to detect GJS' global
        // object.
        "GjsGlobal",
        JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(
            static_cast<uint32_t>(GjsGlobalSlot::LAST)),
        &defaultclassops,
    };

    // clang-format off
    static constexpr JSPropertySpec static_props[] = {
        JS_STRING_SYM_PS(toStringTag, "GjsGlobal", JSPROP_READONLY),
        JS_PS_END};
    // clang-format on

    static constexpr JSFunctionSpec static_funcs[] = {
        JS_FS_END};

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext* cx) {
        return GjsBaseGlobal::create(cx, &klass);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_with_compartment(JSContext* cx,
                                             JS::HandleObject cmp_global) {
        return GjsBaseGlobal::create_with_compartment(cx, cmp_global, &klass);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_properties(JSContext* cx, JS::HandleObject global,
                                  const char* realm_name,
                                  const char* bootstrap_script) {
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (!JS_DefinePropertyById(cx, global, atoms.window(), global,
                                   JSPROP_READONLY | JSPROP_PERMANENT) ||
            !JS_DefineFunctions(cx, global, GjsGlobal::static_funcs) ||
            !JS_DefineProperties(cx, global, GjsGlobal::static_props))
            return false;

        JS::Realm* realm = JS::GetObjectRealmOrNull(global);
        g_assert(realm && "Global object must be associated with a realm");
        // const_cast is allowed here if we never free the realm data
        JS::SetRealmPrivate(realm, const_cast<char*>(realm_name));

        JS::RootedObject native_registry(cx, JS::NewMapObject(cx));
        if (!native_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::NATIVE_REGISTRY,
                            JS::ObjectValue(*native_registry));

        JS::RootedObject module_registry(cx, JS::NewMapObject(cx));
        if (!module_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::MODULE_REGISTRY,
                            JS::ObjectValue(*module_registry));

        JS::RootedObject source_map_registry{cx, JS::NewMapObject(cx)};
        if (!source_map_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::SOURCE_MAP_REGISTRY,
                            JS::ObjectValue(*source_map_registry));

        JS::Value v_importer =
            gjs_get_global_slot(global, GjsGlobalSlot::IMPORTS);
        g_assert(((void) "importer should be defined before passing null "
                  "importer to GjsGlobal::define_properties",
                  v_importer.isObject()));
        JS::RootedObject root_importer(cx, &v_importer.toObject());

        // Wrapping is a no-op if the importer is already in the same realm.
        if (!JS_WrapObject(cx, &root_importer) ||
            !JS_DefinePropertyById(cx, global, atoms.imports(), root_importer,
                                   GJS_MODULE_PROP_FLAGS))
            return false;

        if (bootstrap_script) {
            if (!run_bootstrap(cx, bootstrap_script, global))
                return false;
        }

        return true;
    }
};

class GjsDebuggerGlobal : GjsBaseGlobal {
    static constexpr JSClass klass = {
        "GjsDebuggerGlobal",
        JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(
            static_cast<uint32_t>(GjsDebuggerGlobalSlot::LAST)),
        &defaultclassops,
    };

    static constexpr JSFunctionSpec static_funcs[] = {
        JS_FN("loadNative", &load_native_module, 1, 0), JS_FS_END};

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext* cx) {
        JS::RealmCreationOptions options;
        options.setToSourceEnabled(true);  // debugger uses uneval()
        return GjsBaseGlobal::create(cx, &klass, options);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_with_compartment(JSContext* cx,
                                             JS::HandleObject cmp_global) {
        return GjsBaseGlobal::create_with_compartment(cx, cmp_global, &klass);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_properties(JSContext* cx, JS::HandleObject global,
                                  const char* realm_name,
                                  const char* bootstrap_script) {
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (!JS_DefinePropertyById(cx, global, atoms.window(), global,
                                   JSPROP_READONLY | JSPROP_PERMANENT) ||
            !JS_DefineFunctions(cx, global, GjsDebuggerGlobal::static_funcs))
            return false;

        JS::Realm* realm = JS::GetObjectRealmOrNull(global);
        g_assert(realm && "Global object must be associated with a realm");
        // const_cast is allowed here if we never free the realm data
        JS::SetRealmPrivate(realm, const_cast<char*>(realm_name));

        if (bootstrap_script) {
            if (!run_bootstrap(cx, bootstrap_script, global))
                return false;
        }

        return true;
    }
};

class GjsInternalGlobal : GjsBaseGlobal {
    static constexpr JSFunctionSpec static_funcs[] = {
        JS_FN("compileModule", gjs_internal_compile_module, 2, 0),
        JS_FN("compileInternalModule", gjs_internal_compile_internal_module, 2,
              0),
        JS_FN("getRegistry", gjs_internal_get_registry, 1, 0),
        JS_FN("getSourceMapRegistry", gjs_internal_get_source_map_registry, 1,
              0),
        JS_FN("loadResourceOrFile", gjs_internal_load_resource_or_file, 1, 0),
        JS_FN("loadResourceOrFileAsync",
              gjs_internal_load_resource_or_file_async, 1, 0),
        JS_FN("parseURI", gjs_internal_parse_uri, 1, 0),
        JS_FN("resolveRelativeResourceOrFile",
              gjs_internal_resolve_relative_resource_or_file, 2, 0),
        JS_FN("setGlobalModuleLoader", gjs_internal_set_global_module_loader, 2,
              0),
        JS_FN("setModulePrivate", gjs_internal_set_module_private, 2, 0),
        JS_FN("uriExists", gjs_internal_uri_exists, 1, 0),
        JS_FN("atob", gjs_internal_atob, 1, 0),
        JS_FS_END};

    static constexpr JSClass klass = {
        "GjsInternalGlobal",
        JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(
            static_cast<uint32_t>(GjsInternalGlobalSlot::LAST)),
        &defaultclassops,
    };

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext* cx) {
        return GjsBaseGlobal::create(cx, &klass, {}, get_internal_principals());
    }

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_with_compartment(JSContext* cx,
                                             JS::HandleObject cmp_global) {
        return GjsBaseGlobal::create_with_compartment(
            cx, cmp_global, &klass, {}, get_internal_principals());
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_properties(JSContext* cx, JS::HandleObject global,
                                  const char* realm_name,
                                  const char* bootstrap_script
                                  [[maybe_unused]]) {
        JS::Realm* realm = JS::GetObjectRealmOrNull(global);
        g_assert(realm && "Global object must be associated with a realm");
        // const_cast is allowed here if we never free the realm data
        JS::SetRealmPrivate(realm, const_cast<char*>(realm_name));

        JSAutoRealm ar(cx, global);
        JS::RootedObject native_registry(cx, JS::NewMapObject(cx));
        if (!native_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::NATIVE_REGISTRY,
                            JS::ObjectValue(*native_registry));

        JS::RootedObject module_registry(cx, JS::NewMapObject(cx));
        if (!module_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::MODULE_REGISTRY,
                            JS::ObjectValue(*module_registry));

        JS::RootedObject source_map_registry{cx, JS::NewMapObject(cx)};
        if (!source_map_registry)
            return false;

        gjs_set_global_slot(global, GjsGlobalSlot::SOURCE_MAP_REGISTRY,
                            JS::ObjectValue(*source_map_registry));

        return JS_DefineFunctions(cx, global, static_funcs);
    }
};

/**
 * gjs_create_global_object:
 * @cx: a #JSContext
 *
 * Creates a global object, and initializes it with the default API.
 *
 * Returns: the created global object on success, nullptr otherwise, in which
 * case an exception is pending on @cx
 */
JSObject* gjs_create_global_object(JSContext* cx, GjsGlobalType global_type,
                                   JS::HandleObject current_global) {
    if (current_global) {
        switch (global_type) {
            case GjsGlobalType::DEFAULT:
                return GjsGlobal::create_with_compartment(cx, current_global);
            case GjsGlobalType::DEBUGGER:
                return GjsDebuggerGlobal::create_with_compartment(
                    cx, current_global);
            case GjsGlobalType::INTERNAL:
                return GjsInternalGlobal::create_with_compartment(
                    cx, current_global);
            default:
                return nullptr;
        }
    }

    switch (global_type) {
        case GjsGlobalType::DEFAULT:
            return GjsGlobal::create(cx);
        case GjsGlobalType::DEBUGGER:
            return GjsDebuggerGlobal::create(cx);
        case GjsGlobalType::INTERNAL:
            return GjsInternalGlobal::create(cx);
        default:
            return nullptr;
    }
}

/**
 * gjs_global_is_type:
 * @cx: the current #JSContext
 * @type: the global type to test for
 *
 * Returns: whether the current global is the same type as @type
 */
bool gjs_global_is_type(JSContext* cx, GjsGlobalType type) {
    JSObject* global = JS::CurrentGlobalOrNull(cx);

    g_assert(global && "gjs_global_is_type called before a realm was entered.");

    JS::Value global_type =
        gjs_get_global_slot(global, GjsBaseGlobalSlot::GLOBAL_TYPE);

    g_assert(global_type.isInt32());

    return static_cast<GjsGlobalType>(global_type.toInt32()) == type;
}

GjsGlobalType gjs_global_get_type(JSContext* cx) {
    auto global = JS::CurrentGlobalOrNull(cx);

    g_assert(global &&
             "gjs_global_get_type called before a realm was entered.");

    JS::Value global_type =
        gjs_get_global_slot(global, GjsBaseGlobalSlot::GLOBAL_TYPE);

    g_assert(global_type.isInt32());

    return static_cast<GjsGlobalType>(global_type.toInt32());
}

GjsGlobalType gjs_global_get_type(JSObject* global) {
    JS::Value global_type =
        gjs_get_global_slot(global, GjsBaseGlobalSlot::GLOBAL_TYPE);

    g_assert(global_type.isInt32());

    return static_cast<GjsGlobalType>(global_type.toInt32());
}

/**
 * gjs_global_registry_set:
 * @cx: the current #JSContext
 * @registry: a JS Map object
 * @key: a module identifier, typically a string or symbol
 * @module: a module object
 *
 * This function inserts a module object into a global registry. Global
 * registries are JS Map objects for easy reuse and access within internal JS.
 * This function will assert if a module has already been inserted at the given
 * key.
 *
 * Returns: false if an exception is pending, otherwise true.
 */
bool gjs_global_registry_set(JSContext* cx, JS::HandleObject registry,
                             JS::PropertyKey key, JS::HandleObject module) {
    JS::RootedValue v_key(cx);
    if (!JS_IdToValue(cx, key, &v_key))
        return false;

    bool has_key;
    if (!JS::MapHas(cx, registry, v_key, &has_key))
        return false;

    g_assert(!has_key && "Module key already exists in the registry");

    JS::RootedValue v_value(cx, JS::ObjectValue(*module));

    return JS::MapSet(cx, registry, v_key, v_value);
}

/**
 * gjs_global_registry_get:
 * @cx: the current #JSContext
 * @registry: a JS Map object
 * @key: a module identifier, typically a string or symbol
 * @module_out: (out): handle where a module object will be stored
 *
 * This function retrieves a module record from the global registry, or null if
 * the module record is not present. Global registries are JS Map objects for
 * easy reuse and access within internal JS.
 *
 * Returns: false if an exception is pending, otherwise true.
 */
bool gjs_global_registry_get(JSContext* cx, JS::HandleObject registry,
                             JS::PropertyKey key,
                             JS::MutableHandleObject module_out) {
    JS::RootedValue v_key(cx), v_value(cx);
    if (!JS_IdToValue(cx, key, &v_key) ||
        !JS::MapGet(cx, registry, v_key, &v_value))
        return false;

    g_assert((v_value.isUndefined() || v_value.isObject()) &&
             "Invalid value in module registry");

    if (v_value.isObject()) {
        module_out.set(&v_value.toObject());
        return true;
    }

    module_out.set(nullptr);
    return true;
}

/**
 * gjs_global_source_map_get:
 * @cx: the current #JSContext
 * @registry: a JS Map object
 * @key: a source string, such as retrieved from a stack frame
 * @source_map_consumer_obj: handle where a source map consumer object will be
 *   stored
 *
 * This function retrieves a source map consumer from the source map registry,
 * or null if the source does not have a source map consumer.
 *
 * Returns: false if an exception is pending, otherwise true.
 */
bool gjs_global_source_map_get(
    JSContext* cx, JS::HandleObject registry, JS::HandleString key,
    JS::MutableHandleObject source_map_consumer_obj) {
    JS::RootedValue v_key{cx, JS::StringValue(key)};
    JS::RootedValue v_value{cx};
    if (!JS::MapGet(cx, registry, v_key, &v_value))
        return false;

    g_assert((v_value.isUndefined() || v_value.isObject()) &&
             "Invalid value in source map registry");

    if (v_value.isObject()) {
        source_map_consumer_obj.set(&v_value.toObject());
        return true;
    }

    source_map_consumer_obj.set(nullptr);
    return true;
}

/**
 * gjs_define_global_properties:
 * @cx: a #JSContext
 * @global: a JS global object that has not yet been passed to this function
 * @realm_name: (nullable): name of the realm, for debug output
 * @bootstrap_script: (nullable): name of a bootstrap script (found at
 * resource://org/gnome/gjs/modules/script/_bootstrap/@bootstrap_script) or
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
                                  GjsGlobalType global_type,
                                  const char* realm_name,
                                  const char* bootstrap_script) {
    gjs_set_global_slot(global.get(), GjsBaseGlobalSlot::GLOBAL_TYPE,
                        JS::Int32Value(static_cast<uint32_t>(global_type)));

    switch (global_type) {
        case GjsGlobalType::DEFAULT:
            return GjsGlobal::define_properties(cx, global, realm_name,
                                                bootstrap_script);
        case GjsGlobalType::DEBUGGER:
            return GjsDebuggerGlobal::define_properties(cx, global, realm_name,
                                                        bootstrap_script);
        case GjsGlobalType::INTERNAL:
            return GjsInternalGlobal::define_properties(cx, global, realm_name,
                                                        bootstrap_script);
    }

    // Global type does not handle define_properties
    g_assert_not_reached();
}

void detail::set_global_slot(JSObject* global, uint32_t slot, JS::Value value) {
    JS::SetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot, value);
}

JS::Value detail::get_global_slot(JSObject* global, uint32_t slot) {
    return JS::GetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot);
}

decltype(GjsGlobal::klass) constexpr GjsGlobal::klass;
decltype(GjsGlobal::static_funcs) constexpr GjsGlobal::static_funcs;
decltype(GjsGlobal::static_props) constexpr GjsGlobal::static_props;

decltype(GjsDebuggerGlobal::klass) constexpr GjsDebuggerGlobal::klass;
decltype(
    GjsDebuggerGlobal::static_funcs) constexpr GjsDebuggerGlobal::static_funcs;

decltype(GjsInternalGlobal::klass) constexpr GjsInternalGlobal::klass;
decltype(
    GjsInternalGlobal::static_funcs) constexpr GjsInternalGlobal::static_funcs;
