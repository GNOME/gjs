/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#include <config.h>

#include <stddef.h>  // for size_t

#include <string>
#include <unordered_map>
#include <utility>  // for pair

#include <girepository/girepository.h>
#include <glib.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/foreign.h"
#include "gi/info.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

enum LoadedStatus { NotLoaded, Loaded };
static std::unordered_map<std::string, LoadedStatus> foreign_modules{
    {"cairo", NotLoaded}};

using StructID = std::pair<std::string, std::string>;
struct StructIDHash {
    [[nodiscard]] size_t operator()(StructID val) const {
        std::hash<std::string> hasher;
        return hasher(val.first) ^ hasher(val.second);
    }
};
static std::unordered_map<StructID, GjsForeignInfo*, StructIDHash>
    foreign_structs_table;

[[nodiscard]] static bool gjs_foreign_load_foreign_module(
    JSContext* cx, const char* gi_namespace) {
    auto entry = foreign_modules.find(gi_namespace);
    if (entry == foreign_modules.end())
        return false;

    if (entry->second == Loaded)
        return true;

    // FIXME: Find a way to check if a module is imported and only execute this
    // statement if it isn't
    std::string script = "imports." + entry->first + ';';
    JS::RootedValue retval{cx};
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    if (!gjs->eval_with_scope(nullptr, script.c_str(), script.length(),
                              "<internal>", &retval)) {
        g_critical("ERROR importing foreign module %s\n", gi_namespace);
        return false;
    }
    entry->second = Loaded;
    return true;
}

void gjs_struct_foreign_register(const char* gi_namespace,
                                 const char* type_name, GjsForeignInfo* info) {
    foreign_structs_table.insert({{gi_namespace, type_name}, info});
}

GJS_JSAPI_RETURN_CONVENTION
static GjsForeignInfo* gjs_struct_foreign_lookup(JSContext* cx,
                                                 const GI::StructInfo info) {
    StructID key{info.ns(), info.name()};
    auto entry = foreign_structs_table.find(key);
    if (entry == foreign_structs_table.end()) {
        if (gjs_foreign_load_foreign_module(cx, info.ns()))
            entry = foreign_structs_table.find(key);
    }

    if (entry == foreign_structs_table.end()) {
        gjs_throw(cx, "Unable to find module implementing foreign type %s.%s",
                  key.first.c_str(), key.second.c_str());
        return nullptr;
    }

    return entry->second;
}

bool gjs_struct_foreign_convert_to_gi_argument(
    JSContext* context, JS::Value value, const GI::StructInfo info,
    const char* arg_name, GjsArgumentType argument_type, GITransfer transfer,
    GjsArgumentFlags flags, GIArgument* arg) {
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, info);
    if (!foreign)
        return false;

    if (!foreign->to_func(context, value, arg_name, argument_type, transfer,
                          flags, arg))
        return false;

    return true;
}

bool gjs_struct_foreign_convert_from_gi_argument(JSContext* context,
                                                 JS::MutableHandleValue value_p,
                                                 const GI::StructInfo info,
                                                 GIArgument* arg) {
    GjsForeignInfo* foreign = gjs_struct_foreign_lookup(context, info);
    if (!foreign)
        return false;

    if (!foreign->from_func(context, value_p, arg))
        return false;

    return true;
}

bool gjs_struct_foreign_release_gi_argument(JSContext* context,
                                            GITransfer transfer,
                                            const GI::StructInfo info,
                                            GIArgument* arg) {
    GjsForeignInfo* foreign = gjs_struct_foreign_lookup(context, info);
    if (!foreign)
        return false;

    if (!foreign->release_func)
        return true;

    if (!foreign->release_func(context, transfer, arg))
        return false;

    return true;
}
