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

#include <vector>

#include "gjs/gjsobject.h"
#include "gjs/jsapi-util.h"

namespace {
std::vector<GJSObject*> m_wrappers;
}

struct GJSObject::impl {
    impl(GJSObject* parent, JSContext* cx, const JS::HandleObject& obj)
        : m_parent(parent), m_root(cx, obj) {
        g_atomic_ref_count_init(&m_refcount);
        m_wrappers.push_back(m_parent);
    }

    ~impl() {
        auto it = std::find(m_wrappers.begin(), m_wrappers.end(), m_parent);
        m_wrappers.erase(it);
    }

    void ref() { g_atomic_ref_count_inc(&m_refcount); }

    void unref() {
        if (g_atomic_ref_count_dec(&m_refcount))
            delete m_parent;
    }

    GJSObject* m_parent;
    JS::PersistentRooted<JSObject*> m_root;
    gatomicrefcount m_refcount;
};

GJSObject::GJSObject(JSContext* cx, const JS::HandleObject& obj)
    : m_impl(std::make_unique<GJSObject::impl>(this, cx, obj)) {}

GJSObject::Ptr GJSObject::boxed(JSContext* cx, const JS::HandleObject& obj) {
    GJSObject* gjsobj = nullptr;

    for (auto* g : m_wrappers) {
        if (g->m_impl->m_root == obj) {
            gjsobj = g;
            gjsobj->m_impl->ref();
            break;
        }
    }

    if (!gjsobj)
        gjsobj = new GJSObject(cx, obj);

    return GJSObject::Ptr(gjsobj, [](GJSObject* g) { g->m_impl->unref(); });
}

JSObject* GJSObject::object_for_c_ptr(JSContext* cx, GJSObject* gjsobject) {
    if (!gjsobject) {
        gjs_throw(cx, "Cannot get JSObject for null GJSObject pointer");
        return nullptr;
    }

    return gjsobject->m_impl->m_root.get();
}

GType GJSObject::gtype() {
    static volatile size_t type_id = 0;

    if (g_once_init_enter(&type_id)) {
        auto gjsobject_copy = [](gpointer boxed) -> gpointer {
            auto* gjsobject = static_cast<GJSObject*>(boxed);
            gjsobject->m_impl->ref();
            return gjsobject;
        };
        auto gjsobject_free = [](gpointer boxed) {
            auto* gjsobject = static_cast<GJSObject*>(boxed);
            gjsobject->m_impl->unref();
        };
        GType type = g_boxed_type_register_static("JSObject", gjsobject_copy,
                                                  gjsobject_free);
        g_once_init_leave(&type_id, type);
    }

    return type_id;
}
