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

#include <gio/gio.h>

#include "context-private.h"
#include "importer.h"
#include "jsapi-private.h"
#include "jsapi-util.h"
#include "native.h"
#include "byteArray.h"
#include "compat.h"
#include "runtime.h"

#include "gi.h"
#include "gi/object.h"

#include <modules/modules.h>

#include <util/log.h>
#include <util/glib.h>
#include <util/error.h>

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
struct _GjsContext {
    GObject parent;

    JSRuntime *runtime;
    JSContext *context;
    JSObject *global;

    char *program_name;

    char **search_path;

    gboolean destroying;

    guint    auto_gc_id;

    jsid const_strings[GJS_STRING_LAST];
};

/* Keep this consistent with GjsConstString */
static const char *const_strings[] = {
    "constructor", "prototype", "length",
    "imports", "__parentModule__", "__init__", "searchPath",
    "__gjsKeepAlive", "__gjsPrivateNS",
    "gi", "versions", "overrides",
    "_init", "_new_internal", "new",
    "message", "code", "stack", "fileName", "lineNumber", "name",
    "x", "y", "width", "height",
};

G_STATIC_ASSERT(G_N_ELEMENTS(const_strings) == GJS_STRING_LAST);

struct _GjsContextClass {
    GObjectClass parent;
};

G_DEFINE_TYPE(GjsContext, gjs_context, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_SEARCH_PATH,
    PROP_PROGRAM_NAME,
};

static GMutex contexts_lock;
static GList *all_contexts = NULL;

static JSBool
gjs_log(JSContext *context,
        unsigned   argc,
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
        g_message("JS LOG: <cannot convert value to string>");
        JS_EndRequest(context);
        return JS_TRUE;
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(jstr), &s)) {
        JS_EndRequest(context);
        return JS_FALSE;
    }

    g_message("JS LOG: %s", s);
    g_free(s);

    JS_EndRequest(context);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_log_error(JSContext *context,
              unsigned   argc,
              jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSExceptionState *exc_state;
    JSString *jstr;

    if ((argc != 1 && argc != 2) ||
        !JSVAL_IS_OBJECT (argv[0])) {
        gjs_throw(context, "Must pass an exception and optionally a message to logError()");
        return JS_FALSE;
    }

    JS_BeginRequest(context);

    if (argc == 2) {
        /* JS_ValueToString might throw, in which we will only
         *log that the value could be converted to string */
        exc_state = JS_SaveExceptionState(context);
        jstr = JS_ValueToString(context, argv[1]);
        if (jstr != NULL)
            argv[1] = STRING_TO_JSVAL(jstr);    // GC root
        JS_RestoreExceptionState(context, exc_state);
    } else {
        jstr = NULL;
    }

    gjs_log_exception_full(context, argv[0], jstr);

    JS_EndRequest(context);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_print_parse_args(JSContext *context,
                     unsigned   argc,
                     jsval     *argv,
                     char     **buffer)
{
    GString *str;
    gchar *s;
    guint n;

    JS_BeginRequest(context);

    str = g_string_new("");
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
                JS_EndRequest(context);
                g_string_free(str, TRUE);
                return JS_FALSE;
            }

            g_string_append(str, s);
            g_free(s);
            if (n < (argc-1))
                g_string_append_c(str, ' ');
        } else {
            JS_EndRequest(context);
            *buffer = g_string_free(str, TRUE);
            if (!*buffer)
                *buffer = g_strdup("<invalid string>");
            return JS_TRUE;
        }

    }
    *buffer = g_string_free(str, FALSE);

    JS_EndRequest(context);
    return JS_TRUE;
}

static JSBool
gjs_print(JSContext *context,
          unsigned   argc,
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
             unsigned   argc,
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

    pspec = g_param_spec_string("program-name",
                                "Program Name",
                                "The filename of the launched JS program",
                                "",
                                (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
                                    PROP_PROGRAM_NAME,
                                    pspec);

    /* For GjsPrivate */
    {
        char *priv_typelib_dir = g_build_filename (PKGLIBDIR, "girepository-1.0", NULL);
        g_irepository_prepend_search_path(priv_typelib_dir);
    g_free (priv_typelib_dir);
    }

    gjs_register_native_module("byteArray", gjs_define_byte_array_stuff);
    gjs_register_native_module("_gi", gjs_define_private_gi_stuff);
    gjs_register_native_module("gi", gjs_define_gi_stuff);

    gjs_register_static_modules();
}

static void
gjs_context_dispose(GObject *object)
{
    GjsContext *js_context;

    js_context = GJS_CONTEXT(object);

    if (js_context->global != NULL) {
        js_context->global = NULL;
    }

    if (js_context->context != NULL) {

        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Destroying JS context");

        JS_BeginRequest(js_context->context);
        /* Do a full GC here before tearing down, since once we do
         * that we may not have the JS_GetPrivate() to access the
         * context
         */
        JS_GC(js_context->runtime);
        JS_EndRequest(js_context->context);

        js_context->destroying = TRUE;

        /* Now, release all native objects, to avoid recursion between
         * the JS teardown and the C teardown.  The JSObject proxies
         * still exist, but point to NULL.
         */
        gjs_object_prepare_shutdown(js_context->context);

        if (js_context->auto_gc_id > 0) {
            g_source_remove (js_context->auto_gc_id);
            js_context->auto_gc_id = 0;
        }

        /* Tear down JS */
        JS_DestroyContext(js_context->context);
        js_context->context = NULL;
        js_context->runtime = NULL;
    }

    G_OBJECT_CLASS(gjs_context_parent_class)->dispose(object);
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

    G_OBJECT_CLASS(gjs_context_parent_class)->finalize(object);
}

static JSFunctionSpec global_funcs[] = {
    { "log", JSOP_WRAPPER (gjs_log), 1, GJS_MODULE_PROP_FLAGS },
    { "logError", JSOP_WRAPPER (gjs_log_error), 2, GJS_MODULE_PROP_FLAGS },
    { "print", JSOP_WRAPPER (gjs_print), 0, GJS_MODULE_PROP_FLAGS },
    { "printerr", JSOP_WRAPPER (gjs_printerr), 0, GJS_MODULE_PROP_FLAGS },
    { NULL },
};

static void
gjs_context_constructed(GObject *object)
{
    GjsContext *js_context = GJS_CONTEXT(object);
    int i;

    G_OBJECT_CLASS(gjs_context_parent_class)->constructed(object);

    js_context->runtime = gjs_runtime_for_current_thread();

    js_context->context = JS_NewContext(js_context->runtime, 8192 /* stack chunk size */);
    if (js_context->context == NULL)
        g_error("Failed to create javascript context");

    for (i = 0; i < GJS_STRING_LAST; i++)
        js_context->const_strings[i] = gjs_intern_string_to_id(js_context->context, const_strings[i]);

    JS_BeginRequest(js_context->context);

    /* set ourselves as the private data */
    JS_SetContextPrivate(js_context->context, js_context);

    if (!gjs_init_context_standard(js_context->context))
        g_error("Failed to initialize context");

    js_context->global = JS_GetGlobalObject(js_context->context);
    JSAutoCompartment ac(js_context->context, js_context->global);

    if (!JS_DefineProperty(js_context->context, js_context->global,
                           "window", OBJECT_TO_JSVAL(js_context->global),
                           NULL, NULL,
                           JSPROP_READONLY | JSPROP_PERMANENT))
        g_error("No memory to export global object as 'window'");

    if (!JS_DefineFunctions(js_context->context, js_context->global, &global_funcs[0]))
        g_error("Failed to define properties on the global object");

    /* We create the global-to-runtime root importer with the
     * passed-in search path. If someone else already created
     * the root importer, this is a no-op.
     */
    if (!gjs_create_root_importer(js_context->context,
                                  js_context->search_path ?
                                  (const char**) js_context->search_path :
                                  NULL,
                                  TRUE))
        g_error("Failed to create root importer");

    /* Now copy the global root importer (which we just created,
     * if it didn't exist) to our global object
     */
    if (!gjs_define_root_importer(js_context->context,
                                  js_context->global))
        g_error("Failed to point 'imports' property at root importer");

    JS_EndRequest(js_context->context);

    g_mutex_lock (&contexts_lock);
    all_contexts = g_list_prepend(all_contexts, object);
    g_mutex_unlock (&contexts_lock);
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

gboolean
_gjs_context_destroying (GjsContext *context)
{
    return context->destroying;
}

static gboolean
trigger_gc_if_needed (gpointer user_data)
{
    GjsContext *js_context = GJS_CONTEXT(user_data);
    js_context->auto_gc_id = 0;
    gjs_gc_if_needed(js_context->context);
    return FALSE;
}

void
_gjs_context_schedule_gc_if_needed (GjsContext *js_context)
{
    if (js_context->auto_gc_id > 0)
        return;

    js_context->auto_gc_id = g_idle_add_full(G_PRIORITY_LOW,
                                             trigger_gc_if_needed,
                                             js_context, NULL);
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
    JS_GC(context->runtime);
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

gboolean
gjs_context_eval(GjsContext   *js_context,
                 const char   *script,
                 gssize        script_len,
                 const char   *filename,
                 int          *exit_status_p,
                 GError      **error)
{
    gboolean ret = FALSE;
    jsval retval;

    JSAutoCompartment ac(js_context->context, js_context->global);
    JSAutoRequest ar(js_context->context);

    g_object_ref(G_OBJECT(js_context));

    if (!gjs_eval_with_scope(js_context->context, NULL, script, script_len, filename, &retval)) {
        gjs_log_exception(js_context->context);
        g_set_error(error,
                    GJS_ERROR,
                    GJS_ERROR_FAILED,
                    "JS_EvaluateScript() failed");
        goto out;
    }

    if (exit_status_p) {
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

    ret = TRUE;

 out:
    g_object_unref(G_OBJECT(js_context));
    return ret;
}

gboolean
gjs_context_eval_file(GjsContext    *js_context,
                      const char    *filename,
                      int           *exit_status_p,
                      GError       **error)
{
    char     *script = NULL;
    gsize    script_len;
    gboolean ret = TRUE;

    GFile *file = g_file_new_for_commandline_arg(filename);

    if (!g_file_query_exists(file, NULL)) {
        ret = FALSE;
        goto out;
    }

    if (!g_file_load_contents(file, NULL, &script, &script_len, NULL, error)) {
        ret = FALSE;
        goto out;
    }

    if (!gjs_context_eval(js_context, script, script_len, filename, exit_status_p, error)) {
        ret = FALSE;
        goto out;
    }

out:
    g_free(script);
    g_object_unref(file);
    return ret;
}

gboolean
gjs_context_define_string_array(GjsContext  *js_context,
                                const char    *array_name,
                                gssize         array_length,
                                const char   **array_values,
                                GError       **error)
{
    JSAutoCompartment ac(js_context->context, js_context->global);
    if (!gjs_define_string_array(js_context->context,
                                 js_context->global,
                                 array_name, array_length, array_values,
                                 JSPROP_READONLY | JSPROP_PERMANENT)) {
        gjs_log_exception(js_context->context);
        g_set_error(error,
                    GJS_ERROR,
                    GJS_ERROR_FAILED,
                    "gjs_define_string_array() failed");
        return FALSE;
    }

    return TRUE;
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

jsid
gjs_context_get_const_string(JSContext      *context,
                             GjsConstString  name)
{
    GjsContext *gjs_context = (GjsContext *) JS_GetContextPrivate(context);
    return gjs_context->const_strings[name];
}

gboolean
gjs_object_get_property_const(JSContext      *context,
                              JSObject       *obj,
                              GjsConstString  property_name,
                              jsval          *value_p)
{
    jsid pname;
    pname = gjs_context_get_const_string(context, property_name);
    return JS_GetPropertyById(context, obj, pname, value_p);
}
