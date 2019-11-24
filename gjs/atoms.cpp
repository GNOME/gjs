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

#define GJS_USE_ATOM_FOREACH

#include <config.h>

#include <js/Symbol.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>  // for JS_AtomizeAndPinString

#include "gjs/atoms.h"

bool GjsAtom::init(JSContext* cx, const char* str) {
    JSString* s = JS_AtomizeAndPinString(cx, str);
    if (!s)
        return false;
    m_jsid = INTERNED_STRING_TO_JSID(cx, s);
    return true;
}

bool GjsSymbolAtom::init(JSContext* cx, const char* str) {
    JS::RootedString descr(cx, JS_AtomizeAndPinString(cx, str));
    if (!descr)
        return false;
    JS::Symbol* symbol = JS::NewSymbol(cx, descr);
    if (!symbol)
        return false;
    m_jsid = SYMBOL_TO_JSID(symbol);
    return true;
}

/* Requires a current realm. This can GC, so it needs to be done after the
 * tracing has been set up. */
bool GjsAtoms::init_atoms(JSContext* cx) {
#define INITIALIZE_ATOM(identifier, str) \
    if (!identifier.init(cx, str))       \
        return false;
    FOR_EACH_ATOM(INITIALIZE_ATOM)
    FOR_EACH_SYMBOL_ATOM(INITIALIZE_ATOM)
    return true;
}

void GjsAtoms::trace(JSTracer* trc) {
#define TRACE_ATOM(identifier, str) \
    JS::TraceEdge<jsid>(trc, identifier.id(), "Atom " str);
    FOR_EACH_ATOM(TRACE_ATOM)
    FOR_EACH_SYMBOL_ATOM(TRACE_ATOM)
#undef TRACE_ATOM
}
