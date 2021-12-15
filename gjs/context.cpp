/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include <config.h>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <unordered_map>

#include <gio/gio.h>

#include "context-private.h"
#include "engine.h"
#include "global.h"
#include "importer.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"
#include "mem.h"
#include "native.h"
#include "profiler-private.h"
#include "byteArray.h"
#include "gi/object.h"
#include "gi/repo.h"

#include <modules/modules.h>

#include <util/log.h>
#include <util/glib.h>
#include <util/error.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <string.h>

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

using JobQueue = JS::GCVector<JSObject *, 0, js::SystemAllocPolicy>;

struct _GjsContext {
    GObject parent;

    JSContext *context;
    JS::Heap<JSObject*> global;
    GThread *owner_thread;

    char *program_name;

    char **search_path;

    bool destroying;
    bool in_gc_sweep;

    bool should_exit;
    uint8_t exit_code;

    guint    auto_gc_id;
    bool     force_gc;

    std::array<JS::PersistentRootedId*, GJS_STRING_LAST> const_strings;

    JS::PersistentRooted<JobQueue> *job_queue;
    unsigned idle_drain_handler;
    bool draining_job_queue;

    std::unordered_map<uint64_t, GjsAutoChar> unhandled_rejection_stacks;

    GjsProfiler *profiler;
    bool should_profile : 1;
    bool should_listen_sigusr2 : 1;
};

/* Keep this consistent with GjsConstString */
static const char *const_strings[] = {
    "constructor", "prototype", "length",
    "imports", "__parentModule__", "__init__", "searchPath",
    "__gjsKeepAlive", "__gjsPrivateNS",
    "gi", "versions", "overrides",
    "_init", "_instance_init", "_new_internal", "new",
    "message", "code", "stack", "fileName", "lineNumber", "columnNumber",
    "name", "x", "y", "width", "height", "__modulePath__"
};

G_STATIC_ASSERT(G_N_ELEMENTS(const_strings) == GJS_STRING_LAST);

struct _GjsContextClass {
    GObjectClass parent;
};

/* Temporary workaround for https://bugzilla.gnome.org/show_bug.cgi?id=793175 */
#if __GNUC__ >= 8
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")
#endif
G_DEFINE_TYPE(GjsContext, gjs_context, G_TYPE_OBJECT);
#if __GNUC__ >= 8
_Pragma("GCC diagnostic pop")
#endif

enum {
    PROP_0,
    PROP_SEARCH_PATH,
    PROP_PROGRAM_NAME,
    PROP_PROFILER_ENABLED,
    PROP_PROFILER_SIGUSR2,
};

static GMutex contexts_lock;
static GList *all_contexts = NULL;

static GjsAutoChar dump_heap_output;
static unsigned dump_heap_idle_id = 0;

static GMetricsFile *metrics_file;
static GMetricsInstanceCounter *instance_counter = NULL;
static FILE *heap_fp;

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
        auto js_context = static_cast<GjsContext *>(l->data);
        js::DumpHeap(js_context->context, fp, js::IgnoreNurseryObjects);
    }

    fclose(fp);
}

static gboolean
dump_heap_idle(gpointer user_data)
{
    dump_heap_idle_id = 0;

    gjs_context_dump_heaps();

    return false;
}

static void
dump_heap_signal_handler(int signum)
{
    if (dump_heap_idle_id == 0)
        dump_heap_idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                                            dump_heap_idle, nullptr, nullptr);
}

static void
setup_dump_heap(void)
{
    static bool dump_heap_initialized = false;
    if (!dump_heap_initialized) {
        dump_heap_initialized = true;

        /* install signal handler only if environment variable is set */
        const char *heap_output = g_getenv("GJS_DEBUG_HEAP_OUTPUT");
        if (heap_output) {
            struct sigaction sa;

            dump_heap_output = g_strdup(heap_output);

            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = dump_heap_signal_handler;
            sigaction(SIGUSR1, &sa, nullptr);
        }
    }
}

static void
gjs_context_init(GjsContext *js_context)
{
    gjs_context_make_current(js_context);
}

static void
gjs_context_class_init(GjsContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

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

    /* For GjsPrivate */
    {
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        char *basedir = g_win32_get_package_installation_directory_of_module (gjs_dll);
        char *priv_typelib_dir = g_build_filename (basedir, "lib", "girepository-1.0", NULL);
        g_free (basedir);
#else
        char *priv_typelib_dir = g_build_filename (PKGLIBDIR, "girepository-1.0", NULL);
#endif
        g_irepository_prepend_search_path(priv_typelib_dir);
    g_free (priv_typelib_dir);
    }

    gjs_register_native_module("byteArray", gjs_define_byte_array_stuff);
    gjs_register_native_module("_gi", gjs_define_private_gi_stuff);
    gjs_register_native_module("gi", gjs_define_repo);

    gjs_register_static_modules();
}

static void
gjs_context_tracer(JSTracer *trc, void *data)
{
    GjsContext *gjs_context = reinterpret_cast<GjsContext *>(data);
    JS::TraceEdge<JSObject *>(trc, &gjs_context->global, "GJS global object");
}

static void
warn_about_unhandled_promise_rejections(GjsContext *gjs_context)
{
    for (auto& kv : gjs_context->unhandled_rejection_stacks) {
        const char *stack = kv.second;
        g_warning("Unhandled promise rejection. To suppress this warning, add "
                  "an error handler to your promise chain with .catch() or a "
                  "try-catch block around your await expression. %s%s",
                  stack ? "Stack trace of the failed promise:\n" :
                    "Unfortunately there is no stack trace of the failed promise.",
                  stack ? stack : "");
    }
    gjs_context->unhandled_rejection_stacks.clear();
}

static void
gjs_context_dispose(GObject *object)
{
    gjs_debug(GJS_DEBUG_CONTEXT, "JS shutdown sequence");

    GjsContext *js_context;

    js_context = GJS_CONTEXT(object);

    /* Profiler must be stopped and freed before context is shut down */
    gjs_debug(GJS_DEBUG_CONTEXT, "Stopping profiler");
    if (js_context->profiler)
        g_clear_pointer(&js_context->profiler, _gjs_profiler_free);

    /* Stop accepting entries in the toggle queue before running dispose
     * notifications, which causes all GjsMaybeOwned instances to unroot.
     * We don't want any objects to toggle down after that. */
    gjs_debug(GJS_DEBUG_CONTEXT, "Shutting down toggle queue");
    gjs_object_clear_toggles();
    gjs_object_shutdown_toggle_queue();

    /* Run dispose notifications next, so that anything releasing
     * references in response to this can still get garbage collected */
    gjs_debug(GJS_DEBUG_CONTEXT,
              "Notifying reference holders of GjsContext dispose");
    G_OBJECT_CLASS(gjs_context_parent_class)->dispose(object);

    if (js_context->context != NULL) {

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Checking unhandled promise rejections");
        warn_about_unhandled_promise_rejections(js_context);

        JS_BeginRequest(js_context->context);

        /* Do a full GC here before tearing down, since once we do
         * that we may not have the JS_GetPrivate() to access the
         * context
         */
        gjs_debug(GJS_DEBUG_CONTEXT, "Final triggered GC");
        JS_GC(js_context->context);
        JS_EndRequest(js_context->context);

        gjs_debug(GJS_DEBUG_CONTEXT, "Destroying JS context");
        js_context->destroying = true;

        /* Now, release all native objects, to avoid recursion between
         * the JS teardown and the C teardown.  The JSObject proxies
         * still exist, but point to NULL.
         */
        gjs_debug(GJS_DEBUG_CONTEXT, "Releasing all native objects");
        gjs_object_prepare_shutdown();

        gjs_debug(GJS_DEBUG_CONTEXT, "Disabling auto GC");
        if (js_context->auto_gc_id > 0) {
            g_source_remove (js_context->auto_gc_id);
            js_context->auto_gc_id = 0;
        }

        gjs_debug(GJS_DEBUG_CONTEXT, "Ending trace on global object");
        JS_RemoveExtraGCRootsTracer(js_context->context, gjs_context_tracer,
                                    js_context);
        js_context->global = NULL;

        gjs_debug(GJS_DEBUG_CONTEXT, "Unrooting atoms");
        for (auto& root : js_context->const_strings)
            delete root;

        gjs_debug(GJS_DEBUG_CONTEXT, "Freeing allocated resources");
        delete js_context->job_queue;

        /* Tear down JS */
        JS_DestroyContext(js_context->context);
        js_context->context = NULL;
        gjs_debug(GJS_DEBUG_CONTEXT, "JS context destroyed");
    }
}

static void
gjs_context_finalize(GObject *object)
{
    GjsContext *js_context;

    js_context = GJS_CONTEXT(object);

    if (js_context->search_path != NULL) {
        g_strfreev(js_context->search_path);
        js_context->search_path = NULL;
    }

    if (js_context->program_name != NULL) {
        g_free(js_context->program_name);
        js_context->program_name = NULL;
    }

    if (gjs_context_get_current() == (GjsContext*)object)
        gjs_context_make_current(NULL);

    g_mutex_lock(&contexts_lock);
    all_contexts = g_list_remove(all_contexts, object);
    g_mutex_unlock(&contexts_lock);

    js_context->global.~Heap();
    js_context->const_strings.~array();
    js_context->unhandled_rejection_stacks.~unordered_map();
    G_OBJECT_CLASS(gjs_context_parent_class)->finalize(object);
}

static gboolean
on_javascript_dump_timeout (void)
{
    FILE *fp;
    g_autofree char *filename = NULL;

    if (heap_fp != NULL) {
        return G_SOURCE_CONTINUE;
    }

    filename = g_build_filename (g_metrics_get_log_dir(),
                                 "javascript-heap",
                                 NULL);
    fp = fopen(filename, "w+");
    if (!fp) {
        return G_SOURCE_CONTINUE;
    }

    g_mutex_lock(&contexts_lock);
    for (GList *l = all_contexts; l; l = g_list_next(l)) {
        auto js_context = static_cast<GjsContext *>(l->data);

        JS::GCForReason(js_context->context, GC_SHRINK, JS::gcreason::Reason::API);
        JS_GC(js_context->context);
        js::DumpHeap(js_context->context, fp, js::IgnoreNurseryObjects);
    }
    g_mutex_unlock(&contexts_lock);

    heap_fp = fp;
    return G_SOURCE_CONTINUE;
}

static void
on_javascript_metrics_timeout (void)
{
    char *line = NULL;
    size_t line_size;

    if (heap_fp == NULL) {
        g_metrics_file_start_record(metrics_file);
        g_metrics_file_end_record(metrics_file);
        return;
    }

    if (instance_counter == NULL)
        instance_counter = g_metrics_instance_counter_new();

    g_metrics_instance_counter_start_record(instance_counter);
    rewind(heap_fp);
    while (getline(&line, &line_size, heap_fp) >= 0) {
        char *save_state = NULL, *ignored_field, *name;
        char *address;

        if (line_size <= 1)
            continue;

        if (line[0] == '#')
            continue;

        if (line[0] == '>')
            continue;

        ignored_field = strtok_r(line, " \n", &save_state);

        if (ignored_field == NULL)
            continue;

        ignored_field = strtok_r(NULL, " \n", &save_state);

        if (ignored_field == NULL)
            continue;

        name = strtok_r(NULL, "\n", &save_state);

        if (name == NULL)
            continue;

        address = strstr (name, " 0x");

        if (address != NULL)
          *address = '\0';

        g_metrics_instance_counter_add_instance (instance_counter, name, 1);
    }
    free (line);
    g_metrics_instance_counter_end_record(instance_counter);

    GMetricsInstanceCounterIter iter;
    GMetricsInstanceCounterMetrics *metrics;
    const char *name = NULL;

    g_metrics_file_start_record(metrics_file);
    g_metrics_instance_counter_iter_init (&iter, instance_counter);
    while (g_metrics_instance_counter_iter_next (&iter, &name, &metrics)) {
        g_metrics_file_add_row (metrics_file,
                                name,
                                metrics->instance_count,
                                metrics->instance_change > 0? "+" : "",
                                metrics->instance_change,
                                metrics->instance_watermark,
                                metrics->average_instance_change > 0? "+" : "",
                                metrics->average_instance_change);
    }
    g_metrics_file_end_record(metrics_file);

    fclose(heap_fp);
    heap_fp = NULL;
}

static void
gjs_context_constructed(GObject *object)
{
    GjsContext *js_context = GJS_CONTEXT(object);
    int i;

    G_OBJECT_CLASS(gjs_context_parent_class)->constructed(object);

    js_context->owner_thread = g_thread_self();

    JSContext *cx = gjs_create_js_context(js_context);
    if (!cx)
        g_error("Failed to create javascript context");
    js_context->context = cx;

    const char *env_profiler = g_getenv("GJS_ENABLE_PROFILER");
    if (env_profiler || js_context->should_listen_sigusr2)
        js_context->should_profile = true;

    if (js_context->should_profile) {
        js_context->profiler = _gjs_profiler_new(js_context);

        if (!js_context->profiler) {
            js_context->should_profile = false;
        } else {
            if (js_context->should_listen_sigusr2)
                _gjs_profiler_setup_signals(js_context->profiler, js_context);
        }
    }

    new (&js_context->unhandled_rejection_stacks) std::unordered_map<uint64_t, GjsAutoChar>;
    new (&js_context->const_strings) std::array<JS::PersistentRootedId*, GJS_STRING_LAST>;
    for (i = 0; i < GJS_STRING_LAST; i++) {
        js_context->const_strings[i] = new JS::PersistentRootedId(cx,
            gjs_intern_string_to_id(cx, const_strings[i]));
    }

    js_context->job_queue = new JS::PersistentRooted<JobQueue>(cx);
    if (!js_context->job_queue)
        g_error("Failed to initialize promise job queue");

    JS_BeginRequest(cx);

    JS::RootedObject global(cx, gjs_create_global_object(cx));
    if (!global) {
        gjs_log_exception(js_context->context);
        g_error("Failed to initialize global object");
    }

    JSAutoCompartment ac(cx, global);

    new (&js_context->global) JS::Heap<JSObject *>(global);
    JS_AddExtraGCRootsTracer(cx, gjs_context_tracer, js_context);

    JS::RootedObject importer(cx, gjs_create_root_importer(cx,
        js_context->search_path ? js_context->search_path : nullptr));
    if (!importer)
        g_error("Failed to create root importer");

    JS::Value v_importer = gjs_get_global_slot(cx, GJS_GLOBAL_SLOT_IMPORTS);
    g_assert(((void) "Someone else already created root importer",
              v_importer.isUndefined()));

    gjs_set_global_slot(cx, GJS_GLOBAL_SLOT_IMPORTS, JS::ObjectValue(*importer));

    if (!gjs_define_global_properties(cx, global, "default")) {
        gjs_log_exception(cx);
        g_error("Failed to define properties on global object");
    }

    /* Pre-import the byteArray module. We depend on this module for some of
     * our GObject introspection marshalling, so the ByteArray prototype
     * defined in it needs to be always available. */
    gjs_import_native_module(cx, importer, "byteArray");

    JS_EndRequest(cx);

    g_mutex_lock (&contexts_lock);
    all_contexts = g_list_prepend(all_contexts, object);

    if (metrics_file == NULL) {
        if (g_metrics_requested ("javascript")) {
            metrics_file = g_metrics_file_new ("javascript",
                                               "name", "%s",
                                               "count", "%ld",
                                               "change", "%s%ld",
                                               "max seen", "%lu",
                                               "average change", "%s%ld",
                                               NULL);

            g_metrics_start_timeout (on_javascript_metrics_timeout);
            g_timeout_add_seconds (60, (GSourceFunc) on_javascript_dump_timeout, NULL);
        }
    }
    g_mutex_unlock (&contexts_lock);

    setup_dump_heap();

    g_object_weak_ref(object, gjs_object_context_dispose_notify, nullptr);
}

static void
gjs_context_get_property (GObject     *object,
                          guint        prop_id,
                          GValue      *value,
                          GParamSpec  *pspec)
{
    GjsContext *js_context;

    js_context = GJS_CONTEXT (object);

    switch (prop_id) {
    case PROP_PROGRAM_NAME:
        g_value_set_string(value, js_context->program_name);
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
    GjsContext *js_context;

    js_context = GJS_CONTEXT (object);

    switch (prop_id) {
    case PROP_SEARCH_PATH:
        js_context->search_path = (char**) g_value_dup_boxed(value);
        break;
    case PROP_PROGRAM_NAME:
        js_context->program_name = g_value_dup_string(value);
        break;
    case PROP_PROFILER_ENABLED:
        js_context->should_profile = g_value_get_boolean(value);
        break;
    case PROP_PROFILER_SIGUSR2:
        js_context->should_listen_sigusr2 = g_value_get_boolean(value);
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

bool
_gjs_context_destroying (GjsContext *context)
{
    return context->destroying;
}

static gboolean
trigger_gc_if_needed (gpointer user_data)
{
    GjsContext *js_context = GJS_CONTEXT(user_data);
    js_context->auto_gc_id = 0;

    if (js_context->force_gc)
        JS_GC(js_context->context);
    else
        gjs_gc_if_needed(js_context->context);

    js_context->force_gc = false;

    return G_SOURCE_REMOVE;
}


static void
_gjs_context_schedule_gc_internal(GjsContext *js_context,
                                  bool        force_gc)
{
    js_context->force_gc |= force_gc;

    if (js_context->auto_gc_id > 0)
        return;

    js_context->force_gc |= force_gc;
    js_context->auto_gc_id = g_timeout_add_seconds_full(G_PRIORITY_LOW, 10,
                                                        trigger_gc_if_needed,
                                                        js_context, NULL);
}

void
_gjs_context_schedule_gc(GjsContext *js_context)
{
    _gjs_context_schedule_gc_internal(js_context, true);
}

void
_gjs_context_schedule_gc_if_needed(GjsContext *js_context)
{
    _gjs_context_schedule_gc_internal(js_context, false);
}

void
_gjs_context_exit(GjsContext *js_context,
                  uint8_t     exit_code)
{
    g_assert(!js_context->should_exit);
    js_context->should_exit = true;
    js_context->exit_code = exit_code;
}

bool
_gjs_context_should_exit(GjsContext *js_context,
                         uint8_t    *exit_code_p)
{
    if (exit_code_p != NULL)
        *exit_code_p = js_context->exit_code;
    return js_context->should_exit;
}

static void
context_reset_exit(GjsContext *js_context)
{
    js_context->should_exit = false;
    js_context->exit_code = 0;
}

bool
_gjs_context_get_is_owner_thread(GjsContext *js_context)
{
    return js_context->owner_thread == g_thread_self();
}

void
_gjs_context_set_sweeping(GjsContext *js_context,
                          bool        sweeping)
{
    js_context->in_gc_sweep = sweeping;
}

bool
_gjs_context_is_sweeping(JSContext *cx)
{
    auto js_context = static_cast<GjsContext *>(JS_GetContextPrivate(cx));
    return js_context->in_gc_sweep;
}

static gboolean
drain_job_queue_idle_handler(void *data)
{
    auto gjs_context = static_cast<GjsContext *>(data);
    _gjs_context_run_jobs(gjs_context);
    /* Uncatchable exceptions are swallowed here - no way to get a handle on
     * the main loop to exit it from this idle handler */
    g_assert(((void) "_gjs_context_run_jobs() should have emptied queue",
              gjs_context->idle_drain_handler == 0));
    return G_SOURCE_REMOVE;
}

/* See engine.cpp and JS::SetEnqueuePromiseJobCallback(). */
bool
_gjs_context_enqueue_job(GjsContext      *gjs_context,
                         JS::HandleObject job)
{
    if (gjs_context->idle_drain_handler)
        g_assert(gjs_context->job_queue->length() > 0);
    else
        g_assert(gjs_context->job_queue->length() == 0);

    if (!gjs_context->job_queue->append(job))
        return false;
    if (!gjs_context->idle_drain_handler)
        gjs_context->idle_drain_handler =
            g_idle_add_full(G_PRIORITY_DEFAULT, drain_job_queue_idle_handler,
                            gjs_context, nullptr);

    return true;
}

/**
 * _gjs_context_run_jobs:
 * @gjs_context: The #GjsContext instance
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
bool
_gjs_context_run_jobs(GjsContext *gjs_context)
{
    bool retval = true;
    g_assert(gjs_context->job_queue);

    if (gjs_context->draining_job_queue || gjs_context->should_exit)
        return true;

    auto cx = static_cast<JSContext *>(gjs_context_get_native_context(gjs_context));
    JSAutoRequest ar(cx);

    gjs_context->draining_job_queue = true;  /* Ignore reentrant calls */

    JS::RootedObject job(cx);
    JS::HandleValueArray args(JS::HandleValueArray::empty());
    JS::RootedValue rval(cx);

    /* Execute jobs in a loop until we've reached the end of the queue.
     * Since executing a job can trigger enqueueing of additional jobs,
     * it's crucial to recheck the queue length during each iteration. */
    for (size_t ix = 0; ix < gjs_context->job_queue->length(); ix++) {
        /* A previous job might have set this flag. e.g., System.exit(). */
        if (gjs_context->should_exit)
            break;

        job = gjs_context->job_queue->get()[ix];

        /* It's possible that job draining was interrupted prematurely,
         * leaving the queue partly processed. In that case, slots for
         * already-executed entries will contain nullptrs, which we should
         * just skip. */
        if (!job)
            continue;

        gjs_context->job_queue->get()[ix] = nullptr;
        {
            JSAutoCompartment ac(cx, job);
            if (!JS::Call(cx, JS::UndefinedHandleValue, job, args, &rval)) {
                /* Uncatchable exception - return false so that
                 * System.exit() works in the interactive shell and when
                 * exiting the interpreter. */
                if (!JS_IsExceptionPending(cx)) {
                    /* System.exit() is an uncatchable exception, but does not
                     * indicate a bug. Log everything else. */
                    if (!_gjs_context_should_exit(gjs_context, nullptr))
                        g_critical("Promise callback terminated with uncatchable exception");
                    retval = false;
                    continue;
                }

                /* There's nowhere for the exception to go at this point */
                gjs_log_exception(cx);
            }
        }
    }

    gjs_context->draining_job_queue = false;
    gjs_context->job_queue->clear();
    if (gjs_context->idle_drain_handler) {
        g_source_remove(gjs_context->idle_drain_handler);
        gjs_context->idle_drain_handler = 0;
    }
    return retval;
}

void
_gjs_context_register_unhandled_promise_rejection(GjsContext   *gjs_context,
                                                  uint64_t      id,
                                                  GjsAutoChar&& stack)
{
    gjs_context->unhandled_rejection_stacks[id] = std::move(stack);
}

void
_gjs_context_unregister_unhandled_promise_rejection(GjsContext *gjs_context,
                                                    uint64_t    id)
{
    size_t erased = gjs_context->unhandled_rejection_stacks.erase(id);
    g_assert(((void)"Handler attached to rejected promise that wasn't "
              "previously marked as unhandled", erased == 1));
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
    gjs_maybe_gc(context->context);
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
    JS_GC(context->context);
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
    return js_context->context;
}

bool
gjs_context_eval(GjsContext   *js_context,
                 const char   *script,
                 gssize        script_len,
                 const char   *filename,
                 int          *exit_status_p,
                 GError      **error)
{
    bool ret = false;

    bool auto_profile = js_context->should_profile;
    if (auto_profile && (_gjs_profiler_is_running(js_context->profiler) ||
                         js_context->should_listen_sigusr2))
        auto_profile = false;

    JSAutoCompartment ac(js_context->context, js_context->global);
    JSAutoRequest ar(js_context->context);

    g_object_ref(G_OBJECT(js_context));

    if (auto_profile)
        gjs_profiler_start(js_context->profiler);

    JS::RootedValue retval(js_context->context);
    bool ok = gjs_eval_with_scope(js_context->context, nullptr, script,
                                  script_len, filename, &retval);

    /* The promise job queue should be drained even on error, to finish
     * outstanding async tasks before the context is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks. */
    {
        JS::AutoSaveExceptionState saved_exc(js_context->context);
        ok = _gjs_context_run_jobs(js_context) && ok;
    }

    if (auto_profile)
        gjs_profiler_stop(js_context->profiler);

    if (!ok) {
        uint8_t code;
        if (_gjs_context_should_exit(js_context, &code)) {
            /* exit_status_p is public API so can't be changed, but should be
             * uint8_t, not int */
            *exit_status_p = code;
            g_set_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT,
                        "Exit with code %d", code);
            goto out;  /* Don't log anything */
        }

        if (!JS_IsExceptionPending(js_context->context)) {
            g_critical("Script %s terminated with an uncatchable exception",
                       filename);
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Script %s terminated with an uncatchable exception",
                        filename);
        } else {
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Script %s threw an exception", filename);
        }

        gjs_log_exception(js_context->context);
        /* No exit code from script, but we don't want to exit(0) */
        *exit_status_p = 1;
        goto out;
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

    ret = true;

 out:
    g_object_unref(G_OBJECT(js_context));
    context_reset_exit(js_context);
    return ret;
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

bool
gjs_context_define_string_array(GjsContext  *js_context,
                                const char    *array_name,
                                gssize         array_length,
                                const char   **array_values,
                                GError       **error)
{
    JSAutoCompartment ac(js_context->context, js_context->global);
    JSAutoRequest ar(js_context->context);

    JS::RootedObject global_root(js_context->context, js_context->global);
    if (!gjs_define_string_array(js_context->context,
                                 global_root,
                                 array_name, array_length, array_values,
                                 JSPROP_READONLY | JSPROP_PERMANENT)) {
        gjs_log_exception(js_context->context);
        g_set_error(error,
                    GJS_ERROR,
                    GJS_ERROR_FAILED,
                    "gjs_define_string_array() failed");
        return false;
    }

    return true;
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

/* It's OK to return JS::HandleId here, to avoid an extra root, with the
 * caveat that you should not use this value after the GjsContext has
 * been destroyed. */
JS::HandleId
gjs_context_get_const_string(JSContext      *context,
                             GjsConstString  name)
{
    GjsContext *gjs_context = (GjsContext *) JS_GetContextPrivate(context);
    return *gjs_context->const_strings[name];
}

/**
 * gjs_get_import_global:
 * @context: a #JSContext
 *
 * Gets the "import global" for the context's runtime. The import
 * global object is the global object for the context. It is used
 * as the root object for the scope of modules loaded by GJS in this
 * runtime, and should also be used as the globals 'obj' argument passed
 * to JS_InitClass() and the parent argument passed to JS_ConstructObject()
 * when creating a native classes that are shared between all contexts using
 * the runtime. (The standard JS classes are not shared, but we share
 * classes such as GObject proxy classes since objects of these classes can
 * easily migrate between contexts and having different classes depending
 * on the context where they were first accessed would be confusing.)
 *
 * Return value: the "import global" for the context's
 *  runtime. Will never return %NULL while GJS has an active context
 *  for the runtime.
 */
JSObject*
gjs_get_import_global(JSContext *context)
{
    GjsContext *gjs_context = (GjsContext *) JS_GetContextPrivate(context);
    return gjs_context->global;
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
    return self->profiler;
}

