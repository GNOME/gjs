/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GJS_NATIVE_H_
#define GJS_NATIVE_H_

#include <config.h>
#include <string>
#include <unordered_map>

#include <js/RootingAPI.h>  // for MutableHandle
#include <js/TypeDecls.h>

#include "gjs/macros.h"

namespace Gjs {
class NativeModuleDefineFuncs {
    NativeModuleDefineFuncs() {}
    typedef bool (*GjsDefineModuleFunc)(JSContext* context,
                                        JS::MutableHandleObject module_out);

    std::unordered_map<std::string, GjsDefineModuleFunc> m_modules;

 public:
    static NativeModuleDefineFuncs& get() {
        static NativeModuleDefineFuncs the_singleton;
        return the_singleton;
    }

    /* called on context init */
    void add(const char* module_id, GjsDefineModuleFunc func);

    // called by importer.cpp to to check for already loaded modules
    [[nodiscard]] bool is_registered(const char* name) const;

    // called by importer.cpp to load a built-in native module
    GJS_JSAPI_RETURN_CONVENTION
    bool define(JSContext* cx, const char* name,
                JS::MutableHandleObject module_out) const;
};
};  // namespace Gjs

#endif  // GJS_NATIVE_H_
