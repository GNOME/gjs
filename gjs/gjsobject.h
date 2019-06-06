/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2019 Marco Trevisan <marco.trevisan@canonical.com>
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

#ifndef GJS_GJSOBJECT_H_
#define GJS_GJSOBJECT_H_

#include <glib-object.h>
#include <glib.h>
#include <memory>

#include "gjs/jsapi-wrapper.h"

struct GJSObject {
    using Ptr = std::unique_ptr<GJSObject, void (*)(GJSObject*)>;

    static GType gtype();
    static GJSObject::Ptr boxed(JSContext*, const JS::HandleObject&);
    static JSObject* object_for_c_ptr(JSContext*, GJSObject*);

 private:
    GJSObject(JSContext*, const JS::HandleObject&);

    struct impl;
    std::unique_ptr<impl> m_impl;
};

#endif  // GJS_GJSOBJECT_H_
