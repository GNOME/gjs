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

#include "gjs/atoms.h"
#include "gjs/jsapi-wrapper.h"

GjsAtoms::GjsAtoms(JSContext* cx) {
    JSAutoRequest ar(cx);
    JSString* atom;

#define INITIALIZE_ATOM(identifier, str)    \
    atom = JS_AtomizeAndPinString(cx, str); \
    m_##identifier = INTERNED_STRING_TO_JSID(cx, atom);
    FOR_EACH_ATOM(INITIALIZE_ATOM)
#undef INITIALIZE_ATOM
}

void GjsAtoms::trace(JSTracer* trc) {
#define TRACE_ATOM(identifier, str) \
    JS::TraceEdge<jsid>(trc, &m_##identifier, "Atom " str);
    FOR_EACH_ATOM(TRACE_ATOM)
#undef TRACE_ATOM
}
