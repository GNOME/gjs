/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017 Endless Mobile, Inc.
 * Copyright (c) 2019 Canonical, Ltd.
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

#ifndef GJS_JSAPI_UTIL_ROOT_H_
#define GJS_JSAPI_UTIL_ROOT_H_

#include <stdint.h>  // for uintptr_t

#include <cstddef>  // for nullptr_t
#include <memory>
#include <type_traits>  // for enable_if_t, is_pointer

#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/macros.h"
#include "util/log.h"

/* jsapi-util-root.h - Utilities for dealing with the lifetime and ownership of
 * JS Objects and other things that can be collected by the garbage collector
 * (collectively called "GC things.")
 *
 * GjsMaybeOwned<T> is a multi-purpose wrapper for a GC thing of type T. You can
 * wrap a thing in one of three ways:
 *
 * - trace the thing (tie it to the lifetime of another GC thing),
 * - root the thing (keep it alive as long as the wrapper is in existence),
 * - maintain a weak pointer to the thing (not keep it alive at all and have it
 *   possibly be finalized out from under you).
 *
 * To trace or maintain a weak pointer, simply assign a thing of type T to the
 * GjsMaybeOwned wrapper. For tracing, you must call the trace() method when
 * your other GC thing is traced.
 *
 * Rooting requires a JSContext so can't just assign a thing of type T. Instead
 * you need to call the root() method to set up rooting.
 *
 * If the thing is rooted, it will be unrooted either when the GjsMaybeOwned is
 * destroyed, or when the JSContext is destroyed. In the latter case, you can
 * get an optional notification by passing a callback to root().
 *
 * To switch between one of the three modes, you must first call reset(). This
 * drops all references to any GC thing and leaves the GjsMaybeOwned in the
 * same state as if it had just been constructed.
 */

/* This struct contains operations that must be implemented differently
 * depending on the type of the GC thing. Add more types as necessary. If an
 * implementation is never used, it's OK to leave it out. The compiler will
 * complain if it's used somewhere but not instantiated here.
 */
template<typename T>
struct GjsHeapOperation {
    GJS_USE static bool update_after_gc(JS::Heap<T>* location);
    static void expose_to_js(JS::Heap<T>& thing);
};

template<>
struct GjsHeapOperation<JSObject *> {
    GJS_USE static bool update_after_gc(JS::Heap<JSObject*>* location) {
        JS_UpdateWeakPointerAfterGC(location);
        return (location->unbarrieredGet() == nullptr);
    }

    static void expose_to_js(JS::Heap<JSObject *>& thing) {
        JSObject *obj = thing.unbarrieredGet();
        /* If the object has been swept already, then the zone is nullptr */
        if (!obj || !js::gc::detail::GetGCThingZone(uintptr_t(obj)))
            return;
        if (!JS::CurrentThreadIsHeapCollecting())
            JS::ExposeObjectToActiveJS(obj);
    }
};

template <>
struct GjsHeapOperation<JSFunction*> {
    static void expose_to_js(const JS::Heap<JSFunction*>& thing) {
        JSFunction* func = thing.unbarrieredGet();
        if (!func || !js::gc::detail::GetGCThingZone(uintptr_t(func)))
            return;
        if (!JS::CurrentThreadIsHeapCollecting())
            js::gc::ExposeGCThingToActiveJS(JS::GCCellPtr(func));
    }
};

/* GjsMaybeOwned is intended only for use in heap allocation.
 * Use static method to allocate the GjsMaybeOwned::Ptr */
template<typename T>
class GjsMaybeOwned {
 public:
    using DestroyNotify = void (*)(JS::Handle<T>, void*);
    using Ptr = std::unique_ptr<GjsMaybeOwned<T>>;

 private:
    /* m_root value controls which of these members we can access. When switching
     * from one to the other, be careful to call the constructor and destructor
     * of JS::Heap, since they use post barriers. */
    JS::Heap<T> m_heap;
    std::unique_ptr<JS::PersistentRooted<T>> m_root;

    struct Notifier {
        Notifier(GjsMaybeOwned<T> *parent, DestroyNotify func, void *data)
            : m_parent(parent)
            , m_func(func)
            , m_data(data)
        {
            GjsContextPrivate* gjs = GjsContextPrivate::from_current_context();
            g_assert(GJS_IS_CONTEXT(gjs->public_context()));
            g_object_weak_ref(G_OBJECT(gjs->public_context()),
                              on_context_destroy, this);
        }

        ~Notifier() { disconnect(); }

        static void on_context_destroy(void* data,
                                       GObject* ex_context G_GNUC_UNUSED) {
            auto self = static_cast<Notifier*>(data);
            auto *parent = self->m_parent;
            self->m_parent = nullptr;
            self->m_func(parent->handle(), self->m_data);
        }

        void disconnect() {
            if (!m_parent)
                return;

            GjsContextPrivate* gjs = GjsContextPrivate::from_current_context();
            g_assert(GJS_IS_CONTEXT(gjs->public_context()));
            g_object_weak_unref(G_OBJECT(gjs->public_context()), on_context_destroy,
                                this);
            m_parent = nullptr;
        }

     private:
        GjsMaybeOwned<T> *m_parent;
        DestroyNotify m_func;
        void *m_data;
    };
    std::unique_ptr<Notifier> m_notify;

    /* No-op unless GJS_VERBOSE_ENABLE_LIFECYCLE is defined to 1. */
    inline void debug(const char* what GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(GJS_DEBUG_KEEP_ALIVE, "GjsMaybeOwned %p %s", this,
                            what);
    }

    void
    teardown_rooting(void)
    {
        debug("teardown_rooting()");
        g_assert(m_root);

        m_root.reset();
        m_notify.reset();

        new (&m_heap) JS::Heap<T>();
    }

 private:
    GjsMaybeOwned() {
        debug("created");
    }

    explicit GjsMaybeOwned(const T& thing) {
        debug("created");
        *this = thing;
    }

 public:
    ~GjsMaybeOwned(void)
    {
        debug("destroyed");
    }

    friend std::unique_ptr<GjsMaybeOwned<T>> std::make_unique<GjsMaybeOwned<T>>();
    friend std::unique_ptr<GjsMaybeOwned<T>> std::make_unique<GjsMaybeOwned<T>>(const T&);

    GJS_USE
    static GjsMaybeOwned<T>::Ptr newWrapper() {
        return std::make_unique<GjsMaybeOwned<T>>();
    }

    GJS_USE
    static GjsMaybeOwned<T>::Ptr newWrapper(const T& thing) {
        return std::make_unique<GjsMaybeOwned<T>>(thing);
    }

    GJS_USE
    static GjsMaybeOwned<T>::Ptr newRooted(JSContext* cx, const T& thing,
                                           DestroyNotify notify = nullptr,
                                           void* data = nullptr) {
        auto ptr = std::make_unique<GjsMaybeOwned<T>>(thing);
        ptr->root(cx, notify, data);
        return ptr;
    }

    /* To access the GC thing, call get(). In many cases you can just use the
     * GjsMaybeOwned wrapper in place of the GC thing itself due to the implicit
     * cast operator. But if you want to call methods on the GC thing, for
     * example if it's a JS::Value, you have to use get(). */
    GJS_USE const T get(void) const {
        return m_root ? m_root->get() : m_heap.get();
    }
    operator const T(void) const { return get(); }

    /* Use debug_addr() only for debug logging, because it is unbarriered. */
    template <typename U = T>
    GJS_USE const T
    debug_addr(std::enable_if_t<std::is_pointer<U>::value>* = nullptr) const {
        return m_root ? m_root->get() : m_heap.unbarrieredGet();
    }

    bool
    operator==(const T& other) const
    {
        if (m_root)
            return m_root->get() == other;
        return m_heap == other;
    }
    inline bool operator!=(const T& other) const { return !(*this == other); }

    /* We can access the pointer without a read barrier if the only thing we
     * are doing with it is comparing it to nullptr. */
    bool
    operator==(std::nullptr_t) const
    {
        if (m_root)
            return m_root->get() == nullptr;
        return m_heap.unbarrieredGet() == nullptr;
    }
    inline bool operator!=(std::nullptr_t) const { return !(*this == nullptr); }

    /* Likewise the truth value does not require a read barrier */
    inline operator bool() const { return *this != nullptr; }

    /* You can get a Handle<T> if the thing is rooted, so that you can use this
     * wrapper with stack rooting. However, you must not do this if the
     * JSContext can be destroyed while the Handle is live. */
    GJS_USE JS::Handle<T> handle(void) {
        g_assert(m_root);
        return *m_root;
    }

    /* Roots the GC thing. You must not use this if you're already using the
     * wrapper to store a non-rooted GC thing. */
    void
    root(JSContext    *cx,
         const T&      thing,
         DestroyNotify notify = nullptr,
         void         *data   = nullptr)
    {
        debug("root()");
        g_assert(!m_root);
        g_assert(m_heap.get() == JS::GCPolicy<T>::initial());
        m_heap.~Heap();
        m_root = std::make_unique<JS::PersistentRooted<T>>(cx, thing);

        if (notify)
            m_notify = std::make_unique<Notifier>(this, notify, data);
    }

    /* You can only assign directly to the GjsMaybeOwned wrapper in the
     * non-rooted case. */
    void
    operator=(const T& thing)
    {
        g_assert(!m_root);
        m_heap = thing;
    }

    /* Marks an object as reachable for one GC with ExposeObjectToActiveJS().
     * Use to avoid stopping tracing an object during GC. This makes no sense
     * in the rooted case. */
    void
    prevent_collection(void)
    {
        debug("prevent_collection()");
        g_assert(!m_root);
        GjsHeapOperation<T>::expose_to_js(m_heap);
    }

    void
    reset(void)
    {
        debug("reset()");
        if (!m_root) {
            m_heap = JS::GCPolicy<T>::initial();
            return;
        }

        teardown_rooting();
    }

    void
    switch_to_rooted(JSContext    *cx,
                     DestroyNotify notify = nullptr,
                     void         *data   = nullptr)
    {
        debug("switch to rooted");
        g_assert(!m_root);

        /* Prevent the thing from being garbage collected while it is in neither
         * m_heap nor m_root */
        JSAutoRequest ar(cx);
        JS::Rooted<T> thing(cx, m_heap);

        reset();
        root(cx, thing, notify, data);
        g_assert(m_root);
    }

    void
    switch_to_unrooted(void)
    {
        debug("switch to unrooted");
        g_assert(m_root);

        /* Prevent the thing from being garbage collected while it is in neither
         * m_heap nor m_root */
        JSContext *cx = GjsContextPrivate::from_current_context()->context();
        JSAutoRequest ar(cx);
        JS::Rooted<T> thing(cx, *m_root);

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
        JS::TraceEdge<T>(tracer, &m_heap, name);
    }

    /* If not tracing, then you must call this method during GC in order to
     * update the object's location if it was moved, or null it out if it was
     * finalized. If the object was finalized, returns true. */
    GJS_USE bool update_after_gc(void) {
        debug("update_after_gc()");
        g_assert(!m_root);
        return GjsHeapOperation<T>::update_after_gc(&m_heap);
    }

    GJS_USE bool rooted(void) const { return m_root != nullptr; }
};

#endif  // GJS_JSAPI_UTIL_ROOT_H_
