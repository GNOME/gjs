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

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/Class.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_RE...
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetObjectRealmOrNull, SetRealmPrivate
#include <js/RealmOptions.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <jsapi.h>       // for AutoSaveExceptionState, ...

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/engine.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"

namespace mozilla {
union Utf8Unit;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
run_bootstrap(JSContext       *cx,
              const char      *bootstrap_script,
              JS::HandleObject global)
{
    GjsAutoChar uri = g_strdup_printf(
        "resource:///org/gnome/gjs/modules/script/_bootstrap/%s.js",
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

const JSClassOps defaultclassops = JS::DefaultGlobalClassOps;

class GjsGlobal {
    static constexpr JSClass klass = {
        "GjsGlobal",
        JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(GJS_GLOBAL_SLOT_LAST),
        &defaultclassops,
    };

    static constexpr JSFunctionSpec static_funcs[] = {
        JS_FS_END};

 public:
    GJS_USE
    static JSObject *
    create(JSContext *cx)
    {
        JS::RealmBehaviors behaviors;

        JS::RealmCreationOptions creation;
        creation.setFieldsEnabled(true);
        creation.setBigIntEnabled(true);

        JS::RealmOptions options(creation, behaviors);

        JS::RootedObject global(
            cx, JS_NewGlobalObject(cx, &GjsGlobal::klass, nullptr,
                                   JS::FireOnNewGlobalHook, options));
        if (!global)
            return nullptr;

        JSAutoRealm ar(cx, global);

        if (!JS_InitReflectParse(cx, global) ||
            !JS_DefineDebuggerObject(cx, global))
            return nullptr;

        return global;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_properties(JSContext* cx, JS::HandleObject global,
                                  const char* realm_name,
                                  const char* bootstrap_script) {
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (!JS_DefinePropertyById(cx, global, atoms.window(), global,
                                   JSPROP_READONLY | JSPROP_PERMANENT) ||
            !JS_DefineFunctions(cx, global, GjsGlobal::static_funcs))
            return false;

        JS::Realm* realm = JS::GetObjectRealmOrNull(global);
        g_assert(realm && "Global object must be associated with a realm");
        // const_cast is allowed here if we never free the realm data
        JS::SetRealmPrivate(realm, const_cast<char*>(realm_name));

        JS::Value v_importer = gjs_get_global_slot(cx, GJS_GLOBAL_SLOT_IMPORTS);
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

/**
 * gjs_create_global_object:
 * @cx: a #JSContext
 *
 * Creates a global object, and initializes it with the default API.
 *
 * Returns: the created global object on success, nullptr otherwise, in which
 * case an exception is pending on @cx
 */
JSObject *
gjs_create_global_object(JSContext *cx)
{
    return GjsGlobal::create(cx);
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
                                  const char* realm_name,
                                  const char* bootstrap_script) {
    return GjsGlobal::define_properties(cx, global, realm_name,
                                        bootstrap_script);
}

void
gjs_set_global_slot(JSContext    *cx,
                    GjsGlobalSlot slot,
                    JS::Value     value)
{
    JSObject *global = gjs_get_import_global(cx);
    JS_SetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot, value);
}

JS::Value
gjs_get_global_slot(JSContext    *cx,
                    GjsGlobalSlot slot)
{
    JSObject *global = gjs_get_import_global(cx);
    return JS_GetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot);
}

decltype(GjsGlobal::klass) constexpr GjsGlobal::klass;
decltype(GjsGlobal::static_funcs) constexpr GjsGlobal::static_funcs;
