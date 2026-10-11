/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <vector>

#include <glib.h>

#include <js/ComparisonOperators.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/auto.h"
#include "gjs/jsapi-util.h"
#include "gjs/objectbox.h"
#include "util/log.h"

/* gjs/objectbox.cpp - GObject boxed type used to "box" a JS object so that it
 * can be passed to or returned from a GObject signal, or used as the type of a
 * GObject property.
 */

static std::vector<ObjectBox*> s_wrappers;

struct ObjectBox::impl {
    impl(JSContext* cx, ObjectBox* parent, JSObject* obj)
        : m_parent(parent), m_root(cx, obj) {
        g_atomic_ref_count_init(&m_refcount);
        s_wrappers.push_back(m_parent);
        debug("Constructed");
    }

    ~impl() {
        s_wrappers.erase(
            std::remove(s_wrappers.begin(), s_wrappers.end(), m_parent),
            s_wrappers.end());
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

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void debug(const char* what GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                            "%s: ObjectBox %p, JSObject %s", what, m_parent,
                            gjs_debug_object(m_root).c_str());
    }

    ObjectBox* m_parent;
    JS::PersistentRooted<JSObject*> m_root;
    gatomicrefcount m_refcount;
};

ObjectBox::ObjectBox(JSContext* cx, JSObject* obj)
    : m_impl(new ObjectBox::impl(cx, this, obj)) {}

void ObjectBox::destroy(ObjectBox* object) { object->m_impl->unref(); }

void ObjectBox::destroy_impl(ObjectBox::impl* impl) { delete impl; }

ObjectBox::Ptr ObjectBox::boxed(JSContext* cx, JSObject* obj) {
    for (ObjectBox* box : s_wrappers) {
        if (box->m_impl->m_root == obj) {
            box->m_impl->ref();
            box->m_impl->debug("Reusing box");
            return box;
        }
    }

    return new ObjectBox(cx, obj);
}

JSObject* ObjectBox::object_for_c_ptr(JSContext* cx, ObjectBox* box) {
    if (!box) {
        gjs_throw(cx, "Cannot get JSObject for null ObjectBox pointer");
        return nullptr;
    }

    JSObject* obj = box->m_impl->m_root.get();
    if (!obj) {
        gjs_throw(cx,
                  "JSObject for ObjectBox %p belongs to destroyed JSContext",
                  box);
        return nullptr;
    }

    box->m_impl->debug("retrieved JSObject");
    return obj;
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
