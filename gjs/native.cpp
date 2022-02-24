/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#include <config.h>

#include <string>
#include <tuple>  // for tie
#include <unordered_map>
#include <utility>  // for pair

#include <glib.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/native.h"
#include "util/log.h"

void Gjs::NativeModuleRegistry::add(const char* module_id,
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
bool Gjs::NativeModuleRegistry::is_registered(const char* name) const {
    return m_modules.count(name) > 0;
}

/**
 * gjs_load:
 * @context: the #JSContext
 * @parse_name: Name under which the module was registered with
 *  add(), should be in the format as returned by
 *  g_file_get_parse_name()
 * @module_out: Return location for a #JSObject
 *
 * Loads a builtin native-code module called @name into @module_out.
 *
 * Returns: true on success, false if an exception was thrown.
 */
bool Gjs::NativeModuleRegistry::load(JSContext* context, const char* parse_name,
                                     JS::MutableHandleObject module_out) {
    gjs_debug(GJS_DEBUG_NATIVE, "Defining native module '%s'", parse_name);

    const auto& iter = m_modules.find(parse_name);

    if (iter == m_modules.end()) {
        gjs_throw(context,
                  "No native module '%s' has registered itself",
                  parse_name);
        return false;
    }

    return iter->second(context, module_out);
}
