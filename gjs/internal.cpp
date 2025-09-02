// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <stddef.h>  // for size_t
#include <string.h>

#include <memory>  // for unique_ptr

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallAndConstruct.h>  // for JS_CallFunction
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>  // for JSEXN_ERR
#include <js/Exception.h>
#include <js/Id.h>  // for PropertyKey
#include <js/Modules.h>
#include <js/Promise.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>        // for JS_NewPlainObject, JS_ObjectIsFunction
#include <jsfriendapi.h>  // for JS_GetObjectFunction, SetFunctionNativeReserved

#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/engine.h"
#include "gjs/gerror-result.h"
#include "gjs/global.h"
#include "gjs/internal.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/module.h"
#include "util/log.h"
#include "util/misc.h"

namespace mozilla {
union Utf8Unit;
}

// NOTE: You have to be very careful in this file to only do operations within
// the correct global!

/**
 * gjs_load_internal_module:
 * @cx: the current JSContext
 * @identifier: the identifier of the internal module
 *
 * Loads a module source from an internal resource,
 * resource:///org/gnome/gjs/modules/internal/{#identifier}.js, registers it in
 * the internal global's module registry, and proceeds to compile, initialize,
 * and evaluate the module.
 *
 * Returns: whether an error occurred while loading or evaluating the module.
 */
bool gjs_load_internal_module(JSContext* cx, const char* identifier) {
    Gjs::AutoChar full_path(g_strdup_printf(
        "resource:///org/gnome/gjs/modules/internal/%s.js", identifier));

    gjs_debug(GJS_DEBUG_IMPORTER, "Loading internal module '%s' (%s)",
              identifier, full_path.get());

    Gjs::AutoChar script;
    size_t script_len;
    if (!gjs_load_internal_source(cx, full_path, script.out(), &script_len))
        return false;

    JS::SourceText<mozilla::Utf8Unit> buf;
    if (!buf.init(cx, script.get(), script_len, JS::SourceOwnership::Borrowed))
        return false;

    JS::CompileOptions options(cx);
    options.setIntroductionType("Internal Module Bootstrap");
    options.setFileAndLine(full_path, 1);
    options.setSelfHostingMode(false);

    Gjs::AutoInternalRealm ar{cx};
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    JS::RootedObject internal_global{cx, gjs->internal_global()};
    JS::RootedObject module{cx, JS::CompileModule(cx, options, buf)};
    if (!module)
        return false;

    JS::RootedObject registry{cx, gjs_get_module_registry(internal_global)};

    JS::RootedId key{cx, gjs_intern_string_to_id(cx, full_path)};
    if (key.isVoid())
        return false;

    JS::RootedValue ignore{cx};
    if (!gjs_global_registry_set(cx, registry, key, module) ||
        !JS::ModuleLink(cx, module) ||
        !JS::ModuleEvaluate(cx, module, &ignore)) {
        return false;
    }

    return true;
}

static bool handle_wrong_args(JSContext* cx) {
    gjs_log_exception(cx);
    g_error("Wrong invocation of internal code");
    return false;
}

/**
 * gjs_internal_set_global_module_loader:
 * @global: the JS global object
 * @loader: the JS module loader object to store in @global
 *
 * JS function exposed as `setGlobalModuleLoader` in the internal global scope.
 *
 * Sets the MODULE_LOADER slot of @global. @loader should be an instance of
 * ModuleLoader or InternalModuleLoader. Its `moduleResolveHook` and
 * `moduleLoadHook` properties will be called.
 *
 * Returns: JS undefined
 */
bool gjs_internal_set_global_module_loader(JSContext* cx, unsigned argc,
                                           JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject global(cx), loader(cx);
    if (!gjs_parse_call_args(cx, "setGlobalModuleLoader", args, "oo", "global",
                             &global, "loader", &loader))
        return handle_wrong_args(cx);

    gjs_set_global_slot(global, GjsGlobalSlot::MODULE_LOADER,
                        JS::ObjectValue(*loader));

    args.rval().setUndefined();
    return true;
}

/**
 * compile_module:
 * @cx: the current JSContext
 * @uri: The URI of the module
 * @source: The source text of the module
 * @v_module_out: (out): Return location for the module as a JS value
 *
 * Compiles the a module source text into an internal #Module object given the
 * module's URI as the first argument.
 *
 * Returns: whether an error occurred while compiling the module.
 */
static bool compile_module(JSContext* cx, const JS::UniqueChars& uri,
                           JS::HandleString source,
                           JS::MutableHandleValue v_module_out) {
    JS::CompileOptions options(cx);
    options.setFileAndLine(uri.get(), 1).setSourceIsLazy(false);

    size_t text_len;
    char16_t* text;
    if (!gjs_string_get_char16_data(cx, source, &text, &text_len))
        return false;

    JS::SourceText<char16_t> buf;
    if (!buf.init(cx, text, text_len, JS::SourceOwnership::TakeOwnership))
        return false;

    JS::RootedObject new_module(cx, JS::CompileModule(cx, options, buf));
    if (!new_module)
        return false;

    v_module_out.setObject(*new_module);
    return true;
}

/**
 * gjs_internal_compile_internal_module:
 * @uri: The URI of the module (JS string)
 * @source: The source text of the module (JS string)
 *
 * JS function exposed as `compileInternalModule` in the internal global scope.
 *
 * Compiles a module source text within the internal global's realm.
 *
 * NOTE: Modules compiled with this function can only be executed
 * within the internal global's realm.
 *
 * Returns: The compiled JS module object.
 */
bool gjs_internal_compile_internal_module(JSContext* cx, unsigned argc,
                                          JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    Gjs::AutoInternalRealm ar{cx};

    JS::UniqueChars uri;
    JS::RootedString source(cx);
    if (!gjs_parse_call_args(cx, "compileInternalModule", args, "sS", "uri",
                             &uri, "source", &source))
        return handle_wrong_args(cx);

    return compile_module(cx, uri, source, args.rval());
}

/**
 * gjs_internal_compile_module:
 * @uri: The URI of the module (JS string)
 * @source: The source text of the module (JS string)
 *
 * JS function exposed as `compileModule` in the internal global scope.
 *
 * Compiles a module source text within the main realm.
 *
 * NOTE: Modules compiled with this function can only be executed
 * within the main realm.
 *
 * Returns: The compiled JS module object.
 */
bool gjs_internal_compile_module(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    Gjs::AutoMainRealm ar{cx};

    JS::UniqueChars uri;
    JS::RootedString source(cx);
    if (!gjs_parse_call_args(cx, "compileModule", args, "sS", "uri", &uri,
                             "source", &source))
        return handle_wrong_args(cx);

    return compile_module(cx, uri, source, args.rval());
}

/**
 * gjs_internal_set_module_private:
 * @module: The JS module object
 * @private: The JS module private object for @module
 *
 * JS function exposed as `setModulePrivate` in the internal global scope.
 *
 * Sets the private object of an internal #Module object.
 *
 * Returns: JS undefined
 */
bool gjs_internal_set_module_private(JSContext* cx, unsigned argc,
                                     JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject module(cx), private_obj(cx);
    if (!gjs_parse_call_args(cx, "setModulePrivate", args, "oo", "module",
                             &module, "private", &private_obj))
        return handle_wrong_args(cx);

    JS::SetModulePrivate(module, JS::ObjectValue(*private_obj));
    return true;
}

/**
 * gjs_internal_get_registry:
 * @global: The JS global object
 *
 * JS function exposed as `getRegistry` in the internal global scope.
 *
 * Retrieves the module registry for @global.
 *
 * Returns: the module registry, a JS Map object.
 */
bool gjs_internal_get_registry(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject global(cx);
    if (!gjs_parse_call_args(cx, "getRegistry", args, "o", "global", &global))
        return handle_wrong_args(cx);

    JSAutoRealm ar(cx, global);

    JS::RootedObject registry(cx, gjs_get_module_registry(global));
    args.rval().setObject(*registry);
    return true;
}

/**
 * gjs_internal_get_source_map_registry:
 * @global: The JS global object
 *
 * JS function exposed as `getSourceMapRegistry` in the internal global scope.
 *
 * Retrieves the source map registry for @global.
 *
 * Returns: the source map registry, a JS Map object.
 */
bool gjs_internal_get_source_map_registry(JSContext* cx, unsigned argc,
                                          JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject global{cx};
    if (!gjs_parse_call_args(cx, "getSourceMapRegistry", args, "o", "global",
                             &global))
        return handle_wrong_args(cx);

    JSAutoRealm ar{cx, global};

    JSObject* registry = gjs_get_source_map_registry(global);
    args.rval().setObject(*registry);
    return true;
}

/**
 * gjs_uri_object:
 * @cx: the current JSContext
 * @uri: the URI to parse into a JS URI object
 * @rval: (out): handle to a JSValue where the URI object will be stored
 *
 * Parses @uri and creates a JS object with the various parsed parts available
 * as properties. See type `Uri` in modules/internal/environment.d.ts.
 *
 * Basically a JS wrapper for g_uri_parse() for use in the internal global scope
 * where we don't have access to gobject-introspection.
 *
 * Returns: false if an exception is pending, otherwise true
 */
static bool gjs_uri_object(JSContext* cx, const char* uri,
                           JS::MutableHandleValue rval) {
    using AutoHashTable =
        Gjs::AutoPointer<GHashTable, GHashTable, g_hash_table_destroy>;
    using AutoURI = Gjs::AutoPointer<GUri, GUri, g_uri_unref>;

    Gjs::AutoError error;
    AutoURI parsed = g_uri_parse(uri, G_URI_FLAGS_ENCODED, &error);
    if (!parsed) {
        Gjs::AutoMainRealm ar{cx};

        gjs_throw_custom(cx, JSEXN_ERR, "ImportError",
                         "Attempted to import invalid URI: %s (%s)", uri,
                         error->message);
        return false;
    }

    JS::RootedObject query_obj(cx, JS_NewPlainObject(cx));
    if (!query_obj)
        return false;

    const char* raw_query = g_uri_get_query(parsed);
    if (raw_query) {
        AutoHashTable query =
            g_uri_parse_params(raw_query, -1, "&", G_URI_PARAMS_NONE, &error);
        if (!query) {
            Gjs::AutoMainRealm ar{cx};

            gjs_throw_custom(cx, JSEXN_ERR, "ImportError",
                             "Attempted to import invalid URI: %s (%s)", uri,
                             error->message);
            return false;
        }

        GHashTableIter iter;
        g_hash_table_iter_init(&iter, query);

        void* key_ptr;
        void* value_ptr;
        while (g_hash_table_iter_next(&iter, &key_ptr, &value_ptr)) {
            auto* key = static_cast<const char*>(key_ptr);
            auto* value = static_cast<const char*>(value_ptr);

            JS::ConstUTF8CharsZ value_chars{value, strlen(value)};
            JS::RootedString value_str(cx,
                                       JS_NewStringCopyUTF8Z(cx, value_chars));
            if (!value_str || !JS_DefineProperty(cx, query_obj, key, value_str,
                                                 JSPROP_ENUMERATE))
                return false;
        }
    }

    JS::RootedObject return_obj(cx, JS_NewPlainObject(cx));
    if (!return_obj)
        return false;

    JS::RootedValue v_uri{cx};
    Gjs::AutoChar uri_string{g_uri_to_string(parsed)};
    if (!gjs_string_from_utf8(cx, uri_string, &v_uri))
        return false;

    // JS_NewStringCopyZ() used here and below because the URI components are
    // %-encoded, meaning ASCII-only
    JS::RootedString scheme(cx,
                            JS_NewStringCopyZ(cx, g_uri_get_scheme(parsed)));
    if (!scheme)
        return false;

    JS::RootedString host(cx, JS_NewStringCopyZ(cx, g_uri_get_host(parsed)));
    if (!host)
        return false;

    JS::RootedString path(cx, JS_NewStringCopyZ(cx, g_uri_get_path(parsed)));
    if (!path)
        return false;

    Gjs::AutoChar no_query_str{
        g_uri_to_string_partial(parsed, G_URI_HIDE_QUERY)};
    JS::RootedString uri_no_query{cx, JS_NewStringCopyZ(cx, no_query_str)};
    if (!uri_no_query)
        return false;

    if (!JS_DefineProperty(cx, return_obj, "uri", uri_no_query,
                           JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "uriWithQuery", v_uri,
                           JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "scheme", scheme,
                           JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "host", host, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "path", path, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "query", query_obj,
                           JSPROP_ENUMERATE))
        return false;

    rval.setObject(*return_obj);
    return true;
}

bool gjs_internal_parse_uri(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);
    JS::RootedString string_arg{cx};
    if (!gjs_parse_call_args(cx, "parseURI", args, "S", "uri", &string_arg))
        return handle_wrong_args(cx);

    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;

    return gjs_uri_object(cx, uri.get(), args.rval());
}

bool gjs_internal_resolve_relative_resource_or_file(JSContext* cx,
                                                    unsigned argc,
                                                    JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars uri, relative_path;
    if (!gjs_parse_call_args(cx, "resolveRelativeResourceOrFile", args, "ss",
                             "uri", &uri, "relativePath", &relative_path))
        return handle_wrong_args(cx);

    Gjs::AutoUnref<GFile> module_file{g_file_new_for_uri(uri.get())};

    Gjs::AutoChar output_uri{g_uri_resolve_relative(
        uri.get(), relative_path.get(), G_URI_FLAGS_NONE, nullptr)};

    return gjs_uri_object(cx, output_uri.get(), args.rval());
}

bool gjs_internal_load_resource_or_file(JSContext* cx, unsigned argc,
                                        JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);
    JS::UniqueChars uri;
    if (!gjs_parse_call_args(cx, "loadResourceOrFile", args, "s", "uri", &uri))
        return handle_wrong_args(cx);

    Gjs::AutoUnref<GFile> file{g_file_new_for_uri(uri.get())};

    char* contents;
    size_t length;
    Gjs::AutoError error;
    if (!g_file_load_contents(file, /* cancellable = */ nullptr, &contents,
                              &length, /* etag_out = */ nullptr, &error)) {
        Gjs::AutoMainRealm ar{cx};

        gjs_throw_custom(cx, JSEXN_ERR, "ImportError",
                         "Unable to load file from: %s (%s)", uri.get(),
                         error->message);
        return false;
    }

    JS::ConstUTF8CharsZ contents_chars{contents, length};
    JS::RootedString contents_str(cx,
                                  JS_NewStringCopyUTF8Z(cx, contents_chars));
    g_free(contents);
    if (!contents_str)
        return false;

    args.rval().setString(contents_str);
    return true;
}

bool gjs_internal_uri_exists(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);
    JS::UniqueChars uri;
    if (!gjs_parse_call_args(cx, "uriExists", args, "!s", "uri", &uri))
        return handle_wrong_args(cx);

    Gjs::AutoUnref<GFile> file{g_file_new_for_uri(uri.get())};

    args.rval().setBoolean(g_file_query_exists(file, nullptr));
    return true;
}

bool gjs_internal_atob(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars text;
    size_t result_len;
    if (!gjs_parse_call_args(cx, "atob", args, "!s", "text", &text))
        return handle_wrong_args(cx);

    Gjs::AutoChar decoded =
        reinterpret_cast<char*>(g_base64_decode(text.get(), &result_len));
    JS::ConstUTF8CharsZ contents_chars{decoded, result_len};
    JS::RootedString contents_str{cx,
                                  JS_NewStringCopyUTF8Z(cx, contents_chars)};

    if (!contents_str)
        return false;

    args.rval().setString(contents_str);
    return true;
}

class PromiseData {
 public:
    JSContext* cx;

 private:
    JS::PersistentRooted<JSFunction*> m_resolve;
    JS::PersistentRooted<JSFunction*> m_reject;

    JS::HandleFunction resolver() { return m_resolve; }
    JS::HandleFunction rejecter() { return m_reject; }

 public:
    explicit PromiseData(JSContext* a_cx, JSFunction* resolve,
                         JSFunction* reject)
        : cx(a_cx), m_resolve(cx, resolve), m_reject(cx, reject) {}

    static PromiseData* from_ptr(void* ptr) {
        return static_cast<PromiseData*>(ptr);
    }

    // Adapted from SpiderMonkey js::RejectPromiseWithPendingError()
    // https://searchfox.org/mozilla-central/rev/95cf843de977805a3951f9137f5ff1930599d94e/js/src/builtin/Promise.cpp#4435
    void reject_with_pending_exception() {
        JS::RootedValue exception(cx);
        bool ok GJS_USED_ASSERT = JS_GetPendingException(cx, &exception);
        g_assert(ok && "Cannot reject a promise with an uncatchable exception");

        JS::RootedValueArray<1> args(cx);
        args[0].set(exception);
        JS::RootedValue ignored_rval(cx);
        ok = JS_CallFunction(cx, /* this_obj = */ nullptr, rejecter(), args,
                             &ignored_rval);
        g_assert(ok && "Failed rejecting promise");
    }

    void resolve(JS::Value result) {
        JS::RootedValueArray<1> args(cx);
        args[0].set(result);
        JS::RootedValue ignored_rval(cx);
        bool ok GJS_USED_ASSERT = JS_CallFunction(
            cx, /* this_obj = */ nullptr, resolver(), args, &ignored_rval);
        g_assert(ok && "Failed resolving promise");
    }
};

static void load_async_callback(GObject* file, GAsyncResult* res, void* data) {
    std::unique_ptr<PromiseData> promise(PromiseData::from_ptr(data));

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(promise->cx);
    gjs->main_loop_release();

    Gjs::AutoMainRealm ar{gjs};

    char* contents;
    size_t length;
    Gjs::AutoError error;
    if (!g_file_load_contents_finish(G_FILE(file), res, &contents, &length,
                                     /* etag_out = */ nullptr, &error)) {
        Gjs::AutoChar uri{g_file_get_uri(G_FILE(file))};
        gjs_throw_custom(promise->cx, JSEXN_ERR, "ImportError",
                         "Unable to load file async from: %s (%s)", uri.get(),
                         error->message);
        promise->reject_with_pending_exception();
        return;
    }

    JS::RootedValue text(promise->cx);
    bool ok = gjs_string_from_utf8_n(promise->cx, contents, length, &text);
    g_free(contents);
    if (!ok) {
        promise->reject_with_pending_exception();
        return;
    }

    promise->resolve(text);
}

GJS_JSAPI_RETURN_CONVENTION
static bool load_async_executor(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);
    JS::RootedObject resolve(cx), reject(cx);
    if (!gjs_parse_call_args(cx, "executor", args, "oo", "resolve", &resolve,
                             "reject", &reject))
        return handle_wrong_args(cx);

    g_assert(JS_ObjectIsFunction(resolve) && "Executor called weirdly");
    g_assert(JS_ObjectIsFunction(reject) && "Executor called weirdly");

    JS::Value priv_value = js::GetFunctionNativeReserved(&args.callee(), 0);
    g_assert(!priv_value.isNull() && "Executor called twice");
    Gjs::AutoUnref<GFile> file{G_FILE(priv_value.toPrivate())};
    g_assert(file && "Executor called twice");
    // We now own the GFile, and will pass the reference to the GAsyncResult, so
    // remove it from the executor's private slot so it doesn't become dangling
    js::SetFunctionNativeReserved(&args.callee(), 0, JS::NullValue());

    auto* data = new PromiseData(cx, JS_GetObjectFunction(resolve),
                                 JS_GetObjectFunction(reject));

    // Hold the main loop until this function resolves...
    GjsContextPrivate::from_cx(cx)->main_loop_hold();
    g_file_load_contents_async(file, nullptr, load_async_callback, data);

    args.rval().setUndefined();
    return true;
}

bool gjs_internal_load_resource_or_file_async(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);
    JS::UniqueChars uri;
    if (!gjs_parse_call_args(cx, "loadResourceOrFileAsync", args, "s", "uri",
                             &uri))
        return handle_wrong_args(cx);

    Gjs::AutoUnref<GFile> file{g_file_new_for_uri(uri.get())};

    JS::RootedObject executor(cx,
                              JS_GetFunctionObject(js::NewFunctionWithReserved(
                                  cx, load_async_executor, 2, 0,
                                  "loadResourceOrFileAsync executor")));
    if (!executor)
        return false;

    // Stash the file object for the callback to find later; executor owns it
    js::SetFunctionNativeReserved(executor, 0, JS::PrivateValue(file.copy()));

    JSObject* promise = JS::NewPromiseObject(cx, executor);
    if (!promise)
        return false;

    args.rval().setObject(*promise);
    return true;
}
