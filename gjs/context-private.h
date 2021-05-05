/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2014 Colin Walters <walters@verbum.org>

#ifndef GJS_CONTEXT_PRIVATE_H_
#define GJS_CONTEXT_PRIVATE_H_

#include <config.h>

#include <stdint.h>
#include <sys/types.h>  // for ssize_t

#include <atomic>
#include <string>
#include <type_traits>  // for is_same
#include <unordered_map>
#include <vector>

#include <glib-object.h>
#include <glib.h>

#include <js/GCHashTable.h>
#include <js/GCPolicyAPI.h>
#include <js/GCVector.h>
#include <js/HashTable.h>  // for DefaultHasher
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/UniquePtr.h>
#include <js/ValueArray.h>
#include <jsapi.h>        // for JS_GetContextPrivate
#include <jsfriendapi.h>  // for ScriptEnvironmentPreparer

#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/profiler.h"

namespace js {
class SystemAllocPolicy;
}
class GjsAtoms;
class JSTracer;

using JobQueueStorage =
    JS::GCVector<JS::Heap<JSObject*>, 0, js::SystemAllocPolicy>;
using ObjectInitList =
    JS::GCVector<JS::Heap<JSObject*>, 0, js::SystemAllocPolicy>;
using FundamentalTable =
    JS::GCHashMap<void*, JS::Heap<JSObject*>, js::DefaultHasher<void*>,
                  js::SystemAllocPolicy>;
using GTypeTable =
    JS::GCHashMap<GType, JS::Heap<JSObject*>, js::DefaultHasher<GType>,
                  js::SystemAllocPolicy>;

struct Dummy {};
using GTypeNotUint64 =
    std::conditional_t<!std::is_same_v<GType, uint64_t>, GType, Dummy>;

// The GC sweep method should ignore FundamentalTable and GTypeTable's key types
namespace JS {
// Forward declarations
template <>
struct GCPolicy<void*> : public IgnoreGCPolicy<void*> {};
// We need GCPolicy<GType> for GTypeTable. SpiderMonkey already defines
// GCPolicy<uint64_t> which is equal to GType on some systems; for others we
// need to define it. (macOS's uint64_t is unsigned long long, which is a
// different type from unsigned long, even if they are the same width)
template <>
struct GCPolicy<GTypeNotUint64> : public IgnoreGCPolicy<GTypeNotUint64> {};
}  // namespace JS

class GjsContextPrivate : public JS::JobQueue {
    GjsContext* m_public_context;
    JSContext* m_cx;
    JS::Heap<JSObject*> m_global;
    JS::Heap<JSObject*> m_internal_global;
    GThread* m_owner_thread;

    char* m_program_name;
    char* m_program_path;

    char** m_search_path;

    unsigned m_auto_gc_id;

    GjsAtoms* m_atoms;

    std::vector<std::string> m_args;

    JobQueueStorage m_job_queue;
    unsigned m_idle_drain_handler;

    std::unordered_map<uint64_t, GjsAutoChar> m_unhandled_rejection_stacks;

    GjsProfiler* m_profiler;

    /* Environment preparer needed for debugger, taken from SpiderMonkey's
     * JS shell */
    struct EnvironmentPreparer final : protected js::ScriptEnvironmentPreparer {
        JSContext* m_cx;

        explicit EnvironmentPreparer(JSContext* cx) : m_cx(cx) {
            js::SetScriptEnvironmentPreparer(m_cx, this);
        }

        void invoke(JS::HandleObject scope, Closure& closure) override;
    };
    EnvironmentPreparer m_environment_preparer;

    // Weak pointer mapping from fundamental native pointer to JSObject
    JS::WeakCache<FundamentalTable>* m_fundamental_table;
    JS::WeakCache<GTypeTable>* m_gtype_table;

    // List that holds JSObject GObject wrappers for JS-created classes, from
    // the time of their creation until their GObject instance init function is
    // called
    ObjectInitList m_object_init_list;

    uint8_t m_exit_code;

    /* flags */
    std::atomic_bool m_destroying = ATOMIC_VAR_INIT(false);
    bool m_in_gc_sweep : 1;
    bool m_should_exit : 1;
    bool m_force_gc : 1;
    bool m_draining_job_queue : 1;
    bool m_should_profile : 1;
    bool m_exec_as_module : 1;
    bool m_should_listen_sigusr2 : 1;

    int64_t m_sweep_begin_time;

    void schedule_gc_internal(bool force_gc);
    static gboolean trigger_gc_if_needed(void* data);

    class SavedQueue;
    void start_draining_job_queue(void);
    void stop_draining_job_queue(void);
    static gboolean drain_job_queue_idle_handler(void* data);

    void warn_about_unhandled_promise_rejections(void);

    uint8_t handle_exit_code(const char* type, const char* identifier,
                             GError** error);
    [[nodiscard]] bool auto_profile_enter(void);
    void auto_profile_exit(bool status);

    class AutoResetExit {
        GjsContextPrivate* m_self;

     public:
        explicit AutoResetExit(GjsContextPrivate* self) { m_self = self; }
        ~AutoResetExit() {
            m_self->m_should_exit = false;
            m_self->m_exit_code = 0;
        }
    };

 public:
    /* Retrieving a GjsContextPrivate from JSContext or GjsContext */
    [[nodiscard]] static GjsContextPrivate* from_cx(JSContext* cx) {
        return static_cast<GjsContextPrivate*>(JS_GetContextPrivate(cx));
    }
    [[nodiscard]] static GjsContextPrivate* from_object(
        GObject* public_context);
    [[nodiscard]] static GjsContextPrivate* from_object(
        GjsContext* public_context);
    [[nodiscard]] static GjsContextPrivate* from_current_context();

    GjsContextPrivate(JSContext* cx, GjsContext* public_context);
    ~GjsContextPrivate(void);

    /* Accessors */
    [[nodiscard]] GjsContext* public_context() const {
        return m_public_context;
    }
    [[nodiscard]] JSContext* context() const { return m_cx; }
    [[nodiscard]] JSObject* global() const { return m_global.get(); }
    [[nodiscard]] JSObject* internal_global() const {
        return m_internal_global.get();
    }
    [[nodiscard]] GjsProfiler* profiler() const { return m_profiler; }
    [[nodiscard]] const GjsAtoms& atoms() const { return *m_atoms; }
    [[nodiscard]] bool destroying() const { return m_destroying.load(); }
    [[nodiscard]] bool sweeping() const { return m_in_gc_sweep; }
    [[nodiscard]] const char* program_name() const { return m_program_name; }
    void set_program_name(char* value) { m_program_name = value; }
    GJS_USE const char* program_path(void) const { return m_program_path; }
    void set_program_path(char* value) { m_program_path = value; }
    void set_search_path(char** value) { m_search_path = value; }
    void set_should_profile(bool value) { m_should_profile = value; }
    void set_execute_as_module(bool value) { m_exec_as_module = value; }
    void set_should_listen_sigusr2(bool value) {
        m_should_listen_sigusr2 = value;
    }
    void set_args(std::vector<std::string>&& args);
    GJS_JSAPI_RETURN_CONVENTION JSObject* build_args_array();
    [[nodiscard]] bool is_owner_thread() const {
        return m_owner_thread == g_thread_self();
    }
    [[nodiscard]] JS::WeakCache<FundamentalTable>& fundamental_table() {
        return *m_fundamental_table;
    }
    [[nodiscard]] JS::WeakCache<GTypeTable>& gtype_table() {
        return *m_gtype_table;
    }
    [[nodiscard]] ObjectInitList& object_init_list() {
        return m_object_init_list;
    }
    [[nodiscard]] static const GjsAtoms& atoms(JSContext* cx) {
        return *(from_cx(cx)->m_atoms);
    }

    [[nodiscard]] bool eval(const char* script, ssize_t script_len,
                            const char* filename, int* exit_status_p,
                            GError** error);
    GJS_JSAPI_RETURN_CONVENTION
    bool eval_with_scope(JS::HandleObject scope_object, const char* script,
                         ssize_t script_len, const char* filename,
                         JS::MutableHandleValue retval);
    [[nodiscard]] bool eval_module(const char* identifier, uint8_t* exit_code_p,
                                   GError** error);
    GJS_JSAPI_RETURN_CONVENTION
    bool call_function(JS::HandleObject this_obj, JS::HandleValue func_val,
                       const JS::HandleValueArray& args,
                       JS::MutableHandleValue rval);

    void schedule_gc(void) { schedule_gc_internal(true); }
    void schedule_gc_if_needed(void);

    void exit(uint8_t exit_code);
    [[nodiscard]] bool should_exit(uint8_t* exit_code_p) const;

    // Implementations of JS::JobQueue virtual functions
    GJS_JSAPI_RETURN_CONVENTION
    JSObject* getIncumbentGlobal(JSContext* cx) override;
    GJS_JSAPI_RETURN_CONVENTION
    bool enqueuePromiseJob(JSContext* cx, JS::HandleObject promise,
                           JS::HandleObject job,
                           JS::HandleObject allocation_site,
                           JS::HandleObject incumbent_global) override;
    void runJobs(JSContext* cx) override;
    [[nodiscard]] bool empty() const override { return m_job_queue.empty(); }
    js::UniquePtr<JS::JobQueue::SavedJobQueue> saveJobQueue(
        JSContext* cx) override;

    GJS_JSAPI_RETURN_CONVENTION bool run_jobs_fallible(void);
    void register_unhandled_promise_rejection(uint64_t id, GjsAutoChar&& stack);
    void unregister_unhandled_promise_rejection(uint64_t id);

    [[nodiscard]] bool register_module(const char* identifier,
                                       const char* filename, GError** error);

    void set_sweeping(bool value);

    static void trace(JSTracer* trc, void* data);

    void free_profiler(void);
    void dispose(void);
};
#endif  // GJS_CONTEXT_PRIVATE_H_
