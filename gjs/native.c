/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008-2010  litl, LLC
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

#include <gmodule.h>

#include <util/log.h>

#include "native.h"
#include "compat.h"
#include "jsapi-util.h"
#include "runtime.h"

typedef struct {
    GjsDefineModuleFunc func;
    GjsNativeFlags flags;
} GjsNativeModule;

static GHashTable *modules = NULL;

static void
native_module_free(void *data)
{
    g_slice_free(GjsNativeModule, data);
}

void
gjs_register_native_module (const char            *module_id,
                            GjsDefineModuleFunc  func,
                            GjsNativeFlags       flags)
{
    GjsNativeModule *module;

    if (modules == NULL) {
        modules = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        g_free, native_module_free);
    }

    if (g_hash_table_lookup(modules, module_id) != NULL) {
        g_warning("A second native module tried to register the same id '%s'",
                  module_id);
        return;
    }

    module = g_slice_new(GjsNativeModule);
    module->func = func;
    module->flags = flags;

    g_hash_table_replace(modules,
                         g_strdup(module_id),
                         module);

    gjs_debug(GJS_DEBUG_NATIVE,
              "Registered native JS module '%s'",
              module_id);
}

static JSObject*
module_get_parent(JSContext *context,
                  JSObject  *module_obj)
{
    jsval value;

    if (gjs_object_get_property_const(context, module_obj, GJS_STRING_PARENT_MODULE, &value) &&
        !JSVAL_IS_NULL(value) &&
        JSVAL_IS_OBJECT(value)) {
        return JSVAL_TO_OBJECT(value);
    } else {
        return NULL;
    }
}

/**
 * gjs_is_registered_native_module:
 * @context:
 * @parent: the parent object defining the namespace
 * @name: name of the module
 *
 * Checks if a native module corresponding to @name has already
 * been registered. This is used to check to see if a name is a
 * builtin module without starting to try and load it.
 */
gboolean
gjs_is_registered_native_module(JSContext  *context,
                                JSObject   *parent,
                                const char *name)
{
    if (modules == NULL)
        return FALSE;

    return g_hash_table_lookup(modules, name) != NULL;
}

/**
 * gjs_import_native_module:
 * @context:
 * @module_obj:
 *
 * Return a native module that's been preloaded.
 */
JSBool
gjs_import_native_module(JSContext *context,
                         JSObject  *module_obj,
                         const char *name)
{
    GjsNativeModule *native_module;
    JSObject *parent;

    gjs_debug(GJS_DEBUG_NATIVE,
              "Defining native module '%s'",
              name);

    if (modules != NULL)
        native_module = g_hash_table_lookup(modules, name);
    else
        native_module = NULL;

    if (!native_module) {
        gjs_throw(context,
                  "No native module '%s' has registered itself",
                  name);
        return JS_FALSE;
    }

    if (native_module->flags & GJS_NATIVE_SUPPLIES_MODULE_OBJ) {

        /* In this case we just throw away "module_obj" eventually,
         * since the native module defines itself in the parent of
         * module_obj directly.
         */
        parent = module_get_parent(context, module_obj);
        return (* native_module->func) (context, parent);
    } else {
        return (* native_module->func) (context, module_obj);
    }
}

