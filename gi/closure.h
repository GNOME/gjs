/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#ifndef GI_CLOSURE_H_
#define GI_CLOSURE_H_

#include <config.h>

#include <glib-object.h>
#include <stddef.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/utils-inl.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

class JSTracer;
namespace JS {
class HandleValueArray;
}

namespace Gjs {

class Closure : public GClosure {
 protected:
    Closure(JSContext*, JSFunction*, bool root, const char* description);
    ~Closure() { unset_context(); }

    // Need to call this if inheriting from Closure to call the dtor
    template <class C>
    constexpr void add_finalize_notifier() {
        static_assert(std::is_base_of_v<Closure, C>);
        g_closure_add_finalize_notifier(
            this, nullptr,
            [](void*, GClosure* closure) { static_cast<C*>(closure)->~C(); });
    }

    void* operator new(size_t size) {
        return g_closure_new_simple(size, nullptr);
    }

    void operator delete(void* p) { unref(static_cast<Closure*>(p)); }

    static Closure* ref(Closure* self) {
        return static_cast<Closure*>(g_closure_ref(self));
    }
    static void unref(Closure* self) { g_closure_unref(self); }

 public:
    using Ptr = GjsAutoPointer<Closure, Closure, unref, ref>;

    [[nodiscard]] constexpr static Closure* for_gclosure(GClosure* gclosure) {
        // We need to do this in order to ensure this is a constant expression
        return static_cast<Closure*>(static_cast<void*>(gclosure));
    }

    [[nodiscard]] static Closure* create(JSContext* cx, JSFunction* callable,
                                         const char* description, bool root) {
        auto* self = new Closure(cx, callable, root, description);
        self->add_finalize_notifier<Closure>();
        return self;
    }

    [[nodiscard]] static Closure* create_marshaled(JSContext* cx,
                                                   JSFunction* callable,
                                                   const char* description) {
        auto* self = new Closure(cx, callable, true /* root */, description);
        self->add_finalize_notifier<Closure>();
        g_closure_set_marshal(self, marshal_cb);
        return self;
    }

    [[nodiscard]] static Closure* create_for_signal(JSContext* cx,
                                                    JSFunction* callable,
                                                    const char* description,
                                                    int signal_id) {
        auto* self = new Closure(cx, callable, false /* root */, description);
        self->add_finalize_notifier<Closure>();
        g_closure_set_meta_marshal(self, gjs_int_to_pointer(signal_id),
                                   marshal_cb);
        return self;
    }

    constexpr JSFunction* callable() const { return m_func; }
    [[nodiscard]] constexpr JSContext* context() const { return m_cx; }
    [[nodiscard]] constexpr bool is_valid() const { return !!m_cx; }
    GJS_JSAPI_RETURN_CONVENTION bool invoke(JS::HandleObject,
                                            const JS::HandleValueArray&,
                                            JS::MutableHandleValue);

    void trace(JSTracer* tracer) {
        if (m_func)
            m_func.trace(tracer, "signal connection");
    }

 private:
    void unset_context();

    void reset() {
        unset_context();
        m_func.reset();
        m_cx = nullptr;
    }

    static void marshal_cb(GClosure* closure, GValue* ret, unsigned n_params,
                           const GValue* params, void* hint, void* data) {
        for_gclosure(closure)->marshal(ret, n_params, params, hint, data);
    }

    static void global_context_notifier_cb(JSContext*, void* data) {
        static_cast<Closure*>(data)->global_context_finalized();
    }

    void closure_invalidated();
    void closure_set_invalid();
    void global_context_finalized();
    void marshal(GValue* ret, unsigned n_parms, const GValue* params,
                 void* hint, void* data);

    //  The saved context is used for lifetime management, so that the closure
    //  will be torn down with the context that created it.
    //  The context could be attached to the default context of the runtime
    //  using if we wanted the closure to survive the context that created it.
    JSContext* m_cx;
    GjsMaybeOwned<JSFunction*> m_func;
};

}  // namespace Gjs

#endif  // GI_CLOSURE_H_
