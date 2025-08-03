/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2019 Canonical, Ltd.

#ifndef GJS_JSAPI_UTIL_ROOT_H_
#define GJS_JSAPI_UTIL_ROOT_H_

#include <config.h>

// https://github.com/include-what-you-use/include-what-you-use/issues/1791
#include <cstddef>  // IWYU pragma: keep
#include <memory>
#include <new>  // IWYU pragma: keep (actually for clangd)

#include <glib.h>

#include <js/ComparisonOperators.h>
#include <js/GCAPI.h>
#include <js/HeapAPI.h>     // for ExposeObjectToActiveJS, GetGCThingZone
#include <js/RootingAPI.h>  // for SafelyInitialized
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>

#include "util/log.h"

namespace JS { template <typename T> struct GCPolicy; }

/* jsapi-util-root.h - Utilities for dealing with the lifetime and ownership of
 * JS Objects and other things that can be collected by the garbage collector
 * (collectively called "GC things.")
 *
 * GjsMaybeOwned is a multi-purpose wrapper for a JSObject. You can
 * wrap a thing in one of three ways:
 *
 * - trace the object (tie it to the lifetime of another GC thing),
 * - root the object (keep it alive as long as the wrapper is in existence),
 * - maintain a weak pointer to the object (not keep it alive at all and have it
 *   possibly be finalized out from under you).
 *
 * To trace or maintain a weak pointer, simply assign an object to the
 * GjsMaybeOwned wrapper. For tracing, you must call the trace() method when
 * your other GC thing is traced.
 *
 * Rooting requires a JSContext so can't just assign a thing of type T. Instead
 * you need to call the root() method to set up rooting.
 *
 * If the thing is rooted, it will be unrooted when the GjsMaybeOwned is
 * destroyed.
 *
 * To switch between one of the three modes, you must first call reset(). This
 * drops all references to any object and leaves the GjsMaybeOwned in the
 * same state as if it had just been constructed.
 */

/* GjsMaybeOwned is intended for use as a member of classes that are allocated
 * on the heap. Do not allocate GjsMaybeOwned on the stack, and do not allocate
 * any instances of classes that have it as a member on the stack either. */
class GjsMaybeOwned {
 private:
    /* m_root value controls which of these members we can access. When switching
     * from one to the other, be careful to call the constructor and destructor
     * of JS::Heap, since they use post barriers. */
    JS::Heap<JSObject*> m_heap;
    std::unique_ptr<JS::PersistentRootedObject> m_root;

    /* No-op unless GJS_VERBOSE_ENABLE_LIFECYCLE is defined to 1. */
    inline void debug(const char* what GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(GJS_DEBUG_KEEP_ALIVE, "GjsMaybeOwned %p %s", this,
                            what);
    }

    void
    teardown_rooting()
    {
        debug("teardown_rooting()");
        g_assert(m_root);

        m_root.reset();

        new (&m_heap) JS::Heap<JSObject*>();
    }

 public:
    GjsMaybeOwned() {
        debug("created");
    }

    ~GjsMaybeOwned() {
        debug("destroyed");
    }

    // COMPAT: constexpr in C++23
    [[nodiscard]] JSObject* get() const {
        return m_root ? m_root->get() : m_heap.get();
    }

    // Use debug_addr() only for debug logging, because it is unbarriered.
    // COMPAT: constexpr in C++23
    [[nodiscard]] const void* debug_addr() const {
        return m_root ? m_root->get() : m_heap.unbarrieredGet();
    }

    // COMPAT: constexpr in C++23
    bool operator==(JSObject* other) const {
        if (m_root)
            return m_root->get() == other;
        return m_heap == other;
    }
    bool operator!=(JSObject* other) const { return !(*this == other); }

    // We can access the pointer without a read barrier if the only thing we are
    // are doing with it is comparing it to nullptr.
    // COMPAT: constexpr in C++23
    bool operator==(std::nullptr_t) const {
        if (m_root)
            return m_root->get() == nullptr;
        return m_heap.unbarrieredGet() == nullptr;
    }
    bool operator!=(std::nullptr_t) const { return !(*this == nullptr); }

    // Likewise the truth value does not require a read barrier
    // COMPAT: constexpr in C++23
    explicit operator bool() const { return *this != nullptr; }

    // You can get a Handle<T> if the thing is rooted, so that you can use this
    // wrapper with stack rooting. However, you must not do this if the
    // JSContext can be destroyed while the Handle is live. */
    // COMPAT: constexpr in C++23
    [[nodiscard]] JS::HandleObject handle() {
        g_assert(m_root);
        return *m_root;
    }

    /* Roots the GC thing. You must not use this if you're already using the
     * wrapper to store a non-rooted GC thing. */
    void root(JSContext* cx, JSObject* thing) {
        debug("root()");
        g_assert(!m_root);
        g_assert(!m_heap);
        m_heap.~Heap();
        m_root = std::make_unique<JS::PersistentRootedObject>(cx, thing);
    }

    /* You can only assign directly to the GjsMaybeOwned wrapper in the
     * non-rooted case. */
    void operator=(JSObject* thing) {
        g_assert(!m_root);
        m_heap = thing;
    }

    /* Marks an object as reachable for one GC with ExposeObjectToActiveJS().
     * Use to avoid stopping tracing an object during GC. This makes no sense
     * in the rooted case. */
    void prevent_collection() {
        debug("prevent_collection()");
        g_assert(!m_root);
        JSObject* obj = m_heap.unbarrieredGet();
        // If the object has been swept already, then the zone is nullptr
        if (!obj || !JS::GetGCThingZone(JS::GCCellPtr(obj)))
            return;
        if (!JS::RuntimeHeapIsCollecting())
            JS::ExposeObjectToActiveJS(obj);
    }

    void reset() {
        debug("reset()");
        if (!m_root) {
            m_heap = nullptr;
            return;
        }

        teardown_rooting();
    }

    void switch_to_rooted(JSContext* cx) {
        debug("switch to rooted");
        g_assert(!m_root);

        /* Prevent the thing from being garbage collected while it is in neither
         * m_heap nor m_root */
        JS::RootedObject thing{cx, m_heap};

        reset();
        root(cx, thing);
        g_assert(m_root);
    }

    void switch_to_unrooted(JSContext* cx) {
        debug("switch to unrooted");
        g_assert(m_root);

        /* Prevent the thing from being garbage collected while it is in neither
         * m_heap nor m_root */
        JS::RootedObject thing{cx, *m_root};

        reset();
        m_heap = thing;
        g_assert(!m_root);
    }

    /* Tracing makes no sense in the rooted case, because JS::PersistentRooted
     * already takes care of that. */
    void
    trace(JSTracer   *tracer,
          const char *name)
    {
        debug("trace()");
        g_assert(!m_root);
        JS::TraceEdge(tracer, &m_heap, name);
    }

    /* If not tracing, then you must call this method during GC in order to
     * update the object's location if it was moved, or null it out if it was
     * finalized. If the object was finalized, returns true. */
    bool update_after_gc(JSTracer* trc) {
        debug("update_after_gc()");
        g_assert(!m_root);
        JS_UpdateWeakPointerAfterGC(trc, &m_heap);
        return !m_heap;
    }

    // COMPAT: constexpr in C++23
    [[nodiscard]] bool rooted() const { return m_root != nullptr; }
};

namespace Gjs {

template <typename T>
class WeakPtr : public JS::Heap<T> {
 public:
    using JS::Heap<T>::Heap;
    using JS::Heap<T>::operator=;
};

}  // namespace Gjs

namespace JS {

template <typename T>
struct GCPolicy<Gjs::WeakPtr<T>> {
    static void trace(JSTracer* trc, Gjs::WeakPtr<T>* thingp,
                      const char* name) {
        return JS::TraceEdge(trc, thingp, name);
    }

    static bool traceWeak(JSTracer* trc, Gjs::WeakPtr<T>* thingp) {
        return js::gc::TraceWeakEdge(trc, thingp);
    }

    static bool needsSweep(JSTracer* trc, const Gjs::WeakPtr<T>* thingp) {
        Gjs::WeakPtr<T> thing{*thingp};
        return !js::gc::TraceWeakEdge(trc, &thing);
    }
};

}  // namespace JS

#endif  // GJS_JSAPI_UTIL_ROOT_H_
