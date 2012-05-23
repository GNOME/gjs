/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include "context.h"
#include "importer.h"
#include "jsapi-util.h"
#include "profiler.h"
#include "native.h"
#include "byteArray.h"
#include "compat.h"

#include "gi.h"

#include <util/log.h>
#include <util/glib.h>
#include <util/error.h>

#include <string.h>

#include <jsapi.h>

#define _GJS_JS_VERSION_DEFAULT "1.8"

static void     gjs_context_dispose           (GObject               *object);
static void     gjs_context_finalize          (GObject               *object);
static GObject* gjs_context_constructor       (GType                  type,
                                                  guint                  n_construct_properties,
                                                  GObjectConstructParam *construct_params);
static void     gjs_context_get_property      (GObject               *object,
                                                  guint                  prop_id,
                                                  GValue                *value,
                                                  GParamSpec            *pspec);
static void     gjs_context_set_property      (GObject               *object,
                                                  guint                  prop_id,
                                                  const GValue          *value,
                                                  GParamSpec            *pspec);
static JSBool gjs_on_context_gc (JSContext *cx,
                                 JSGCStatus status);

struct _GjsContext {
    GObject parent;

    JSRuntime *runtime;
    JSContext *context;
    JSObject *global;

    GjsProfiler *profiler;

    char *jsversion_string;

    char **search_path;

    guint idle_emit_gc_id;

    guint we_own_runtime : 1;
    guint gc_notifications_enabled : 1;
};

struct _GjsContextClass {
    GObjectClass parent;
};

G_DEFINE_TYPE(GjsContext, gjs_context, G_TYPE_OBJECT);

enum {
    SIGNAL_GC,
    LAST_SIGNAL
};

static int signals[LAST_SIGNAL];

enum {
    PROP_0,
    PROP_JS_VERSION,
    PROP_SEARCH_PATH,
    PROP_RUNTIME,
    PROP_GC_NOTIFICATIONS
};


static GStaticMutex contexts_lock = G_STATIC_MUTEX_INIT;
static GList *all_contexts = NULL;


static JSBool
gjs_log(JSContext *context,
        uintN      argc,
        jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *s;
    JSExceptionState *exc_state;
    JSString *jstr;

    if (argc != 1) {
        gjs_throw(context, "Must pass a single argument to log()");
        return JS_FALSE;
    }

    JS_BeginRequest(context);

    /* JS_ValueToString might throw, in which we will only
     *log that the value could be converted to string */
    exc_state = JS_SaveExceptionState(context);
    jstr = JS_ValueToString(context, argv[0]);
    if (jstr != NULL)
        argv[0] = STRING_TO_JSVAL(jstr);    // GC root
    JS_RestoreExceptionState(context, exc_state);

    if (jstr == NULL) {
        gjs_debug(GJS_DEBUG_LOG, "<cannot convert value to string>");
        JS_EndRequest(context);
        return JS_TRUE;
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    gjs_debug(GJS_DEBUG_LOG, "%s", s);
    g_free(s);

    JS_EndRequest(context);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_log_error(JSContext *context,
              uintN      argc,
              jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *s;
    JSExceptionState *exc_state;
    JSString *jstr;
    jsval exc;

    if (argc != 2) {
        gjs_throw(context, "Must pass an exception and message string to logError()");
        return JS_FALSE;
    }

    JS_BeginRequest(context);

    exc = argv[0];

    /* JS_ValueToString might throw, in which we will only
     *log that the value could be converted to string */
    exc_state = JS_SaveExceptionState(context);
    jstr = JS_ValueToString(context, argv[1]);
    if (jstr != NULL)
        argv[1] = STRING_TO_JSVAL(jstr);    // GC root
    JS_RestoreExceptionState(context, exc_state);

    if (jstr == NULL) {
        gjs_debug(GJS_DEBUG_ERROR, "<cannot convert value to string>");
        gjs_log_exception_props(context, exc);
        JS_EndRequest(context);
        return JS_TRUE;
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    gjs_debug(GJS_DEBUG_ERROR, "%s", s);
    gjs_log_exception_props(context, exc);
    g_free(s);

    JS_EndRequest(context);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_print_parse_args(JSContext *context,
                     uintN      argc,
                     jsval     *argv,
                     char     **buffer)
{
    GString *str;
    gchar *s;
    guint n;

    JS_BeginRequest(context);

    str = g_string_new("");
    (void)JS_EnterLocalRootScope(context);
    for (n = 0; n < argc; ++n) {
        JSExceptionState *exc_state;
        JSString *jstr;

        /* JS_ValueToString might throw, in which we will only
         * log that the value could be converted to string */
        exc_state = JS_SaveExceptionState(context);

        jstr = JS_ValueToString(context, argv[n]);
        if (jstr != NULL)
            argv[n] = STRING_TO_JSVAL(jstr); // GC root

        JS_RestoreExceptionState(context, exc_state);

        if (jstr != NULL) {
            if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
                JS_LeaveLocalRootScope(context);
                JS_EndRequest(context);
                g_string_free(str, TRUE);
                return JS_FALSE;
            }

            g_string_append(str, s);
            g_free(s);
            if (n < (argc-1))
                g_string_append_c(str, ' ');
        } else {
            JS_LeaveLocalRootScope(context);
            JS_EndRequest(context);
            *buffer = g_string_free(str, TRUE);
            if (!*buffer)
                *buffer = g_strdup("<invalid string>");
            return JS_TRUE;
        }

    }
    JS_LeaveLocalRootScope(context);
    *buffer = g_string_free(str, FALSE);

    JS_EndRequest(context);
    return JS_TRUE;
}

static JSBool
gjs_print(JSContext *context,
          uintN      argc,
          jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *buffer;

    if (!gjs_print_parse_args(context, argc, argv, &buffer)) {
        return FALSE;
    }

    g_print("%s\n", buffer);
    g_free(buffer);

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_printerr(JSContext *context,
             uintN      argc,
             jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *buffer;

    if (!gjs_print_parse_args(context, argc, argv, &buffer)) {
        return FALSE;
    }

    g_printerr("%s\n", buffer);
    g_free(buffer);

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static void
gjs_context_init(GjsContext *js_context)
{
    js_context->jsversion_string = g_strdup(_GJS_JS_VERSION_DEFAULT);
}

static void
gjs_context_class_init(GjsContextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    object_class->dispose = gjs_context_dispose;
    object_class->finalize = gjs_context_finalize;

    object_class->constructor = gjs_context_constructor;
    object_class->get_property = gjs_context_get_property;
    object_class->set_property = gjs_context_set_property;

    pspec = g_param_spec_boxed("search-path",
                               "Search path",
                               "Path where modules to import should reside",
                               G_TYPE_STRV,
                               G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_property(object_class,
                                    PROP_SEARCH_PATH,
                                    pspec);

    pspec = g_param_spec_pointer("runtime",
                                 "JSRuntime",
                                 "A runtime to use instead of creating our own",
                                 G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_property(object_class,
                                    PROP_RUNTIME,
                                    pspec);

    pspec = g_param_spec_string("js-version",
                                 "JS Version",
                                 "A string giving the default for the (SpiderMonkey) JavaScript version",
                                 _GJS_JS_VERSION_DEFAULT,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_property(object_class,
                                    PROP_JS_VERSION,
                                    pspec);

    pspec = g_param_spec_boolean("gc-notifications",
                                 "",
                                 "Whether or not to emit the \"gc\" signal",
                                 FALSE,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_property(object_class,
                                    PROP_GC_NOTIFICATIONS,
                                    pspec);

    signals[SIGNAL_GC] = g_signal_new("gc", G_TYPE_FROM_CLASS(klass),
                                      G_SIGNAL_RUN_LAST, 0,
                                      NULL, NULL,
                                      NULL,
                                      G_TYPE_NONE, 0);

    gjs_register_native_module("byteArray", gjs_define_byte_array_stuff, 0);
    gjs_register_native_module("_gi", gjs_define_private_gi_stuff, 0);
}

static void
gjs_context_dispose(GObject *object)
{
    GjsContext *js_context;

    js_context = GJS_CONTEXT(object);

    if (js_context->profiler) {
        gjs_profiler_free(js_context->profiler);
        js_context->profiler = NULL;
    }

    if (js_context->global != NULL) {
        js_context->global = NULL;
    }

    if (js_context->context != NULL) {

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Destroying JS context");

        if (js_context->we_own_runtime)
            gjs_runtime_set_default_context(js_context->runtime, NULL);
        JS_DestroyContext(js_context->context);
        js_context->context = NULL;
    }

    if (js_context->runtime != NULL) {
        if (js_context->we_own_runtime) {
            /* Cleans up data as well as destroying the runtime. Foreign
             * contexts aren't supported at the moment; if we supported
             * them, then the data simply would't get cleaned up.
             */
            gjs_runtime_destroy(js_context->runtime);
        }
        js_context->runtime = NULL;
    }

    G_OBJECT_CLASS(gjs_context_parent_class)->dispose(object);
}

static void
gjs_context_finalize(GObject *object)
{
    GjsContext *js_context;

    js_context = GJS_CONTEXT(object);

    if (js_context->idle_emit_gc_id > 0) {
        g_source_remove (js_context->idle_emit_gc_id);
        js_context->idle_emit_gc_id = 0;
    }

    if (js_context->search_path != NULL) {
        g_strfreev(js_context->search_path);
        js_context->search_path = NULL;
    }

    g_free(js_context->jsversion_string);

    g_static_mutex_lock(&contexts_lock);
    all_contexts = g_list_remove(all_contexts, object);
    g_static_mutex_unlock(&contexts_lock);

    G_OBJECT_CLASS(gjs_context_parent_class)->finalize(object);
}

/* Implementations of locale-specific operations; these are used
 * in the implementation of String.localeCompare(), Date.toLocaleDateString(),
 * and so forth. We take the straight-forward approach of converting
 * to UTF-8, using the appropriate GLib functions, and converting
 * back if necessary.
 */
static JSBool
gjs_locale_to_upper_case (JSContext *context,
                          JSString  *src,
                          jsval     *retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *upper_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    upper_case_utf8 = g_utf8_strup (utf8, -1);

    if (!gjs_string_from_utf8(context, upper_case_utf8, -1, retval))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(upper_case_utf8);

    return success;
}

static JSBool
gjs_locale_to_lower_case (JSContext *context,
                          JSString  *src,
                          jsval     *retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *lower_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    lower_case_utf8 = g_utf8_strdown (utf8, -1);

    if (!gjs_string_from_utf8(context, lower_case_utf8, -1, retval))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(lower_case_utf8);

    return success;
}

static JSBool
gjs_locale_compare (JSContext *context,
                    JSString  *src_1,
                    JSString  *src_2,
                    jsval     *retval)
{
    JSBool success = JS_FALSE;
    char *utf8_1 = NULL, *utf8_2 = NULL;
    int result;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src_1), &utf8_1) ||
        !gjs_string_to_utf8(context, STRING_TO_JSVAL(src_2), &utf8_2))
        goto out;

    result = g_utf8_collate (utf8_1, utf8_2);
    *retval = INT_TO_JSVAL(result);

    success = JS_TRUE;

out:
    g_free(utf8_1);
    g_free(utf8_2);

    return success;
}

static JSBool
gjs_locale_to_unicode (JSContext  *context,
                       const char *src,
                       jsval      *retval)
{
    JSBool success;
    char *utf8;
    GError *error = NULL;

    utf8 = g_locale_to_utf8(src, -1, NULL, NULL, &error);
    if (!utf8) {
        gjs_throw(context,
                  "Failed to convert locale string to UTF8: %s",
                  error->message);
        g_error_free(error);
        return JS_FALSE;
    }

    success = gjs_string_from_utf8(context, utf8, -1, retval);
    g_free (utf8);

    return success;
}

static JSLocaleCallbacks gjs_locale_callbacks =
{
    gjs_locale_to_upper_case,
    gjs_locale_to_lower_case,
    gjs_locale_compare,
    gjs_locale_to_unicode
};

static GObject*
gjs_context_constructor (GType                  type,
                         guint                  n_construct_properties,
                         GObjectConstructParam *construct_params)
{
    GObject *object;
    GjsContext *js_context;
    guint32 options_flags;
    JSVersion js_version;

    object = (* G_OBJECT_CLASS (gjs_context_parent_class)->constructor) (type,
                                                                         n_construct_properties,
                                                                         construct_params);

    js_context = GJS_CONTEXT(object);

    if (js_context->runtime == NULL) {
        js_context->runtime = JS_NewRuntime(32*1024*1024 /* max bytes */);
        if (js_context->runtime == NULL)
            gjs_fatal("Failed to create javascript runtime");
        JS_SetGCParameter(js_context->runtime, JSGC_MAX_BYTES, 0xffffffff);
        js_context->we_own_runtime = TRUE;
        gjs_runtime_init(js_context->runtime);
    }

    js_context->context = JS_NewContext(js_context->runtime, 8192 /* stack chunk size */);
    if (js_context->context == NULL)
        gjs_fatal("Failed to create javascript context");

    JS_BeginRequest(js_context->context);

    /* same as firefox, see discussion at
     * https://bugzilla.mozilla.org/show_bug.cgi?id=420869 */
    JS_SetScriptStackQuota(js_context->context, 100*1024*1024);

    /* JSOPTION_DONT_REPORT_UNCAUGHT: Don't send exceptions to our
     * error report handler; instead leave them set.  This allows us
     * to get at the exception object.
     *
     * JSOPTION_STRICT: Report warnings to error reporter function.
     */
    options_flags = JSOPTION_DONT_REPORT_UNCAUGHT | JSOPTION_STRICT;

#ifdef JSOPTION_JIT
    if (!g_getenv("GJS_DISABLE_JIT")) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling JIT");
        options_flags |= JSOPTION_JIT;
    }
#endif

    JS_SetOptions(js_context->context,
                  JS_GetOptions(js_context->context) | options_flags);

    JS_SetLocaleCallbacks(js_context->context, &gjs_locale_callbacks);

    JS_SetErrorReporter(js_context->context, gjs_error_reporter);

    /* set ourselves as the private data */
    JS_SetContextPrivate(js_context->context, js_context);

    js_version = JS_StringToVersion(js_context->jsversion_string);
    /* It doesn't make sense to throw here; just use the default if we
     * don't know.
     */
    if (js_version == JSVERSION_UNKNOWN)
        js_version = JSVERSION_DEFAULT;
    /* Set the version if we need to. */
    if (js_version != JSVERSION_DEFAULT && JS_GetVersion(js_context->context) != js_version) {
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Changing JavaScript version to %s from %s",
                  JS_VersionToString(js_version),
                  JS_VersionToString(JS_GetVersion(js_context->context)));

        JS_SetVersion(js_context->context, js_version);
    }

    if (!gjs_init_context_standard(js_context->context))
        gjs_fatal("Failed to initialize context");
    js_context->global = JS_GetGlobalObject(js_context->context);

    if (!JS_DefineProperty(js_context->context, js_context->global,
                           "window", OBJECT_TO_JSVAL(js_context->global),
                           NULL, NULL,
                           JSPROP_READONLY | JSPROP_PERMANENT))
        gjs_fatal("No memory to export global object as 'window'");

    /* Define a global function called log() */
    if (!JS_DefineFunction(js_context->context, js_context->global,
                           "log",
                           (JSNative)gjs_log,
                           1, GJS_MODULE_PROP_FLAGS))
        gjs_fatal("Failed to define log function");

    if (!JS_DefineFunction(js_context->context, js_context->global,
                           "logError",
                           (JSNative)gjs_log_error,
                           2, GJS_MODULE_PROP_FLAGS))
        gjs_fatal("Failed to define logError function");

    /* Define global functions called print() and printerr() */
    if (!JS_DefineFunction(js_context->context, js_context->global,
                           "print",
                           (JSNative)gjs_print,
                           3, GJS_MODULE_PROP_FLAGS))
        gjs_fatal("Failed to define print function");
    if (!JS_DefineFunction(js_context->context, js_context->global,
                           "printerr",
                           (JSNative)gjs_printerr,
                           4, GJS_MODULE_PROP_FLAGS))
        gjs_fatal("Failed to define printerr function");

    /* We need to know what the default context is, since it's the context whose
     * global object is used to load imported JS modules. We currently say that
     * it's the context of the runtime's owner, but if we needed to support
     * externally created runtimes, we could define it in some other fashion.
     */
    if (js_context->we_own_runtime) {
        gjs_runtime_set_default_context(js_context->runtime, js_context->context);
    } else {
        if (gjs_runtime_get_default_context(js_context->runtime) == NULL)
            gjs_fatal("GjsContext created for a runtime not owned by GJS");
    }

    /* We create the global-to-runtime root importer with the
     * passed-in search path. If someone else already created
     * the root importer, this is a no-op.
     */
    if (!gjs_create_root_importer(js_context->context,
                                  js_context->search_path ?
                                  (const char**) js_context->search_path :
                                  NULL,
                                  TRUE))
        gjs_fatal("Failed to create root importer");

    /* Now copy the global root importer (which we just created,
     * if it didn't exist) to our global object
     */
    if (!gjs_define_root_importer(js_context->context,
                                  js_context->global,
                                  "imports"))
        gjs_fatal("Failed to point 'imports' property at root importer");

    if (js_context->we_own_runtime) {
        js_context->profiler = gjs_profiler_new(js_context->runtime);
    }

    if (!gjs_is_registered_native_module(js_context->context, NULL, "gi"))
        gjs_register_native_module("gi", gjs_define_gi_stuff, GJS_NATIVE_SUPPLIES_MODULE_OBJ);

    /* For GjsDBus */
    {
        char *priv_typelib_dir = g_build_filename (PKGLIBDIR, "girepository-1.0", NULL);
        g_irepository_prepend_search_path(priv_typelib_dir);
        g_free (priv_typelib_dir);
    }

    if (js_context->gc_notifications_enabled)
        JS_SetGCCallback(js_context->context, gjs_on_context_gc);

    JS_EndRequest(js_context->context);

    g_static_mutex_lock (&contexts_lock);
    all_contexts = g_list_prepend(all_contexts, object);
    g_static_mutex_unlock (&contexts_lock);

    return object;
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
    case PROP_JS_VERSION:
        g_value_set_string(value, js_context->jsversion_string);
        break;
    case PROP_GC_NOTIFICATIONS:
        g_value_set_boolean(value, js_context->gc_notifications_enabled);
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
        js_context->search_path = g_value_dup_boxed(value);
        break;
    case PROP_RUNTIME:
        js_context->runtime = g_value_get_pointer(value);
        break;
    case PROP_JS_VERSION:
        g_free(js_context->jsversion_string);
        if (g_value_get_string (value) == NULL)
            js_context->jsversion_string = g_strdup(_GJS_JS_VERSION_DEFAULT);
        else
            js_context->jsversion_string = g_value_dup_string(value);
        break;
    case PROP_GC_NOTIFICATIONS:
        js_context->gc_notifications_enabled = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/**
 * gjs_context_scan_buffer_for_js_version:
 * @buffer: A UTF-8 string
 * @maxbytes: Maximum number of bytes to scan in buffer
 *
 * Given a buffer of JavaScript source code (in UTF-8), look for a
 * comment in it which tells us which version to enable in the
 * SpiderMonkey engine.
 *
 * The comment is inspired by the Firefox MIME type, see e.g.
 * https://developer.mozilla.org/en/JavaScript/New_in_JavaScript/1.8
 *
 * A valid comment string looks like the following, on its own line:
 * <literal>// application/javascript;version=1.8</literal>
 *
 * Returns: A string suitable for use as the GjsContext::version property.
 *   If the version is unknown or invalid, %NULL will be returned.
 */
const char *
gjs_context_scan_buffer_for_js_version (const char *buffer,
                                        gssize      maxbytes)
{
    const char *prefix = "// application/javascript;version=";
    const char *substr;
    JSVersion ver;
    char buf[20];
    gssize remaining_bytes;
    guint i;

    substr = g_strstr_len(buffer, maxbytes, prefix);
    if (!substr)
        return NULL;

    remaining_bytes = maxbytes - ((substr - buffer) + strlen (prefix));
    /* 20 should give us enough space for all the valid JS version strings; anyways
     * it's really a bug if we're close to the limit anyways. */
    if (remaining_bytes < (gssize)sizeof(buf)-1)
        return NULL;

    buf[sizeof(buf)-1] = '\0';
    strncpy(buf, substr + strlen (prefix), sizeof(buf)-1);
    for (i = 0; i < sizeof(buf)-1; i++) {
        if (buf[i] == '\n') {
            buf[i] = '\0';
            break;
        }
    }

    ver = JS_StringToVersion(buf);
    if (ver == JSVERSION_UNKNOWN)
        return NULL;
    return JS_VersionToString(ver);
}

/**
 * gjs_context_scan_file_for_js_version:
 * @file_path: (type filename): A file path
 *
 * Like gjs_context_scan_buffer_for_js_version(), but will open
 * the file and use the initial 1024 bytes as a buffer.
 *
 * Returns: A string suitable for use as GjsContext::version property.
 */
const char *
gjs_context_scan_file_for_js_version (const char *file_path)
{
    char *utf8_buf;
    guint8 buf[1024];
    const char *version = NULL;
    gssize len;
    FILE *f;

    f = fopen(file_path, "r");
    if (!f)
        return NULL;

    len = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    if (len < 0)
        return NULL;
    buf[len] = '\0';

    utf8_buf = _gjs_g_utf8_make_valid((const char*)buf);
    version = gjs_context_scan_buffer_for_js_version(utf8_buf, sizeof(buf));
    g_free(utf8_buf);

    return version;
}


GjsContext*
gjs_context_new(void)
{
    return g_object_new (GJS_TYPE_CONTEXT, NULL);
}

GjsContext*
gjs_context_new_with_search_path(char** search_path)
{
    return g_object_new (GJS_TYPE_CONTEXT,
                         "search-path", search_path,
                         NULL);
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

static gboolean
gjs_context_idle_emit_gc (gpointer data)
{
    GjsContext *gjs_context = data;

    gjs_context->idle_emit_gc_id = 0;
    
    g_signal_emit (gjs_context, signals[SIGNAL_GC], 0);
    
    return FALSE;
}

static JSBool
gjs_on_context_gc (JSContext *cx,
                   JSGCStatus status)
{
    GjsContext *gjs_context = JS_GetContextPrivate(cx);

    if (status == JSGC_END) {
        if (gjs_context->idle_emit_gc_id == 0)
            gjs_context->idle_emit_gc_id = g_idle_add (gjs_context_idle_emit_gc, gjs_context);
    }
    
    return TRUE;
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
  g_static_mutex_lock (&contexts_lock);
  result = g_list_copy(all_contexts);
  for (iter = result; iter; iter = iter->next)
    g_object_ref((GObject*)iter->data);
  g_static_mutex_unlock (&contexts_lock);
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

gboolean
gjs_context_eval(GjsContext *js_context,
                 const char   *script,
                 gssize        script_len,
                 const char   *filename,
                 int          *exit_status_p,
                 GError      **error)
{
    int line_number;
    jsval retval;
    gboolean success;
    gunichar2 *u16_script;
    glong u16_script_len;

    g_object_ref(G_OBJECT(js_context));

    if (exit_status_p)
        *exit_status_p = 1; /* "Failure" (like a shell script) */

    /* whether we evaluated the script OK; not related to whether
     * script returned nonzero. We set GError if success = FALSE
     */
    success = TRUE;

    /* handle scripts with UNIX shebangs */
    line_number = 1;
    if (script != NULL && script[0] == '#' && script[1] == '!') {
        const char *s;

        s = (const char *) strstr (script, "\n");
        if (s != NULL) {
            if (script_len > 0)
                script_len -= (s + 1 - script);
            script = s + 1;
            line_number = 2;
        }
    }

    if ((u16_script = g_utf8_to_utf16 (script, script_len, NULL, &u16_script_len, error)) == NULL)
        return FALSE;
    g_assert (u16_script_len < G_MAXUINT);

    /* log and clear exception if it's set (should not be, normally...) */
    if (gjs_log_exception(js_context->context,
                             NULL)) {
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Exception was set prior to JS_EvaluateScript()");
    }

    /* JS_EvaluateScript requires a request even though it sort of seems like
     * it means we're always in a request?
     */
    gjs_runtime_push_context(js_context->runtime, js_context->context);
    JS_BeginRequest(js_context->context);

    retval = JSVAL_VOID;
    if (!JS_EvaluateUCScript(js_context->context,
                             js_context->global,
                             (const jschar*)u16_script,
                             (guint) u16_script_len,
                             filename,
                             line_number,
                             &retval)) {
        char *message;

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Script evaluation failed");

        /* if message is NULL then somehow exception wasn't set */
        message = NULL;
        gjs_log_exception(js_context->context,
                             &message);
        if (message) {
            g_set_error(error,
                        GJS_ERROR,
                        GJS_ERROR_FAILED,
                        "%s", message);
            g_free(message);
        } else {
            gjs_debug(GJS_DEBUG_CONTEXT,
                      "JS_EvaluateScript() failed but no exception message?");
            g_set_error(error,
                        GJS_ERROR,
                        GJS_ERROR_FAILED,
                        "JS_EvaluateScript() failed but no exception message?");
        }

        success = FALSE;
    }
    g_free (u16_script);

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Script evaluation succeeded");

    if (gjs_log_exception(js_context->context, NULL)) {
        g_set_error(error,
                    GJS_ERROR,
                    GJS_ERROR_FAILED,
                    "Exception was set even though JS_EvaluateScript() returned true - did you gjs_throw() but not return false somewhere perhaps?");
        success = FALSE;
    }

    if (success && exit_status_p) {
        if (JSVAL_IS_INT(retval)) {
            int code;
            if (JS_ValueToInt32(js_context->context, retval, &code)) {

                gjs_debug(GJS_DEBUG_CONTEXT,
                          "Script returned integer code %d", code);

                *exit_status_p = code;
            }
        } else {
            /* Assume success if no integer was returned */
            *exit_status_p = 0;
        }
    }

    JS_EndRequest(js_context->context);
    gjs_runtime_pop_context(js_context->runtime);

    g_object_unref(G_OBJECT(js_context));

    return success;
}

gboolean
gjs_context_eval_file(GjsContext  *js_context,
                      const char    *filename,
                      int           *exit_status_p,
                      GError       **error)
{
    char *script;
    gsize script_len;

    if (!g_file_get_contents(filename, &script, &script_len, error))
        return FALSE;

    if (!gjs_context_eval(js_context, script, script_len, filename, exit_status_p, error)) {
        g_free(script);
        return FALSE;
    }

    g_free(script);
    return TRUE;
}

gboolean
gjs_context_define_string_array(GjsContext  *js_context,
                                const char    *array_name,
                                gssize         array_length,
                                const char   **array_values,
                                GError       **error)
{
    if (!gjs_define_string_array(js_context->context,
                                 js_context->global,
                                 array_name, array_length, array_values,
                                 JSPROP_READONLY | JSPROP_PERMANENT)) {
        char *message;

        message = NULL;
        gjs_log_exception(js_context->context, &message);
        if (message) {
            g_set_error(error,
                        GJS_ERROR,
                        GJS_ERROR_FAILED,
                        "%s", message);
            g_free(message);
        } else {
            message = "gjs_define_string_array() failed but no exception message?";
            gjs_debug(GJS_DEBUG_CONTEXT, "%s", message);
            g_set_error(error,
                        GJS_ERROR,
                        GJS_ERROR_FAILED,
                        "%s", message);
        }
        return FALSE;
    }

    return TRUE;
}

#if GJS_BUILD_TESTS

void
gjstest_test_func_gjs_context_construct_destroy(void)
{
    GjsContext *context;

    /* Construct twice just to possibly a case where global state from
     * the first leaks.
     */
    context = gjs_context_new ();
    g_object_unref (context);

    context = gjs_context_new ();
    g_object_unref (context);
}

void
gjstest_test_func_gjs_context_construct_eval(void)
{
    GjsContext *context;
    int estatus;
    GError *error = NULL;

    context = gjs_context_new ();
    if (!gjs_context_eval (context, "1+1", -1, "<input>", &estatus, &error))
        g_error ("%s", error->message);
    g_object_unref (context);
}

#endif /* GJS_BUILD_TESTS */
