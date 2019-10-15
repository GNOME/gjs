/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2014 Colin Walters <walters@verbum.org>
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

#ifndef GJS_CONTEXT_PRIVATE_H_
#define GJS_CONTEXT_PRIVATE_H_

#include <stdint.h>
#include <sys/types.h>  // for ssize_t

#include <type_traits>  // for is_same
#include <unordered_map>

#include <string>
#include <vector>

#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"
#include "js/GCHashTable.h"
#include "js/Promise.h"

#include "gjs/atoms.h"
#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/profiler.h"

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

class CppStringHashPolicy {
 public:
    typedef std::string Lookup;

    static js::HashNumber hash(const Lookup& l) {
        return std::hash<std::string>{}(std::string(l));
    }

    static bool match(const std::string& k, const Lookup& l) {
        return k.compare(l) == 0;
    }

    static void rekey(std::string& k, const std::string& newKey) { k = newKey; }
};

namespace JS {
template <>
struct GCPolicy<std::string> : public IgnoreGCPolicy<std::string> {};
}  // namespace JS
using ModuleTable = JS::GCHashMap<std::string, JS::Heap<JSObject*>,
                                  CppStringHashPolicy, js::SystemAllocPolicy>;

struct Dummy {};
using GTypeNotUint64 =
    std::conditional_t<!std::is_same<GType, uint64_t>::value, GType, Dummy>;

// The GC sweep method should ignore FundamentalTable and GTypeTable's key types
namespace JS {
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
    JS::Heap<JSObject*> m_legacy_global;
    JS::Heap<JSObject*> m_module_global;
    GThread* m_owner_thread;

    std::vector<std::string> m_args;

    char* m_program_name;

    char** m_search_path;

    unsigned m_auto_gc_id;

    GjsAtoms* m_atoms;

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
    bool m_destroying : 1;
    bool m_in_gc_sweep : 1;
    bool m_should_exit : 1;
    bool m_force_gc : 1;
    bool m_draining_job_queue : 1;
    bool m_should_profile : 1;
    bool m_exec_as_module: 1;
    bool m_should_listen_sigusr2 : 1;

    int64_t m_sweep_begin_time;

    void schedule_gc_internal(bool force_gc);
    static gboolean trigger_gc_if_needed(void* data);

    class SavedQueue;
    void start_draining_job_queue(void);
    void stop_draining_job_queue(void);
    static gboolean drain_job_queue_idle_handler(void* data);

    void warn_about_unhandled_promise_rejections(void);
    void reset_exit(void) {
        m_should_exit = false;
        m_exit_code = 0;
    }

 public:
    /* Retrieving a GjsContextPrivate from JSContext or GjsContext */
    GJS_USE static GjsContextPrivate* from_cx(JSContext* cx) {
        return static_cast<GjsContextPrivate*>(JS_GetContextPrivate(cx));
    }
    GJS_USE static GjsContextPrivate* from_object(GObject* public_context);
    GJS_USE static GjsContextPrivate* from_object(GjsContext* public_context);
    GJS_USE static GjsContextPrivate* from_current_context(void) {
        return from_object(gjs_context_get_current());
    }

    GjsContextPrivate(JSContext* cx, GjsContext* public_context);
    ~GjsContextPrivate(void);

    /* Accessors */
    GJS_USE GjsContext* public_context(void) const { return m_public_context; }
    GJS_USE JSContext* context(void) const { return m_cx; }
    GJS_USE GjsProfiler* profiler(void) const { return m_profiler; }
    GJS_USE const GjsAtoms& atoms(void) const { return *m_atoms; }
    GJS_USE bool destroying(void) const { return m_destroying; }
    GJS_USE bool sweeping(void) const { return m_in_gc_sweep; }
    GJS_USE const char* program_name(void) const { return m_program_name; }
    void set_program_name(char* value) { m_program_name = value; }
    void set_search_path(char** value) { m_search_path = value; }
    void set_should_profile(bool value) { m_should_profile = value; }
    void set_execute_as_module(bool value) { m_exec_as_module = value; }
    void set_should_listen_sigusr2(bool value) {
        m_should_listen_sigusr2 = value;
    }
    void set_args(std::vector<std::string> args);
    std::vector<std::string> get_args();
    GJS_USE bool is_owner_thread(void) const {
        return m_owner_thread == g_thread_self();
    }
    GJS_USE JS::WeakCache<FundamentalTable>& fundamental_table(void) {
        return *m_fundamental_table;
    }
    GJS_USE JS::WeakCache<GTypeTable>& gtype_table(void) {
        return *m_gtype_table;
    }
    GJS_USE ObjectInitList& object_init_list(void) {
        return m_object_init_list;
    }
    GJS_USE
    static const GjsAtoms& atoms(JSContext* cx) {
        return *(from_cx(cx)->m_atoms);
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool eval(const char* script, ssize_t script_len, const char* filename,
              int* exit_status_p, GError** error);
    GJS_JSAPI_RETURN_CONVENTION
    bool eval_with_scope(JS::HandleObject scope_object, const char* script,
                         ssize_t script_len, const char* filename,
                         JS::MutableHandleValue retval);
    GJS_JSAPI_RETURN_CONVENTION
    bool eval_module(const char* identifier, uint8_t* exit_code_p,
                     GError** error);

    GJS_JSAPI_RETURN_CONVENTION
    bool call_function(JS::HandleObject this_obj, JS::HandleValue func_val,
                       const JS::HandleValueArray& args,
                       JS::MutableHandleValue rval);

    void schedule_gc(void) { schedule_gc_internal(true); }
    void schedule_gc_if_needed(void);

    void exit(uint8_t exit_code);
    GJS_USE bool should_exit(uint8_t* exit_code_p) const;

    // Implementations of JS::JobQueue virtual functions
    GJS_JSAPI_RETURN_CONVENTION
    JSObject* getIncumbentGlobal(JSContext* cx) override;
    GJS_JSAPI_RETURN_CONVENTION
    bool enqueuePromiseJob(JSContext* cx, JS::HandleObject promise,
                           JS::HandleObject job,
                           JS::HandleObject allocation_site,
                           JS::HandleObject incumbent_global) override;
    void runJobs(JSContext* cx) override;
    GJS_USE bool empty(void) const override { return m_job_queue.empty(); };
    js::UniquePtr<JS::JobQueue::SavedJobQueue> saveJobQueue(
        JSContext* cx) override;

    GJS_JSAPI_RETURN_CONVENTION bool run_jobs_fallible(void);
    void register_unhandled_promise_rejection(uint64_t id, GjsAutoChar&& stack);
    void unregister_unhandled_promise_rejection(uint64_t id);

    void context_reset_exit();

    bool register_module(const char* identifier, const char* filename,
                         const char* mod_text, size_t mod_len, GError** error);

    void set_sweeping(bool value);

    static void trace(JSTracer* trc, void* data);

    void free_profiler(void);
    void dispose(void);
};

#endif  // GJS_CONTEXT_PRIVATE_H_
