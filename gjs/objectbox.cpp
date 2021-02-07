/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <stddef.h>  // for size_t

#include <algorithm>  // for find

#include <glib.h>

#include <js/AllocPolicy.h>
#include <js/ComparisonOperators.h>
#include <js/GCPolicyAPI.h>
#include <js/GCVector.h>
#include <js/RootingAPI.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/objectbox.h"
#include "util/log.h"

/* gjs/objectbox.cpp - GObject boxed type used to "box" a JS object so that it
 * can be passed to or returned from a GObject signal, or used as the type of a
 * GObject property.
 */

namespace JS {
template <>
struct GCPolicy<ObjectBox*> : NonGCPointerPolicy<ObjectBox*> {};
}  // namespace JS

namespace {
JS::PersistentRooted<JS::GCVector<ObjectBox*, 0, js::SystemAllocPolicy>>
    m_wrappers;
}

struct ObjectBox::impl {
    impl(ObjectBox* parent, JSObject* obj) : m_parent(parent), m_root(obj) {
        g_atomic_ref_count_init(&m_refcount);
        debug("Constructed");
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool init(JSContext* cx) {
        if (!m_wrappers.append(m_parent)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }
        return true;
    }

    ~impl() {
        auto it = std::find(m_wrappers.begin(), m_wrappers.end(), m_parent);
        m_wrappers.erase(it);
        debug("Finalized");
    }

    void ref() {
        debug("incref");
        g_atomic_ref_count_inc(&m_refcount);
    }

    void unref() {
        debug("decref");
        if (g_atomic_ref_count_dec(&m_refcount))
            delete m_parent;
    }

    void debug(const char* what GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                            "%s: ObjectBox %p, JSObject %s", what, m_parent,
                            gjs_debug_object(m_root).c_str());
    }

    ObjectBox* m_parent;
    JS::Heap<JSObject*> m_root;
    gatomicrefcount m_refcount;
};

ObjectBox::ObjectBox(JSObject* obj)
    : m_impl(std::make_unique<ObjectBox::impl>(this, obj)) {}

ObjectBox::Ptr ObjectBox::boxed(JSContext* cx, JSObject* obj) {
    ObjectBox* box = nullptr;

    for (auto* b : m_wrappers) {
        if (b->m_impl->m_root == obj) {
            box = b;
            box->m_impl->ref();
            box->m_impl->debug("Reusing box");
            break;
        }
    }

    if (!box) {
        box = new ObjectBox(obj);
        if (!box->m_impl->init(cx))
            return ObjectBox::Ptr(nullptr, [](ObjectBox*) {});
    }

    return ObjectBox::Ptr(box, [](ObjectBox* b) { b->m_impl->unref(); });
}

JSObject* ObjectBox::object_for_c_ptr(JSContext* cx, ObjectBox* box) {
    if (!box) {
        gjs_throw(cx, "Cannot get JSObject for null ObjectBox pointer");
        return nullptr;
    }

    box->m_impl->debug("retrieved JSObject");
    return box->m_impl->m_root.get();
}

GType ObjectBox::gtype() {
    static volatile size_t type_id = 0;

    if (g_once_init_enter(&type_id)) {
        auto objectbox_copy = [](void* boxed) -> void* {
            auto* box = static_cast<ObjectBox*>(boxed);
            box->m_impl->ref();
            return box;
        };
        auto objectbox_free = [](void* boxed) {
            auto* box = static_cast<ObjectBox*>(boxed);
            box->m_impl->unref();
        };
        GType type = g_boxed_type_register_static("JSObject", objectbox_copy,
                                                  objectbox_free);
        g_once_init_leave(&type_id, type);
    }

    return type_id;
}

void ObjectBox::trace(JSTracer* trc) {
    JS::TraceEdge(trc, &m_impl->m_root, "object in ObjectBox");
}
