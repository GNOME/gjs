/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2018 Marco Trevisan <marco.trevisan@canonical.com>

#ifndef GJS_ATOMS_H_
#define GJS_ATOMS_H_

#include <config.h>

#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

class JSTracer;

// clang-format off
#define FOR_EACH_ATOM(macro) \
    macro(cause, "cause") \
    macro(code, "code") \
    macro(column_number, "columnNumber") \
    macro(connect_after, "connect_after") \
    macro(constructor, "constructor") \
    macro(debuggee, "debuggee") \
    macro(detail, "detail") \
    macro(emit, "emit") \
    macro(file, "__file__") \
    macro(file_name, "fileName") \
    macro(func, "func") \
    macro(gc_bytes, "gcBytes") \
    macro(gi, "gi") \
    macro(gio, "Gio") \
    macro(glib, "GLib") \
    macro(gobject, "GObject") \
    macro(gtype, "$gtype") \
    macro(height, "height") \
    macro(imports, "imports") \
    macro(importSync, "importSync") \
    macro(init, "_init") \
    macro(instance_init, "_instance_init") \
    macro(interact, "interact") \
    macro(internal, "internal") \
    macro(length, "length") \
    macro(line_number, "lineNumber") \
    macro(malloc_bytes, "mallocBytes") \
    macro(message, "message") \
    macro(module_init, "__init__") \
    macro(module_name, "__moduleName__") \
    macro(module_path, "__modulePath__") \
    macro(name, "name") \
    macro(new_, "new") \
    macro(new_internal, "_new_internal") \
    macro(override, "override") \
    macro(overrides, "overrides") \
    macro(param_spec, "ParamSpec") \
    macro(parent_module, "__parentModule__") \
    macro(program_args, "programArgs") \
    macro(program_invocation_name, "programInvocationName") \
    macro(program_path, "programPath") \
    macro(prototype, "prototype") \
    macro(search_path, "searchPath") \
    macro(signal_id, "signalId") \
    macro(stack, "stack") \
    macro(to_string, "toString") \
    macro(uri, "uri") \
    macro(url, "url") \
    macro(value_of, "valueOf") \
    macro(version, "version") \
    macro(versions, "versions") \
    macro(width, "width") \
    macro(window, "window") \
    macro(x, "x") \
    macro(y, "y") \
    macro(zone, "zone")

#define FOR_EACH_SYMBOL_ATOM(macro) \
    macro(gobject_prototype, "__GObject__prototype") \
    macro(hook_up_vfunc, "__GObject__hook_up_vfunc") \
    macro(private_ns_marker, "__gjsPrivateNS") \
    macro(signal_find, "__GObject__signal_find") \
    macro(signals_block, "__GObject__signals_block") \
    macro(signals_disconnect, "__GObject__signals_disconnect") \
    macro(signals_unblock, "__GObject__signals_unblock")
// clang-format on

struct GjsAtom {
    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx, const char* str);

    /* It's OK to return JS::HandleId here, to avoid an extra root, with the
     * caveat that you should not use this value after the GjsContext has been
     * destroyed.*/
    [[nodiscard]] JS::HandleId operator()() const {
        return JS::HandleId::fromMarkedLocation(&m_jsid.get());
    }

    [[nodiscard]] JS::Heap<jsid>* id() { return &m_jsid; }

 protected:
    JS::Heap<jsid> m_jsid;
};

struct GjsSymbolAtom : GjsAtom {
    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx, const char* str);
};

class GjsAtoms {
 public:
    GjsAtoms(void) {}
    ~GjsAtoms(void) {}  // prevents giant destructor from being inlined
    GJS_JSAPI_RETURN_CONVENTION bool init_atoms(JSContext* cx);

    void trace(JSTracer* trc);

#define DECLARE_ATOM_MEMBER(identifier, str) GjsAtom identifier;
#define DECLARE_SYMBOL_ATOM_MEMBER(identifier, str) GjsSymbolAtom identifier;
    FOR_EACH_ATOM(DECLARE_ATOM_MEMBER)
    FOR_EACH_SYMBOL_ATOM(DECLARE_SYMBOL_ATOM_MEMBER)
#undef DECLARE_ATOM_MEMBER
#undef DECLARE_SYMBOL_ATOM_MEMBER
};

#if !defined(GJS_USE_ATOM_FOREACH) && !defined(USE_UNITY_BUILD)
#    undef FOR_EACH_ATOM
#    undef FOR_EACH_SYMBOL_ATOM
#endif

#endif  // GJS_ATOMS_H_
