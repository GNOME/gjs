/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <stddef.h>     // for size_t
#include <string.h>
#include <sys/types.h>  // for ssize_t

#include <string>  // for u16string

#include <gio/gio.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for ConstUTF8CharsZ
#include <js/Class.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/Conversions.h>
#include <js/GCVector.h>  // for RootedVector
#include <js/Id.h>
#include <js/Modules.h>
#include <js/Promise.h>
#include <js/PropertyDescriptor.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>  // for JS_DefinePropertyById, ...
#include <jsfriendapi.h>  // for SetFunctionNativeReserved

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "util/log.h"
#include "util/misc.h"

class GjsScriptModule {
    char *m_name;

    GjsScriptModule(const char* name) {
        m_name = g_strdup(name);
        GJS_INC_COUNTER(module);
    }

    ~GjsScriptModule() {
        g_free(m_name);
        GJS_DEC_COUNTER(module);
    }

    GjsScriptModule(GjsScriptModule&) = delete;
    GjsScriptModule& operator=(GjsScriptModule&) = delete;

    /* Private data accessors */

    [[nodiscard]] static inline GjsScriptModule* priv(JSObject* module) {
        return static_cast<GjsScriptModule*>(JS_GetPrivate(module));
    }

    /* Creates a JS module object. Use instead of the class's constructor */
    [[nodiscard]] static JSObject* create(JSContext* cx, const char* name) {
        JSObject* module = JS_NewObject(cx, &GjsScriptModule::klass);
        JS_SetPrivate(module, new GjsScriptModule(name));
        return module;
    }

    /* Defines the empty module as a property on the importer */
    GJS_JSAPI_RETURN_CONVENTION
    bool
    define_import(JSContext       *cx,
                  JS::HandleObject module,
                  JS::HandleObject importer,
                  JS::HandleId     name) const
    {
        if (!JS_DefinePropertyById(cx, importer, name, module,
                                   GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT)) {
            gjs_debug(GJS_DEBUG_IMPORTER, "Failed to define '%s' in importer",
                      m_name);
            return false;
        }

        return true;
    }

    /* Carries out the actual execution of the module code */
    GJS_JSAPI_RETURN_CONVENTION
    bool evaluate_import(JSContext* cx, JS::HandleObject module,
                         const char* script, ssize_t script_len,
                         const char* filename, const char* uri) {
        long items_written;  // NOLINT(runtime/int) - required by GLib API
        GError* error;
        GjsAutoChar16 utf16_string =
            g_utf8_to_utf16(script, script_len,
                            /* items_read = */ nullptr, &items_written, &error);
        if (!utf16_string)
            return gjs_throw_gerror_message(cx, error);

        // COMPAT: This could use JS::SourceText<mozilla::Utf8Unit> directly,
        // but that messes up code coverage. See bug
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1404784
        JS::SourceText<char16_t> buf;
        if (!buf.init(cx, reinterpret_cast<char16_t*>(utf16_string.get()),
                      items_written, JS::SourceOwnership::Borrowed))
            return false;

        JS::RootedObjectVector scope_chain(cx);
        if (!scope_chain.append(module)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        JS::CompileOptions options(cx);
        options.setFileAndLine(filename, 1);

        JS::RootedObject priv(cx, build_private(cx, uri));
        options.setPrivateValue(JS::ObjectValue(*priv));

        JS::RootedValue ignored_retval(cx);
        if (!JS::Evaluate(cx, scope_chain, options, buf, &ignored_retval))
            return false;

        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
        gjs->schedule_gc_if_needed();

        gjs_debug(GJS_DEBUG_IMPORTER, "Importing module %s succeeded", m_name);

        return true;
    }

    /* Loads JS code from a file and imports it */
    GJS_JSAPI_RETURN_CONVENTION
    bool
    import_file(JSContext       *cx,
                JS::HandleObject module,
                GFile           *file)
    {
        GError *error = nullptr;
        char *unowned_script;
        size_t script_len = 0;

        if (!(g_file_load_contents(file, nullptr, &unowned_script, &script_len,
                                   nullptr, &error)))
            return gjs_throw_gerror_message(cx, error);

        GjsAutoChar script = unowned_script;  /* steals ownership */
        g_assert(script);

        GjsAutoChar full_path = g_file_get_parse_name(file);
        GjsAutoChar uri = g_file_get_uri(file);
        return evaluate_import(cx, module, script, script_len, full_path, uri);
    }

    /* JSClass operations */

    GJS_JSAPI_RETURN_CONVENTION
    bool
    resolve_impl(JSContext       *cx,
                 JS::HandleObject module,
                 JS::HandleId     id,
                 bool            *resolved)
    {
        JS::RootedObject lexical(cx, JS_ExtensibleLexicalEnvironment(module));
        if (!lexical) {
            *resolved = false;
            return true;  /* nothing imported yet */
        }

        if (!JS_HasPropertyById(cx, lexical, id, resolved))
            return false;
        if (!*resolved)
            return true;

        /* The property is present in the lexical environment. This should not
         * be supported according to ES6. For compatibility with earlier GJS,
         * we treat it as if it were a real property, but warn about it. */

        g_warning("Some code accessed the property '%s' on the module '%s'. "
                  "That property was defined with 'let' or 'const' inside the "
                  "module. This was previously supported, but is not correct "
                  "according to the ES6 standard. Any symbols to be exported "
                  "from a module must be defined with 'var'. The property "
                  "access will work as previously for the time being, but "
                  "please fix your code anyway.",
                  gjs_debug_id(id).c_str(), m_name);

        JS::Rooted<JS::PropertyDescriptor> desc(cx);
        return JS_GetPropertyDescriptorById(cx, lexical, id, &desc) &&
            JS_DefinePropertyById(cx, module, id, desc);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool
    resolve(JSContext       *cx,
            JS::HandleObject module,
            JS::HandleId     id,
            bool            *resolved)
    {
        return priv(module)->resolve_impl(cx, module, id, resolved);
    }

    static void finalize(JSFreeOp*, JSObject* module) { delete priv(module); }

    static constexpr JSClassOps class_ops = {
        nullptr,  // addProperty
        nullptr,  // deleteProperty
        nullptr,  // enumerate
        nullptr,  // newEnumerate
        &GjsScriptModule::resolve,
        nullptr,  // mayResolve
        &GjsScriptModule::finalize,
    };

    static constexpr JSClass klass = {
        "GjsScriptModule",
        JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &GjsScriptModule::class_ops,
    };

 public:
    /*
     * Creates a JS object to pass to JS::CompileOptions as a script's private.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* build_private(JSContext* cx, const char* script_uri) {
        JS::RootedObject priv(cx, JS_NewPlainObject(cx));
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

        JS::RootedValue val(cx);
        if (!gjs_string_from_utf8(cx, script_uri, &val) ||
            !JS_SetPropertyById(cx, priv, atoms.uri(), val))
            return nullptr;

        return priv;
    }

    /* Carries out the import operation */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject *
    import(JSContext       *cx,
           JS::HandleObject importer,
           JS::HandleId     id,
           const char      *name,
           GFile           *file)
    {
        JS::RootedObject module(cx, GjsScriptModule::create(cx, name));
        if (!module ||
            !priv(module)->define_import(cx, module, importer, id) ||
            !priv(module)->import_file(cx, module, file))
            return nullptr;

        return module;
    }
};

/**
 * gjs_script_module_build_private:
 * @param cx the #JSContext
 * @param uri the URI this script module is loaded from
 *
 * @brief To support dynamic imports from scripts, we need to provide private
 * data when we compile scripts which is compatible with our module resolution
 * hooks in modules/internal/loader.js
 *
 * @returns a JSObject which can be used for a JSScript's private data.
 */
JSObject* gjs_script_module_build_private(JSContext* cx, const char* uri) {
    return GjsScriptModule::build_private(cx, uri);
}

/**
 * gjs_module_import:
 * @cx: the JS context
 * @importer: the JS importer object, parent of the module to be imported
 * @id: module name in the form of a jsid
 * @name: module name, used for logging and identification
 * @file: location of the file to import
 *
 * Carries out an import of a GJS module.
 * Defines a property @name on @importer pointing to the module object, which
 * is necessary in the case of cyclic imports.
 * This property is not permanent; the caller is responsible for making it
 * permanent if the import succeeds.
 *
 * Returns: the JS module object, or nullptr on failure.
 */
JSObject *
gjs_module_import(JSContext       *cx,
                  JS::HandleObject importer,
                  JS::HandleId     id,
                  const char      *name,
                  GFile           *file)
{
    return GjsScriptModule::import(cx, importer, id, name, file);
}

decltype(GjsScriptModule::klass) constexpr GjsScriptModule::klass;
decltype(GjsScriptModule::class_ops) constexpr GjsScriptModule::class_ops;

/**
 * gjs_get_native_registry:
 *
 * @brief Retrieves a global's native registry from the NATIVE_REGISTRY slot.
 * Registries are JS Map objects created with JS::NewMapObject instead
 * of GCHashMaps (used elsewhere in GJS) because the objects need to be
 * exposed to internal JS code and accessed from native C++ code.
 *
 * @param global a global #JSObject
 *
 * @returns the registry map as a #JSObject
 */
JSObject* gjs_get_native_registry(JSObject* global) {
    JS::Value native_registry =
        gjs_get_global_slot(global, GjsGlobalSlot::NATIVE_REGISTRY);

    g_assert(native_registry.isObject());
    return &native_registry.toObject();
}

/**
 * gjs_get_module_registry:
 *
 * @brief Retrieves a global's module registry from the MODULE_REGISTRY slot.
 * Registries are JS Maps. See gjs_get_native_registry for more detail.
 *
 * @param cx the current #JSContext
 * @param global a global #JSObject
 *
 * @returns the registry map as a #JSObject
 */
JSObject* gjs_get_module_registry(JSObject* global) {
    JS::Value esm_registry =
        gjs_get_global_slot(global, GjsGlobalSlot::MODULE_REGISTRY);

    g_assert(esm_registry.isObject());
    return &esm_registry.toObject();
}

/**
 * gjs_module_load:
 *
 * Loads and registers a module given a specifier and
 * URI.
 *
 * @returns whether an error occurred while resolving the specifier.
 */
JSObject* gjs_module_load(JSContext* cx, const char* identifier,
                          const char* file_uri) {
    g_assert((gjs_global_is_type(cx, GjsGlobalType::DEFAULT) ||
              gjs_global_is_type(cx, GjsGlobalType::INTERNAL)) &&
             "gjs_module_load can only be called from module-enabled "
             "globals.");

    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    JS::RootedValue v_loader(
        cx, gjs_get_global_slot(global, GjsGlobalSlot::MODULE_LOADER));
    g_assert(v_loader.isObject());
    JS::RootedObject loader(cx, &v_loader.toObject());

    JS::ConstUTF8CharsZ id_chars(identifier, strlen(identifier));
    JS::ConstUTF8CharsZ uri_chars(file_uri, strlen(file_uri));
    JS::RootedString id(cx, JS_NewStringCopyUTF8Z(cx, id_chars));
    if (!id)
        return nullptr;
    JS::RootedString uri(cx, JS_NewStringCopyUTF8Z(cx, uri_chars));
    if (!uri)
        return nullptr;

    JS::RootedValueArray<2> args(cx);
    args[0].setString(id);
    args[1].setString(uri);

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Module resolve hook for module '%s' (%s), global %p", identifier,
              file_uri, global.get());

    JS::RootedValue result(cx);
    if (!JS::Call(cx, loader, "moduleLoadHook", args, &result))
        return nullptr;

    g_assert(result.isObject() && "Module hook failed to return an object!");
    return &result.toObject();
}

/**
 * import_native_module_sync:
 *
 * @brief Synchronously imports native "modules" from the import global's
 * native registry. This function does not do blocking I/O so it is
 * safe to call it synchronously for accessing native "modules" within
 * modules. This function is always called within the import global's
 * realm.
 *
 * Compare gjs_import_native_module() for the legacy importer.
 *
 * @param cx the current JSContext
 * @param argc
 * @param vp
 *
 * @returns whether an error occurred while importing the native module.
 */
static bool import_native_module_sync(JSContext* cx, unsigned argc,
                                      JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars id;
    if (!gjs_parse_call_args(cx, "importSync", args, "s", "identifier", &id))
        return false;

    JS::RootedObject global(cx, gjs_get_import_global(cx));
    JSAutoRealm ar(cx, global);

    JS::AutoSaveExceptionState exc_state(cx);

    JS::RootedObject native_registry(cx, gjs_get_native_registry(global));
    JS::RootedObject v_module(cx);

    JS::RootedId key(cx, gjs_intern_string_to_id(cx, id.get()));
    if (!gjs_global_registry_get(cx, native_registry, key, &v_module))
        return false;

    if (v_module) {
        args.rval().setObject(*v_module);
        return true;
    }

    JS::RootedObject native_obj(cx);
    if (!gjs_load_native_module(cx, id.get(), &native_obj)) {
        gjs_throw(cx, "Failed to load native module: %s", id.get());
        return false;
    }

    if (!gjs_global_registry_set(cx, native_registry, key, native_obj))
        return false;

    args.rval().setObject(*native_obj);
    return true;
}

/**
 * gjs_populate_module_meta:
 *
 * Hook SpiderMonkey calls to populate the import.meta object.
 * Defines a property "import.meta.url", and additionally a method
 * "import.meta.importSync" if this is an internal module.
 *
 * @param private_ref the private value for the #Module object
 * @param meta the import.meta object
 *
 * @returns whether an error occurred while populating the module meta.
 */
bool gjs_populate_module_meta(JSContext* cx, JS::HandleValue private_ref,
                              JS::HandleObject meta) {
    g_assert(private_ref.isObject());
    JS::RootedObject module(cx, &private_ref.toObject());

    gjs_debug(GJS_DEBUG_IMPORTER, "Module metadata hook for module %p",
              &private_ref.toObject());

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue v_uri(cx);
    if (!JS_GetPropertyById(cx, module, atoms.uri(), &v_uri) ||
        !JS_DefinePropertyById(cx, meta, atoms.url(), v_uri,
                               GJS_MODULE_PROP_FLAGS))
        return false;

    JS::RootedValue v_internal(cx);
    if (!JS_GetPropertyById(cx, module, atoms.internal(), &v_internal))
        return false;
    if (JS::ToBoolean(v_internal)) {
        gjs_debug(GJS_DEBUG_IMPORTER, "Defining meta.importSync for module %p",
                  &private_ref.toObject());
        if (!JS_DefineFunctionById(cx, meta, atoms.importSync(),
                                   import_native_module_sync, 1,
                                   GJS_MODULE_PROP_FLAGS))
            return false;
    }

    return true;
}

/**
 * gjs_module_resolve:
 *
 * Hook SpiderMonkey calls to resolve import specifiers.
 *
 * @param importingModulePriv the private value of the #Module object initiating
 *   the import.
 * @param specifier the import specifier to resolve
 *
 * @returns whether an error occurred while resolving the specifier.
 */
JSObject* gjs_module_resolve(JSContext* cx, JS::HandleValue importingModulePriv,
                             JS::HandleString specifier) {
    g_assert((gjs_global_is_type(cx, GjsGlobalType::DEFAULT) ||
              gjs_global_is_type(cx, GjsGlobalType::INTERNAL)) &&
             "gjs_module_resolve can only be called from module-enabled "
             "globals.");
    g_assert(importingModulePriv.isObject() &&
             "the importing module can't be null, don't add import to the "
             "bootstrap script");

    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    JS::RootedValue v_loader(
        cx, gjs_get_global_slot(global, GjsGlobalSlot::MODULE_LOADER));
    g_assert(v_loader.isObject());
    JS::RootedObject loader(cx, &v_loader.toObject());

    JS::RootedValueArray<2> args(cx);
    args[0].set(importingModulePriv);
    args[1].setString(specifier);

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Module resolve hook for module '%s' (relative to %p), global %p",
              gjs_debug_string(specifier).c_str(),
              &importingModulePriv.toObject(), global.get());

    JS::RootedValue result(cx);
    if (!JS::Call(cx, loader, "moduleResolveHook", args, &result))
        return nullptr;

    g_assert(result.isObject() && "resolve hook failed to return an object!");
    return &result.toObject();
}

// Call JS::FinishDynamicModuleImport() with the values stashed in the function.
// Can fail in JS::FinishDynamicModuleImport(), but will assert if anything
// fails in fetching the stashed values, since that would be a serious GJS bug.
GJS_JSAPI_RETURN_CONVENTION
static bool finish_import(JSContext* cx, const JS::CallArgs& args) {
    JS::Value callback_priv = js::GetFunctionNativeReserved(&args.callee(), 0);
    g_assert(callback_priv.isObject() && "Wrong private value");
    JS::RootedObject callback_data(cx, &callback_priv.toObject());

    JS::RootedValue importing_module_priv(cx);
    JS::RootedValue v_specifier(cx);
    JS::RootedValue v_internal_promise(cx);
    bool ok GJS_USED_ASSERT =
        JS_GetProperty(cx, callback_data, "priv", &importing_module_priv) &&
        JS_GetProperty(cx, callback_data, "promise", &v_internal_promise) &&
        JS_GetProperty(cx, callback_data, "specifier", &v_specifier);
    g_assert(ok && "Wrong properties on private value");

    g_assert(v_specifier.isString() && "Wrong type for specifier");
    g_assert(v_internal_promise.isObject() && "Wrong type for promise");

    JS::RootedString specifier(cx, v_specifier.toString());
    JS::RootedObject internal_promise(cx, &v_internal_promise.toObject());

    args.rval().setUndefined();
    return JS::FinishDynamicModuleImport(cx, importing_module_priv, specifier,
                                         internal_promise);
}

// Failing a JSAPI function may result either in an exception pending on the
// context, in which case we must call JS::FinishDynamicModuleImport() to reject
// the internal promise; or in an uncatchable exception such as OOM, in which
// case we must not call JS::FinishDynamicModuleImport().
GJS_JSAPI_RETURN_CONVENTION
static bool fail_import(JSContext* cx, const JS::CallArgs& args) {
    if (JS_IsExceptionPending(cx))
        return finish_import(cx, args);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool import_rejected(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    gjs_debug(GJS_DEBUG_IMPORTER, "Async import promise rejected");

    // Throw the value that the promise is rejected with, so that
    // FinishDynamicModuleImport will reject the internal_promise with it.
    JS_SetPendingException(cx, args.get(0),
                           JS::ExceptionStackBehavior::DoNotCapture);

    return finish_import(cx, args);
}

GJS_JSAPI_RETURN_CONVENTION
static bool import_resolved(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    gjs_debug(GJS_DEBUG_IMPORTER, "Async import promise resolved");

    JS::RootedObject global(cx, gjs_get_import_global(cx));
    JSAutoRealm ar(cx, global);

    g_assert(args[0].isObject());
    JS::RootedObject module(cx, &args[0].toObject());

    if (!JS::ModuleInstantiate(cx, module) || !JS::ModuleEvaluate(cx, module))
        return fail_import(cx, args);

    return finish_import(cx, args);
}

bool gjs_dynamic_module_resolve(JSContext* cx,
                                JS::HandleValue importing_module_priv,
                                JS::HandleString specifier,
                                JS::HandleObject internal_promise) {
    g_assert(gjs_global_is_type(cx, GjsGlobalType::DEFAULT) &&
             "gjs_dynamic_module_resolve can only be called from the default "
             "global.");

    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    JSAutoRealm ar(cx, global);

    JS::RootedValue v_loader(
        cx, gjs_get_global_slot(global, GjsGlobalSlot::MODULE_LOADER));
    g_assert(v_loader.isObject());
    JS::RootedObject loader(cx, &v_loader.toObject());

    JS::RootedObject callback_data(cx, JS_NewPlainObject(cx));
    if (!callback_data ||
        !JS_DefineProperty(cx, callback_data, "specifier", specifier,
                           JSPROP_PERMANENT) ||
        !JS_DefineProperty(cx, callback_data, "promise", internal_promise,
                           JSPROP_PERMANENT) ||
        !JS_DefineProperty(cx, callback_data, "priv", importing_module_priv,
                           JSPROP_PERMANENT))
        return false;

    if (importing_module_priv.isObject()) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Async module resolve hook for module '%s' (relative to %p), "
                  "global %p",
                  gjs_debug_string(specifier).c_str(),
                  &importing_module_priv.toObject(), global.get());
    } else {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Async module resolve hook for module '%s' (unknown path), "
                  "global %p",
                  gjs_debug_string(specifier).c_str(), global.get());
    }

    JS::RootedValueArray<2> args(cx);
    args[0].set(importing_module_priv);
    args[1].setString(specifier);

    JS::RootedValue result(cx);
    if (!JS::Call(cx, loader, "moduleResolveAsyncHook", args, &result))
        return JS::FinishDynamicModuleImport(cx, importing_module_priv,
                                             specifier, internal_promise);

    JS::RootedObject resolved(
        cx, JS_GetFunctionObject(js::NewFunctionWithReserved(
                cx, import_resolved, 1, 0, "async import resolved")));
    if (!resolved)
        return false;
    JS::RootedObject rejected(
        cx, JS_GetFunctionObject(js::NewFunctionWithReserved(
                cx, import_rejected, 1, 0, "async import rejected")));
    if (!rejected)
        return false;
    js::SetFunctionNativeReserved(resolved, 0, JS::ObjectValue(*callback_data));
    js::SetFunctionNativeReserved(rejected, 0, JS::ObjectValue(*callback_data));

    JS::RootedObject promise(cx, &result.toObject());

    // Calling JS::FinishDynamicModuleImport() at the end of the resolve and
    // reject handlers will also call the module resolve hook. The module will
    // already have been resolved, but that is how SpiderMonkey obtains the
    // module object.
    return JS::AddPromiseReactions(cx, promise, resolved, rejected);
}
