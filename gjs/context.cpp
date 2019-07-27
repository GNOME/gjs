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

#include <signal.h>  // for sigaction, SIGUSR1, sa_handler
#include <stdint.h>
#include <stdio.h>      // for FILE, fclose, size_t
#include <string.h>     // for memset
#include <sys/types.h>  // IWYU pragma: keep
#include <codecvt>
#include <locale>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>  // for getpid
#endif

#include <new>
#include <string>  // for u16string
#include <unordered_map>
#include <utility>  // for move

#include <gio/gio.h>
#include "byteArray.h"
#include "context-private.h"
#include "engine.h"
#include "gi/object.h"
#include "gi/private.h"
#include "gi/repo.h"
#include "gjs/jsapi-util-args.h"
#include "global.h"
#include "importer.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"
#include "mem.h"
#include "native.h"
#include "profiler-private.h"

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <modules/modules.h>

#ifdef G_OS_WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include "gjs/jsapi-wrapper.h"
#include "js/GCHashTable.h"  // for WeakCache

#include "gi/object.h"
#include "gi/private.h"
#include "gi/repo.h"
#include "gjs/atoms.h"
#include "gjs/byteArray.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/engine.h"
#include "gjs/error-types.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem.h"
#include "gjs/native.h"
#include "gjs/profiler-private.h"
#include "gjs/profiler.h"
#include "modules/modules.h"
#include "util/log.h"

static void gjs_context_dispose(GObject* object);
static void gjs_context_finalize(GObject* object);
static void gjs_context_constructed(GObject* object);
static void gjs_context_get_property(GObject* object, guint prop_id,
                                     GValue* value, GParamSpec* pspec);
static void gjs_context_set_property(GObject* object, guint prop_id,
                                     const GValue* value, GParamSpec* pspec);

void GjsContextPrivate::EnvironmentPreparer::invoke(JS::HandleObject scope,
                                                    Closure& closure) {
    g_assert(!JS_IsExceptionPending(m_cx));

    JSAutoCompartment ac(m_cx, scope);
    if (!closure(m_cx))
        gjs_log_exception(m_cx);
}

struct _GjsContext {
    GObject parent;
};

struct _GjsContextClass {
    GObjectClass parent;
};

/* Temporary workaround for https://bugzilla.gnome.org/show_bug.cgi?id=793175 */
#if __GNUC__ >= 8
_Pragma("GCC diagnostic push")
    _Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")
#endif
        G_DEFINE_TYPE_WITH_PRIVATE(GjsContext, gjs_context, G_TYPE_OBJECT);
#if __GNUC__ >= 8
_Pragma("GCC diagnostic pop")
#endif

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

enum {
    PROP_0,
    PROP_SEARCH_PATH,
    PROP_PROGRAM_NAME,
    PROP_PROFILER_ENABLED,
    PROP_PROFILER_SIGUSR2,
};

static GMutex contexts_lock;
static GList* all_contexts = NULL;

static GjsAutoChar dump_heap_output;
static unsigned dump_heap_idle_id = 0;

#ifdef G_OS_UNIX
/* Currently heap dumping is only supported on UNIX platforms! */
static void gjs_context_dump_heaps(void) {
    static unsigned counter = 0;

    gjs_memory_report("signal handler", false);

    /* dump to sequential files to allow easier comparisons */
    GjsAutoChar filename = g_strdup_printf("%s.%jd.%u", dump_heap_output.get(),
                                           intmax_t(getpid()), counter);
    ++counter;

    FILE* fp = fopen(filename, "w");
    if (!fp)
        return;

    for (GList* l = all_contexts; l; l = g_list_next(l)) {
        auto* gjs = static_cast<GjsContextPrivate*>(l->data);
        js::DumpHeap(gjs->context(), fp, js::IgnoreNurseryObjects);
    }

    fclose(fp);
}

static gboolean dump_heap_idle(void*) {
    dump_heap_idle_id = 0;

    gjs_context_dump_heaps();

    return false;
}

static void dump_heap_signal_handler(int signum G_GNUC_UNUSED) {
    if (dump_heap_idle_id == 0)
        dump_heap_idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                                            dump_heap_idle, nullptr, nullptr);
}
#endif

static void setup_dump_heap(void) {
    static bool dump_heap_initialized = false;
    if (!dump_heap_initialized) {
        dump_heap_initialized = true;

        /* install signal handler only if environment variable is set */
        const char* heap_output = g_getenv("GJS_DEBUG_HEAP_OUTPUT");
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

static void gjs_context_init(GjsContext* js_context) {
    gjs_context_make_current(js_context);
}

static void gjs_context_class_init(GjsContextClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GParamSpec* pspec;

    object_class->dispose = gjs_context_dispose;
    object_class->finalize = gjs_context_finalize;

    object_class->constructed = gjs_context_constructed;
    object_class->get_property = gjs_context_get_property;
    object_class->set_property = gjs_context_set_property;

    pspec = g_param_spec_boxed(
        "search-path", "Search path",
        "Path where modules to import should reside", G_TYPE_STRV,
        (GParamFlags)(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class, PROP_SEARCH_PATH, pspec);
    g_param_spec_unref(pspec);

    pspec = g_param_spec_string(
        "program-name", "Program Name",
        "The filename of the launched JS program", "",
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class, PROP_PROGRAM_NAME, pspec);
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
    pspec = g_param_spec_boolean(
        "profiler-enabled", "Profiler enabled",
        "Whether to profile JS code run by this context", FALSE,
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
    pspec = g_param_spec_boolean(
        "profiler-sigusr2", "Profiler SIGUSR2",
        "Whether to activate the profiler on SIGUSR2", FALSE,
        GParamFlags(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class, PROP_PROFILER_SIGUSR2, pspec);
    g_param_spec_unref(pspec);

    /* For GjsPrivate */
    {
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        char* basedir =
            g_win32_get_package_installation_directory_of_module(gjs_dll);
        char* priv_typelib_dir =
            g_build_filename(basedir, "lib", "girepository-1.0", NULL);
        g_free(basedir);
#else
        char* priv_typelib_dir =
            g_build_filename(PKGLIBDIR, "girepository-1.0", NULL);
#endif
        g_irepository_prepend_search_path(priv_typelib_dir);
        g_free(priv_typelib_dir);
    }

    gjs_register_native_module("_byteArrayNative", gjs_define_byte_array_stuff);
    gjs_register_native_module("_gi", gjs_define_private_gi_stuff);
    gjs_register_native_module("gi", gjs_define_repo);

    gjs_register_static_modules();
}

void GjsContextPrivate::trace(JSTracer* trc, void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    JS::TraceEdge<JSObject*>(trc, &gjs->m_global, "GJS global object");
    gjs->m_atoms->trace(trc);
    gjs->m_job_queue.trace(trc);
    gjs->m_object_init_list.trace(trc);
}

void GjsContextPrivate::warn_about_unhandled_promise_rejections(void) {
    for (auto& kv : m_unhandled_rejection_stacks) {
        const char* stack = kv.second;
        g_warning(
            "Unhandled promise rejection. To suppress this warning, add "
            "an error handler to your promise chain with .catch() or a "
            "try-catch block around your await expression. %s%s",
            stack ? "Stack trace of the failed promise:\n"
                  : "Unfortunately there is no stack trace of the failed "
                    "promise.",
            stack ? stack : "");
    }
    m_unhandled_rejection_stacks.clear();
}

static void gjs_context_dispose(GObject* object) {
    gjs_debug(GJS_DEBUG_CONTEXT, "JS shutdown sequence");

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    /* Profiler must be stopped and freed before context is shut down */
    gjs->free_profiler();

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

    gjs->dispose();
}

void GjsContextPrivate::free_profiler(void) {
    gjs_debug(GJS_DEBUG_CONTEXT, "Stopping profiler");
    if (m_profiler)
        g_clear_pointer(&m_profiler, _gjs_profiler_free);
}

void GjsContextPrivate::dispose(void) {
    if (m_cx) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Checking unhandled promise rejections");
        warn_about_unhandled_promise_rejections();

        JS_BeginRequest(m_cx);

        gjs_debug(GJS_DEBUG_CONTEXT, "Releasing cached JS wrappers");
        m_fundamental_table->clear();
        m_gtype_table->clear();

        /* Do a full GC here before tearing down, since once we do
         * that we may not have the JS_GetPrivate() to access the
         * context
         */
        gjs_debug(GJS_DEBUG_CONTEXT, "Final triggered GC");
        JS_GC(m_cx);
        JS_EndRequest(m_cx);

        gjs_debug(GJS_DEBUG_CONTEXT, "Destroying JS context");
        m_destroying = true;

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
    g_clear_pointer(&m_program_name, g_free);
}

static void gjs_context_finalize(GObject* object) {
    if (gjs_context_get_current() == (GjsContext*)object)
        gjs_context_make_current(NULL);

    g_mutex_lock(&contexts_lock);
    all_contexts = g_list_remove(all_contexts, object);
    g_mutex_unlock(&contexts_lock);

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);
    gjs->~GjsContextPrivate();
    G_OBJECT_CLASS(gjs_context_parent_class)->finalize(object);
}

bool GjsContextPrivate::eval_module(const char* identifier,
                                    uint8_t* exit_status_p, GError** error) {
    bool ret = false;

    bool auto_profile = m_should_profile;

    if (auto_profile &&
        (_gjs_profiler_is_running(m_profiler) || m_should_listen_sigusr2))
        auto_profile = false;

    if (auto_profile)
        gjs_profiler_start(m_profiler);

    auto it = m_id_to_module.find(identifier);
    if (it == m_id_to_module.end()) {
        g_error("Attempted to evaluate unknown module: %s", identifier);
        return false;
    }

    JSAutoCompartment ac(m_cx, m_global);
    JSAutoRequest ar(m_cx);

    if (!JS::ModuleInstantiate(m_cx, it->second)) {
        gjs_log_exception(m_cx);
        g_error("Failed to instantiate module: %s", identifier);
        return false;
    }

    bool ok = true;

    if (!JS::ModuleEvaluate(m_cx, it->second)) {
        gjs_log_exception(m_cx);
        g_warning("Failed to evaluate module! %s", identifier);
        ok = false;
    }

    schedule_gc_if_needed();

    if (JS_IsExceptionPending(m_cx)) {
        g_warning(
            "ModuleEvaluation returned true but exception was pending; "
            "did somebody call gjs_throw() without returning false?");
        ok = false;
    }

    gjs_debug(GJS_DEBUG_CONTEXT, "Module evaluation succeeded");

    /* The promise job queue should be drained even on error, to finish
     * outstanding async tasks before the context is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks. */
    {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs() && ok;
    }

    if (auto_profile)
        gjs_profiler_stop(m_profiler);

    if (!ok) {
        uint8_t code;

        if (should_exit(&code)) {
            *exit_status_p = code;
            g_set_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT,
                        "Exit with code %d", code);
            goto out; /* Don't log anything */
        }

        if (!JS_IsExceptionPending(m_cx)) {
            g_critical("Module %s terminated with an uncatchable exception",
                       identifier);
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Module %s terminated with an uncatchable exception",
                        identifier);
        } else {
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Module %s threw an exception", identifier);
        }

        gjs_log_exception(m_cx);
        /* No exit code from script, but we don't want to exit(0) */
        *exit_status_p = 1;
        goto out;
    }

    if (exit_status_p) {
        /* Assume success if no integer was returned */
        *exit_status_p = 0;
    }

    ret = true;

out:
    reset_exit();
    return ret;
}

/*
    An internal API for registering modules that returns
    errors by throwing within the JS context. This allows
    it to be used as part of the module resolve hook.
*/
bool GjsContextPrivate::register_module_inner(GjsContext* gjs_cx,

                                              const char* identifier,
                                              const char* filename,
                                              const char* mod_text,
                                              size_t mod_len) {
    if (m_id_to_module.find(identifier) != m_id_to_module.end()) {
        gjs_throw(m_cx, "Module '%s' is already registered", identifier);
        return false;
    }

    unsigned int start_line_number = 1;

    JS::CompileOptions options(m_cx);
    options.setFileAndLine(identifier, start_line_number).setSourceIsLazy(true);

    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(mod_text);
    size_t offset = gjs_unix_shebang_len(utf16_string, &start_line_number);

    JS::SourceBufferHolder buf(utf16_string.c_str() + offset,
                               utf16_string.size() - offset,
                               JS::SourceBufferHolder::NoOwnership);

    JS::RootedObject new_module(m_cx);
    if (!CompileModule(m_cx, options, buf, &new_module)) {
        gjs_throw(m_cx, "Failed to compile module: %s", identifier);
        return false;
    }

    if (filename != NULL) {
        // Since this is a file-based module, be sure to set it's host field
        JS::RootedValue filename_val(m_cx);
        if (!gjs_string_from_utf8(m_cx, filename, &filename_val)) {
            gjs_throw(m_cx,
                      "Failed to encode full module path (%s) as JS string",
                      filename);
            return false;
        }

        JS::SetModuleHostDefinedField(new_module, filename_val);
    }

    m_id_to_module[identifier].init(m_cx, new_module);
    return true;
}

static SoupURI* get_gi_uri(const char* uri) { return soup_uri_new(uri); }

static char* gir_uri_ns(const char* uri) {
    SoupURI_autoptr url = get_gi_uri(uri);

    const char* path = soup_uri_get_host(url);

    return strdup(path);
}

static char* gir_uri_version(const char* uri) {
    SoupURI_autoptr url = get_gi_uri(uri);

    const char* q = soup_uri_get_query(url);

    if (q == NULL) {
        return NULL;
    }

    GHashTable_autoptr queries = soup_form_decode(q);
    GjsAutoChar key = g_strdup("v");
    char* version = (char*)g_hash_table_lookup(queries, key);

    return strdup(version);
}

static bool is_gir_uri(const char* uri) {
    const char* scheme = g_uri_parse_scheme(uri);
    return scheme != NULL && strcmp(scheme, "gi") == 0;
}

static char* gir_js_mod_ver(const char* ns, const char* version) {
    return g_strdup_printf(
        "imports.gi.versions.%s = '%s';\nimport gi from \'gi\';\nexport "
        "default (gi.%s);",
        ns, version, ns);
}

static char* gir_js_mod(const char* ns) {
    return g_strdup_printf("import gi from \'gi\';\nexport default (gi.%s);",
                           ns);
}

bool GjsContextPrivate::module_resolve(unsigned argc, JS::Value* vp) {
    auto gjs_cx = static_cast<GjsContext*>(JS_GetContextPrivate(m_cx));

    // The module from which the resolve request is coming
    JS::RootedObject mod_obj(m_cx);
    JS::UniqueChars
        id;  // The string identifier of the module we wish to import

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    args.rval().setNull();
    if (!gjs_parse_call_args(m_cx, "ModuleResolver", args, "os", "sourceModule",
                             &mod_obj, "identifier", &id))
        return false;

    // check if path is relative

    if (id[0] == '.' && (id[1] == '/' || (id[1] == '.' && id[2] == '/'))) {
        // If a module has a path, we'll have stored it in the host field
        JS::Value mod_loc_val = JS::GetModuleHostDefinedField(mod_obj);

        JS::UniqueChars mod_loc;
        if (!gjs_string_to_utf8(m_cx, mod_loc_val, &mod_loc)) {
            gjs_throw(m_cx,
                      "Attempting to resolve relative import (%s) from "
                      "non-file module",
                      mod_loc.get());
            return false;
        }

        GjsAutoChar mod_dir(g_path_get_dirname(g_strdup(mod_loc.get())));

        GFile* output =
            g_file_new_for_commandline_arg_and_cwd(id.get(), mod_dir);
        GjsAutoChar full_path(g_file_get_path(output));

        auto module = m_id_to_module.find(full_path.get());
        if (module != m_id_to_module.end()) {
            args.rval().setObject(*(module->second));
            return true;
        }

        char* mod_text_raw;
        gsize mod_len;
        if (!g_file_get_contents(full_path, &mod_text_raw, &mod_len, nullptr)) {
            gjs_throw(m_cx, "Failed to read file: %s", full_path.get());
            return false;
        }

        GjsAutoChar mod_text(mod_text_raw);

        if (!register_module_inner(gjs_cx, full_path, full_path, mod_text,
                                   mod_len))
            // GjsContextPrivate::_register_module_inner should have already
            // thrown any relevant errors
            return false;

        args.rval().setObject(*m_id_to_module[full_path.get()]);
        return true;
    }

    if (is_gir_uri(id.get())) {
        GjsAutoChar ns = gir_uri_ns(id.get());
        GjsAutoChar version = gir_uri_version(id.get());
        GjsAutoChar gir_mod = NULL;
        if (ns == NULL && version == NULL) {
            gjs_throw(m_cx, "Attempted to load invalid module path %s",
                      id.get());
            return false;
        } else if (version == NULL) {
            gir_mod = gir_js_mod(ns);
        } else if (ns == NULL) {
            gjs_throw(m_cx, "Attempted to load invalid module path %s",
                      id.get());
            return false;
        } else {
            gir_mod = gir_js_mod_ver(ns, version);
        }

        auto module = m_id_to_module.find(id.get());
        if (gir_mod != NULL && module == m_id_to_module.end()) {
            register_module(id.get(), id.get(), gir_mod, strlen(gir_mod),
                            nullptr);
        }
    }

    auto module = m_id_to_module.find(id.get());

    if (module == m_id_to_module.end()) {
        const char* dirname = "resource:///org/gnome/gjs/modules/esm/";
        GjsAutoChar filename = g_strdup_printf("%s.js", id.get());
        GjsAutoChar full_path =
            g_build_filename(dirname, filename.get(), nullptr);
        GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(full_path);

        bool exists = g_file_query_exists(gfile, NULL);

        if (exists) {
            char* mod_text_raw;
            gsize mod_len;
            GError* err = NULL;

            if (!g_file_load_contents(gfile, NULL, &mod_text_raw, &mod_len,
                                      NULL, &err)) {
                gjs_throw(m_cx, "Failed to read internal resource: %s \n%s",
                          full_path.get(), err->message);

                return false;
            }

            GjsAutoChar mod_text(mod_text_raw);

            if (!register_module(id.get(), full_path, mod_text, mod_len, NULL))
                return false;

            args.rval().setObject(*m_id_to_module[id.get()]);

            return true;
        } else {
            gjs_throw(m_cx, "Attempted to load unregistered global module: %s",
                      id.get());
            return false;
        }
    }

    args.rval().setObject(*module->second);
    return true;
}

/*
    Attempts to register a module, reporting any errors
    through a combination of false return and error parameter
*/
bool GjsContextPrivate::register_module(const char* identifier,
                                        const char* filename,
                                        const char* mod_text, size_t mod_len,
                                        GError** error) {
    auto gjs_cx = static_cast<GjsContext*>(JS_GetContextPrivate(m_cx));

    JSAutoRequest ar(m_cx);
    JSAutoCompartment ac(m_cx, m_global);

    // Module registration uses exceptions to report errors
    // so we'll store the exception state, clear it, attempt to load the
    // module, then restore the original exception state.
    JS::AutoSaveExceptionState exp_state(m_cx);

    if (register_module_inner(gjs_cx, identifier, filename, mod_text, mod_len))
        return true;

    // Our message could come from memory owned by us or by the runtime.
    const char* msg = nullptr;
    JS::UniqueChars auto_msg = nullptr;

    JS::RootedValue exc(m_cx);
    if (JS_GetPendingException(m_cx, &exc)) {
        JS::RootedObject exc_obj(m_cx, &exc.toObject());
        JSErrorReport* report = JS_ErrorFromException(m_cx, exc_obj);
        if (report) {
            msg = report->message().c_str();
        } else {
            JS::RootedString js_message(m_cx, JS::ToString(m_cx, exc));
            if (js_message) {
                // auto_msg = JS_EncodeStringToUTF8(m_cx, js_message);
                // auto_msg = js_message;
                msg = "TODO: Fix Broken Message";  // auto_msg.get();
            }
        }
    }

    g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                "Error registering module '%s': %s", identifier,
                msg ? msg : "unknown");

    // We've successfully handled the exception so we can clear it.
    // This is necessary because AutoSaveExceptionState doesn't erase
    // exceptions when it restores the previous exception state.
    JS_ClearPendingException(m_cx);

    return false;
}

static bool gjs_module_resolve(JSContext* cx, unsigned argc, JS::Value* vp) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);

    return gjs->module_resolve(argc, vp);
}

static void gjs_context_constructed(GObject* object) {
    GjsContext* js_context = GJS_CONTEXT(object);

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

    g_object_weak_ref(object, &ObjectInstance::context_dispose_notify, nullptr);
}

GjsContextPrivate::GjsContextPrivate(JSContext* cx, GjsContext* public_context)
    : m_public_context(public_context), m_cx(cx), m_environment_preparer(cx) {
    m_owner_thread = g_thread_self();

    const char* env_profiler = g_getenv("GJS_ENABLE_PROFILER");
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
    if (!m_fundamental_table->init())
        g_error("Failed to initialize fundamental objects table");

    m_gtype_table = new JS::WeakCache<GTypeTable>(rt);
    if (!m_gtype_table->init())
        g_error("Failed to initialize GType objects table");

    m_atoms = new GjsAtoms();

    JS_BeginRequest(m_cx);

    JS::RootedObject global(m_cx, gjs_create_global_object(m_cx));
    if (!global) {
        gjs_log_exception(m_cx);
        g_error("Failed to initialize global object");
    }

    JSAutoCompartment ac(m_cx, global);

    m_global = global;
    JS_AddExtraGCRootsTracer(m_cx, &GjsContextPrivate::trace, this);

    JS::RootedFunction mod_resolve(
        cx, JS_NewFunction(cx, gjs_module_resolve, 2, 0, nullptr));
    SetModuleResolveHook(cx, mod_resolve);

    if (!m_atoms->init_atoms(m_cx)) {
        gjs_log_exception(m_cx);
        g_error("Failed to initialize global strings");
    }

    JS::RootedObject importer(m_cx,
                              gjs_create_root_importer(m_cx, m_search_path));
    if (!importer) {
        gjs_log_exception(cx);
        g_error("Failed to create root importer");
    }

    JS::Value v_importer = gjs_get_global_slot(m_cx, GJS_GLOBAL_SLOT_IMPORTS);
    g_assert(((void)"Someone else already created root importer",
              v_importer.isUndefined()));

    gjs_set_global_slot(m_cx, GJS_GLOBAL_SLOT_IMPORTS,
                        JS::ObjectValue(*importer));

    if (!gjs_define_global_properties(m_cx, global, "default")) {
        gjs_log_exception(m_cx);
        g_error("Failed to define properties on global object");
    }

    JS_EndRequest(m_cx);
}

static void gjs_context_get_property(GObject* object, guint prop_id,
                                     GValue* value, GParamSpec* pspec) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    switch (prop_id) {
        case PROP_PROGRAM_NAME:
            g_value_set_string(value, gjs->program_name());
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void gjs_context_set_property(GObject* object, guint prop_id,
                                     const GValue* value, GParamSpec* pspec) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(object);

    switch (prop_id) {
        case PROP_SEARCH_PATH:
            gjs->set_search_path(static_cast<char**>(g_value_dup_boxed(value)));
            break;
        case PROP_PROGRAM_NAME:
            gjs->set_program_name(g_value_dup_string(value));
            break;
        case PROP_PROFILER_ENABLED:
            gjs->set_should_profile(g_value_get_boolean(value));
            break;
        case PROP_PROFILER_SIGUSR2:
            gjs->set_should_listen_sigusr2(g_value_get_boolean(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

GjsContext* gjs_context_new(void) {
    return (GjsContext*)g_object_new(GJS_TYPE_CONTEXT, NULL);
}

GjsContext* gjs_context_new_with_search_path(char** search_path) {
    return (GjsContext*)g_object_new(GJS_TYPE_CONTEXT, "search-path",
                                     search_path, NULL);
}

gboolean GjsContextPrivate::trigger_gc_if_needed(void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    gjs->m_auto_gc_id = 0;

    if (gjs->m_force_gc)
        JS_GC(gjs->m_cx);
    else
        gjs_gc_if_needed(gjs->m_cx);

    gjs->m_force_gc = false;

    return G_SOURCE_REMOVE;
}

void GjsContextPrivate::schedule_gc_internal(bool force_gc) {
    m_force_gc |= force_gc;

    if (m_auto_gc_id > 0)
        return;

    m_auto_gc_id = g_timeout_add_seconds_full(
        G_PRIORITY_LOW, 10, trigger_gc_if_needed, this, nullptr);
}

/*
 * GjsContextPrivate::schedule_gc_if_needed:
 *
 * Does a minor GC immediately if the JS engine decides one is needed, but
 * also schedules a full GC in the next idle time.
 */
void GjsContextPrivate::schedule_gc_if_needed(void) {
    // We call JS_MaybeGC immediately, but defer a check for a full GC cycle
    // to an idle handler.
    JS_MaybeGC(m_cx);

    schedule_gc_internal(false);
}

void GjsContextPrivate::set_sweeping(bool value) {
    // If we have a profiler enabled, record the duration of GC sweep
    if (this->m_profiler != nullptr) {
        int64_t now = g_get_monotonic_time() * 1000L;

        if (value) {
            m_sweep_begin_time = now;
        } else {
            if (m_sweep_begin_time != 0) {
                _gjs_profiler_add_mark(this->m_profiler, m_sweep_begin_time,
                                       now - m_sweep_begin_time, "GJS", "Sweep",
                                       nullptr);
                m_sweep_begin_time = 0;
            }
        }
    }

    m_in_gc_sweep = value;
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

gboolean GjsContextPrivate::drain_job_queue_idle_handler(void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    if (!gjs->run_jobs())
        gjs_log_exception(gjs->context());
    /* Uncatchable exceptions are swallowed here - no way to get a handle on
     * the main loop to exit it from this idle handler */
    g_assert(((void)"GjsContextPrivate::run_jobs() should have emptied queue",
              gjs->m_idle_drain_handler == 0));
    return G_SOURCE_REMOVE;
}

/* See engine.cpp and JS::SetEnqueuePromiseJobCallback(). */
bool GjsContextPrivate::enqueue_job(JS::HandleObject job) {
    if (m_idle_drain_handler)
        g_assert(m_job_queue.length() > 0);
    else
        g_assert(m_job_queue.length() == 0);

    if (!m_job_queue.append(job)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }
    if (!m_idle_drain_handler)
        m_idle_drain_handler = g_idle_add_full(
            G_PRIORITY_DEFAULT, drain_job_queue_idle_handler, this, nullptr);

    return true;
}

/*
 * GjsContext::run_jobs:
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
bool GjsContextPrivate::run_jobs(void) {
    bool retval = true;

    if (m_draining_job_queue || m_should_exit)
        return true;

    JSAutoRequest ar(m_cx);

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
            JSAutoCompartment ac(m_cx, job);
            if (!JS::Call(m_cx, JS::UndefinedHandleValue, job, args, &rval)) {
                /* Uncatchable exception - return false so that
                 * System.exit() works in the interactive shell and when
                 * exiting the interpreter. */
                if (!JS_IsExceptionPending(m_cx)) {
                    /* System.exit() is an uncatchable exception, but does
                     * not indicate a bug. Log everything else. */
                    if (!should_exit(nullptr))
                        g_critical(
                            "Promise callback terminated with uncatchable "
                            "exception");
                    retval = false;
                    continue;
                }

                /* There's nowhere for the exception to go at this point */
                gjs_log_exception(m_cx);
            }
        }
    }

    m_draining_job_queue = false;
    m_job_queue.clear();
    if (m_idle_drain_handler) {
        g_source_remove(m_idle_drain_handler);
        m_idle_drain_handler = 0;
    }
    return retval;
}

void GjsContextPrivate::register_unhandled_promise_rejection(
    uint64_t id, GjsAutoChar&& stack) {
    m_unhandled_rejection_stacks[id] = std::move(stack);
}

void GjsContextPrivate::unregister_unhandled_promise_rejection(uint64_t id) {
    size_t erased = m_unhandled_rejection_stacks.erase(id);
    g_assert(((void)"Handler attached to rejected promise that wasn't "
                    "previously marked as unhandled",
              erased == 1));
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
void gjs_context_maybe_gc(GjsContext* context) {
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
void gjs_context_gc(GjsContext* context) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(context);
    JS_GC(gjs->context());
}

/**
 * gjs_context_get_all:
 *
 * Returns a newly-allocated list containing all known instances of
 * #GjsContext. This is useful for operating on the contexts from a
 * process-global situation such as a debugger.
 *
 * Return value: (element-type GjsContext) (transfer full): Known
 * #GjsContext instances
 */
GList* gjs_context_get_all(void) {
    GList* result;
    GList* iter;
    g_mutex_lock(&contexts_lock);
    result = g_list_copy(all_contexts);
    for (iter = result; iter; iter = iter->next)
        g_object_ref((GObject*)iter->data);
    g_mutex_unlock(&contexts_lock);
    return result;
}

/**
 * gjs_context_get_native_context:
 *
 * Returns a pointer to the underlying native context.  For SpiderMonkey,
 * this is a JSContext *
 */
void* gjs_context_get_native_context(GjsContext* js_context) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), NULL);
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->context();
}

bool gjs_context_eval(GjsContext* js_context, const char* script,
                      gssize script_len, const char* filename,
                      int* exit_status_p, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsAutoUnref<GjsContext> js_context_ref(js_context, GjsAutoTakeOwnership());

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->eval(script, script_len, filename, exit_status_p, error);
}

bool gjs_context_eval_module(GjsContext* js_context, const char* identifier,
                             uint8_t* exit_status_p, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsAutoUnref<GjsContext> js_context_ref(js_context, GjsAutoTakeOwnership());

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->eval_module(identifier, exit_status_p, error);
}

bool gjs_context_register_module(GjsContext* js_context, const char* identifier,
                                 const char* filename, const char* mod_text,
                                 size_t mod_len, GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);

    GjsAutoUnref<GjsContext> js_context_ref(js_context, GjsAutoTakeOwnership());

    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);
    return gjs->register_module(identifier, filename, mod_text, mod_len, error);
}

bool GjsContextPrivate::eval(const char* script, ssize_t script_len,
                             const char* filename, int* exit_status_p,
                             GError** error) {
    bool ret = false;

    bool auto_profile = m_should_profile;
    if (auto_profile &&
        (_gjs_profiler_is_running(m_profiler) || m_should_listen_sigusr2))
        auto_profile = false;

    JSAutoCompartment ac(m_cx, m_global);
    JSAutoRequest ar(m_cx);

    if (auto_profile)
        gjs_profiler_start(m_profiler);

    JS::RootedValue retval(m_cx);
    bool ok = eval_with_scope(nullptr, script, script_len, filename, &retval);

    /* The promise job queue should be drained even on error, to finish
     * outstanding async tasks before the context is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks.
     */
    {
        JS::AutoSaveExceptionState saved_exc(m_cx);
        ok = run_jobs() && ok;
    }

    if (auto_profile)
        gjs_profiler_stop(m_profiler);

    if (!ok) {
        uint8_t code;
        if (should_exit(&code)) {
            /* exit_status_p is public API so can't be changed, but should
             * be uint8_t, not int */
            *exit_status_p = code;
            g_set_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT,
                        "Exit with code %d", code);
            goto out; /* Don't log anything */
        }

        if (!JS_IsExceptionPending(m_cx)) {
            g_critical("Script %s terminated with an uncatchable exception",
                       filename);
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Script %s terminated with an uncatchable exception",
                        filename);
        } else {
            g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                        "Script %s threw an exception", filename);
        }

        gjs_log_exception(m_cx);
        /* No exit code from script, but we don't want to exit(0) */
        *exit_status_p = 1;
        goto out;
    }

    if (exit_status_p) {
        if (retval.isInt32()) {
            int code = retval.toInt32();
            gjs_debug(GJS_DEBUG_CONTEXT, "Script returned integer code %d",
                      code);
            *exit_status_p = code;
        } else {
            /* Assume success if no integer was returned */
            *exit_status_p = 0;
        }
    }

    ret = true;

out:
    reset_exit();
    return ret;
}

bool gjs_context_eval_file(GjsContext* js_context, const char* filename,
                           int* exit_status_p, GError** error) {
    char* script;
    size_t script_len;
    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(filename);

    if (!g_file_load_contents(file, nullptr, &script, &script_len, nullptr,
                              error))
        return false;
    GjsAutoChar script_ref = script;

    return gjs_context_eval(js_context, script, script_len, filename,
                            exit_status_p, error);
}

/*
 * GjsContextPrivate::eval_with_scope:
 * @scope_object: an object to use as the global scope, or nullptr
 * @script: JavaScript program encoded in UTF-8
 * @script_len: length of @script, or -1 if @script is 0-terminated
 * @filename: filename to use as the origin of @script
 * @retval: location for the return value of @script
 *
 * Executes @script with a local scope so that nothing from the script leaks
 * out into the global scope. If @scope_object is given, then everything
 * that @script placed in the global namespace is defined on @scope_object.
 * Otherwise, the global definitions are just discarded.
 */
bool GjsContextPrivate::eval_with_scope(JS::HandleObject scope_object,
                                        const char* script, ssize_t script_len,
                                        const char* filename,
                                        JS::MutableHandleValue retval) {
    JSAutoRequest ar(m_cx);

    /* log and clear exception if it's set (should not be, normally...) */
    if (JS_IsExceptionPending(m_cx)) {
        g_warning("eval_with_scope() called with a pending exception");
        return false;
    }

    JS::RootedObject eval_obj(m_cx, scope_object);
    if (!eval_obj)
        eval_obj = JS_NewPlainObject(m_cx);

    std::u16string utf16_string = gjs_utf8_script_to_utf16(script, script_len);

    unsigned start_line_number = 1;
    size_t offset = gjs_unix_shebang_len(utf16_string, &start_line_number);

    JS::SourceBufferHolder buf(utf16_string.c_str() + offset,
                               utf16_string.size() - offset,
                               JS::SourceBufferHolder::NoOwnership);

    JS::AutoObjectVector scope_chain(m_cx);
    if (!scope_chain.append(eval_obj)) {
        JS_ReportOutOfMemory(m_cx);
        return false;
    }

    JS::CompileOptions options(m_cx);
    options.setFileAndLine(filename, start_line_number);

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
 * one is needed. It's good practice to check if a GC should be run every
 * time we return from JS back into C++.
 */
bool GjsContextPrivate::call_function(JS::HandleObject this_obj,
                                      JS::HandleValue func_val,
                                      const JS::HandleValueArray& args,
                                      JS::MutableHandleValue rval) {
    JSAutoRequest ar(m_cx);

    if (!JS_CallFunctionValue(m_cx, this_obj, func_val, args, rval))
        return false;

    schedule_gc_if_needed();

    return true;
}

bool gjs_context_define_string_array(GjsContext* js_context,
                                     const char* array_name,
                                     gssize array_length,
                                     const char** array_values,
                                     GError** error) {
    g_return_val_if_fail(GJS_IS_CONTEXT(js_context), false);
    GjsContextPrivate* gjs = GjsContextPrivate::from_object(js_context);

    JSAutoCompartment ac(gjs->context(), gjs->global());
    JSAutoRequest ar(gjs->context());

    JS::RootedObject global_root(gjs->context(), gjs->global());
    if (!gjs_define_string_array(gjs->context(), global_root, array_name,
                                 array_length, array_values,
                                 JSPROP_READONLY | JSPROP_PERMANENT)) {
        gjs_log_exception(gjs->context());
        g_set_error(error, GJS_ERROR, GJS_ERROR_FAILED,
                    "gjs_define_string_array() failed");
        return false;
    }

    return true;
}

static GjsContext* current_context;

GjsContext* gjs_context_get_current(void) { return current_context; }

void gjs_context_make_current(GjsContext* context) {
    g_assert(context == NULL || current_context == NULL);

    current_context = context;
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
JSObject* gjs_get_import_global(JSContext* context) {
    return GjsContextPrivate::from_cx(context)->global();
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
GjsProfiler* gjs_context_get_profiler(GjsContext* self) {
    return GjsContextPrivate::from_object(self)->profiler();
}

/**
 * gjs_get_js_version:
 *
 * Returns the underlying version of the JS engine.
 *
 * Returns: a string
 */
const char* gjs_get_js_version(void) { return JS_GetImplementationVersion(); }
