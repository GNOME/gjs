// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#include "gjs/internal.h"

#include <config.h>
#include <gio/gio.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>
#include <js/Array.h>
#include <js/Class.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/Conversions.h>
#include <js/GCVector.h>  // for RootedVector
#include <js/Modules.h>
#include <js/Promise.h>
#include <js/PropertyDescriptor.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Wrapper.h>
#include <jsapi.h>  // for JS_DefinePropertyById, ...
#include <jsfriendapi.h>
#include <stddef.h>     // for size_t
#include <sys/types.h>  // for ssize_t

#include <codecvt>  // for codecvt_utf8_utf16
#include <locale>   // for wstring_convert
#include <memory>   // for unique_ptr
#include <string>   // for u16string
#include <vector>

#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/engine.h"
#include "gjs/error-types.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "util/log.h"
#include "util/misc.h"

#include "gi/repo.h"

// NOTE: You have to be very careful in this file to only do operations within
// the correct global!

/**
 * gjs_load_internal_module:
 *
 * @brief Loads a module source from an internal resource,
 * resource:///org/gnome/gjs/modules/internal/{#identifier}.js, registers it in
 * the internal global's module registry, and proceeds to compile, initialize,
 * and evaluate the module.
 *
 * @param cx the current JSContext
 * @param identifier the identifier of the internal module
 *
 * @returns whether an error occurred while loading or evaluating the module.
 */
bool gjs_load_internal_module(JSContext* cx, const char* identifier) {
    GjsAutoChar full_path(g_strdup_printf(
        "resource:///org/gnome/gjs/modules/internal/%s.js", identifier));

    gjs_debug(GJS_DEBUG_IMPORTER, "Loading internal module '%s' (%s)",
              identifier, full_path.get());

    char* script;
    size_t script_len;

    if (!gjs_load_internal_source(cx, full_path, &script, &script_len))
        return false;

    std::u16string utf16_string = gjs_utf8_script_to_utf16(script, script_len);
    g_free(script);

    // COMPAT: This could use JS::SourceText<mozilla::Utf8Unit> directly,
    // but that messes up code coverage. See bug
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1404784
    JS::SourceText<char16_t> buf;
    if (!buf.init(cx, utf16_string.c_str(), utf16_string.size(),
                  JS::SourceOwnership::Borrowed))
        return false;

    JS::CompileOptions options(cx);
    options.setIntroductionType("Internal Module Bootstrap");
    options.setFileAndLine(full_path, 1);
    options.setSelfHostingMode(false);

    JS::RootedObject internal_global(cx, gjs_get_internal_global(cx));
    JSAutoRealm ar(cx, internal_global);

    JS::RootedObject module(cx, JS::CompileModule(cx, options, buf));
    JS::RootedObject registry(cx, gjs_get_module_registry(internal_global));

    JS::RootedId key(cx, gjs_intern_string_to_id(cx, full_path));

    if (!gjs_global_registry_set(cx, registry, key, module) ||
        !JS::ModuleInstantiate(cx, module) || !JS::ModuleEvaluate(cx, module)) {
        return false;
    }

    return true;
}

/**
 * gjs_internal_set_global_module_loader:
 *
 * @brief Sets the MODULE_LOADER slot of the passed global object.
 * The second argument should be an instance of ModuleLoader or
 * InternalModuleLoader. Its moduleResolveHook and moduleLoadHook properties
 * will be called.
 *
 * @returns guaranteed to return true or assert.
 */
bool gjs_internal_set_global_module_loader(JSContext*, unsigned argc,
                                           JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(args.length() == 2 && "setGlobalModuleLoader takes 2 arguments");

    JS::Value v_global = args[0];
    JS::Value v_loader = args[1];

    g_assert(v_global.isObject() && "first argument must be an object");
    g_assert(v_loader.isObject() && "second argument must be an object");

    gjs_set_global_slot(&v_global.toObject(), GjsGlobalSlot::MODULE_LOADER,
                        v_loader);

    args.rval().setUndefined();
    return true;
}

/**
 * compile_module:
 *
 * @brief Compiles the a module source text into an internal #Module object
 * given the module's URI as the first argument.
 *
 * @param cx the current JSContext
 * @param args the call args from the native function call
 *
 * @returns whether an error occurred while compiling the module.
 */
static bool compile_module(JSContext* cx, JS::CallArgs args) {
    g_assert(args[0].isString());
    g_assert(args[1].isString());

    JS::RootedString s1(cx, args[0].toString());
    JS::RootedString s2(cx, args[1].toString());

    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, s1);
    if (!uri)
        return false;

    JS::CompileOptions options(cx);
    options.setFileAndLine(uri.get(), 1).setSourceIsLazy(false);

    size_t text_len;
    char16_t* text;
    if (!gjs_string_get_char16_data(cx, s2, &text, &text_len))
        return false;

    JS::SourceText<char16_t> buf;
    if (!buf.init(cx, text, text_len, JS::SourceOwnership::TakeOwnership))
        return false;

    JS::RootedObject new_module(cx, JS::CompileModule(cx, options, buf));
    if (!new_module)
        return false;

    args.rval().setObject(*new_module);
    return true;
}

/**
 * gjs_internal_compile_internal_module:
 *
 * @brief Compiles a module source text within the internal global's realm.
 *
 * NOTE: Modules compiled with this function can only be executed
 * within the internal global's realm.
 *
 * @param cx the current JSContext
 * @param argc
 * @param vp
 *
 * @returns whether an error occurred while compiling the module.
 */
bool gjs_internal_compile_internal_module(JSContext* cx, unsigned argc,
                                          JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(args.length() == 2 && "compileInternalModule takes 2 arguments");

    JS::RootedObject global(cx, gjs_get_internal_global(cx));
    JSAutoRealm ar(cx, global);
    return compile_module(cx, args);
}

/**
 * gjs_internal_compile_module:
 *
 * @brief Compiles a module source text within the import global's realm.
 *
 * NOTE: Modules compiled with this function can only be executed
 * within the import global's realm.
 *
 * @param cx the current JSContext
 * @param argc
 * @param vp
 *
 * @returns whether an error occurred while compiling the module.
 */
bool gjs_internal_compile_module(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(args.length() == 2 && "compileModule takes 2 arguments");

    JS::RootedObject global(cx, gjs_get_import_global(cx));
    JSAutoRealm ar(cx, global);
    return compile_module(cx, args);
}

/**
 * gjs_internal_set_module_private:
 *
 * @brief Sets the private object of an internal #Module object.
 * The private object must be a #JSObject.
 *
 * @param cx the current JSContext
 * @param argc
 * @param vp
 *
 * @returns whether an error occurred while setting the private.
 */
bool gjs_internal_set_module_private(JSContext* cx, unsigned argc,
                                     JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(args.length() == 2 && "setModulePrivate takes 2 arguments");
    g_assert(args[0].isObject());
    g_assert(args[1].isObject());

    JS::RootedObject moduleObj(cx, &args[0].toObject());
    JS::RootedObject privateObj(cx, &args[1].toObject());

    JS::SetModulePrivate(moduleObj, JS::ObjectValue(*privateObj));
    return true;
}

/**
 * gjs_internal_get_registry:
 *
 * @brief Retrieves the module registry for the passed global object.
 *
 * @param cx the current JSContext
 * @param argc
 * @param vp
 *
 * @returns whether an error occurred while retrieving the registry.
 */
bool gjs_internal_get_registry(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(args.length() == 1 && "getRegistry takes 1 argument");
    g_assert(args[0].isObject());

    JS::RootedObject global(cx, &args[0].toObject());
    JSAutoRealm ar(cx, global);

    JS::RootedObject registry(cx, gjs_get_module_registry(global));
    args.rval().setObject(*registry);
    return true;
}

bool gjs_internal_parse_uri(JSContext* cx, unsigned argc, JS::Value* vp) {
    using AutoHashTable =
        GjsAutoPointer<GHashTable, GHashTable, g_hash_table_destroy>;
    using AutoURI = GjsAutoPointer<GUri, GUri, g_uri_unref>;

    JS::CallArgs args = CallArgsFromVp(argc, vp);

    g_assert(args.length() == 1 && "parseUri() takes one string argument");
    g_assert(args[0].isString() && "parseUri() takes one string argument");

    JS::RootedString string_arg(cx, args[0].toString());
    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;

    GError* error = nullptr;
    AutoURI parsed = g_uri_parse(uri.get(), G_URI_FLAGS_NONE, &error);
    if (!parsed) {
        gjs_throw_custom(cx, JSProto_Error, "ImportError",
                         "Attempted to import invalid URI: %s (%s)", uri.get(),
                         error->message);
        g_clear_error(&error);
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
            gjs_throw_custom(cx, JSProto_Error, "ImportError",
                             "Attempted to import invalid URI: %s (%s)",
                             uri.get(), error->message);
            g_clear_error(&error);
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

    if (!JS_DefineProperty(cx, return_obj, "uri", string_arg,
                           JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "scheme", scheme,
                           JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "host", host, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "path", path, JSPROP_ENUMERATE) ||
        !JS_DefineProperty(cx, return_obj, "query", query_obj,
                           JSPROP_ENUMERATE))
        return false;

    args.rval().setObject(*return_obj);
    return true;
}

bool gjs_internal_resolve_relative_resource_or_file(JSContext* cx,
                                                    unsigned argc,
                                                    JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    g_assert(args.length() == 2 && "resolveRelativeResourceOrFile(str, str)");
    g_assert(args[0].isString() && "resolveRelativeResourceOrFile(str, str)");
    g_assert(args[1].isString() && "resolveRelativeResourceOrFile(str, str)");

    JS::RootedString string_arg(cx, args[0].toString());
    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;
    string_arg = args[1].toString();
    JS::UniqueChars relative_path = JS_EncodeStringToUTF8(cx, string_arg);
    if (!relative_path)
        return false;

    GjsAutoUnref<GFile> module_file = g_file_new_for_uri(uri.get());
    GjsAutoUnref<GFile> module_parent_file = g_file_get_parent(module_file);

    if (module_parent_file) {
        GjsAutoUnref<GFile> output = g_file_resolve_relative_path(
            module_parent_file, relative_path.get());
        GjsAutoChar output_uri = g_file_get_uri(output);

        JS::ConstUTF8CharsZ uri_chars(output_uri, strlen(output_uri));
        JS::RootedString retval(cx, JS_NewStringCopyUTF8Z(cx, uri_chars));
        if (!retval)
            return false;

        args.rval().setString(retval);
        return true;
    }

    args.rval().setNull();
    return true;
}

bool gjs_internal_load_resource_or_file(JSContext* cx, unsigned argc,
                                        JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);

    g_assert(args.length() == 1 && "loadResourceOrFile(str)");
    g_assert(args[0].isString() && "loadResourceOrFile(str)");

    JS::RootedString string_arg(cx, args[0].toString());
    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;

    GjsAutoUnref<GFile> file = g_file_new_for_uri(uri.get());

    char* contents;
    size_t length;
    GError* error = nullptr;
    if (!g_file_load_contents(file, /* cancellable = */ nullptr, &contents,
                              &length, /* etag_out = */ nullptr, &error)) {
        gjs_throw_custom(cx, JSProto_Error, "ImportError",
                         "Unable to load file from: %s (%s)", uri.get(),
                         error->message);
        g_clear_error(&error);
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

    g_assert(args.length() >= 1 && "uriExists(str)");  // extra args are OK
    g_assert(args[0].isString() && "uriExists(str)");

    JS::RootedString string_arg(cx, args[0].toString());
    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;

    GjsAutoUnref<GFile> file = g_file_new_for_uri(uri.get());

    args.rval().setBoolean(g_file_query_exists(file, nullptr));
    return true;
}

class PromiseData {
 public:
    JSContext* cx;

 private:
    JS::Heap<JSFunction*> m_resolve;
    JS::Heap<JSFunction*> m_reject;

    JS::HandleFunction resolver() {
        return JS::HandleFunction::fromMarkedLocation(m_resolve.address());
    }
    JS::HandleFunction rejecter() {
        return JS::HandleFunction::fromMarkedLocation(m_reject.address());
    }

    static void trace(JSTracer* trc, void* data) {
        auto* self = PromiseData::from_ptr(data);
        JS::TraceEdge(trc, &self->m_resolve, "loadResourceOrFileAsync resolve");
        JS::TraceEdge(trc, &self->m_reject, "loadResourceOrFileAsync reject");
    }

 public:
    explicit PromiseData(JSContext* a_cx, JSFunction* resolve,
                         JSFunction* reject)
        : cx(a_cx), m_resolve(resolve), m_reject(reject) {
        JS_AddExtraGCRootsTracer(cx, &PromiseData::trace, this);
    }

    ~PromiseData() {
        JS_RemoveExtraGCRootsTracer(cx, &PromiseData::trace, this);
    }

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

    char* contents;
    size_t length;
    GError* error = nullptr;
    if (!g_file_load_contents_finish(G_FILE(file), res, &contents, &length,
                                     /* etag_out = */ nullptr, &error)) {
        GjsAutoChar uri = g_file_get_uri(G_FILE(file));
        gjs_throw_custom(promise->cx, JSProto_Error, "ImportError",
                         "Unable to load file from: %s (%s)", uri.get(),
                         error->message);
        g_clear_error(&error);
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

    g_assert(args.length() == 2 && "Executor called weirdly");
    g_assert(args[0].isObject() && "Executor called weirdly");
    g_assert(args[1].isObject() && "Executor called weirdly");
    g_assert(JS_ObjectIsFunction(&args[0].toObject()) &&
             "Executor called weirdly");
    g_assert(JS_ObjectIsFunction(&args[1].toObject()) &&
             "Executor called weirdly");

    JS::Value priv_value = js::GetFunctionNativeReserved(&args.callee(), 0);
    g_assert(!priv_value.isNull() && "Executor called twice");
    GjsAutoUnref<GFile> file = G_FILE(priv_value.toPrivate());
    g_assert(file && "Executor called twice");
    // We now own the GFile, and will pass the reference to the GAsyncResult, so
    // remove it from the executor's private slot so it doesn't become dangling
    js::SetFunctionNativeReserved(&args.callee(), 0, JS::NullValue());

    auto* data = new PromiseData(cx, JS_GetObjectFunction(&args[0].toObject()),
                                 JS_GetObjectFunction(&args[1].toObject()));
    g_file_load_contents_async(file, nullptr, load_async_callback, data);

    args.rval().setUndefined();
    return true;
}

bool gjs_internal_load_resource_or_file_async(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    JS::CallArgs args = CallArgsFromVp(argc, vp);

    g_assert(args.length() == 1 && "loadResourceOrFileAsync(str)");
    g_assert(args[0].isString() && "loadResourceOrFileAsync(str)");

    JS::RootedString string_arg(cx, args[0].toString());
    JS::UniqueChars uri = JS_EncodeStringToUTF8(cx, string_arg);
    if (!uri)
        return false;

    GjsAutoUnref<GFile> file = g_file_new_for_uri(uri.get());

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
