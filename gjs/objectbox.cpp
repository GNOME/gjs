/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <stddef.h>  // for size_t

#include <glib.h>

#include <js/ComparisonOperators.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/objectbox.h"
#include "util/log.h"

/* gjs/objectbox.cpp - GObject boxed type used to "box" a JS object so that it
 * can be passed to or returned from a GObject signal, or used as the type of a
 * GObject property.
 */

struct ObjectBox::impl {
    impl(ObjectBox* parent, JSContext* cx, JSObject* obj)
        : m_parent(parent), m_root(cx, obj) {
        g_atomic_ref_count_init(&m_refcount);
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
    JS::PersistentRooted<JSObject*> m_root;
    gatomicrefcount m_refcount;
};

ObjectBox::ObjectBox(JSContext* cx, JSObject* obj)
    : m_impl(std::make_unique<ObjectBox::impl>(this, cx, obj)) {}

ObjectBox::Ptr ObjectBox::boxed(JSContext* cx, JSObject* obj) {
    return ObjectBox::Ptr(new ObjectBox(cx, obj),
                          [](ObjectBox* box) { box->m_impl->unref(); });
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
