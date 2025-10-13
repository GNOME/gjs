/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <signal.h>  // for sigaction, SIGUSR1, sa_handler
#include <stdint.h>
#include <stdio.h>   // for FILE, fclose, size_t
#include <stdlib.h>  // for exit
#include <string.h>  // for memset

#ifdef HAVE_UNISTD_H
#    include <unistd.h>  // for getpid
#endif

#ifdef G_OS_WIN32
#    include <process.h>
#    include <windows.h>
#endif

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#endif

#include <new>
#include <string>       // for u16string
#include <thread>       // for get_id
#include <unordered_map>
#include <unordered_set>
#include <utility>  // for move
#include <vector>

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallAndConstruct.h>  // for Call, JS_CallFunctionValue
#include <js/CallArgs.h>          // for UndefinedHandleValue
#include <js/CharacterEncoding.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/Context.h>
#include <js/EnvironmentChain.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>     // for StealPendingExceptionStack
#include <js/GCAPI.h>         // for JS_GC, JS_AddExtraGCRootsTr...
#include <js/GCHashTable.h>   // for WeakCache
#include <js/GlobalObject.h>  // for CurrentGlobalOrNull
#include <js/HeapAPI.h>       // for ExposeObjectToActiveJS
#include <js/Id.h>
#include <js/Modules.h>
#include <js/Promise.h>  // for JobQueue::SavedJobQueue
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_RE...
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/ScriptPrivate.h>
#include <js/SourceText.h>
#include <js/String.h>  // for JS_NewStringCopyZ
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>
#include <js/UniquePtr.h>
#include <js/Utility.h>  // for DeletePolicy via WeakCache
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/friend/DumpFunctions.h>
#include <jsapi.h>              // for JS_GetFunctionObject, JS_Ge...
#include <jsfriendapi.h>        // for ScriptEnvironmentPreparer
#include <mozilla/Result.h>
#include <mozilla/UniquePtr.h>  // for UniquePtr::get

#include "gi/function.h"
#include "gi/info.h"
#include "gi/object.h"
#include "gi/private.h"
#include "gi/repo.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"  // IWYU pragma: associated
#include "gjs/context.h"
#include "gjs/engine.h"
#include "gjs/error-types.h"
#include "gjs/gerror-result.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/internal.h"
#include "gjs/jsapi-util.h"
#include "gjs/mainloop.h"
#include "gjs/mem.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "gjs/objectbox.h"
#include "gjs/profiler-private.h"
#include "gjs/profiler.h"
#include "gjs/promise.h"
#include "gjs/text-encoding.h"
#include "modules/cairo-module.h"
#include "modules/console.h"
#include "modules/print.h"
#include "modules/system.h"
#include "util/log.h"

namespace mozilla {
union Utf8Unit;
}

using Gjs::GErrorResult;
using mozilla::Err, mozilla::Ok;

static void     gjs_context_dispose           (GObject               *object);
static void     gjs_context_finalize          (GObject               *object);
static void     gjs_context_constructed       (GObject               *object);
static void     gjs_context_get_property      (GObject               *object,
                                                  guint                  prop_id,
                                                  GValue                *value,
                                                  GParamSpec            *pspec);
static void     gjs_context_set_property      (GObject               *object,
                                                  guint                  prop_id,
                                                  const GValue          *value,
                                                  GParamSpec            *pspec);

void GjsContextPrivate::EnvironmentPreparer::invoke(JS::HandleObject scope,
                                                    Closure& closure) {
    g_assert(!JS_IsExceptionPending(m_cx));

    JSAutoRealm ar(m_cx, scope);
    if (!closure(m_cx))
        gjs_log_exception(m_cx);
}

struct _GjsContext {
    GObject parent;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsContext, gjs_context, G_TYPE_OBJECT);

GjsContextPrivate* GjsContextPrivate::from_object(GObject* js_context) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), nullptr);
    return static_cast<GjsContextPrivate*>(
        gjs_context_get_instance_private(GJS_CONTEXT(js_context)));
}

GjsContextPrivate* GjsContextPrivate::from_object(GjsContext* js_context) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), nullptr);
    return static_cast<GjsContextPrivate*>(
        gjs_context_get_instance_private(js_context));
}

GjsContextPrivate* GjsContextPrivate::from_current_context() {
    return from_object(gjs_context_get_current());
}

enum {
    PROP_CONTEXT_0,
    PROP_PROGRAM_PATH,
    PROP_SEARCH_PATH,
    PROP_PROGRAM_NAME,
    PROP_PROFILER_ENABLED,
    PROP_PROFILER_SIGUSR2,
    PROP_EXEC_AS_MODULE,
    PROP_REPL_HISTORY_PATH
};

static GMutex contexts_lock;
static GList *all_contexts = NULL;

static Gjs::AutoChar dump_heap_output;
static unsigned dump_heap_idle_id = 0;

#ifdef G_OS_UNIX
// Currently heap dumping via SIGUSR1 is only supported on UNIX platforms!
// This can reduce performance. See note in system.cpp on System.dumpHeap().
static void
gjs_context_dump_heaps(void)
{
    static unsigned counter = 0;

    gjs_memory_report("signal handler", false);

    /* dump to sequential files to allow easier comparisons */
    Gjs::AutoChar filename{g_strdup_printf("%s.%jd.%u", dump_heap_output.get(),
                                           intmax_t(getpid()), counter)};
    ++counter;

    FILE *fp = fopen(filename, "w");
    if (!fp)
        return;

    for (GList *l = all_contexts; l; l = g_list_next(l)) {
        auto* gjs = static_cast<GjsContextPrivate*>(l->data);
        js::DumpHeap(gjs->context(), fp, js::CollectNurseryBeforeDump);
    }

    fclose(fp);
}

static gboolean dump_heap_idle(void*) {
    dump_heap_idle_id = 0;

    gjs_context_dump_heaps();

    return false;
}

static void dump_heap_signal_handler(int signum [[maybe_unused]]) {
    if (dump_heap_idle_id == 0)
        dump_heap_idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                                            dump_heap_idle, nullptr, nullptr);
}
#endif

static void
setup_dump_heap(void)
{
    static bool dump_heap_initialized = false;
    if (!dump_heap_initialized) {
        dump_heap_initialized = true;

        /* install signal handler only if environment variable is set */
        const char *heap_output = g_getenv("GJS_DEBUG_HEAP_OUTPUT");
        if (heap_output) {
#ifdef G_OS_UNIX
            struct sigaction sa;

            dump_heap_output = g_strdup(heap_output);

            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = dump_heap_signal_handler;
            sigaction(SIGUSR1, &sa, nullptr);
#else
            g_message(
                "heap dump is currently only supported on UNIX platforms");
#endif
        }
    }
}

static void
gjs_context_init(GjsContext *js_context)
{
    gjs_log_init();
    gjs_context_make_current(js_context);
}

static void
gjs_context_class_init(GjsContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    gjs_log_init();

    object_class->dispose = gjs_context_dispose;
    object_class->finalize = gjs_context_finalize;

    object_class->constructed = gjs_context_constructed;
    object_class->get_property = gjs_context_get_property;
    object_class->set_property = gjs_context_set_property;

    pspec = g_param_spec_boxed("search-path",
                               "Search path",
                               "Path where modules to import should reside",
                               G_TYPE_STRV,
                               (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
                                    PROP_SEARCH_PATH,
                                    pspec);
    g_param_spec_unref(pspec);

    pspec = g_param_spec_string("program-name",
                                "Program Name",
                                "The filename of the launched JS program",
                                "",
                                (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
                                    PROP_PROGRAM_NAME,
                                    pspec);
    g_param_spec_unref(pspec);

    pspec = g_param_spec_string(
        "program-path", "Executed File Path",
        "The full path of the launched file or NULL if GJS was launched from "
        "the C API or interactive console.",
        nullptr, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class, PROP_PROGRAM_PATH, pspec);
    g_param_spec_unref(pspec);

    /**
     * GjsContext:profiler-enabled:
     *
     * Set this property to profile any JS code run by this context. By
     * default, the profiler is started and stopped when you call
     * gjs_context_eval().
     *
     * The value of this property is superseded by the GJS_ENABLE_PROFILER
     * environment variable.
     *
     * You may only have one context with the profiler enabled at a time.
     */
    pspec = g_param_spec_boolean("profiler-enabled", "Profiler enabled",
                                 "Whether to profile JS code run by this context",
                                 FALSE,
                                 GParamFlags(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class, PROP_PROFILER_ENABLED, pspec);
    g_param_spec_unref(pspec);

    /**
     * GjsContext:profiler-sigusr2:
     *
     * Set this property to install a SIGUSR2 signal handler that starts and
     * stops the profiler. This property also implies that
     * #GjsContext:profiler-enabled is set.
     */
    pspec = g_param_spec_boolean("profiler-sigusr2", "Profiler SIGUSR2",
                                 "Whether to activate the profiler on SIGUSR2",
                                 FALSE,
                                 GParamFlags(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class, PROP_PROFILER_SIGUSR2, pspec);
    g_param_spec_unref(pspec);

    pspec = g_param_spec_boolean(
        "exec-as-module", "Execute as module",
        "Whether to execute the file as a module", FALSE,
        GParamFlags(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class, PROP_EXEC_AS_MODULE, pspec);
    g_param_spec_unref(pspec);

    /**
     * GjsContext:repl-history-path:
     *
     * Set this property to persist repl command history in the console or
     * debugger. If NULL, then command history will not be persisted.
     */
    pspec = g_param_spec_string(
        "repl-history-path", "REPL History Path",
        "The writable path to persist repl history", nullptr,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(object_class, PROP_REPL_HISTORY_PATH,
                                    pspec);
    g_param_spec_unref(pspec);

    /* For GjsPrivate */
    if (!g_getenv("GJS_USE_UNINSTALLED_FILES")) {
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        Gjs::AutoChar basedir{
            g_win32_get_package_installation_directory_of_module(gjs_dll)};
        Gjs::AutoChar priv_typelib_dir{g_build_filename(
            basedir, "lib", "gjs", "girepository-1.0", nullptr)};
#else
        Gjs::AutoChar priv_typelib_dir{
            g_build_filename(PKGLIBDIR, "girepository-1.0", nullptr)};
#endif
        GI::Repository repo;
        repo.prepend_search_path(priv_typelib_dir);
    }
    auto& registry = Gjs::NativeModuleDefineFuncs::get();
    registry.add("_promiseNative", gjs_define_native_promise_stuff);
    registry.add("_byteArrayNative", gjs_define_byte_array_stuff);
    registry.add("_encodingNative", gjs_define_text_encoding_stuff);
    registry.add("_gi", gjs_define_private_gi_stuff);
    registry.add("gi", gjs_define_repo);
    registry.add("cairoNative", gjs_js_define_cairo_stuff);
    registry.add("system", gjs_js_define_system_stuff);
    registry.add("console", gjs_define_console_stuff);
    registry.add("_print", gjs_define_print_stuff);
}

void GjsContextPrivate::trace(JSTracer* trc, void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    JS::TraceEdge<JSObject*>(trc, &gjs->m_global, "GJS global object");
    JS::TraceEdge<JSObject*>(trc, &gjs->m_internal_global,
                             "GJS internal global object");
    JS::TraceEdge<JSObject*>(trc, &gjs->m_main_loop_hook, "GJS main loop hook");
    gjs->m_atoms->trace(trc);
    gjs->m_job_queue.trace(trc);
    gjs->m_cleanup_tasks.trace(trc);
    gjs->m_object_init_list.trace(trc);
}

void GjsContextPrivate::warn_about_unhandled_promise_rejections(void) {
    for (auto& kv : m_unhandled_rejection_stacks) {
        const char* stack = kv.second.get();
        g_warning("Unhandled promise rejection. To suppress this warning, add "
                  "an error handler to your promise chain with .catch() or a "
                  "try-catch block around your await expression. %s%s",
                  stack ? "Stack trace of the failed promise:\n" :
                    "Unfortunately there is no stack trace of the failed promise.",
                  stack ? stack : "");
    }
    m_unhandled_rejection_stacks.clear();
}

static void
gjs_context_dispose(GObject *object)
{
    gjs_debug(GJS_DEBUG_CONTEXT, "JS shutdown sequence");

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    g_assert(gjs->is_owner_thread() &&
             "Gjs Context disposed from another thread");

    /* Profiler must be stopped and freed before context is shut down */
    gjs->free_profiler();

    /* Stop accepting entries in the toggle queue before running dispose
     * notifications, which causes all GjsMaybeOwned instances to unroot.
     * We don't want any objects to toggle down after that. */
    gjs_debug(GJS_DEBUG_CONTEXT, "Shutting down toggle queue");
    gjs_object_clear_toggles();
    gjs_object_shutdown_toggle_queue();

    if (gjs->context())
        ObjectInstance::context_dispose_notify(nullptr, object);

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Notifying external reference holders of GjsContext dispose");
    G_OBJECT_CLASS(gjs_context_parent_class)->dispose(object);

    gjs->dispose();
}

void GjsContextPrivate::free_profiler(void) {
    gjs_debug(GJS_DEBUG_CONTEXT, "Stopping profiler");
    if (m_profiler)
        g_clear_pointer(&m_profiler, _gjs_profiler_free);
}

void GjsContextPrivate::register_notifier(DestroyNotify notify_func,
                                          void* data) {
    m_destroy_notifications.insert({notify_func, data});
}

void GjsContextPrivate::unregister_notifier(DestroyNotify notify_func,
                                            void* data) {
    auto target = std::make_pair(notify_func, data);
    m_destroy_notifications.erase(target);
}

void GjsContextPrivate::dispose(void) {
    if (m_cx) {
        stop_draining_job_queue();

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Notifying reference holders of GjsContext dispose");

        for (auto const& destroy_notify : m_destroy_notifications)
            destroy_notify.first(m_cx, destroy_notify.second);

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Checking unhandled promise rejections");
        warn_about_unhandled_promise_rejections();

        gjs_debug(GJS_DEBUG_CONTEXT, "Releasing cached JS wrappers");
        m_fundamental_table->clear();
        m_gtype_table->clear();

        /* Do a full GC here before tearing down, since once we do
         * that we may not have the JS::GetReservedSlot(, 0) to access the
         * context
         */
        gjs_debug(GJS_DEBUG_CONTEXT, "Final triggered GC");
        JS_GC(m_cx, Gjs::GCReason::GJS_CONTEXT_DISPOSE);

        gjs_debug(GJS_DEBUG_CONTEXT, "Destroying JS context");
        m_destroying.store(true);

        /* Now, release all native objects, to avoid recursion between
         * the JS teardown and the C teardown.  The JSObject proxies
         * still exist, but point to NULL.
         */
        gjs_debug(GJS_DEBUG_CONTEXT, "Releasing all native objects");
        ObjectInstance::prepare_shutdown();
        GjsCallbackTrampoline::prepare_shutdown();

        gjs_debug(GJS_DEBUG_CONTEXT, "Disabling auto GC");
        if (m_auto_gc_id > 0) {
            g_source_remove(m_auto_gc_id);
            m_auto_gc_id = 0;
        }

        gjs_debug(GJS_DEBUG_CONTEXT, "Ending trace on global object");
        JS_RemoveExtraGCRootsTracer(m_cx, &GjsContextPrivate::trace, this);
        m_global = nullptr;
        m_internal_global = nullptr;
        m_main_loop_hook = nullptr;

        gjs_debug(GJS_DEBUG_CONTEXT, "Freeing allocated resources");
        delete m_fundamental_table;
        delete m_gtype_table;
        delete m_atoms;

        m_job_queue.clear();
        m_object_init_list.clear();

        /* Tear down JS */
        JS_DestroyContext(m_cx);
        m_cx = nullptr;
        // don't use g_clear_pointer() as we want the pointer intact while we
        // destroy the context in case we dump stack
        gjs_debug(GJS_DEBUG_CONTEXT, "JS context destroyed");
    }
}

GjsContextPrivate::~GjsContextPrivate(void) {
    g_clear_pointer(&m_search_path, g_strfreev);
    g_clear_pointer(&m_program_path, g_free);
    g_clear_pointer(&m_program_name, g_free);
    g_clear_pointer(&m_repl_history_path, g_free);
}

static void
gjs_context_finalize(GObject *object)
{
    if (gjs_context_get_current() == (GjsContext*)object)
        gjs_context_make_current(NULL);

    g_mutex_lock(&contexts_lock);
    all_contexts = g_list_remove(all_contexts, object);
    g_mutex_unlock(&contexts_lock);

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);
    gjs->~GjsContextPrivate();
    G_OBJECT_CLASS(gjs_context_parent_class)->finalize(object);

    g_mutex_lock(&contexts_lock);
    if (!all_contexts)
        gjs_log_cleanup();
    g_mutex_unlock(&contexts_lock);
}

static void
gjs_context_constructed(GObject *object)
{
    GjsContext *js_context = GJS_CONTEXT(object);

    G_OBJECT_CLASS(gjs_context_parent_class)->constructed(object);

    GjsContextPrivate* gjs_location = GjsContextPrivate::from_object(object);
    JSContext* cx = gjs_create_js_context(gjs_location);
    if (!cx)
        g_error("Failed to create javascript context");

    new (gjs_location) GjsContextPrivate(cx, js_context);

    g_mutex_lock(&contexts_lock);
    all_contexts = g_list_prepend(all_contexts, object);
    g_mutex_unlock(&contexts_lock);

    setup_dump_heap();

#ifdef HAVE_READLINE_READLINE_H
    const char* path = gjs_context_get_repl_history_path(js_context);
    // Populate command history from persisted file
    if (path) {
        int err = read_history(path);
        if (err != 0 && g_getenv("GJS_REPL_HISTORY"))
            g_warning("Could not read REPL history file %s: %s", path,
                      g_strerror(err));
    }
#endif
}

static bool on_context_module_rejected_log_exception(JSContext* cx,
                                                     unsigned argc,
                                                     JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    gjs_debug(GJS_DEBUG_IMPORTER, "Module evaluation promise rejected: %s",
              gjs_debug_callable(&args.callee()).c_str());

    JS::HandleValue error = args.get(0);

    GjsContextPrivate* gjs_cx = GjsContextPrivate::from_cx(cx);
    gjs_cx->report_unhandled_exception();

    gjs_log_exception_full(cx, error, nullptr, G_LOG_LEVEL_CRITICAL);

    gjs_cx->main_loop_release();

    args.rval().setUndefined();
    return true;
}

static bool on_context_module_resolved(JSContext* cx, unsigned argc,
                                       JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    gjs_debug(GJS_DEBUG_IMPORTER, "Module evaluation promise resolved: %s",
              gjs_debug_callable(&args.callee()).c_str());

    args.rval().setUndefined();

    GjsContextPrivate::from_cx(cx)->main_loop_release();

    return true;
}

static bool add_promise_reactions(JSContext* cx, JS::HandleValue promise,
                                  JSNative resolve, JSNative reject,
                                  const std::string& debug_tag) {
    g_assert(promise.isObject() && "got weird value from JS::ModuleEvaluate");
    JS::RootedObject promise_object(cx, &promise.toObject());

    std::string resolved_tag = debug_tag + " async resolved";
    std::string rejected_tag = debug_tag + " async rejected";

    JS::RootedFunction on_rejected(
        cx,
        js::NewFunctionWithReserved(cx, reject, 1, 0, rejected_tag.c_str()));
    if (!on_rejected)
        return false;
    JS::RootedFunction on_resolved(
        cx,
        js::NewFunctionWithReserved(cx, resolve, 1, 0, resolved_tag.c_str()));
    if (!on_resolved)
        return false;

    JS::RootedObject resolved(cx, JS_GetFunctionObject(on_resolved));
    JS::RootedObject rejected(cx, JS_GetFunctionObject(on_rejected));

    return JS::AddPromiseReactions(cx, promise_object, resolved, rejected);
}

static void load_context_module(JSContext* cx, const char* uri,
                                const char* debug_identifier) {
    JS::RootedObject loader(cx, gjs_module_load(cx, uri, uri));

    if (!loader) {
        gjs_log_exception(cx);
        g_error("Failed to load %s module.", debug_identifier);
    }

    if (!JS::ModuleLink(cx, loader)) {
        gjs_log_exception(cx);
        g_error("Failed to instantiate %s module.", debug_identifier);
    }

    JS::RootedValue evaluation_promise(cx);
    if (!JS::ModuleEvaluate(cx, loader, &evaluation_promise)) {
        gjs_log_exception(cx);
        g_error("Failed to evaluate %s module.", debug_identifier);
    }

    GjsContextPrivate::from_cx(cx)->main_loop_hold();
    bool ok = add_promise_reactions(
        cx, evaluation_promise, on_context_module_resolved,
        [](JSContext* cx, unsigned argc, JS::Value* vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Module evaluation promise rejected: %s",
                      gjs_debug_callable(&args.callee()).c_str());

            JS::HandleValue error = args.get(0);
            // Abort because this module is required.
            gjs_log_exception_full(cx, error, nullptr, G_LOG_LEVEL_ERROR);

            GjsContextPrivate::from_cx(cx)->main_loop_release();
            return false;
        },
        debug_identifier);

    if (!ok) {
        gjs_log_exception(cx);
        g_error("Failed to load %s module.", debug_identifier);
    }
}

GjsContextPrivate::GjsContextPrivate(JSContext* cx, GjsContext* public_context)
    : m_public_context(public_context),
      m_cx(cx),
      m_owner_thread(std::this_thread::get_id()),
      m_dispatcher(this),
      m_memory_monitor(g_memory_monitor_dup_default()),
      m_environment_preparer(cx) {
    JS_SetGCCallback(
        cx,
        [](JSContext*, JSGCStatus status, JS::GCReason reason, void* data) {
            static_cast<GjsContextPrivate*>(data)->on_garbage_collection(
                status, reason);
        },
        this);

    const char *env_profiler = g_getenv("GJS_ENABLE_PROFILER");
    if (env_profiler || m_should_listen_sigusr2)
        m_should_profile = true;

    if (m_should_profile) {
        m_profiler = _gjs_profiler_new(public_context);

        if (!m_profiler) {
            m_should_profile = false;
        } else {
            if (m_should_listen_sigusr2)
                _gjs_profiler_setup_signals(m_profiler, public_context);
        }
    }

    JSRuntime* rt = JS_GetRuntime(m_cx);
    m_fundamental_table = new JS::WeakCache<FundamentalTable>(rt);
    m_gtype_table = new JS::WeakCache<GTypeTable>(rt);

    m_atoms = new GjsAtoms();

    if (ObjectBox::gtype() == 0)
        g_error("Failed to initialize JSObject GType");

    JS::RootedObject internal_global(
        m_cx, gjs_create_global_object(cx, GjsGlobalType::INTERNAL));

    if (!internal_global) {
        gjs_log_exception(m_cx);
        g_error("Failed to initialize internal global object");
    }

    m_internal_global = internal_global;
    Gjs::AutoInternalRealm ar{this};
    JS_AddExtraGCRootsTracer(m_cx, &GjsContextPrivate::trace, this);

    if (!m_atoms->init_atoms(m_cx)) {
        gjs_log_exception(m_cx);
        g_error("Failed to initialize global strings");
    }

    if (!gjs_define_global_properties(m_cx, internal_global,
                                      GjsGlobalType::INTERNAL,
                                      "GJS internal global", "nullptr")) {
        gjs_log_exception(m_cx);
        g_error("Failed to define properties on internal global object");
    }

    JS::RootedObject global(
        m_cx,
        gjs_create_global_object(cx, GjsGlobalType::DEFAULT, internal_global));

    if (!global) {
        gjs_log_exception(m_cx);
        g_error("Failed to initialize global object");
    }

    m_global = global;

    {
        Gjs::AutoMainRealm ar{this};

        std::vector<std::string> paths;
        if (m_search_path)
            paths = {m_search_path,
                     m_search_path + g_strv_length(m_search_path)};
        JS::RootedObject importer(m_cx, gjs_create_root_importer(m_cx, paths));
        if (!importer) {
            gjs_log_exception(cx);
            g_error("Failed to create root importer");
        }

        g_assert(
            gjs_get_global_slot(global, GjsGlobalSlot::IMPORTS).isUndefined() &&
            "Someone else already created root importer");

        gjs_set_global_slot(global, GjsGlobalSlot::IMPORTS,
                            JS::ObjectValue(*importer));

        if (!gjs_define_global_properties(m_cx, global, GjsGlobalType::DEFAULT,
                                          "GJS", "default")) {
            gjs_log_exception(m_cx);
            g_error("Failed to define properties on global object");
        }
    }

    JS::SetModuleResolveHook(rt, gjs_module_resolve);
    JS::SetModuleDynamicImportHook(rt, gjs_dynamic_module_resolve);
    JS::SetModuleMetadataHook(rt, gjs_populate_module_meta);

    if (!JS_DefineProperty(m_cx, internal_global, "moduleGlobalThis", global,
                           JSPROP_PERMANENT)) {
        gjs_log_exception(m_cx);
        g_error("Failed to define module global in internal global.");
    }

    if (!gjs_load_internal_module(cx, "internalLoader")) {
        gjs_log_exception(cx);
        g_error("Failed to load internal module loaders.");
    }

    load_context_module(cx,
                        "resource:///org/gnome/gjs/modules/internal/loader.js",
                        "module loader");

    {
        Gjs::AutoMainRealm ar{this};
        load_context_module(
            cx, "resource:///org/gnome/gjs/modules/esm/_bootstrap/default.js",
            "ESM bootstrap");
    }

    [[maybe_unused]] bool success = m_main_loop.spin(this);
    g_assert(success && "bootstrap should not call system.exit()");

    g_signal_connect_object(
        m_memory_monitor, "low-memory-warning",
        G_CALLBACK(+[](GjsContext* js_cx, GMemoryMonitorWarningLevel level) {
            auto* cx =
                static_cast<JSContext*>(gjs_context_get_native_context(js_cx));
            JS::PrepareForFullGC(cx);
            JS::GCOptions gc_strength = JS::GCOptions::Normal;
            if (level > G_MEMORY_MONITOR_WARNING_LEVEL_LOW)
                gc_strength = JS::GCOptions::Shrink;
            JS::NonIncrementalGC(cx, gc_strength, Gjs::GCReason::LOW_MEMORY);
        }),
        m_public_context, G_CONNECT_SWAPPED);

    start_draining_job_queue();
}

void GjsContextPrivate::set_args(std::vector<std::string>&& args) {
    m_args = args;
}

JSObject* GjsContextPrivate::build_args_array() {
    return gjs_build_string_array(m_cx, m_args);
}

static void
gjs_context_get_property (GObject     *object,
                          guint        prop_id,
                          GValue      *value,
                          GParamSpec  *pspec)
{
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    switch (prop_id) {
    case PROP_PROGRAM_NAME:
        g_value_set_string(value, gjs->program_name());
        break;
    case PROP_PROGRAM_PATH:
        g_value_set_string(value, gjs->program_path());
        break;
    case PROP_REPL_HISTORY_PATH:
        g_value_set_string(value, gjs->repl_history_path());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gjs_context_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    switch (prop_id) {
    case PROP_SEARCH_PATH:
        gjs->set_search_path(static_cast<char**>(g_value_dup_boxed(value)));
        break;
    case PROP_PROGRAM_NAME:
        gjs->set_program_name(g_value_dup_string(value));
        break;
    case PROP_PROGRAM_PATH:
        gjs->set_program_path(g_value_dup_string(value));
        break;
    case PROP_PROFILER_ENABLED:
        gjs->set_should_profile(g_value_get_boolean(value));
        break;
    case PROP_PROFILER_SIGUSR2:
        gjs->set_should_listen_sigusr2(g_value_get_boolean(value));
        break;
    case PROP_EXEC_AS_MODULE:
        gjs->set_execute_as_module(g_value_get_boolean(value));
        break;
    case PROP_REPL_HISTORY_PATH:
        gjs->set_repl_history_path(g_value_dup_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


GjsContext*
gjs_context_new(void)
{
    return (GjsContext*) g_object_new (GJS_TYPE_CONTEXT, NULL);
}

GjsContext*
gjs_context_new_with_search_path(char** search_path)
{
    return (GjsContext*) g_object_new (GJS_TYPE_CONTEXT,
                         "search-path", search_path,
                         NULL);
}

gboolean GjsContextPrivate::trigger_gc_if_needed(void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    gjs->m_auto_gc_id = 0;

    if (gjs->m_force_gc) {
        gjs_debug_lifecycle(GJS_DEBUG_CONTEXT, "Big Hammer hit");
        JS_GC(gjs->m_cx, Gjs::GCReason::BIG_HAMMER);
    } else {
        gjs_gc_if_needed(gjs->m_cx);
    }
    gjs->m_force_gc = false;

    return G_SOURCE_REMOVE;
}

void GjsContextPrivate::schedule_gc_internal(bool force_gc) {
    m_force_gc |= force_gc;

    if (m_auto_gc_id > 0)
        return;

    if (force_gc)
        gjs_debug_lifecycle(GJS_DEBUG_CONTEXT, "Big Hammer scheduled");

    m_auto_gc_id = g_timeout_add_seconds_full(G_PRIORITY_LOW, 10,
                                              trigger_gc_if_needed, this,
                                              nullptr);

    if (force_gc)
        g_source_set_name_by_id(m_auto_gc_id, "[gjs] Garbage Collection (Big Hammer)");
    else
        g_source_set_name_by_id(m_auto_gc_id, "[gjs] Garbage Collection");
}

/*
 * GjsContextPrivate::schedule_gc_if_needed:
 *
 * Does a minor GC immediately if the JS engine decides one is needed, but also
 * schedules a full GC in the next idle time.
 */
void GjsContextPrivate::schedule_gc_if_needed(void) {
    // We call JS_MaybeGC immediately, but defer a check for a full GC cycle
    // to an idle handler.
    JS_MaybeGC(m_cx);

    schedule_gc_internal(false);
}

void GjsContextPrivate::on_garbage_collection(JSGCStatus status, JS::GCReason reason) {
    if (m_profiler)
        _gjs_profiler_set_gc_status(m_profiler, status, reason);

    switch (status) {
        case JSGC_BEGIN:
            gjs_debug_lifecycle(GJS_DEBUG_CONTEXT,
                                "Begin garbage collection because of %s",
                                gjs_explain_gc_reason(reason));

            // We finalize any pending toggle refs before doing any garbage
            // collection, so that we can collect the JS wrapper objects, and in
            // order to minimize the chances of objects having a pending toggle
            // up queued when they are garbage collected.
            gjs_object_clear_toggles();

            m_async_closures.clear();
            m_async_closures.shrink_to_fit();
            break;
        case JSGC_END:
            gjs_debug_lifecycle(GJS_DEBUG_CONTEXT, "End garbage collection");
            break;
        default:
            g_assert_not_reached();
    }
}

void GjsContextPrivate::exit(uint8_t exit_code) {
    g_assert(!m_should_exit);
    m_should_exit = true;
    m_exit_code = exit_code;
}

bool GjsContextPrivate::should_exit(uint8_t* exit_code_p) const {
    if (exit_code_p != NULL)
        *exit_code_p = m_exit_code;
    return m_should_exit;
}

void GjsContextPrivate::exit_immediately(uint8_t exit_code) {
    warn_about_unhandled_promise_rejections();

    ::exit(exit_code);
}

void GjsContextPrivate::start_draining_job_queue(void) { m_dispatcher.start(); }

void GjsContextPrivate::stop_draining_job_queue(void) {
    m_draining_job_queue = false;
    m_dispatcher.stop();
}

bool GjsContextPrivate::getHostDefinedData(JSContext* cx,
                                           JS::MutableHandleObject data) const {
    // This is equivalent to SpiderMonkey's behavior.
    data.set(JS::CurrentGlobalOrNull(cx));
    return true;
}

// See engine.cpp and JS::SetJobQueue().
bool GjsContextPrivate::enqueuePromiseJob(JSContext* cx [[maybe_unused]],
                                          JS::HandleObject promise,
                                          JS::HandleObject job,
                                          JS::HandleObject allocation_site,
                                          JS::HandleObject incumbent_global
                                          [[maybe_unused]]) {
    g_assert(cx == m_cx);
    g_assert(from_cx(cx) == this);

    gjs_debug(GJS_DEBUG_MAINLOOP,
              "Enqueue job %s, promise=%s, allocation site=%s",
              gjs_debug_object(job).c_str(), gjs_debug_object(promise).c_str(),
              gjs_debug_object(allocation_site).c_str());

    if (!m_job_queue.append(job)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    JS::JobQueueMayNotBeEmpty(m_cx);
    m_dispatcher.start();
    return true;
}

// Override of JobQueue::runJobs(). Called by js::RunJobs(), and when execution
// of the job queue was interrupted by the debugger and is resuming.
void GjsContextPrivate::runJobs(JSContext* cx) {
    g_assert(cx == m_cx);
    g_assert(from_cx(cx) == this);
    if (!run_jobs_fallible())
        gjs_log_exception(cx);
}

/*
 * GjsContext::run_jobs_fallible:
 *
 * Drains the queue of promise callbacks that the JS engine has reported
 * finished, calling each one and logging any exceptions that it throws.
 *
 * Adapted from js::RunJobs() in SpiderMonkey's default job queue
 * implementation.
 *
 * Returns: false if one of the jobs threw an uncatchable exception;
 * otherwise true.
 */
bool GjsContextPrivate::run_jobs_fallible() {
    bool retval = true;

    if (m_draining_job_queue || m_should_exit)
        return true;

    m_draining_job_queue = true;  // Ignore reentrant calls

    JS::RootedObject job(m_cx);
    JS::HandleValueArray args(JS::HandleValueArray::empty());
    JS::RootedValue rval(m_cx);

    if (m_job_queue.length() == 0) {
        // Check FinalizationRegistry cleanup tasks at least once if there are
        // no microtasks queued. This may enqueue more microtasks, which will be
        // appended to m_job_queue.
        if (!run_finalization_registry_cleanup())
            retval = false;
    }

    /* Execute jobs in a loop until we've reached the end of the queue.
     * Since executing a job can trigger enqueueing of additional jobs,
     * it's crucial to recheck the queue length during each iteration. */
    for (size_t ix = 0; ix < m_job_queue.length(); ix++) {
        /* A previous job might have set this flag. e.g., System.exit(). */
        if (m_should_exit || !m_dispatcher.is_running()) {
            gjs_debug(GJS_DEBUG_MAINLOOP, "Stopping jobs because of %s",
                      m_should_exit ? "exit" : "main loop cancel");
            break;
        }

        job = m_job_queue[ix];

        /* It's possible that job draining was interrupted prematurely,
         * leaving the queue partly processed. In that case, slots for
         * already-executed entries will contain nullptrs, which we should
         * just skip. */
        if (!job)
            continue;

        m_job_queue[ix] = nullptr;
        {
            JSAutoRealm ar(m_cx, job);
            gjs_debug(GJS_DEBUG_MAINLOOP, "handling job %zu, %s", ix,
                      gjs_debug_object(job).c_str());
            if (!JS::Call(m_cx, JS::UndefinedHandleValue, job, args, &rval)) {
                /* Uncatchable exception - return false so that
                 * System.exit() works in the interactive shell and when
                 * exiting the interpreter. */
                if (!JS_IsExceptionPending(m_cx)) {
                    /* System.exit() is an uncatchable exception, but does not
                     * indicate a bug. Log everything else. */
                    if (!should_exit(nullptr))
                        g_critical("Promise callback terminated with uncatchable exception");
                    retval = false;
                    continue;
                }

                /* There's nowhere for the exception to go at this point */
                gjs_log_exception_uncaught(m_cx);
            }
        }
        gjs_debug(GJS_DEBUG_MAINLOOP, "Completed job %zu", ix);

        // Run FinalizationRegistry cleanup tasks after each job. Cleanup tasks
        // may enqueue more microtasks, which will be appended to m_job_queue.
        if (!run_finalization_registry_cleanup())
            retval = false;
    }

    m_draining_job_queue = false;
    m_job_queue.clear();
    warn_about_unhandled_promise_rejections();
    JS::JobQueueIsEmpty(m_cx);
    return retval;
}

bool GjsContextPrivate::run_finalization_registry_cleanup() {
    bool retval = true;

    JS::Rooted<FunctionVector> tasks{m_cx};
    std::swap(tasks.get(), m_cleanup_tasks);
    g_assert(m_cleanup_tasks.empty());

    JS::RootedFunction task{m_cx};
    JS::RootedValue unused_rval{m_cx};
    for (JSFunction* func : tasks) {
        gjs_debug(GJS_DEBUG_MAINLOOP,
                  "Running FinalizationRegistry cleanup callback");

        task.set(func);
        JS::ExposeObjectToActiveJS(JS_GetFunctionObject(func));

        JSAutoRealm ar{m_cx, JS_GetFunctionObject(func)};
        if (!JS_CallFunction(m_cx, nullptr, task, JS::HandleValueArray::empty(),
                             &unused_rval)) {
            // Same logic as above
            if (!JS_IsExceptionPending(m_cx)) {
                if (!should_exit(nullptr))
                    g_critical(
                        "FinalizationRegistry callback terminated with "
                        "uncatchable exception");
                retval = false;
                continue;
            }
            gjs_log_exception_uncaught(m_cx);
        }

        gjs_debug(GJS_DEBUG_MAINLOOP,
                  "Completed FinalizationRegistry cleanup callback");
    }

    return retval;
}

class GjsContextPrivate::SavedQueue : public JS::JobQueue::SavedJobQueue {
 private:
    GjsContextPrivate* m_gjs;
    JS::PersistentRooted<JobQueueStorage> m_queue;
    bool m_was_draining : 1;

 public:
    explicit SavedQueue(GjsContextPrivate* gjs)
        : m_gjs(gjs),
          m_queue(gjs->m_cx, std::move(gjs->m_job_queue)),
          m_was_draining(gjs->m_draining_job_queue) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Pausing job queue");
        gjs->stop_draining_job_queue();
    }

    ~SavedQueue(void) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Unpausing job queue");
        m_gjs->m_job_queue = std::move(m_queue.get());
        m_gjs->m_draining_job_queue = m_was_draining;
        m_gjs->start_draining_job_queue();
    }
};

js::UniquePtr<JS::JobQueue::SavedJobQueue> GjsContextPrivate::saveJobQueue(
    JSContext* cx) {
    g_assert(cx == m_cx);
    g_assert(from_cx(cx) == this);

    auto saved_queue = js::MakeUnique<SavedQueue>(this);
    if (!saved_queue) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }

    g_assert(m_job_queue.empty());
    return saved_queue;
}

void GjsContextPrivate::register_unhandled_promise_rejection(
    uint64_t id, JS::UniqueChars&& stack) {
    m_unhandled_rejection_stacks[id] = std::move(stack);
}

void GjsContextPrivate::unregister_unhandled_promise_rejection(uint64_t id) {
    size_t erased = m_unhandled_rejection_stacks.erase(id);
    if (erased != 1) {
        g_critical("Promise %" G_GUINT64_FORMAT
                   " Handler attached to rejected promise that wasn't "
                   "previously marked as unhandled or that we wrongly reported "
                   "as unhandled",
                   id);
    }
}

bool GjsContextPrivate::queue_finalization_registry_cleanup(
    JSFunction* cleanup_task) {
    return m_cleanup_tasks.append(cleanup_task);
}

void GjsContextPrivate::async_closure_enqueue_for_gc(Gjs::Closure* trampoline) {
    //  Because we can't free the mmap'd data for a callback
    //  while it's in use, this list keeps track of ones that
    //  will be freed the next time gc happens
    g_assert(!trampoline->context() || trampoline->context() == m_cx);
    m_async_closures.emplace_back(trampoline);
}

/**
 * gjs_context_maybe_gc:
 * @context: a #GjsContext
 *
 * Similar to the Spidermonkey JS_MaybeGC() call which
 * heuristically looks at JS runtime memory usage and
 * may initiate a garbage collection.
 *
 * This function always unconditionally invokes JS_MaybeGC(), but
 * additionally looks at memory usage from the system malloc()
 * when available, and if the delta has grown since the last run
 * significantly, also initiates a full JavaScript garbage
 * collection.  The idea is that since GJS is a bridge between
 * JavaScript and system libraries, and JS objects act as proxies
 * for these system memory objects, GJS consumers need a way to
 * hint to the runtime that it may be a good idea to try a
 * collection.
 *
 * A good time to call this function is when your application
 * transitions to an idle state.
 */
void
gjs_context_maybe_gc (GjsContext  *context)
{
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(context);
    gjs_maybe_gc(gjs->context());
}

/**
 * gjs_context_gc:
 * @context: a #GjsContext
 *
 * Initiate a full GC; may or may not block until complete.  This
 * function just calls Spidermonkey JS_GC().
 */
void
gjs_context_gc (GjsContext  *context)
{
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(context);
    JS_GC(gjs->context(), Gjs::GCReason::GJS_API_CALL);
}

/**
 * gjs_context_get_all:
 *
 * Returns a newly-allocated list containing all known instances of #GjsContext.
 * This is useful for operating on the contexts from a process-global situation
 * such as a debugger.
 *
 * Return value: (element-type GjsContext) (transfer full): Known #GjsContext instances
 */
GList*
gjs_context_get_all(void)
{
  GList *result;
  GList *iter;
  g_mutex_lock (&contexts_lock);
  result = g_list_copy(all_contexts);
  for (iter = result; iter; iter = iter->next)
    g_object_ref((GObject*)iter->data);
  g_mutex_unlock (&contexts_lock);
  return result;
}

/**
 * gjs_context_get_native_context:
 *
 * Returns a pointer to the underlying native context.  For SpiderMonkey, this
 * is a JSContext *
 */
void*
gjs_context_get_native_context (GjsContext *js_context)
{
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), NULL);
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->context();
}

static inline bool result_to_c(GErrorResult<> result, GError** error_out) {
    if (result.isOk())
        return true;
    *error_out = result.unwrapErr().release();
    return false;
}

bool
gjs_context_eval(GjsContext   *js_context,
                 const char   *script,
                 gssize        script_len,
                 const char   *filename,
                 int          *exit_status_p,
                 GError      **error)
{
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    size_t real_len = script_len < 0 ? strlen(script) : script_len;

    Gjs::AutoUnref<GjsContext> js_context_ref{js_context, Gjs::TakeOwnership{}};
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);

    gjs->register_non_module_sourcemap(script, filename);
    return result_to_c(gjs->eval(script, real_len, filename, exit_status_p),
                       error);
}

bool gjs_context_eval_module(GjsContext* js_context, const char* identifier,
                             uint8_t* exit_code, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    Gjs::AutoUnref<GjsContext> js_context_ref{js_context, Gjs::TakeOwnership{}};

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return result_to_c(gjs->eval_module(identifier, exit_code), error);
}

bool gjs_context_register_module(GjsContext* js_context, const char* identifier,
                                 const char* uri, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);

    return result_to_c(gjs->register_module(identifier, uri), error);
}

bool GjsContextPrivate::auto_profile_enter() {
    bool auto_profile = m_should_profile;
    if (auto_profile &&
        (_gjs_profiler_is_running(m_profiler) || m_should_listen_sigusr2))
        auto_profile = false;

    Gjs::AutoMainRealm ar{this};

    if (auto_profile)
        gjs_profiler_start(m_profiler);

    return auto_profile;
}

void GjsContextPrivate::auto_profile_exit(bool auto_profile) {
    if (auto_profile)
        gjs_profiler_stop(m_profiler);
}

GErrorResult<> GjsContextPrivate::handle_exit_code(bool no_sync_error_pending,
                                                   const char* source_type,
                                                   const char* identifier,
                                                   uint8_t* exit_code) {
    uint8_t code;
    if (should_exit(&code)) {
        /* exit_status_p is public API so can't be changed, but should be
         * uint8_t, not int */
        Gjs::AutoError error;
        g_set_error(error.out(), GJS_ERROR, GJS_ERROR_SYSTEM_EXIT,
                    "Exit with code %d", code);

        *exit_code = code;
        return Err(error.release());  // Don't log anything
    }

    // Once the main loop exits an exception could
    // be pending even if the script returned
    // true synchronously
    if (JS_IsExceptionPending(m_cx)) {
        Gjs::AutoError error;
        g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                    "%s %s threw an exception", source_type, identifier);
        gjs_log_exception_uncaught(m_cx);

        *exit_code = 1;
        return Err(error.release());
    }

    if (m_unhandled_exception) {
        Gjs::AutoError error;
        g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                    "%s %s threw an exception", source_type, identifier);
        *exit_code = 1;
        return Err(error.release());
    }

    // Assume success if no error was thrown and should exit isn't
    // set
    if (no_sync_error_pending) {
        *exit_code = 0;
        return Ok{};
    }

    g_critical("%s %s terminated with an uncatchable exception", source_type,
               identifier);
    Gjs::AutoError error;
    g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                "%s %s terminated with an uncatchable exception", source_type,
                identifier);

    gjs_log_exception_uncaught(m_cx);
    /* No exit code from script, but we don't want to exit(0) */
    *exit_code = 1;
    return Err(error.release());
}

bool GjsContextPrivate::set_main_loop_hook(JSObject* callable) {
    g_assert(JS::IsCallable(callable) && "main loop hook must be a callable object");

    if (!callable) {
        m_main_loop_hook = nullptr;
        return true;
    }

    if (m_main_loop_hook)
        return false;

    m_main_loop_hook = callable;
    return true;
}

bool GjsContextPrivate::run_main_loop_hook() {
    JS::RootedObject hook(m_cx, m_main_loop_hook.get());
    m_main_loop_hook = nullptr;
    gjs_debug(GJS_DEBUG_MAINLOOP, "Running and clearing main loop hook");
    JS::RootedValue ignored_rval(m_cx);
    return JS::Call(m_cx, JS::NullHandleValue, hook,
                    JS::HandleValueArray::empty(), &ignored_rval);
}

// source maps parsing is built upon hooks for resolving or loading modules
// for non-module runs we need to invoke this logic manually
void GjsContextPrivate::register_non_module_sourcemap(const char* script,
                                                      const char* filename) {
    using AutoURI = Gjs::AutoPointer<GUri, GUri, g_uri_unref>;
    Gjs::AutoMainRealm ar{this};

    JS::RootedObject global{m_cx, JS::CurrentGlobalOrNull(m_cx)};
    JS::RootedValue v_loader{
        m_cx, gjs_get_global_slot(global, GjsGlobalSlot::MODULE_LOADER)};
    g_assert(v_loader.isObject());
    JS::RootedObject v_loader_obj{m_cx, &v_loader.toObject()};

    JS::RootedValueArray<3> args{m_cx};
    JS::RootedString script_str{m_cx, JS_NewStringCopyZ(m_cx, script)};
    JS::RootedString file_name_str{m_cx, JS_NewStringCopyZ(m_cx, filename)};
    args[0].setString(script_str);
    args[1].setString(file_name_str);

    // if the file uri is not an absolute uri with a scheme, build one assuming
    // file:// this parameter is used to help locate non-inlined source map file
    AutoURI uri = g_uri_parse(filename, G_URI_FLAGS_NONE, nullptr);
    if (!uri) {
        Gjs::AutoUnref<GFile> file = g_file_new_for_path(filename);
        Gjs::AutoChar file_uri = g_file_get_uri(file);
        JS::RootedString abs_filename_scheme_str{
            m_cx, JS_NewStringCopyZ(m_cx, file_uri.get())};
        args[2].setString(abs_filename_scheme_str);
    }

    JS::RootedValue ignored{m_cx};
    JS::Call(m_cx, v_loader_obj, "populateSourceMap", args, &ignored);
}

GErrorResult<> GjsContextPrivate::eval(const char* script, size_t script_len,
                                       const char* filename,
                                       int* exit_status_p) {
    AutoResetExit reset(this);

    bool auto_profile = auto_profile_enter();

    Gjs::AutoMainRealm ar{this};

    JS::RootedValue retval(m_cx);
    bool ok = eval_with_scope(nullptr, script, script_len, filename, &retval);

    // If there are no errors and the mainloop hook is set, call it.
    if (ok && m_main_loop_hook)
        ok = run_main_loop_hook();

    bool exiting = false;

    // Spin the internal loop until the main loop hook is set or no holds
    // remain.
    // If the main loop returns false we cannot guarantee the state of our
    // promise queue (a module promise could be pending) so instead of draining
    // draining the queue we instead just exit.
    if (ok && !m_main_loop.spin(this)) {
        exiting = true;
    }

    // If the hook has been set again, enter a loop until an error is
    // encountered or the main loop is quit.
    while (ok && !exiting && m_main_loop_hook) {
        ok = run_main_loop_hook();

        // Additional jobs could have been enqueued from the
        // main loop hook
        if (ok && !m_main_loop.spin(this)) {
            exiting = true;
        }
    }

    // The promise job queue should be drained even on error, to finish
    // outstanding async tasks before the context is torn down. Drain after
    // uncaught exceptions have been reported since draining runs callbacks.
    // We do not drain if we are exiting.
    if (!ok && !exiting) {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs_fallible() && ok;
    }

    auto_profile_exit(auto_profile);

    uint8_t out_code;
    GErrorResult<> result = handle_exit_code(ok, "Script", filename, &out_code);

    if (exit_status_p) {
        if (result.isOk() && retval.isInt32()) {
            int code = retval.toInt32();
            gjs_debug(GJS_DEBUG_CONTEXT,
                      "Script returned integer code %d", code);
            *exit_status_p = code;
        } else {
            *exit_status_p = out_code;
        }
    }

    return result;
}

GErrorResult<> GjsContextPrivate::eval_module(const char* identifier,
                                              uint8_t* exit_status_p) {
    AutoResetExit reset(this);

    bool auto_profile = auto_profile_enter();

    Gjs::AutoMainRealm ar{this};

    JS::RootedObject registry(m_cx, gjs_get_module_registry(m_global));
    JS::RootedId key(m_cx, gjs_intern_string_to_id(m_cx, identifier));
    JS::RootedObject obj(m_cx);
    if (!gjs_global_registry_get(m_cx, registry, key, &obj) || !obj) {
        Gjs::AutoError error;
        g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                    "Cannot load module with identifier: '%s'", identifier);

        if (exit_status_p)
            *exit_status_p = 1;
        return Err(error.release());
    }

    if (!JS::ModuleLink(m_cx, obj)) {
        gjs_log_exception(m_cx);
        Gjs::AutoError error;
        g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                    "Failed to resolve imports for module: '%s'", identifier);

        if (exit_status_p)
            *exit_status_p = 1;
        return Err(error.release());
    }

    JS::RootedValue evaluation_promise(m_cx);
    bool ok = JS::ModuleEvaluate(m_cx, obj, &evaluation_promise);

    if (ok) {
        GjsContextPrivate::from_cx(m_cx)->main_loop_hold();

        ok = add_promise_reactions(
            m_cx, evaluation_promise, on_context_module_resolved,
            on_context_module_rejected_log_exception, identifier);
    }

    bool exiting = false;

    do {
        // If there are no errors and the mainloop hook is set, call it.
        if (ok && m_main_loop_hook)
            ok = run_main_loop_hook();

        // Spin the internal loop until the main loop hook is set or no holds
        // remain.
        //
        // If the main loop returns false we cannot guarantee the state of our
        // promise queue (a module promise could be pending) so instead of
        // draining the queue we instead just exit.
        //
        // Additional jobs could have been enqueued from the
        // main loop hook
        if (ok && !m_main_loop.spin(this)) {
            exiting = true;
        }
    } while (ok && !exiting && m_main_loop_hook);

    // The promise job queue should be drained even on error, to finish
    // outstanding async tasks before the context is torn down. Drain after
    // uncaught exceptions have been reported since draining runs callbacks.
    // We do not drain if we are exiting.
    if (!ok && !exiting) {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs_fallible() && ok;
    }

    auto_profile_exit(auto_profile);

    uint8_t out_code;
    GErrorResult<> result =
        handle_exit_code(ok, "Module", identifier, &out_code);
    if (exit_status_p)
        *exit_status_p = out_code;

    return result;
}

GErrorResult<> GjsContextPrivate::register_module(const char* identifier,
                                                  const char* uri) {
    Gjs::AutoMainRealm ar{this};

    if (gjs_module_load(m_cx, identifier, uri))
        return Ok{};

    const char* msg = "unknown";
    JS::ExceptionStack exn_stack(m_cx);
    JS::ErrorReportBuilder builder(m_cx);
    if (JS::StealPendingExceptionStack(m_cx, &exn_stack) &&
        builder.init(m_cx, exn_stack,
                     JS::ErrorReportBuilder::WithSideEffects)) {
        msg = builder.toStringResult().c_str();
    } else {
        JS_ClearPendingException(m_cx);
    }

    Gjs::AutoError error;
    g_set_error(error.out(), GJS_ERROR, GJS_ERROR_FAILED,
                "Failed to parse module '%s': %s", identifier,
                msg ? msg : "unknown");

    return Err(error.release());
}

bool
gjs_context_eval_file(GjsContext    *js_context,
                      const char    *filename,
                      int           *exit_status_p,
                      GError       **error)
{
    Gjs::AutoChar script;
    size_t script_len;
    Gjs::AutoUnref<GFile> file{g_file_new_for_commandline_arg(filename)};

    if (!g_file_load_contents(file, nullptr, script.out(), &script_len, nullptr,
                              error))
        return false;

    return gjs_context_eval(js_context, script, script_len, filename,
                            exit_status_p, error);
}

bool gjs_context_eval_module_file(GjsContext* js_context, const char* filename,
                                  uint8_t* exit_status_p, GError** error) {
    Gjs::AutoUnref<GFile> file{g_file_new_for_commandline_arg(filename)};
    Gjs::AutoChar uri{g_file_get_uri(file)};

    return gjs_context_register_module(js_context, uri, uri, error) &&
           gjs_context_eval_module(js_context, uri, exit_status_p, error);
}

/*
 * GjsContextPrivate::eval_with_scope:
 * @scope_object: an object to use as the global scope, or nullptr
 * @source: JavaScript program encoded in UTF-8
 * @source_len: length of @source, or -1 if @source is 0-terminated
 * @filename: filename to use as the origin of @source
 * @retval: location for the return value of @source
 *
 * Executes @source with a local scope so that nothing from the source code
 * leaks out into the global scope.
 * If @scope_object is given, then everything that @source placed in the global
 * namespace is defined on @scope_object.
 * Otherwise, the global definitions are just discarded.
 */
bool GjsContextPrivate::eval_with_scope(JS::HandleObject scope_object,
                                        const char* source, size_t source_len,
                                        const char* filename,
                                        JS::MutableHandleValue retval) {
    /* log and clear exception if it's set (should not be, normally...) */
    if (JS_IsExceptionPending(m_cx)) {
        g_warning("eval_with_scope() called with a pending exception");
        return false;
    }

    JS::RootedObject eval_obj(m_cx, scope_object);
    if (!eval_obj)
        eval_obj = JS_NewPlainObject(m_cx);

    JS::SourceText<mozilla::Utf8Unit> buf;
    if (!buf.init(m_cx, source, source_len, JS::SourceOwnership::Borrowed))
        return false;

    JS::EnvironmentChain scope_chain{m_cx, JS::SupportUnscopables::No};
    if (!scope_chain.append(eval_obj)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    JS::CompileOptions options(m_cx);
    options.setFileAndLine(filename, 1).setNonSyntacticScope(true);

    Gjs::AutoUnref<GFile> file{g_file_new_for_commandline_arg(filename)};
    Gjs::AutoChar uri{g_file_get_uri(file)};
    JS::RootedObject priv(m_cx, gjs_script_module_build_private(m_cx, uri));
    if (!priv)
        return false;

    JS::RootedScript script(m_cx);
    script.set(JS::Compile(m_cx, options, buf));
    if (!script)
        return false;

    JS::SetScriptPrivate(script, JS::ObjectValue(*priv));
    if (!JS_ExecuteScript(m_cx, scope_chain, script, retval))
        return false;

    schedule_gc_if_needed();

    if (JS_IsExceptionPending(m_cx)) {
        g_warning(
            "JS::Evaluate() returned true but exception was pending; "
            "did somebody call gjs_throw() without returning false?");
        return false;
    }

    gjs_debug(GJS_DEBUG_CONTEXT, "Script evaluation succeeded");

    return true;
}

/*
 * GjsContextPrivate::call_function:
 * @this_obj: Object to use as the 'this' for the function call
 * @func_val: Callable to call, as a JS value
 * @args: Arguments to pass to the callable
 * @rval: Location for the return value
 *
 * Use this instead of JS_CallFunctionValue(), because it schedules a GC if
 * one is needed. It's good practice to check if a GC should be run every time
 * we return from JS back into C++.
 */
bool GjsContextPrivate::call_function(JS::HandleObject this_obj,
                                      JS::HandleValue func_val,
                                      const JS::HandleValueArray& args,
                                      JS::MutableHandleValue rval) {
    if (!JS_CallFunctionValue(m_cx, this_obj, func_val, args, rval))
        return false;

    schedule_gc_if_needed();

    return true;
}

bool
gjs_context_define_string_array(GjsContext  *js_context,
                                const char    *array_name,
                                gssize         array_length,
                                const char   **array_values,
                                GError       **error)
{
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);

    Gjs::AutoMainRealm ar{gjs};

    std::vector<std::string> strings;
    if (array_values) {
        if (array_length < 0)
            array_length = g_strv_length(const_cast<char**>(array_values));
        strings = {array_values, array_values + array_length};
    }

    // ARGV is a special case to preserve backwards compatibility.
    if (strcmp(array_name, "ARGV") == 0) {
        gjs->set_args(std::move(strings));

        return true;
    }

    JS::RootedObject global_root(gjs->context(), gjs->global());
    if (!gjs_define_string_array(gjs->context(), global_root, array_name,
                                 strings, JSPROP_READONLY | JSPROP_PERMANENT)) {
        gjs_log_exception(gjs->context());
        g_set_error(error,
                    GJS_ERROR,
                    GJS_ERROR_FAILED,
                    "gjs_define_string_array() failed");
        return false;
    }

    return true;
}

void gjs_context_set_argv(GjsContext* js_context, ssize_t array_length,
                          const char** array_values) {
    g_return_if_fail(GJS_IS_CONTEXT(js_context));
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    std::vector<std::string> args(array_values, array_values + array_length);
    gjs->set_args(std::move(args));
}

static GjsContext *current_context;

GjsContext *
gjs_context_get_current (void)
{
    return current_context;
}

void
gjs_context_make_current (GjsContext *context)
{
    g_assert (context == NULL || current_context == NULL);

    current_context = context;
}

namespace Gjs {
/*
 * Gjs::AutoMainRealm:
 * @gjs: a #GjsContextPrivate
 *
 * Enters the realm of the "main global" for the context, and leaves when the
 * object is destructed at the end of the scope. The main global is the global
 * object under which all user JS code is executed. It is used as the root
 * object for the scope of modules loaded by GJS in this context.
 *
 * Only code in a different realm, such as the debugger code or the module
 * loader code, uses a different global object.
 */
AutoMainRealm::AutoMainRealm(GjsContextPrivate* gjs)
    : JSAutoRealm(gjs->context(), gjs->global()) {}

AutoMainRealm::AutoMainRealm(JSContext* cx)
    : AutoMainRealm(static_cast<GjsContextPrivate*>(JS_GetContextPrivate(cx))) {
}

/*
 * Gjs::AutoInternalRealm:
 * @gjs: a #GjsContextPrivate
 *
 * Enters the realm of the "internal global" for the context, and leaves when
 * the object is destructed at the end of the scope. The internal global is only
 * used for executing the module loader code, to keep it separate from user
 * code.
 */
AutoInternalRealm::AutoInternalRealm(GjsContextPrivate* gjs)
    : JSAutoRealm(gjs->context(), gjs->internal_global()) {}

AutoInternalRealm::AutoInternalRealm(JSContext* cx)
    : AutoInternalRealm(
          static_cast<GjsContextPrivate*>(JS_GetContextPrivate(cx))) {}

}  // namespace Gjs

/**
 * gjs_context_get_profiler:
 * @self: the #GjsContext
 *
 * Returns the profiler's internal instance of #GjsProfiler for you to
 * customize, or %NULL if profiling is not enabled on this #GjsContext.
 *
 * Returns: (transfer none) (nullable): a #GjsProfiler
 */
GjsProfiler *
gjs_context_get_profiler(GjsContext *self)
{
    return GjsContextPrivate::from_object(self)->profiler();
}

/**
 * gjs_get_js_version:
 *
 * Returns the underlying version of the JS engine.
 *
 * Returns: a string
 */
const char *
gjs_get_js_version(void)
{
    return JS_GetImplementationVersion();
}

/**
 * gjs_context_get_repl_history_path:
 *
 * Returns the path property for persisting REPL command history
 *
 * Returns: a string
 */
const char* gjs_context_get_repl_history_path(GjsContext* self) {
    return GjsContextPrivate::from_object(self)->repl_history_path();
}

/**
 * gjs_context_run_in_realm:
 * @self: the #GjsContext
 * @func: (scope call): function to run
 * @user_data: (closure func): pointer to pass to @func
 *
 * Runs @func immediately, not asynchronously, after entering the JS context's
 * main realm. After @func completes, leaves the realm again.
 *
 * You only need this if you are using JSAPI calls from the SpiderMonkey API
 * directly.
 */
void gjs_context_run_in_realm(GjsContext* self, GjsContextInRealmFunc func,
                              void* user_data) {
    g_return_if_fail(GJS_IS_CONTEXT(self));
    g_return_if_fail(func);

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(self);
    Gjs::AutoMainRealm ar{gjs};
    func(self, user_data);
}
