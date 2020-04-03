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

#include <config.h>

#include <stddef.h>     // for size_t
#include <sys/types.h>  // for ssize_t

#include <string>  // for u16string

#include <gio/gio.h>
#include <glib.h>

#include <js/Class.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/GCVector.h>  // for RootedVector
#include <js/PropertyDescriptor.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_DefinePropertyById, ...

#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "gjs/module.h"
#include "util/log.h"

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

    /* Private data accessors */

    GJS_USE
    static inline GjsScriptModule* priv(JSObject* module) {
        return static_cast<GjsScriptModule*>(JS_GetPrivate(module));
    }

    /* Creates a JS module object. Use instead of the class's constructor */
    GJS_USE
    static JSObject *
    create(JSContext  *cx,
           const char *name)
    {
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
                         const char* filename) {
        std::u16string utf16_string =
            gjs_utf8_script_to_utf16(script, script_len);
        // COMPAT: This could use JS::SourceText<mozilla::Utf8Unit> directly,
        // but that messes up code coverage. See bug
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1404784
        JS::SourceText<char16_t> buf;
        if (!buf.init(cx, utf16_string.c_str(), utf16_string.size(),
                      JS::SourceOwnership::Borrowed))
            return false;

        JS::RootedObjectVector scope_chain(cx);
        if (!scope_chain.append(module)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        JS::CompileOptions options(cx);
        options.setFileAndLine(filename, 1);

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
        return evaluate_import(cx, module, script, script_len, full_path);
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
