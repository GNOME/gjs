/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <algorithm>  // for find

#include <glib.h>

#include <js/AllocPolicy.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/GCPolicyAPI.h>  // for GCPolicy (ptr only), NonGCPointe...
#include <js/GCVector.h>
#include <js/RootingAPI.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/auto.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
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

ObjectBox::ObjectBox(JSObject* obj) : m_impl(new ObjectBox::impl(this, obj)) {}

void ObjectBox::destroy(ObjectBox* object) { object->m_impl->unref(); }

void ObjectBox::destroy_impl(ObjectBox::impl* impl) { delete impl; }

ObjectBox::Ptr ObjectBox::boxed(JSContext* cx, JSObject* obj) {
    ObjectBox::Ptr box;

    ObjectBox** found =
        std::find_if(m_wrappers.begin(), m_wrappers.end(),
                     [obj](ObjectBox* b) { return b->m_impl->m_root == obj; });
    if (found != m_wrappers.end()) {
        box = *found;
        box->m_impl->ref();
        box->m_impl->debug("Reusing box");
    } else {
        box = new ObjectBox(obj);
        if (!box->m_impl->init(cx))
            return nullptr;
    }

    return box;
}

JSObject* ObjectBox::object_for_c_ptr(JSContext* cx, ObjectBox* box) {
    if (!box) {
        gjs_throw(cx, "Cannot get JSObject for null ObjectBox pointer");
        return nullptr;
    }

    box->m_impl->debug("retrieved JSObject");
    return box->m_impl->m_root.get();
}

void* ObjectBox::boxed_copy(void* boxed) {
    auto* box = static_cast<ObjectBox*>(boxed);
    box->m_impl->ref();
    return box;
}

void ObjectBox::boxed_free(void* boxed) {
    auto* box = static_cast<ObjectBox*>(boxed);
    box->m_impl->unref();
}

GType ObjectBox::gtype() {
    // Initialization of static local variable guaranteed only once in C++11
    static GType type_id = g_boxed_type_register_static(
        "JSObject", &ObjectBox::boxed_copy, &ObjectBox::boxed_free);
    return type_id;
}

void ObjectBox::trace(JSTracer* trc) {
    JS::TraceEdge(trc, &m_impl->m_root, "object in ObjectBox");
}
