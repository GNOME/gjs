/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2018 Marco Trevisan <marco.trevisan@canonical.com>

#define GJS_USE_ATOM_FOREACH

#include <config.h>

#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/atoms.h"

bool GjsAtom::init(JSContext* cx, const char* str) {
    JSString* s = JS_AtomizeAndPinString(cx, str);
    if (!s)
        return false;
    m_jsid = JS::Heap<jsid>{JS::PropertyKey::fromPinnedString(s)};
    return true;
}

bool GjsSymbolAtom::init(JSContext* cx, const char* str) {
    JS::RootedString descr(cx, JS_AtomizeAndPinString(cx, str));
    if (!descr)
        return false;
    JS::Symbol* symbol = JS::NewSymbol(cx, descr);
    if (!symbol)
        return false;
    m_jsid = JS::Heap<jsid>{JS::PropertyKey::Symbol(symbol)};
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
