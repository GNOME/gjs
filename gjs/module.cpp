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

#include <gio/gio.h>

#include "jsapi-util.h"
#include "jsapi-wrapper.h"
#include "module.h"
#include "util/log.h"

class GjsModule {
    char *m_name;

    GjsModule(const char *name)
    {
        m_name = g_strdup(name);
    }

    ~GjsModule()
    {
        g_free(m_name);
    }

    /* Private data accessors */

    static inline GjsModule *
    priv(JSObject *module)
    {
        return static_cast<GjsModule *>(JS_GetPrivate(module));
    }

    /* Creates a JS module object. Use instead of the class's constructor */
    static JSObject *
    create(JSContext  *cx,
           const char *name)
    {
        JSObject *module = JS_NewObject(cx, &GjsModule::klass);
        JS_SetPrivate(module, new GjsModule(name));
        return module;
    }

    /* Defines the empty module as a property on the importer */
    bool
    define_import(JSContext       *cx,
                  JS::HandleObject module,
                  JS::HandleObject importer,
                  JS::HandleId     name)
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
    bool
    evaluate_import(JSContext       *cx,
                    JS::HandleObject module,
                    const char      *script,
                    size_t           script_len,
                    const char      *filename,
                    int              line_number)
    {
        JS::CompileOptions options(cx);
        options.setUTF8(true)
               .setFileAndLine(filename, line_number)
               .setSourceIsLazy(true);

        JS::RootedScript compiled_script(cx);
        if (!JS::Compile(cx, options, script, script_len, &compiled_script))
            return false;

        JS::AutoObjectVector scope_chain(cx);
        if (!scope_chain.append(module))
            g_error("Unable to append to vector");

        JS::RootedValue ignored_retval(cx);
        if (!JS_ExecuteScript(cx, scope_chain, compiled_script, &ignored_retval))
            return false;

        gjs_schedule_gc_if_needed(cx);

        gjs_debug(GJS_DEBUG_IMPORTER, "Importing module %s succeeded", m_name);

        return true;
    }

    /* Loads JS code from a file and imports it */
    bool
    import_file(JSContext       *cx,
                JS::HandleObject module,
                GFile           *file)
    {
        GError *error = nullptr;
        char *unowned_script;
        size_t script_len = 0;
        int start_line_number = 1;

        if (!(g_file_load_contents(file, nullptr, &unowned_script, &script_len,
                                   nullptr, &error))) {
            gjs_throw_g_error(cx, error);
            return false;
        }

        GjsAutoChar script = unowned_script;  /* steals ownership */
        g_assert(script != nullptr);

        const char *stripped_script =
            gjs_strip_unix_shebang(script, &script_len, &start_line_number);

        GjsAutoChar full_path = g_file_get_parse_name(file);
        return evaluate_import(cx, module, stripped_script, script_len,
                               full_path, start_line_number);
    }

    /* JSClass operations */

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

        GjsAutoJSChar prop_name;
        if (!gjs_get_string_id(cx, id, &prop_name))
            return false;

        g_warning("Some code accessed the property '%s' on the module '%s'. "
                  "That property was defined with 'let' or 'const' inside the "
                  "module. This was previously supported, but is not correct "
                  "according to the ES6 standard. Any symbols to be exported "
                  "from a module must be defined with 'var'. The property "
                  "access will work as previously for the time being, but "
                  "please fix your code anyway.", prop_name.get(), m_name);

        JS::Rooted<JS::PropertyDescriptor> desc(cx);
        return JS_GetPropertyDescriptorById(cx, lexical, id, &desc) &&
            JS_DefinePropertyById(cx, module, id, desc);
    }

    static bool
    resolve(JSContext       *cx,
            JS::HandleObject module,
            JS::HandleId     id,
            bool            *resolved)
    {
        return priv(module)->resolve_impl(cx, module, id, resolved);
    }

    static void
    finalize(JSFreeOp *op,
             JSObject *module)
    {
        delete priv(module);
    }

    static constexpr JSClassOps class_ops = {
        nullptr,  /* addProperty */
        nullptr,  /* deleteProperty */
        nullptr,  /* getProperty */
        nullptr,  /* setProperty */
        nullptr,  /* enumerate */
        &GjsModule::resolve,
        nullptr,  /* mayResolve */
        &GjsModule::finalize,
    };

    static constexpr JSClass klass = {
        "GjsModule",
        JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &GjsModule::class_ops,
    };

public:

    /* Carries out the import operation */
    static JSObject *
    import(JSContext       *cx,
           JS::HandleObject importer,
           JS::HandleId     id,
           const char      *name,
           GFile           *file)
    {
        JS::RootedObject module(cx, GjsModule::create(cx, name));
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
    return GjsModule::import(cx, importer, id, name, file);
}

decltype(GjsModule::klass) constexpr GjsModule::klass;
decltype(GjsModule::class_ops) constexpr GjsModule::class_ops;
