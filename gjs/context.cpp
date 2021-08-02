/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <signal.h>  // for sigaction, SIGUSR1, sa_handler
#include <stdint.h>
#include <stdio.h>      // for FILE, fclose, size_t
#include <string.h>     // for memset

#ifdef HAVE_UNISTD_H
#    include <unistd.h>  // for getpid
#elif defined (_WIN32)
#    include <process.h>
#endif

#ifdef DEBUG
#    include <algorithm>  // for find
#endif
#include <atomic>
#include <new>
#include <string>       // for u16string
#include <thread>       // for get_id
#include <unordered_map>
#include <utility>  // for move
#include <vector>

#include <gio/gio.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <js/AllocPolicy.h>  // for SystemAllocPolicy
#include <js/CallArgs.h>     // for UndefinedHandleValue
#include <js/CharacterEncoding.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>           // for StealPendingExceptionStack
#include <js/GCAPI.h>               // for JS_GC, JS_AddExtraGCRootsTr...
#include <js/GCHashTable.h>         // for WeakCache
#include <js/GCVector.h>            // for RootedVector
#include <js/Id.h>
#include <js/Modules.h>
#include <js/Promise.h>             // for JobQueue::SavedJobQueue
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_RE...
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>
#include <js/UniquePtr.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>        // for JS_IsExceptionPending, ...
#include <jsfriendapi.h>  // for DumpHeap, IgnoreNurseryObjects
#include <mozilla/UniquePtr.h>

#include "gi/closure.h"  // for Closure::Ptr, Closure
#include "gi/object.h"
#include "gi/private.h"
#include "gi/repo.h"
#include "gi/utils-inl.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/engine.h"
#include "gjs/error-types.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/internal.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "gjs/objectbox.h"
#include "gjs/profiler-private.h"
#include "gjs/profiler.h"
#include "gjs/text-encoding.h"
#include "modules/modules.h"
#include "util/log.h"

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

struct _GjsContextClass {
    GObjectClass parent;
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
};

static GMutex contexts_lock;
static GList *all_contexts = NULL;

static GjsAutoChar dump_heap_output;
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
    GjsAutoChar filename = g_strdup_printf("%s.%jd.%u", dump_heap_output.get(),
                                           intmax_t(getpid()), counter);
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

    /* For GjsPrivate */
    if (!g_getenv("GJS_USE_UNINSTALLED_FILES")) {
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        char *basedir = g_win32_get_package_installation_directory_of_module (gjs_dll);
        char *priv_typelib_dir = g_build_filename (basedir, "lib", "gjs", "girepository-1.0", NULL);
        g_free (basedir);
#else
        char *priv_typelib_dir = g_build_filename (PKGLIBDIR, "girepository-1.0", NULL);
#endif
        g_irepository_prepend_search_path(priv_typelib_dir);
    g_free (priv_typelib_dir);
    }

    gjs_register_native_module("_byteArrayNative", gjs_define_byte_array_stuff);
    gjs_register_native_module("_encodingNative",
                               gjs_define_text_encoding_stuff);
    gjs_register_native_module("_gi", gjs_define_private_gi_stuff);
    gjs_register_native_module("gi", gjs_define_repo);

    gjs_register_static_modules();
}

void GjsContextPrivate::trace(JSTracer* trc, void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    JS::TraceEdge<JSObject*>(trc, &gjs->m_global, "GJS global object");
    JS::TraceEdge<JSObject*>(trc, &gjs->m_internal_global,
                             "GJS internal global object");
    gjs->m_atoms->trace(trc);
    gjs->m_job_queue.trace(trc);
    gjs->m_object_init_list.trace(trc);
}

void GjsContextPrivate::warn_about_unhandled_promise_rejections(void) {
    for (auto& kv : m_unhandled_rejection_stacks) {
        const char *stack = kv.second;
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
    m_destroy_notifications.push_back({notify_func, data});
}

void GjsContextPrivate::unregister_notifier(DestroyNotify notify_func,
                                            void* data) {
    auto target = std::make_pair(notify_func, data);
    Gjs::remove_one_from_unsorted_vector(&m_destroy_notifications, target);
}

void GjsContextPrivate::dispose(void) {
    if (m_cx) {
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
         * that we may not have the JS_GetPrivate() to access the
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

        gjs_debug(GJS_DEBUG_CONTEXT, "Disabling auto GC");
        if (m_auto_gc_id > 0) {
            g_source_remove(m_auto_gc_id);
            m_auto_gc_id = 0;
        }

        gjs_debug(GJS_DEBUG_CONTEXT, "Ending trace on global object");
        JS_RemoveExtraGCRootsTracer(m_cx, &GjsContextPrivate::trace, this);
        m_global = nullptr;
        m_internal_global = nullptr;

        gjs_debug(GJS_DEBUG_CONTEXT, "Freeing allocated resources");
        delete m_fundamental_table;
        delete m_gtype_table;
        delete m_atoms;

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
}

static void load_context_module(JSContext* cx, const char* uri,
                                const char* debug_identifier) {
    JS::RootedObject loader(cx, gjs_module_load(cx, uri, uri));

    if (!loader) {
        gjs_log_exception(cx);
        g_error("Failed to load %s module.", debug_identifier);
    }

    if (!JS::ModuleInstantiate(cx, loader)) {
        gjs_log_exception(cx);
        g_error("Failed to instantiate %s module.", debug_identifier);
    }

    if (!JS::ModuleEvaluate(cx, loader)) {
        gjs_log_exception(cx);
        g_error("Failed to evaluate %s module.", debug_identifier);
    }
}

GjsContextPrivate::GjsContextPrivate(JSContext* cx, GjsContext* public_context)
    : m_public_context(public_context),
      m_cx(cx),
      m_owner_thread(std::this_thread::get_id()),
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

    JSAutoRealm ar(m_cx, internal_global);

    m_internal_global = internal_global;
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
        JSAutoRealm ar(cx, global);

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
        JSAutoRealm ar(cx, global);
        load_context_module(
            cx, "resource:///org/gnome/gjs/modules/esm/_bootstrap/default.js",
            "ESM bootstrap");
    }
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
    int64_t now = 0;
    if (m_profiler)
        now = g_get_monotonic_time() * 1000L;

    switch (status) {
        case JSGC_BEGIN:
            m_gc_begin_time = now;
            m_gc_reason = gjs_explain_gc_reason(reason);
            gjs_debug_lifecycle(GJS_DEBUG_CONTEXT,
                                "Begin garbage collection because of %s",
                                m_gc_reason);

            // We finalize any pending toggle refs before doing any garbage
            // collection, so that we can collect the JS wrapper objects, and in
            // order to minimize the chances of objects having a pending toggle
            // up queued when they are garbage collected.
            gjs_object_clear_toggles();

            m_async_closures.clear();
            m_async_closures.shrink_to_fit();
            break;
        case JSGC_END:
            if (m_profiler && m_gc_begin_time != 0) {
                _gjs_profiler_add_mark(m_profiler, m_gc_begin_time,
                                       now - m_gc_begin_time, "GJS",
                                       "Garbage collection", m_gc_reason);
            }
            m_gc_begin_time = 0;
            m_gc_reason = nullptr;

            m_destroy_notifications.shrink_to_fit();
            gjs_debug_lifecycle(GJS_DEBUG_CONTEXT, "End garbage collection");
            break;
        default:
            g_assert_not_reached();
    }
}

void GjsContextPrivate::set_finalize_status(JSFinalizeStatus status) {
    // Implementation note for mozjs-24:
    //
    // Sweeping happens in two phases, in the first phase all GC things from the
    // allocation arenas are queued for sweeping, then the actual sweeping
    // happens. The first phase is marked by JSFINALIZE_GROUP_START, the second
    // one by JSFINALIZE_GROUP_END, and finally we will see
    // JSFINALIZE_COLLECTION_END at the end of all GC. (see jsgc.cpp,
    // BeginSweepPhase/BeginSweepingZoneGroup and SweepPhase, all called from
    // IncrementalCollectSlice).
    //
    // Incremental GC muddies the waters, because BeginSweepPhase is always run
    // to entirety, but SweepPhase can be run incrementally and mixed with JS
    // code runs or even native code, when MaybeGC/IncrementalGC return.
    //
    // Luckily for us, objects are treated specially, and are not really queued
    // for deferred incremental finalization (unless they are marked for
    // background sweeping). Instead, they are finalized immediately during
    // phase 1, so the following guarantees are true (and we rely on them):
    // - phase 1 of GC will begin and end in the same JSAPI call (i.e., our
    //   callback will be called with GROUP_START and the triggering JSAPI call
    //   will not return until we see a GROUP_END)
    // - object finalization will begin and end in the same JSAPI call
    // - therefore, if there is a finalizer frame somewhere in the stack,
    //   GjsContextPrivate::sweeping() will return true.
    //
    // Comments in mozjs-24 imply that this behavior might change in the future,
    // but it hasn't changed in mozilla-central as of 2014-02-23. In addition to
    // that, the mozilla-central version has a huge comment in a different
    // portion of the file, explaining why finalization of objects can't be
    // mixed with JS code, so we can probably rely on this behavior.

    int64_t now = 0;

    if (m_profiler)
        now = g_get_monotonic_time() * 1000L;

    switch (status) {
        case JSFINALIZE_GROUP_PREPARE:
            m_in_gc_sweep = true;
            m_sweep_begin_time = now;
            break;
        case JSFINALIZE_GROUP_START:
            m_group_sweep_begin_time = now;
            break;
        case JSFINALIZE_GROUP_END:
            if (m_profiler && m_group_sweep_begin_time != 0) {
                _gjs_profiler_add_mark(m_profiler, m_group_sweep_begin_time,
                                       now - m_group_sweep_begin_time, "GJS",
                                       "Group sweep", nullptr);
            }
            m_group_sweep_begin_time = 0;
            break;
        case JSFINALIZE_COLLECTION_END:
            m_in_gc_sweep = false;
            if (m_profiler && m_sweep_begin_time != 0) {
                _gjs_profiler_add_mark(m_profiler, m_sweep_begin_time,
                                       now - m_sweep_begin_time, "GJS", "Sweep",
                                       nullptr);
            }
            m_sweep_begin_time = 0;
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

void GjsContextPrivate::start_draining_job_queue(void) {
    if (!m_idle_drain_handler) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Starting promise job queue handler");
        m_idle_drain_handler = g_idle_add_full(
            G_PRIORITY_DEFAULT, drain_job_queue_idle_handler, this, nullptr);
    }
}

void GjsContextPrivate::stop_draining_job_queue(void) {
    m_draining_job_queue = false;
    if (m_idle_drain_handler) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Stopping promise job queue handler");
        g_source_remove(m_idle_drain_handler);
        m_idle_drain_handler = 0;
    }
}

gboolean GjsContextPrivate::drain_job_queue_idle_handler(void* data) {
    gjs_debug(GJS_DEBUG_CONTEXT, "Promise job queue handler");
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    gjs->runJobs(gjs->context());
    /* Uncatchable exceptions are swallowed here - no way to get a handle on
     * the main loop to exit it from this idle handler */
    gjs_debug(GJS_DEBUG_CONTEXT, "Promise job queue handler finished");
    g_assert(gjs->empty() && gjs->m_idle_drain_handler == 0 &&
             "GjsContextPrivate::runJobs() should have emptied queue");
    return G_SOURCE_REMOVE;
}

JSObject* GjsContextPrivate::getIncumbentGlobal(JSContext* cx) {
    // This is equivalent to SpiderMonkey's behavior.
    return JS::CurrentGlobalOrNull(cx);
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

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Enqueue job %s, promise=%s, allocation site=%s",
              gjs_debug_object(job).c_str(), gjs_debug_object(promise).c_str(),
              gjs_debug_object(allocation_site).c_str());

    if (m_idle_drain_handler)
        g_assert(!empty());
    else
        g_assert(empty());

    if (!m_job_queue.append(job)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    JS::JobQueueMayNotBeEmpty(m_cx);
    start_draining_job_queue();
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
bool GjsContextPrivate::run_jobs_fallible(void) {
    bool retval = true;

    if (m_draining_job_queue || m_should_exit)
        return true;

    m_draining_job_queue = true;  // Ignore reentrant calls

    JS::RootedObject job(m_cx);
    JS::HandleValueArray args(JS::HandleValueArray::empty());
    JS::RootedValue rval(m_cx);

    /* Execute jobs in a loop until we've reached the end of the queue.
     * Since executing a job can trigger enqueueing of additional jobs,
     * it's crucial to recheck the queue length during each iteration. */
    for (size_t ix = 0; ix < m_job_queue.length(); ix++) {
        /* A previous job might have set this flag. e.g., System.exit(). */
        if (m_should_exit)
            break;

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
            gjs_debug(GJS_DEBUG_CONTEXT, "handling job %s",
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
    }

    m_job_queue.clear();
    stop_draining_job_queue();
    JS::JobQueueIsEmpty(m_cx);
    return retval;
}

class GjsContextPrivate::SavedQueue : public JS::JobQueue::SavedJobQueue {
 private:
    GjsContextPrivate* m_gjs;
    JS::PersistentRooted<JobQueueStorage> m_queue;
    bool m_idle_was_pending : 1;
    bool m_was_draining : 1;

 public:
    explicit SavedQueue(GjsContextPrivate* gjs)
        : m_gjs(gjs),
          m_queue(gjs->m_cx, std::move(gjs->m_job_queue)),
          m_idle_was_pending(gjs->m_idle_drain_handler != 0),
          m_was_draining(gjs->m_draining_job_queue) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Pausing job queue");
        gjs->stop_draining_job_queue();
    }

    ~SavedQueue(void) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Unpausing job queue");
        m_gjs->m_job_queue = std::move(m_queue.get());
        m_gjs->m_draining_job_queue = m_was_draining;
        if (m_idle_was_pending)
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
    uint64_t id, GjsAutoChar&& stack) {
    m_unhandled_rejection_stacks[id] = std::move(stack);
}

void GjsContextPrivate::unregister_unhandled_promise_rejection(uint64_t id) {
    // Return value unused in G_DISABLE_ASSERT case
    [[maybe_unused]] size_t erased = m_unhandled_rejection_stacks.erase(id);
    g_assert(((void)"Handler attached to rejected promise that wasn't "
              "previously marked as unhandled", erased == 1));
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

bool
gjs_context_eval(GjsContext   *js_context,
                 const char   *script,
                 gssize        script_len,
                 const char   *filename,
                 int          *exit_status_p,
                 GError      **error)
{
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsAutoUnref<GjsContext> js_context_ref(js_context, GjsAutoTakeOwnership());

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->eval(script, script_len, filename, exit_status_p, error);
}

bool gjs_context_eval_module(GjsContext* js_context, const char* identifier,
                             uint8_t* exit_code, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsAutoUnref<GjsContext> js_context_ref(js_context, GjsAutoTakeOwnership());

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->eval_module(identifier, exit_code, error);
}

bool gjs_context_register_module(GjsContext* js_context, const char* identifier,
                                 const char* uri, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);

    return gjs->register_module(identifier, uri, error);
}

bool GjsContextPrivate::auto_profile_enter() {
    bool auto_profile = m_should_profile;
    if (auto_profile &&
        (_gjs_profiler_is_running(m_profiler) || m_should_listen_sigusr2))
        auto_profile = false;

    JSAutoRealm ar(m_cx, m_global);

    if (auto_profile)
        gjs_profiler_start(m_profiler);

    return auto_profile;
}

void GjsContextPrivate::auto_profile_exit(bool auto_profile) {
    if (auto_profile)
        gjs_profiler_stop(m_profiler);
}

uint8_t GjsContextPrivate::handle_exit_code(const char* type,
                                            const char* identifier,
                                            GError** error) {
    uint8_t code;
    if (should_exit(&code)) {
        /* exit_status_p is public API so can't be changed, but should be
         * uint8_t, not int */
        g_set_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT,
                    "Exit with code %d", code);
        return code;  // Don't log anything
    }
    if (!JS_IsExceptionPending(m_cx)) {
        g_critical("%s %s terminated with an uncatchable exception", type,
                   identifier);
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                    "%s %s terminated with an uncatchable exception", type,
                    identifier);
    } else {
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                    "%s %s threw an exception", type, identifier);
    }

    gjs_log_exception_uncaught(m_cx);
    /* No exit code from script, but we don't want to exit(0) */
    return 1;
}

bool GjsContextPrivate::eval(const char* script, ssize_t script_len,
                             const char* filename, int* exit_status_p,
                             GError** error) {
    AutoResetExit reset(this);

    bool auto_profile = auto_profile_enter();

    JSAutoRealm ar(m_cx, m_global);

    JS::RootedValue retval(m_cx);
    bool ok = eval_with_scope(nullptr, script, script_len, filename, &retval);

    /* The promise job queue should be drained even on error, to finish
     * outstanding async tasks before the context is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks. */
    {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs_fallible() && ok;
    }

    auto_profile_exit(auto_profile);

    if (!ok) {
        *exit_status_p = handle_exit_code("Script", filename, error);
        return false;
    }

    if (exit_status_p) {
        if (retval.isInt32()) {
            int code = retval.toInt32();
            gjs_debug(GJS_DEBUG_CONTEXT,
                      "Script returned integer code %d", code);
            *exit_status_p = code;
        } else {
            /* Assume success if no integer was returned */
            *exit_status_p = 0;
        }
    }

    return true;
}

bool GjsContextPrivate::eval_module(const char* identifier,
                                    uint8_t* exit_status_p, GError** error) {
    AutoResetExit reset(this);

    bool auto_profile = auto_profile_enter();

    JSAutoRealm ac(m_cx, m_global);

    JS::RootedObject registry(m_cx, gjs_get_module_registry(m_global));
    JS::RootedId key(m_cx, gjs_intern_string_to_id(m_cx, identifier));
    JS::RootedObject obj(m_cx);
    if (!gjs_global_registry_get(m_cx, registry, key, &obj) || !obj) {
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                    "Cannot load module with identifier: '%s'", identifier);
        *exit_status_p = 1;
        return false;
    }

    if (!JS::ModuleInstantiate(m_cx, obj)) {
        gjs_log_exception(m_cx);
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                    "Failed to resolve imports for module: '%s'", identifier);
        *exit_status_p = 1;
        return false;
    }

    bool ok = true;
    if (!JS::ModuleEvaluate(m_cx, obj))
        ok = false;

    /* The promise job queue should be drained even on error, to finish
     * outstanding async tasks before the context is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks.
     */
    {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs_fallible() && ok;
    }

    auto_profile_exit(auto_profile);

    if (!ok) {
        *exit_status_p = handle_exit_code("Module", identifier, error);
        return false;
    }

    /* Assume success if no errors were thrown or exit code set. */
    *exit_status_p = 0;
    return true;
}

bool GjsContextPrivate::register_module(const char* identifier, const char* uri,
                                        GError** error) {
    JSAutoRealm ar(m_cx, m_global);

    if (gjs_module_load(m_cx, identifier, uri))
        return true;

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

    g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                "Failed to parse module '%s': %s", identifier,
                msg ? msg : "unknown");

    return false;
}

bool
gjs_context_eval_file(GjsContext    *js_context,
                      const char    *filename,
                      int           *exit_status_p,
                      GError       **error)
{
    char *script;
    size_t script_len;
    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(filename);

    if (!g_file_load_contents(file, nullptr, &script, &script_len, nullptr,
                              error))
        return false;
    GjsAutoChar script_ref = script;

    return gjs_context_eval(js_context, script, script_len, filename,
                            exit_status_p, error);
}

bool gjs_context_eval_module_file(GjsContext* js_context, const char* filename,
                                  uint8_t* exit_status_p, GError** error) {
    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(filename);
    GjsAutoChar uri = g_file_get_uri(file);

    return gjs_context_register_module(js_context, uri, uri, error) &&
           gjs_context_eval_module(js_context, uri, exit_status_p, error);
}

/*
 * GjsContextPrivate::eval_with_scope:
 * @scope_object: an object to use as the global scope, or nullptr
 * @script: JavaScript program encoded in UTF-8
 * @script_len: length of @script, or -1 if @script is 0-terminated
 * @filename: filename to use as the origin of @script
 * @retval: location for the return value of @script
 *
 * Executes @script with a local scope so that nothing from the script leaks out
 * into the global scope.
 * If @scope_object is given, then everything that @script placed in the global
 * namespace is defined on @scope_object.
 * Otherwise, the global definitions are just discarded.
 */
bool GjsContextPrivate::eval_with_scope(JS::HandleObject scope_object,
                                        const char* script, ssize_t script_len,
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

    long items_written;  // NOLINT(runtime/int) - this type required by GLib API
    GError* error;
    GjsAutoChar16 utf16_string =
        g_utf8_to_utf16(script, script_len,
                        /* items_read = */ nullptr, &items_written, &error);
    if (!utf16_string)
        return gjs_throw_gerror_message(m_cx, error);

    // COMPAT: This could use JS::SourceText<mozilla::Utf8Unit> directly,
    // but that messes up code coverage. See bug
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1404784
    JS::SourceText<char16_t> buf;
    if (!buf.init(m_cx, reinterpret_cast<char16_t*>(utf16_string.get()),
                  items_written, JS::SourceOwnership::Borrowed))
        return false;

    JS::RootedObjectVector scope_chain(m_cx);
    if (!scope_chain.append(eval_obj)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    JS::CompileOptions options(m_cx);
    options.setFileAndLine(filename, 1);

    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(filename);
    GjsAutoChar uri = g_file_get_uri(file);
    JS::RootedObject priv(m_cx, gjs_script_module_build_private(m_cx, uri));
    if (!priv)
        return false;

    options.setPrivateValue(JS::ObjectValue(*priv));

    if (!JS::Evaluate(m_cx, scope_chain, options, buf, retval))
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

    JSAutoRealm ar(gjs->context(), gjs->global());

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
