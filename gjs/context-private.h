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

#ifndef __GJS_CONTEXT_PRIVATE_H__
#define __GJS_CONTEXT_PRIVATE_H__

#include <inttypes.h>

#include <unordered_map>

#include "context.h"
#include "gjs/atoms.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"

#include "js/GCHashTable.h"
#include "js/SweepingAPI.h"

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using JobQueue = JS::GCVector<JSObject*, 0, js::SystemAllocPolicy>;
using FundamentalTable =
    JS::GCHashMap<void*, JS::Heap<JSObject*>, js::DefaultHasher<void*>,
                  js::SystemAllocPolicy>;

// FundamentalTable's key type is void*, the GC sweep method should ignore it
namespace JS {
template <>
struct GCPolicy<void*> : public IgnoreGCPolicy<void*> {};
}  // namespace JS

class GjsContextPrivate {
    GjsContext* m_public_context;
    JSContext* m_cx;
    JS::Heap<JSObject*> m_global;
    GThread* m_owner_thread;

    char* m_program_name;

    char** m_search_path;

    unsigned m_auto_gc_id;

    GjsAtoms m_atoms;

    JS::PersistentRooted<JobQueue>* m_job_queue;
    unsigned m_idle_drain_handler;

    std::unordered_map<uint64_t, GjsAutoChar> m_unhandled_rejection_stacks;

    GjsProfiler* m_profiler;

    /* Environment preparer needed for debugger, taken from SpiderMonkey's
     * JS shell */
    struct EnvironmentPreparer final : public js::ScriptEnvironmentPreparer {
        JSContext* m_cx;

        explicit EnvironmentPreparer(JSContext* cx) : m_cx(cx) {
            js::SetScriptEnvironmentPreparer(m_cx, this);
        }

        void invoke(JS::HandleObject scope, Closure& closure) override;
    };
    EnvironmentPreparer m_environment_preparer;

    // Weak pointer mapping from fundamental native pointer to JSObject
    JS::WeakCache<FundamentalTable>* m_fundamental_table;

    uint8_t m_exit_code;

    /* flags */
    bool m_destroying : 1;
    bool m_in_gc_sweep : 1;
    bool m_should_exit : 1;
    bool m_force_gc : 1;
    bool m_draining_job_queue : 1;
    bool m_should_profile : 1;
    bool m_should_listen_sigusr2 : 1;

    void schedule_gc_internal(bool force_gc);
    static gboolean trigger_gc_if_needed(void* data);
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
    GJS_USE JSObject* global(void) const { return m_global.get(); }
    GJS_USE GjsProfiler* profiler(void) const { return m_profiler; }
    GJS_USE const GjsAtoms& atoms(void) const { return m_atoms; }
    GJS_USE bool destroying(void) const { return m_destroying; }
    GJS_USE bool sweeping(void) const { return m_in_gc_sweep; }
    void set_sweeping(bool value) { m_in_gc_sweep = value; }
    GJS_USE const char* program_name(void) const { return m_program_name; }
    void set_program_name(char* value) { m_program_name = value; }
    void set_search_path(char** value) { m_search_path = value; }
    void set_should_profile(bool value) { m_should_profile = value; }
    void set_should_listen_sigusr2(bool value) {
        m_should_listen_sigusr2 = value;
    }
    GJS_USE bool is_owner_thread(void) const {
        return m_owner_thread == g_thread_self();
    }
    GJS_USE JS::WeakCache<FundamentalTable>& fundamental_table(void) {
        return *m_fundamental_table;
    }
    GJS_USE
    static const GjsAtoms& atoms(JSContext* cx) { return from_cx(cx)->m_atoms; }

    GJS_JSAPI_RETURN_CONVENTION
    bool eval(const char* script, ssize_t script_len, const char* filename,
              int* exit_status_p, GError** error);
    GJS_JSAPI_RETURN_CONVENTION
    bool eval_with_scope(JS::HandleObject scope_object, const char* script,
                         ssize_t script_len, const char* filename,
                         JS::MutableHandleValue retval);
    GJS_JSAPI_RETURN_CONVENTION
    bool call_function(JS::HandleObject this_obj, JS::HandleValue func_val,
                       const JS::HandleValueArray& args,
                       JS::MutableHandleValue rval);

    void schedule_gc(void) { schedule_gc_internal(true); }
    void schedule_gc_if_needed(void);

    void exit(uint8_t exit_code);
    GJS_USE bool should_exit(uint8_t* exit_code_p) const;

    GJS_JSAPI_RETURN_CONVENTION bool enqueue_job(JS::HandleObject job);
    GJS_JSAPI_RETURN_CONVENTION bool run_jobs(void);
    void register_unhandled_promise_rejection(uint64_t id, GjsAutoChar&& stack);
    void unregister_unhandled_promise_rejection(uint64_t id);

    static void trace(JSTracer* trc, void* data);

    void free_profiler(void);
    void dispose(void);
};

#if defined (G_OS_WIN32) && defined (_MSC_VER) && (_MSC_VER >= 1900)
/* Unfortunately Visual Studio's C++ .lib somehow did not contain the right codecvt stuff
 * that we need to convert from utf8 to utf16 (char16_t), so we need to work around this
 * Visual Studio bug.  Use Windows API MultiByteToWideChar() and obtain the std::u16string on
 * the std::wstring we obtain from MultiByteToWideChar().  See:
 * https://social.msdn.microsoft.com/Forums/en-US/8f40dcd8-c67f-4eba-9134-a19b9178e481/vs-2015-rc-linker-stdcodecvt-error?forum=vcgeneral
 */
static std::u16string
utf8_to_utf16 (std::string str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (len == 0)
        return nullptr;

    std::wstring wstr(str.length(), 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len) == 0)
        return nullptr;

    std::u16string u16str(wstr.begin(), wstr.end());
    return u16str;
}
#endif
#endif  /* __GJS_CONTEXT_PRIVATE_H__ */
