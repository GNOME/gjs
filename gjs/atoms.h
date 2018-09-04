/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018 Philip Chimento <philip.chimento@gmail.com>
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

// clang-format off
#define FOR_EACH_ATOM(macro) \
    macro(code, "code") \
    macro(column_number, "columnNumber") \
    macro(constructor, "constructor") \
    macro(file_name, "fileName") \
    macro(gi, "gi") \
    macro(height, "height") \
    macro(imports, "imports") \
    macro(init, "_init") \
    macro(instance_init, "_instance_init") \
    macro(length, "length") \
    macro(line_number, "lineNumber") \
    macro(message, "message") \
    macro(module_init, "__init__") \
    macro(module_path, "__modulePath__") \
    macro(name, "name") \
    macro(new_, "new") \
    macro(new_internal, "_new_internal") \
    macro(overrides, "overrides") \
    macro(parent_module, "__parentModule__") \
    macro(private_ns_marker, "__gjsPrivateNS") \
    macro(prototype, "prototype") \
    macro(search_path, "searchPath") \
    macro(stack, "stack") \
    macro(versions, "versions") \
    macro(width, "width") \
    macro(x, "x") \
    macro(y, "y")
// clang-format on

class GjsAtoms {
#define DECLARE_ATOM_MEMBER(identifier, str) JS::Heap<jsid> m_##identifier;
    FOR_EACH_ATOM(DECLARE_ATOM_MEMBER)
#undef DECLARE_ATOM_MEMBER

 public:
    explicit GjsAtoms(JSContext* cx);

    void trace(JSTracer* trc);

/* It's OK to return JS::HandleId here, to avoid an extra root, with the caveat
 * that you should not use this value after the GjsContext has been destroyed.*/
#define DECLARE_ATOM_ACCESSOR(identifier, str)                          \
    JS::HandleId identifier(void) const {                               \
        return JS::HandleId::fromMarkedLocation(&m_##identifier.get()); \
    }
    FOR_EACH_ATOM(DECLARE_ATOM_ACCESSOR)
#undef DECLARE_ATOM_ACCESSOR
};

#endif  // GJS_ATOMS_H_
