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

#include <stddef.h>     // for size_t
#include <sys/types.h>  // for ssize_t

#include <string>  // for u16string

#include <gio/gio.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"
#include "js/CompilationAndEvaluation.h"
#include "js/SourceText.h"

#include <girepository.h>
#include <glib-object.h>
#include <libsoup/soup.h>
#include <codecvt>  // for codecvt_utf8_utf16
#include <locale>   // for wstring_convert
#include <vector>
#include "gjs/context-private.h"
#include "gjs/error-types.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "util/log.h"

using AutoURI = GjsAutoPointer<SoupURI, SoupURI, soup_uri_free>;
using AutoGHashTable =
    GjsAutoPointer<GHashTable, GHashTable, g_hash_table_destroy>;

static char* gir_uri_version(SoupURI* url) {
    const char* q = soup_uri_get_query(url);

    if (!q) {
        return NULL;
    }

    AutoGHashTable queries = soup_form_decode(q);
    GjsAutoChar key = g_strdup("v");
    char* version = (char*)g_hash_table_lookup(queries.get(), key);

    return g_strdup(version);
}

static bool is_gir_uri(SoupURI* uri) {
    const char* scheme = soup_uri_get_scheme(uri);

    return scheme && strcmp(scheme, "gi") == 0;
}

static char* gir_js_mod_ver(const char* ns, const char* version) {
    return g_strdup_printf(R"js(
        import gi from "gi";
        export default gi.require('%s', '%s');
        )js",
                           ns, version);
}

static char* gir_js_mod(const char* ns) {
    return g_strdup_printf(R"js(
        import gi from "gi";
        export default gi.require('%s');
        )js",
                           ns);
}

class GjsLegacyModule {
    char* m_name;

    GjsLegacyModule(const char* name) {
        m_name = g_strdup(name);
        GJS_INC_COUNTER(module);
    }

    ~GjsLegacyModule() {
        g_free(m_name);
        GJS_DEC_COUNTER(module);
    }

    /* Private data accessors */

    GJS_USE
    static inline GjsLegacyModule* priv(JSObject* module) {
        return static_cast<GjsLegacyModule*>(JS_GetPrivate(module));
    }

    /* Creates a JS module object. Use instead of the class's constructor */
    GJS_USE
    static JSObject* create(JSContext* cx, const char* name) {
        JSObject* module = JS_NewObject(cx, &GjsLegacyModule::klass);
        JS_SetPrivate(module, new GjsLegacyModule(name));
        return module;
    }

    /* Defines the empty module as a property on the importer */
    GJS_JSAPI_RETURN_CONVENTION
    bool define_import(JSContext* cx, JS::HandleObject module,
                       JS::HandleObject importer, JS::HandleId name) const {
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
                         const char* filename) {
        std::u16string utf16_string =
            gjs_utf8_script_to_utf16(script, script_len);

        unsigned start_line_number = 1;
        size_t offset = gjs_unix_shebang_len(utf16_string, &start_line_number);

        // FIXME compile utf8string
        JS::SourceText<char16_t> buf;
        if (!buf.init(cx, utf16_string.c_str() + offset,
                      utf16_string.size() - offset,
                      JS::SourceOwnership::Borrowed))
            return false;

        JS::RootedObjectVector scope_chain(cx);
        if (!scope_chain.append(module)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        JS::CompileOptions options(cx);
        options.setFileAndLine(filename, start_line_number);

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
    bool import_file(JSContext* cx, JS::HandleObject module, GFile* file) {
        GError* error = nullptr;
        char* unowned_script;
        size_t script_len = 0;

        if (!(g_file_load_contents(file, nullptr, &unowned_script, &script_len,
                                   nullptr, &error)))
            return gjs_throw_gerror_message(cx, error);

        GjsAutoChar script = unowned_script; /* steals ownership */
        g_assert(script);

        GjsAutoChar full_path = g_file_get_parse_name(file);
        return evaluate_import(cx, module, script, script_len, full_path);
    }

    /* JSClass operations */

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject module, JS::HandleId id,
                      bool* resolved) {
        JS::RootedObject lexical(cx, JS_ExtensibleLexicalEnvironment(module));
        if (!lexical) {
            *resolved = false;
            return true; /* nothing imported yet */
        }

        if (!JS_HasPropertyById(cx, lexical, id, resolved))
            return false;
        if (!*resolved)
            return true;

        /* The property is present in the lexical environment. This should not
         * be supported according to ES6. For compatibility with earlier GJS,
         * we treat it as if it were a real property, but warn about it. */

        g_warning(
            "Some code accessed the property '%s' on the module '%s'. "
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
    static bool resolve(JSContext* cx, JS::HandleObject module, JS::HandleId id,
                        bool* resolved) {
        return priv(module)->resolve_impl(cx, module, id, resolved);
    }

    static void finalize(JSFreeOp*, JSObject* module) { delete priv(module); }

    static constexpr JSClassOps class_ops = {
        nullptr,  // addProperty
        nullptr,  // deleteProperty
        nullptr,  // enumerate
        nullptr,  // newEnumerate
        &GjsLegacyModule::resolve,
        nullptr,  // mayResolve
        &GjsLegacyModule::finalize,
    };

    static constexpr JSClass klass = {
        "GjsLegacyModule",
        JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &GjsLegacyModule::class_ops,
    };

 public:
    /* Carries out the import operation */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* import(JSContext* cx, JS::HandleObject importer,
                            JS::HandleId id, const char* name, GFile* file) {
        JS::RootedObject module(cx, GjsLegacyModule::create(cx, name));
        if (!module || !priv(module)->define_import(cx, module, importer, id) ||
            !priv(module)->import_file(cx, module, file))
            return nullptr;

        return module;
    }
};

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
JSObject* gjs_module_import(JSContext* cx, JS::HandleObject importer,
                            JS::HandleId id, const char* name, GFile* file) {
    return GjsLegacyModule::import(cx, importer, id, name, file);
}

decltype(GjsLegacyModule::klass) constexpr GjsLegacyModule::klass;
decltype(GjsLegacyModule::class_ops) constexpr GjsLegacyModule::class_ops;

bool GjsModuleLoader::populate_module_meta(
    JSContext* m_cx, JS::Handle<JS::Value> private_ref,
    JS::Handle<JSObject*> meta_object_handle) {
    JS::RootedObject meta_object(m_cx, meta_object_handle);
    std::string uri = "(unknown)";
    bool allow_require = true;

    if (!private_ref.isUndefined()) {
        GjsModule* module = static_cast<GjsModule*>(private_ref.toPrivate());

        uri = module->module_uri();
        allow_require = module->is_internal();
    }

    JS::Rooted<JSString*> uri_str(m_cx, JS_NewStringCopyZ(m_cx, uri.c_str()));

    if (!uri_str) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    if (!JS_DefineProperty(m_cx, meta_object, "url", uri_str,
                           JSPROP_ENUMERATE)) {
        gjs_throw(m_cx, "Could not define url!");
        return false;
    }

    if (allow_require) {
        if (!JS_DefineFunction(m_cx, meta_object, "require",
                               gjs_load_internal_module, 1,
                               GJS_MODULE_PROP_FLAGS)) {
            gjs_throw(m_cx, "Could not define require!");
            return false;
        }
    }

    // If we can't retrieve the metadata, return false;
    return true;
}

bool gjs_populate_module_meta(JSContext* m_cx,
                              JS::Handle<JS::Value> private_ref,
                              JS::Handle<JSObject*> meta_object) {
    return GjsModuleLoader::populate_module_meta(m_cx, private_ref,
                                                 meta_object);
}

GJS_JSAPI_RETURN_CONVENTION
bool GjsModuleLoader::load_internal_module(JSContext* m_cx, unsigned argc,
                                           JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    if (argc != 1) {
        gjs_throw(m_cx, "Must pass a single argument to require()");
        return false;
    }

    /* JS::ToString might throw, in which case we will only log that the
     * value could not be converted to string */
    JS::AutoSaveExceptionState exc_state(m_cx);
    JS::RootedString jstr(m_cx, JS::ToString(m_cx, argv[0]));
    exc_state.restore();

    if (!jstr) {
        g_message("JS LOG: <cannot convert value to string>");
        return true;
    }

    JS::UniqueChars id(JS_EncodeStringToUTF8(m_cx, jstr));
    if (!id) {
        gjs_throw(m_cx, "Invalid native id.");
        return false;
    }

    auto moduleAdd = m_id_to_native_module->lookupForAdd(id.get());

    if (!moduleAdd.found()) {
        JS::RootedObject native_obj(m_cx);
        bool is_native = gjs_is_registered_native_module(id.get());

        if (is_native) {
            if (!gjs_load_native_module(m_cx, id.get(), &native_obj)) {
                gjs_throw(m_cx, "Failed to load native module: %s", id.get());
                return false;
            }

            if (!m_id_to_native_module->add(moduleAdd, id.get(), native_obj)) {
                JS_ReportOutOfMemory(m_cx);
                return false;
            }

            argv.rval().setObject(*native_obj);
            return true;
        }

        auto internalModuleAdd =
            m_id_to_internal_module->lookupForAdd(id.get());

        if (internalModuleAdd.found()) {
            JS::RootedObject obj(m_cx, internalModuleAdd->value().get());

            argv.rval().setObject(*obj);
            return true;
        } else {
            if (!load_legacy_module(m_cx, id.get(), &native_obj)) {
                gjs_throw(m_cx, "Failed to load native module: %s", id.get());
                return false;
            }

            if (!m_id_to_internal_module->add(internalModuleAdd, id.get(),
                                              native_obj)) {
                JS_ReportOutOfMemory(m_cx);
                return false;
            }

            argv.rval().setObject(*native_obj);
            return true;
        }

        argv.rval().setUndefined();
        return false;
    }

    JS::RootedObject obj(m_cx, moduleAdd->value().get());

    argv.rval().setObject(*obj);
    return true;
}

bool GjsModuleLoader::register_module(JSContext* m_cx, const char* identifier,
                                      const char* filename,
                                      const char* mod_text, size_t mod_len,
                                      GError** error) {
    // Module registration uses exceptions to report errors
    // so we'll store the exception state, clear it, attempt to load the
    // module, then restore the original exception state.
    JS::AutoSaveExceptionState exp_state(m_cx);

    if (register_esm_module(m_cx, identifier, filename, mod_text, mod_len))
        return true;

    // Our message could come from memory owned by us or by the runtime.
    const char* msg = nullptr;

    JS::RootedValue exc(m_cx);
    if (JS_GetPendingException(m_cx, &exc)) {
        JS::RootedObject exc_obj(m_cx, &exc.toObject());
        JSErrorReport* report = JS_ErrorFromException(m_cx, exc_obj);
        if (report) {
            msg = report->message().c_str();
        } else {
            JS::RootedString js_message(m_cx, JS::ToString(m_cx, exc));

            if (js_message) {
                JS::UniqueChars cstr(JS_EncodeStringToUTF8(m_cx, js_message));
                msg = cstr.get();
            }
        }
    }

    g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                "Error registering module '%s': %s", identifier,
                msg ? msg : "unknown");

    // We've successfully handled the exception so we can clear it.
    // This is necessary because AutoSaveExceptionState doesn't erase
    // exceptions when it restores the previous exception state.
    JS_ClearPendingException(m_cx);

    return false;
}

bool GjsModuleLoader::register_gi_module(JSContext* m_cx, const char* ns,
                                         const char* mod_text, size_t mod_len) {
    auto it = m_id_to_gi_module->lookupForAdd(ns);

    if (it.found()) {
        gjs_throw(m_cx, "Module '%s' is already registered (gi)", ns);
        return false;
    }

    unsigned int start_line_number = 1;

    JS::CompileOptions options(m_cx);

    options.setFileAndLine(ns, start_line_number).setSourceIsLazy(false);

    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(mod_text);
    size_t offset = gjs_unix_shebang_len(utf16_string, &start_line_number);

    JS::SourceText<char16_t> buf;
    if (!buf.init(m_cx, utf16_string.c_str() + offset,
                  utf16_string.size() - offset, JS::SourceOwnership::Borrowed))
        return false;

    JS::RootedObject new_module(m_cx);

    if (!JS::CompileModule(m_cx, options, buf, &new_module)) {
        g_warning("Failed to compile module.");
        return false;
    }

    auto priv_mod = new GjsModule(true);

    JS::SetModulePrivate(new_module, JS::PrivateValue(priv_mod));

    GjsAutoChar dns = g_strdup(ns);
    if (!m_id_to_gi_module->add(it, dns, new_module)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    return true;
}

bool GjsModuleLoader::load_legacy_module(JSContext* m_cx,
                                         const char* identifier,
                                         JS::MutableHandleObject module_out) {
    JS::RootedValue ret_val(m_cx);
    const char* dirname = "resource:///org/gnome/gjs/modules/shared/";

    GjsAutoChar filename = g_strdup_printf("%s.js", identifier);
    GjsAutoChar full_path = g_build_filename(dirname, filename.get(), nullptr);
    GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(full_path);

    bool exists = g_file_query_exists(gfile, NULL);

    if (!exists) {
        gjs_throw(m_cx, "No file found: %s", full_path.get());
        return false;
    }

    GError* err = NULL;

    char* mod_text_raw;
    gsize mod_len;

    if (!g_file_load_contents(gfile, NULL, &mod_text_raw, &mod_len, NULL,
                              &err)) {
        gjs_throw(m_cx, "Failed to read internal resource: %s \n%s",
                  full_path.get(), err->message);
        return false;
    }

    GjsAutoChar mod_text(mod_text_raw);
    auto gjs_cx = GjsContextPrivate::from_cx(m_cx);
    JS::RootedObject module_object(m_cx, JS_NewPlainObject(m_cx));

    JS_DefineFunction(m_cx, module_object, "require", gjs_load_internal_module,
                      1, GJS_MODULE_PROP_FLAGS);

    auto success = gjs_cx->eval_with_scope(module_object, mod_text, mod_len,
                                           filename, &ret_val);
    JS_DeleteProperty(m_cx, module_object, "require");
    if (!success) {
        gjs_log_exception(m_cx);
        return false;
    }

    module_out.set(module_object);

    return success;
}

bool GjsModuleLoader::wrap_legacy_module(JSContext* m_cx,
                                         const char* identifier,
                                         const char* filename,
                                         const char* mod_text, size_t mod_len) {
    auto gjs_cx = GjsContextPrivate::from_cx(m_cx);
    JS::RootedValue ret_val(m_cx);

    JS::RootedObject importer(
        m_cx, JS_NewPlainObject(m_cx));  // TODO Remove eventually

    const char* full_path =
        "resource://org/gnome/gjs/modules/internal/wrapper.js";
    GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(full_path);
    jsid id = gjs_intern_string_to_id(m_cx, full_path);
    JS::RootedId handle(m_cx, id);
    auto scope_obj =
        GjsLegacyModule::import(m_cx, importer, handle, "esm-wrapper", gfile);

    JS::RootedObject module_object(m_cx, JS_NewPlainObject(m_cx));

    auto success = true;

    auto moduleAdd = m_id_to_internal_module->lookupForAdd(identifier);
    auto found = moduleAdd.found();
    if (!found) {
        JS_DefineFunction(m_cx, module_object, "require",
                          gjs_load_internal_module, 1, GJS_MODULE_PROP_FLAGS);

        success = gjs_cx->eval_with_scope(module_object, mod_text, mod_len,
                                          filename, &ret_val);

        JS_DeleteProperty(m_cx, module_object, "require");

        m_id_to_internal_module->add(moduleAdd, identifier, module_object);
    }

    JS::RootedObject mobj(m_cx,
                          found ? moduleAdd->value().get() : module_object);

    if (!success) {
        gjs_log_exception(m_cx);
        return false;
    }

    JS::RootedObject obj(m_cx, scope_obj);

    JS::RootedObject arr(
        m_cx, JS_NewArrayObject(m_cx, JS::HandleValueArray::empty()));

    JS::RootedValue module_name_val(m_cx);

    if (!gjs_string_from_utf8(m_cx, identifier, &module_name_val)) {
        return false;
    }
    JS::RootedString module_name(m_cx, module_name_val.toString());

    JS::RootedValue robj(m_cx);

    robj.setObject(*mobj);

    JS::RootedValue rname(m_cx);
    rname.setString(module_name);

    JS::AutoValueArray<2> js_values(m_cx);

    js_values[0].set(rname);
    js_values[1].set(robj);
    JS::HandleValueArray varr(js_values);
    JS::RootedValue ret_val2(m_cx);
    JS_CallFunctionName(m_cx, obj, "wrap", varr, &ret_val2);

    JS::RootedString str_val(m_cx, ret_val2.toString());
    JS::UniqueChars mod_text_str = JS_EncodeStringToUTF8(m_cx, str_val);

    success =
        register_esm_module(m_cx, identifier, filename, mod_text_str.get(),
                            strlen(mod_text_str.get()), true);

    if (!success) {
        gjs_log_exception(m_cx);
        return false;
    }

    return true;
}

bool GjsModuleLoader::register_esm_module(JSContext* m_cx,
                                          const char* identifier,
                                          const char* filename,
                                          const char* mod_text, size_t mod_len,
                                          bool internal) {
    auto it = m_id_to_esm_module->lookupForAdd(identifier);

    if (it.found()) {
        gjs_throw(m_cx, "Module '%s' is already registered", identifier);
        return false;
    }

    unsigned int start_line_number = 1;

    JS::CompileOptions options(m_cx);

    options.setFileAndLine(identifier, start_line_number)
        .setSourceIsLazy(false);

    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(mod_text);
    size_t offset = gjs_unix_shebang_len(utf16_string, &start_line_number);

    JS::SourceText<char16_t> buf;
    if (!buf.init(m_cx, utf16_string.c_str() + offset,
                  utf16_string.size() - offset, JS::SourceOwnership::Borrowed))
        return false;

    JS::RootedObject new_module(m_cx);

    if (!JS::CompileModule(m_cx, options, buf, &new_module)) {
        g_warning("Failed to compile module.");
        return false;
    }

    auto priv_mod = new GjsModule(internal);

    if (filename) {
        priv_mod->set_module_uri(filename);
    }

    JS::SetModulePrivate(new_module, JS::PrivateValue(priv_mod));

    GjsAutoChar iden = g_strdup(identifier);
    if (!m_id_to_esm_module->add(it, iden, new_module)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    return true;
}

bool GjsModuleLoader::register_internal_module(JSContext* m_cx,
                                               const char* identifier,
                                               const char* resource_path,
                                               GError** error) {
    auto it = m_id_to_internal_module->lookupForAdd(identifier);

    if (it.found()) {
        gjs_throw(m_cx, "Internal module '%s' is already registered",
                  identifier);
        return false;
    }

    GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(resource_path);

    bool exists = g_file_query_exists(gfile, NULL);

    if (!exists) {
        return false;
    }

    char* module_text_raw;
    gsize module_len;
    GError* err = NULL;

    if (!g_file_load_contents(gfile, NULL, &module_text_raw, &module_len, NULL,
                              &err)) {
        return false;
    }

    GjsAutoChar mod_text(module_text_raw);

    unsigned int start_line_number = 1;

    JS::CompileOptions options(m_cx);

    // All internal modules as lazy.
    options.setFileAndLine(identifier, start_line_number).setSourceIsLazy(true);

    // Shebangs will never appear in internal modules.
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(mod_text);

    JS::SourceText<char16_t> buf;
    if (!buf.init(m_cx, utf16_string.c_str(), utf16_string.size(),
                  JS::SourceOwnership::Borrowed))
        return false;

    JS::RootedObject new_module(m_cx);

    if (!CompileModule(m_cx, options, buf, &new_module)) {
        g_warning("Failed to compile internal module.");
        return false;
    }

    auto module = new GjsModule(true);
    module->set_module_uri(resource_path);
    JS::SetModulePrivate(new_module, JS::PrivateValue(module));

    GjsAutoChar iden = g_strdup(identifier);
    if (!m_id_to_internal_module->add(it, iden.get(), new_module)) {
        JS_ReportOutOfMemory(m_cx);

        return false;
    }

    return true;
}

JSObject* GjsModuleLoader::module_resolve(JSContext* m_cx,
                                          JS::HandleValue mod_val,
                                          JS::HandleString specifier) {
    JS::RootedString str(m_cx, specifier);
    JS::UniqueChars id = JS_EncodeStringToUTF8(m_cx, str);
    // The module from which the resolve request is coming
    // g_warning("Resolving module %s of type %i", id.get(),
    // (int)mod_val.type());

    // The string identifier of the module we wish to import

    auto is_relative_path =
        id[0] == '.' && (id[1] == '/' || (id[1] == '.' && id[2] == '/'));

    auto esm_module = m_id_to_esm_module->lookup(id.get());

    if (esm_module.found()) {
        return esm_module->value().get();
    }

    // 1) Handle relative imports (file-based)
    if (!mod_val.isUndefined() && is_relative_path) {
        // If a module has a path, we'll have stored it in the host field
        GjsModule* priv_module = static_cast<GjsModule*>(mod_val.toPrivate());
        auto module_location = priv_module->module_uri();

        GjsAutoChar module_dir(
            g_path_get_dirname(g_strdup(module_location.c_str())));

        GFile* output =
            g_file_new_for_commandline_arg_and_cwd(id.get(), module_dir);
        GjsAutoChar full_path(g_file_get_path(output));

        auto esm_module = m_id_to_esm_module->lookup(full_path.get());

        if (esm_module.found()) {
            JS::RootedObject obj(m_cx, esm_module->value().get());
            return obj;
        }

        char* module_text_raw;
        gsize mod_len;

        if (!g_file_get_contents(full_path, &module_text_raw, &mod_len,
                                 nullptr)) {
            gjs_throw(m_cx,
                      "Attempting to resolve relative import (%s) from "
                      "non-file module",
                      full_path.get());
            return nullptr;
        }

        GjsAutoChar module_text(module_text_raw);

        if (!register_esm_module(m_cx, full_path, full_path, module_text,
                                 mod_len)) {
            // GjsContextPrivate::_register_module_inner should have already
            // thrown any relevant errors
            g_warning("Failed to register module: %s", full_path.get());
            return nullptr;
        }

        auto registered_lookup = m_id_to_esm_module->lookup(full_path.get());

        if (registered_lookup.found()) {
            return registered_lookup->value().get();
        }
    }

    // 2) Handle gi:// imports
    AutoURI uri = soup_uri_new(id.get());
    auto c_uri = uri.get();

    if (c_uri && is_gir_uri(c_uri)) {
        const char* ns = soup_uri_get_host(uri.get());
        GjsAutoChar version = gir_uri_version(uri.get());

        if (!ns) {
            gjs_throw(m_cx, "Attempted to load invalid module path %s",
                      id.get());
            return nullptr;
        }

        auto c_version = version.get();
        GjsAutoChar gir_mod =
            !c_version ? gir_js_mod(ns) : gir_js_mod_ver(ns, c_version);
        JS::RootedValue gi(m_cx);

        auto c_id = id.get();

        auto module = m_id_to_gi_module->lookup(c_id);

        if (!module.found()) {
            register_gi_module(m_cx, c_id, gir_mod.get(),
                               strlen(gir_mod.get()));
        }

        auto registered_lookup = m_id_to_gi_module->lookup(c_id);

        if (registered_lookup.found()) {
            return registered_lookup->value().get();
        }
    }

    auto internal_module = m_id_to_internal_module->lookup(id.get());

    if (internal_module.found()) {
        return internal_module->value().get();
    }

    // 4) Handle global imports
    const char* dirname = "resource:///org/gnome/gjs/modules/esm/";
    const char* shared_dirname = "resource:///org/gnome/gjs/modules/shared/";

    GjsAutoChar filename = g_strdup_printf("%s.js", id.get());
    GjsAutoChar full_path = g_build_filename(dirname, filename.get(), nullptr);
    GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(full_path);

    bool exists = g_file_query_exists(gfile, NULL);
    bool is_script = false;

    if (!exists) {
        filename = g_strdup_printf("%s.js", id.get());
        full_path = g_build_filename(shared_dirname, filename.get(), nullptr);
        gfile = g_file_new_for_commandline_arg(full_path);

        exists = g_file_query_exists(gfile, NULL);

        if (!exists) {
            gjs_throw(m_cx, "Attempted to load unregistered global module: %s",
                      id.get());
            return nullptr;
        }

        is_script = true;
    }

    GError* err = NULL;

    if (is_script) {
        char* mod_text_raw;
        gsize mod_len;

        if (!g_file_load_contents(gfile, NULL, &mod_text_raw, &mod_len, NULL,
                                  &err)) {
            gjs_throw(m_cx, "Failed to read internal resource: %s \n%s",
                      full_path.get(), err->message);
            return nullptr;
        }

        GjsAutoChar mod_text(mod_text_raw);

        if (!wrap_legacy_module(m_cx, id.get(), full_path.get(), mod_text,
                                mod_len)) {
            return nullptr;
        }

        return m_id_to_esm_module->lookup(id.get())->value().get();
    } else if (!register_internal_module(m_cx, id.get(), full_path.get(),
                                         &err)) {
        g_warning("Could not register internal...");
        return nullptr;
    }

    auto new_val = m_id_to_internal_module->lookup(id.get());

    if (new_val.found()) {
        return new_val->value().get();
    }

    return nullptr;
}

JSObject* GjsModuleLoader::lookup_module(const char* identifier) {
    auto entry = m_id_to_esm_module->lookup(identifier);

    if (!entry.found())
        return nullptr;

    return entry->value();
}

JSObject* gjs_module_resolve(JSContext* cx, JS::HandleValue mod_val,
                             JS::HandleString specifier) {
    JSObject* glob = JS::CurrentGlobalOrNull(cx);

    JSAutoRealm ar(cx, glob);

    GjsGlobal* priv = static_cast<GjsGlobal*>(JS_GetPrivate(glob));

    g_assert_true(priv->global_type() == GjsModuleGlobal::type());

    GjsModuleGlobal* module_global = (GjsModuleGlobal*)priv;

    JS::RootedObject obj(
        cx, module_global->loader()->module_resolve(cx, mod_val, specifier));

    return obj;
}
