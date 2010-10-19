/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018 Philip Chimento <philip.chimento@gmail.com>
 *                    Marco Trevisan <marco.trevisan@canonical.com>
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

#ifndef GJS_ATOMS_H_
#define GJS_ATOMS_H_

#include "gjs/jsapi-wrapper.h"
#include "gjs/macros.h"

// clang-format off
#define FOR_EACH_ATOM(macro) \
    macro(code, "code") \
    macro(column_number, "columnNumber") \
    macro(connect_after, "connect_after") \
    macro(constructor, "constructor") \
    macro(debuggee, "debuggee") \
    macro(emit, "emit") \
    macro(file, "__file__") \
    macro(file_name, "fileName") \
    macro(gi, "gi") \
    macro(gio, "Gio") \
    macro(glib, "GLib") \
    macro(gobject, "GObject") \
    macro(gtype, "$gtype") \
    macro(height, "height") \
    macro(imports, "imports") \
    macro(init, "_init") \
    macro(instance_init, "_instance_init") \
    macro(interact, "interact") \
    macro(length, "length") \
    macro(line_number, "lineNumber") \
    macro(message, "message") \
    macro(module_init, "__init__") \
    macro(module_name, "__moduleName__") \
    macro(module_path, "__modulePath__") \
    macro(name, "name") \
    macro(new_, "new") \
    macro(new_internal, "_new_internal") \
    macro(overrides, "overrides") \
    macro(param_spec, "ParamSpec") \
    macro(parent_module, "__parentModule__") \
    macro(program_invocation_name, "programInvocationName") \
    macro(prototype, "prototype") \
    macro(search_path, "searchPath") \
    macro(stack, "stack") \
    macro(to_string, "toString") \
    macro(value_of, "valueOf") \
    macro(version, "version") \
    macro(versions, "versions") \
    macro(width, "width") \
    macro(window, "window") \
    macro(x, "x") \
    macro(y, "y")

#define FOR_EACH_SYMBOL_ATOM(macro) \
    macro(hook_up_vfunc, "__GObject__hook_up_vfunc") \
    macro(private_ns_marker, "__gjsPrivateNS")
// clang-format on

struct GjsAtom {
    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx, const char* str);

    /* It's OK to return JS::HandleId here, to avoid an extra root, with the
     * caveat that you should not use this value after the GjsContext has been
     * destroyed.*/
    GJS_USE JS::HandleId operator()() const {
        return JS::HandleId::fromMarkedLocation(&m_jsid.get());
    }

    GJS_USE JS::Heap<jsid>* id() { return &m_jsid; }

 protected:
    JS::Heap<jsid> m_jsid;
};

struct GjsSymbolAtom : GjsAtom {
    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx, const char* str);
};

class GjsAtoms {
 public:
    explicit GjsAtoms(JSContext* cx) {}
    GJS_JSAPI_RETURN_CONVENTION bool init_atoms(JSContext* cx);

    void trace(JSTracer* trc);

#define DECLARE_ATOM_MEMBER(identifier, str) GjsAtom identifier;
#define DECLARE_SYMBOL_ATOM_MEMBER(identifier, str) GjsSymbolAtom identifier;
    FOR_EACH_ATOM(DECLARE_ATOM_MEMBER)
    FOR_EACH_SYMBOL_ATOM(DECLARE_SYMBOL_ATOM_MEMBER)
#undef DECLARE_ATOM_MEMBER
#undef DECLARE_SYMBOL_ATOM_MEMBER
};

#ifndef GJS_USE_ATOM_FOREACH
#    undef FOR_EACH_ATOM
#    undef FOR_EACH_SYMBOL_ATOM
#endif

#endif  // GJS_ATOMS_H_
