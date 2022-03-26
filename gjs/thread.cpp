/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <iostream>
#include <mutex>
#include <thread>

#include <glib-object.h>
#include <glib.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsfriendapi.h>  // for DumpBacktrace

#include <js/ContextOptions.h>
#include <js/StructuredClone.h>
#include <js/Vector.h>
#include <jsapi.h>
#include <jsfriendapi.h>

#include "gi/cwrapper.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/thread.h"

#include <algorithm>
#include <functional>
#include <tuple>

namespace Gjs {
class NativeWorker;

static int worker_count = 0;
static std::mutex workerThreadsLock;
static std::vector<NativeWorker*> workerThreads;

class NativeWorkerOptions {
    friend NativeWorker;

    GjsAutoChar m_uri;
    GjsAutoChar m_name;

    explicit NativeWorkerOptions(const char* uri, const char* name)
        : m_uri(const_cast<char*>(uri), GjsAutoTakeOwnership()),
          m_name(const_cast<char*>(name), GjsAutoTakeOwnership()) {}
};

NativeWorker* GetCurrentThreadWorkerPrivate(JSContext* cx) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    if (!gjs || gjs->is_main_thread()) {
        return nullptr;
    }

    JS::RootedValue workerPrivate(
        gjs->context(),
        gjs_get_global_slot(gjs->global(), GjsWorkerGlobalSlot::WORKER));

    if (!workerPrivate.isDouble())
        return nullptr;

    NativeWorker* worker =
        static_cast<NativeWorker*>(workerPrivate.toPrivate());

    if (!worker) {
        return nullptr;
    }

    return worker;
}

class NativeWorker : public CWrapper<NativeWorker> {
    friend CWrapperPointerOps<NativeWorker>;
    friend CWrapper<NativeWorker>;
    friend NativeWorkerOptions;

    GThread* m_thread;
    NativeWorkerOptions m_options;

    GMainContext* m_parent_main_context;
    GMainContext* m_main_context;

    std::unique_ptr<JSAutoStructuredCloneBuffer> m_buffer;
    std::unique_ptr<JSAutoStructuredCloneBuffer> m_host_buffer;

    JS::Heap<JSFunction*> m_received;

    static void* NativeWorkerMain(NativeWorker* worker) {
        // Set the main context for this thread...
        g_main_context_push_thread_default(worker->m_main_context);

        GjsContext* object = gjs_context_new_worker();
        GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);
        gjs_set_global_slot(gjs->global(), GjsWorkerGlobalSlot::WORKER,
                            JS::PrivateValue(worker));
        JSContext* cx = gjs->context();
        JSAutoRealm ar(cx, gjs->global());

        GError* error;
        char* str = g_strdup(worker->m_options.m_uri);

        if (!gjs->register_module(str, str, &error)) {
            gjs_log_exception(cx);
            return nullptr;
        }

        if (!gjs->eval_module(str, nullptr, &error)) {
            gjs_log_exception(cx);
            return nullptr;
        }

        GMainLoop* ml = g_main_loop_new(worker->m_main_context, false);
        g_main_loop_run(ml);

        return nullptr;
    }

    NativeWorker(NativeWorker&) = delete;
    NativeWorker(NativeWorker&&) = delete;

    static constexpr GjsGlobalSlot PROTOTYPE_SLOT =
        GjsGlobalSlot::PROTOTYPE_worker;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_CONTEXT;
    static constexpr unsigned constructor_nargs = 1;

    NativeWorker(NativeWorkerOptions& options)
        : m_thread(nullptr),
          m_options(options),
          m_parent_main_context(g_main_context_ref_thread_default()),
          m_main_context(g_main_context_new()),
          m_buffer(std::make_unique<JSAutoStructuredCloneBuffer>(
              JS::StructuredCloneScope::SameProcess, nullptr, this)),
          m_host_buffer(std::make_unique<JSAutoStructuredCloneBuffer>(
              JS::StructuredCloneScope::SameProcess, nullptr, this)){};

    ~NativeWorker() {
        g_thread_unref(m_thread);
        g_main_context_unref(m_parent_main_context);
        g_main_context_pop_thread_default(m_main_context);
    }

    void run() {
        workerThreadsLock.lock();
        GjsAutoChar name(g_strdup_printf("NativeWorker %i", worker_count++));
        workerThreadsLock.unlock();

        m_thread = g_thread_new(
            name.get(), reinterpret_cast<GThreadFunc>(&NativeWorkerMain), this);
    }

 public:
    bool writeToHost(JSContext* cx, JS::HandleValue write) {
        bool ok = m_host_buffer->write(cx, write, nullptr, this);
        if (!ok)
            return false;

        GSource* source = g_idle_source_new();

        g_source_set_callback(
            source,
            [](void* user_data) -> int {
                NativeWorker* worker = static_cast<NativeWorker*>(user_data);
                if (worker->m_received) {
                    GjsContextPrivate* gjs =
                        GjsContextPrivate::from_current_context();
                    JSContext* cx = gjs->context();
                    JSAutoRealm ar(cx, gjs->global());

                    JS::CloneDataPolicy policy;
                    policy.allowSharedMemoryObjects();
                    JS::RootedValue read(cx);

                    if (!worker->m_host_buffer->read(cx, &read, policy, nullptr,
                                                     worker))
                        return false;
                    JS::RootedFunction fn(cx, worker->m_received);

                    JS::RootedValueArray<1> args(cx);
                    args[0].set(read);
                    JS::RootedValue ignored_rval(cx);
                    if (!JS_CallFunction(cx, nullptr, fn, args,
                                         &ignored_rval)) {
                        gjs_log_exception(cx);
                        return false;
                    }

                    return false;
                }

                return false;
            },
            this, nullptr);

        g_source_attach(source, m_parent_main_context);
        return true;
    }

 private:
    bool writeToWorker(JSContext* cx, JS::HandleValue write) {
        bool ok = m_buffer->write(cx, write, nullptr, this);
        if (!ok)
            return false;

        GSource* source = g_idle_source_new();
        g_source_set_callback(
            source,
            [](void* user_data) -> int {
                NativeWorker* worker = static_cast<NativeWorker*>(user_data);
                worker->receive();

                return false;
            },
            this, nullptr);

        g_source_attach(source, m_main_context);
        return true;
    }

    bool receive() {
        GjsContextPrivate* gjs = GjsContextPrivate::from_current_context();
        JSContext* cx = gjs->context();
        JS::RootedObject global(cx, gjs->global());
        JSAutoRealm ar(cx, global);

        JS::CloneDataPolicy policy;
        policy.allowSharedMemoryObjects();
        JS::RootedValue read(cx);

        if (!m_buffer->read(cx, &read, policy, nullptr, this))
            return false;

        JS::RootedString str(cx, JS::ToString(cx, read));
        JS::UniqueChars c = JS_EncodeStringToUTF8(cx, str);

        JS::RootedValue v_onmessage(
            cx, gjs_get_global_slot(global, GjsWorkerGlobalSlot::ONMESSAGE));
        if (!v_onmessage.isUndefined()) {
            JS::RootedValueArray<1> args(cx);
            args[0].set(read);
            JS::RootedValue ignored_rval(cx);
            if (!JS_CallFunctionValue(cx, nullptr, v_onmessage, args,
                                      &ignored_rval)) {
                gjs_log_exception(cx);
                return false;
            }
        }
        return true;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static NativeWorker* constructor_impl(JSContext* cx,
                                          const JS::CallArgs& args) {
        JS::UniqueChars specifier, name;
        if (!gjs_parse_call_args(cx, "NativeWorker", args, "s?s", "uri",
                                 &specifier, "name", &name))
            return nullptr;

        GjsContextPrivate::from_cx(cx)->main_loop_hold();
        GjsAutoUnref<GFile> file(
            g_file_new_for_commandline_arg(specifier.get()));
        GjsAutoChar uri(g_file_get_uri(file));

        workerThreadsLock.lock();
        GjsAutoChar resolved_name(
            name ? g_strdup_printf("GJS Worker %s", name.get())
                 : g_strdup_printf("GJS Worker %i", worker_count++));
        workerThreadsLock.unlock();

        NativeWorkerOptions options(uri, name.get());

        auto* worker = new NativeWorker(options);

        workerThreadsLock.lock();
        workerThreads.push_back(worker);
        workerThreadsLock.unlock();

        worker->run();

        return worker;
    }

    static bool write(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        if (!args.requireAtLeast(cx, "write", 1))
            return false;

        JS::RootedObject wrapper(cx);
        if (!args.computeThis(cx, &wrapper))
            return false;
        JS::RootedValue value(cx, args[0]);
        NativeWorker* worker;
        if (!NativeWorker::for_js_typecheck(cx, wrapper, &worker))
            return false;

        return worker->writeToWorker(cx, value);
    }

    static bool set_host_receiver(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        JS::RootedObject object(cx);
        if (!gjs_parse_call_args(cx, "set_host_receiver", args, "o", "object",
                                 &object))
            return false;

        JS::RootedObject wrapper(cx);
        if (!args.computeThis(cx, &wrapper))
            return false;

        NativeWorker* worker;
        if (!NativeWorker::for_js_typecheck(cx, wrapper, &worker))
            return false;

        if (!JS_ObjectIsFunction(object))
            return false;

        worker->m_received = JS_GetObjectFunction(object);
        return true;
    }

    static void finalize_impl(JSFreeOp*, NativeWorker* thread) {
        thread->~NativeWorker();
    }

    static void trace(JSTracer* tracer, JSObject* object) {
        NativeWorker* priv = NativeWorker::for_js_nocheck(object);

        JS::TraceEdge<JSFunction*>(tracer, &priv->m_received,
                                   "NativeWorker::m_received");
    }

    static constexpr JSPropertySpec proto_props[] = {JS_PS_END};

    static constexpr JSFunctionSpec proto_funcs[] = {
        JS_FN("write", &NativeWorker::write, 1, 0),
        JS_FN("setReceiver", &NativeWorker::set_host_receiver, 1, 0),
        JS_FS_END};

    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        NativeWorker::proto_funcs,
        NativeWorker::proto_props,
        nullptr,  // define_gtype_prop
    };
    static constexpr struct JSClassOps class_ops = {
        nullptr,
        nullptr,  // deleteProperty
        nullptr,  // enumerate
        nullptr,
        nullptr,
        nullptr,  // mayResolve
        &NativeWorker::finalize,
        NULL,
        NULL,
        NULL,
        &NativeWorker::trace,
    };

    static constexpr JSClass klass = {
        "NativeWorker", JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
        &NativeWorker::class_ops, &NativeWorker::class_spec};

 public:
    static bool set_worker_receiver(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        JS::RootedObject global(cx, gjs_get_import_global(cx));
        JSAutoRealm ar(cx, global);

        JS::RootedObject object(cx);
        if (!gjs_parse_call_args(cx, "set_receiver", args, "o", "object",
                                 &object))
            return false;

        NativeWorker* worker = GetCurrentThreadWorkerPrivate(cx);
        if (!worker)
            return false;

        if (!JS_ObjectIsFunction(object))
            return false;

        gjs_set_global_slot(global, GjsWorkerGlobalSlot::ONMESSAGE,
                            JS::ObjectValue(*object));
        return true;
    };

    static bool get_worker_name(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        JS::RootedObject global(cx, gjs_get_import_global(cx));
        JSAutoRealm ar(cx, global);

        NativeWorker* worker = GetCurrentThreadWorkerPrivate(cx);
        if (!worker)
            return false;

        if (!worker->m_options.m_name.get()) {
            args.rval().setUndefined();
            return true;
        }

        JS::RootedString str(
            cx, JS_NewStringCopyZ(cx, worker->m_options.m_name.get()));
        if (!str)
            return false;
        args.rval().setString(str);
        return true;
    };
};

namespace WorkerGlobal {
bool post_message(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject global(cx, gjs_get_import_global(cx));
    JSAutoRealm ar(cx, global);

    JS::RootedValue value(cx, args[0]);

    Gjs::NativeWorker* worker = GetCurrentThreadWorkerPrivate(cx);

    return worker->writeToHost(cx, value);
}

};  // namespace WorkerGlobal

};  // namespace Gjs

bool gjs_define_worker_stuff(JSContext* cx, JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));

    return Gjs::NativeWorker::create_prototype(cx, module) &&
           JS_DefineFunction(cx, module, "setReceiver",
                             Gjs::NativeWorker::set_worker_receiver, 1, 0) &&
           JS_DefineFunction(cx, module, "getName",
                             Gjs::NativeWorker::get_worker_name, 0, 0);
}