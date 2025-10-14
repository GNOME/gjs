/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#include <config.h>

#include <tuple>  // for tie
#include <unordered_map>
#include <utility>  // for ignore

#include <glib.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/native.h"
#include "util/log.h"

void Gjs::NativeModuleDefineFuncs::add(const char* module_id,
                                       GjsDefineModuleFunc func) {
    bool inserted;
    std::tie(std::ignore, inserted) = m_modules.insert({module_id, func});
    if (!inserted) {
        g_warning("A second native module tried to register the same id '%s'",
                  module_id);
        return;
    }

    gjs_debug(GJS_DEBUG_NATIVE,
              "Registered native JS module '%s'",
              module_id);
}

/**
 * is_registered:
 * @name: name of the module
 *
 * Checks if a native module corresponding to @name has already
 * been registered. This is used to check to see if a name is a
 * builtin module without starting to try and load it.
 */
bool Gjs::NativeModuleDefineFuncs::is_registered(const char* name) const {
    return m_modules.count(name) > 0;
}

/**
 * define:
 * @context: the #JSContext
 * @id: Name under which the module was registered with add()
 * @module_out: Return location for a #JSObject
 *
 * Loads a builtin native-code module called @name into @module_out by calling
 * the function to define it.
 *
 * Returns: true on success, false if an exception was thrown.
 */
bool Gjs::NativeModuleDefineFuncs::define(
    JSContext* context, const char* id,
    JS::MutableHandleObject module_out) const {
    gjs_debug(GJS_DEBUG_NATIVE, "Defining native module '%s'", id);

    const auto& iter = m_modules.find(id);

    if (iter == m_modules.end()) {
        gjs_throw(context, "No native module '%s' has registered itself", id);
        return false;
    }

    return iter->second(context, module_out);
}
