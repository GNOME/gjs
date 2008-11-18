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

#include <util/log.h>
#include <util/glib.h>

#include "jsapi-util.h"
#include "context-jsapi.h"

#include <string.h>

typedef struct {
    GHashTable *dynamic_classes;
} RuntimeData;

typedef struct {
    JSClass base;
    JSClass *static_class;
} DynamicJSClass;

void*
gjs_runtime_get_data(JSRuntime      *runtime,
                     const char     *name)
{
    return g_dataset_get_data(runtime, name);
}

void
gjs_runtime_set_data(JSRuntime      *runtime,
                     const char     *name,
                     void           *data,
                     GDestroyNotify  dnotify)
{
    g_dataset_set_data_full(runtime, name, data, dnotify);
}

/* The "load context" is the one we use for loading
 * modules and initializing classes.
 */
JSContext*
gjs_runtime_get_load_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-load-context");
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Creating load context for runtime %p",
                  runtime);
        context = g_object_new(GJS_TYPE_CONTEXT,
                               "runtime", runtime,
                               "is-load-context", TRUE,
                               NULL);
        gjs_runtime_set_data(runtime,
                                "gjs-load-context",
                                context,
                                g_object_unref);
    }

    return gjs_context_get_context(context);
}

static JSContext*
gjs_runtime_peek_load_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-load-context");
    if (context == NULL) {
        return NULL;
    } else {
        return gjs_context_get_context(context);
    }
}

void
gjs_runtime_clear_load_context(JSRuntime *runtime)
{
    gjs_debug(GJS_DEBUG_CONTEXT, "Clearing load context");
    gjs_runtime_set_data(runtime,
                            "gjs-load-context",
                            NULL,
                            NULL);
    gjs_debug(GJS_DEBUG_CONTEXT, "Load context cleared");
}

/* The call context exists because when we call a closure, the scope
 * chain on the context is set to the original scope chain of the
 * closure. We want to avoid using any existing context (especially
 * the load context) because the closure "messes up" the scope chain
 * on the context.
 *
 * Unlike the load context, which is expected to be an eternal
 * singleton, we only cache the call context for efficiency. It would
 * be just as workable to recreate it for each call.
 */
JSContext*
gjs_runtime_get_call_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-call-context");
    if (context == NULL) {
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Creating call context for runtime %p",
                  runtime);
        context = g_object_new(GJS_TYPE_CONTEXT,
                               "runtime", runtime,
                               NULL);
        gjs_runtime_set_data(runtime,
                                "gjs-call-context",
                                context,
                                g_object_unref);
    }

    return gjs_context_get_context(context);
}

static JSContext*
gjs_runtime_peek_call_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-call-context");
    if (context == NULL) {
        return NULL;
    } else {
        return gjs_context_get_context(context);
    }
}

void
gjs_runtime_clear_call_context(JSRuntime *runtime)
{
    gjs_debug(GJS_DEBUG_CONTEXT, "Clearing call context");
    gjs_runtime_set_data(runtime,
                            "gjs-call-context",
                            NULL,
                            NULL);
    gjs_debug(GJS_DEBUG_CONTEXT, "Call context cleared");
}

static void
runtime_data_destroy_notify(void *data)
{
    RuntimeData *rd = data;
    void *key;
    void *value;

    while (gjs_g_hash_table_remove_one(rd->dynamic_classes, &key, &value)) {
        JSClass *clasp = value;

        gjs_debug(GJS_DEBUG_GREPO,
                  "Finalizing dynamic class '%s'",
                  clasp->name);

        g_free( (char*) clasp->name); /* we know we malloc'd the char* even though it's const */
        g_slice_free(DynamicJSClass, (DynamicJSClass*) clasp);
    }

    g_hash_table_destroy(rd->dynamic_classes);
    g_slice_free(RuntimeData, rd);
}

static RuntimeData*
get_data_from_runtime(JSRuntime *runtime)
{
    RuntimeData *rd;

    rd = gjs_runtime_get_data(runtime, "gjs-api-util-data");
    if (rd == NULL) {
        rd = g_slice_new0(RuntimeData);
        rd->dynamic_classes = g_hash_table_new(g_direct_hash, g_direct_equal);
        gjs_runtime_set_data(runtime, "gjs-api-util-data",
                                rd, runtime_data_destroy_notify);
    }

    return rd;
}

static RuntimeData*
get_data_from_context(JSContext *context)
{
    return get_data_from_runtime(JS_GetRuntime(context));
}

/* Checks whether an object has a property; unlike JS_GetProperty(),
 * never sets an exception. Treats a property with a value of JSVAL_VOID
 * the same as an absent property and returns false in both cases.
 */
gboolean
gjs_object_has_property(JSContext  *context,
                           JSObject   *obj,
                           const char *property_name)
{
    return gjs_object_get_property(context, obj, property_name, NULL);
}

/* Checks whether an object has a property; unlike JS_GetProperty(),
 * never sets an exception. Treats a property with a value of JSVAL_VOID
 * the same as an absent property and returns false in both cases.
 * Always initializes *value_p, if only to JSVAL_VOID, even if it
 * returns FALSE.
 */
gboolean
gjs_object_get_property(JSContext  *context,
                           JSObject   *obj,
                           const char *property_name,
                           jsval      *value_p)
{
    jsval value;
    JSExceptionState *state;

    value = JSVAL_VOID;
    state = JS_SaveExceptionState(context);
    JS_GetProperty(context, obj, property_name, &value);
    JS_RestoreExceptionState(context, state);

    if (value_p)
        *value_p = value;

    return value != JSVAL_VOID;
}

/* Returns whether the object had the property; if the object did
 * not have the property, always sets an exception. Treats
 * "the property's value is JSVAL_VOID" the same as "no such property,"
 * while JS_GetProperty() treats only "no such property" as an error.
 * Guarantees that *value_p is set to something, if only JSVAL_VOID,
 * even if an exception is set and false is returned.
 */
gboolean
gjs_object_require_property(JSContext       *context,
                               JSObject        *obj,
                               const char      *property_name,
                               jsval           *value_p)
{
    jsval value;

    value = JSVAL_VOID;
    JS_GetProperty(context, obj, property_name, &value);

    if (value_p)
        *value_p = value;

    if (value != JSVAL_VOID) {
        JS_ClearPendingException(context); /* in case JS_GetProperty() was on crack */
        return TRUE;
    } else {
        /* remember gjs_throw() is a no-op if JS_GetProperty()
         * already set an exception
         */
        gjs_throw(context,
                     "No property '%s' in object %p (or its value was undefined)",
                     property_name, obj);
        return FALSE;
    }
}

JSObject*
gjs_init_class_dynamic(JSContext      *context,
                       JSObject       *in_object,
                       JSObject       *parent_proto,
                       const char     *ns_name,
                       const char     *class_name,
                       JSClass        *clasp,
                       JSNative        constructor,
                       uintN           nargs,
                       JSPropertySpec *ps,
                       JSFunctionSpec *fs,
                       JSPropertySpec *static_ps,
                       JSFunctionSpec *static_fs)
{
    jsval value;
    char *private_name;
    JSObject *prototype;

    if (clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    /* We replace the passed-in context and global object with our
     * runtime-global permanent load context. Otherwise, in a
     * process with multiple contexts, we'd arbitrarily define
     * the class in whatever global object initialized the
     * class first, which is not desirable.
     */
    context = gjs_runtime_get_load_context(JS_GetRuntime(context));

    /* JS_InitClass() wants to define the constructor in the global object, so
     * we give it a private and namespaced name... passing in the namespace
     * object instead of global object seems to break JS_ConstructObject()
     * which then can't find the constructor for the class. I am probably
     * missing something.
     */
    private_name = g_strdup_printf("_private_%s_%s", ns_name, class_name);

    prototype = NULL;
    if (gjs_object_get_property(context, JS_GetGlobalObject(context),
                                   private_name, &value) &&
        JSVAL_IS_OBJECT(value)) {
        jsval proto_val;

        g_free(private_name); /* don't need it anymore */

        if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(value),
                                            "prototype", &proto_val) ||
            !JSVAL_IS_OBJECT(proto_val)) {
            gjs_throw(context, "prototype was not defined or not an object?");
            return NULL;
        }
        prototype = JSVAL_TO_OBJECT(proto_val);
    } else {
        DynamicJSClass *class_copy;
        RuntimeData *rd;

        rd = get_data_from_context(context);

        class_copy = g_slice_new0(DynamicJSClass);
        class_copy->base = *clasp;

        class_copy->base.name = private_name; /* Pass ownership of memory */
        class_copy->static_class = clasp;

        /* record the allocated class to be destroyed with the runtime and so
         * we can do an IS_DYNAMIC_CLASS check
         */
        g_hash_table_replace(rd->dynamic_classes,
                             class_copy, class_copy);

        gjs_debug(GJS_DEBUG_GREPO,
                  "Initializing dynamic class %s %p",
                  class_name, class_copy);

        prototype = JS_InitClass(context, JS_GetGlobalObject(context),
                                 parent_proto, &class_copy->base,
                                 constructor, nargs,
                                 ps, fs,
                                 static_ps, static_fs);

        /* Retrieve the property again so we can define it in
         * in_object
         */
        if (!gjs_object_require_property(context, JS_GetGlobalObject(context),
                                            class_copy->base.name, &value))
            return NULL;
    }
    g_assert(value != JSVAL_VOID);
    g_assert(prototype != NULL);

    /* Now manually define our constructor with a sane name, in the
     * namespace object.
     */
    if (!JS_DefineProperty(context, in_object,
                           class_name,
                           value,
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return NULL;

    return prototype;
}

gboolean
gjs_check_constructing(JSContext *context)
{
    if (!JS_IsConstructing(context)) {
        gjs_throw(context,
                  "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
        return FALSE;
    }

    return TRUE;
}

void*
gjs_get_instance_private_dynamic(JSContext      *context,
                                 JSObject       *obj,
                                 JSClass        *static_clasp,
                                 jsval          *argv)
{
    RuntimeData *rd;
    JSClass *obj_class;

    if (static_clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    obj_class = JS_GetClass(context, obj);
    g_assert(obj_class != NULL);

    rd = get_data_from_context(context);
    g_assert(rd != NULL);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, obj_class) == NULL) {
        gjs_throw(context,
                     "Object %p proto %p doesn't have a dynamically-registered class, it has %s",
                     obj, JS_GetPrototype(context, obj), obj_class->name);
        return NULL;
    }

    if (static_clasp != ((DynamicJSClass*) obj_class)->static_class) {
        gjs_throw(context, "Object is not a dynamically-registered class based on expected static class pointer");
        return NULL;
    }

    return JS_GetInstancePrivate(context, obj, obj_class, argv);
}

void*
gjs_get_instance_private_dynamic_with_typecheck(JSContext      *context,
                                                JSObject       *obj,
                                                JSClass        *static_clasp,
                                                jsval          *argv)
{
    RuntimeData *rd;
    JSClass *obj_class;

    if (static_clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    obj_class = JS_GetClass(context, obj);
    g_assert(obj_class != NULL);

    rd = get_data_from_context(context);
    g_assert(rd != NULL);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, obj_class) == NULL) {
        return NULL;
    }

    if (static_clasp != ((DynamicJSClass*) obj_class)->static_class) {
        return NULL;
    }

    return JS_GetInstancePrivate(context, obj, obj_class, argv);
}

JSObject*
gjs_construct_object_dynamic(JSContext      *context,
                             JSObject       *proto,
                             uintN           argc,
                             jsval          *argv)
{
    RuntimeData *rd;
    JSClass *proto_class;

    /* We replace the passed-in context and global object with our
     * runtime-global permanent load context. Otherwise, JS_ConstructObject
     * can't find the constructor in whatever random global object is set
     * on the passed-in context.
     */
    context = gjs_runtime_get_load_context(JS_GetRuntime(context));

    proto_class = JS_GetClass(context, proto);

    rd = get_data_from_context(context);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, proto_class) == NULL) {
        gjs_throw(context, "Prototype is not for a dynamically-registered class");
        return NULL;
    }

    gjs_debug_lifecycle(GJS_DEBUG_GREPO,
                        "Constructing instance of dynamic class %s %p from proto %p",
                        proto_class->name, proto_class, proto);

    if (argc > 0)
        return JS_ConstructObjectWithArguments(context, proto_class, proto, NULL, argc, argv);
    else
        return JS_ConstructObject(context, proto_class, proto, NULL);
}

JSObject*
gjs_define_string_array(JSContext   *context,
                        JSObject    *in_object,
                        const char  *array_name,
                        gssize       array_length,
                        const char **array_values,
                        uintN        attrs)
{
    GArray *elems;
    JSObject *array;
    int i;

    if (!JS_EnterLocalRootScope(context))
        return JS_FALSE;

    if (array_length == -1)
        array_length = g_strv_length((char**)array_values);

    elems = g_array_sized_new(FALSE, FALSE, sizeof(jsval), array_length);

    for (i = 0; i < array_length; ++i) {
        jsval element;
        element = STRING_TO_JSVAL(JS_NewStringCopyZ(context, array_values[i]));
        g_array_append_val(elems, element);
    }

    array = JS_NewArrayObject(context, elems->len, (jsval*) elems->data);
    g_array_free(elems, TRUE);

    if (array != NULL) {
        if (!JS_DefineProperty(context, in_object,
                               array_name, OBJECT_TO_JSVAL(array),
                               NULL, NULL, attrs))
            array = NULL;
    }

    JS_LeaveLocalRootScope(context);
    return array;
}

const char*
gjs_value_debug_string(JSContext      *context,
                       jsval           value)
{
    JSString *str;

    str = JS_ValueToString(context, value);

    if (str == NULL) {
        if (JSVAL_IS_OBJECT(value)) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            JSClass *klass;

            klass = JS_GetClass(context, JSVAL_TO_OBJECT(value));
            if (klass != NULL) {
                str = JS_NewStringCopyZ(context, klass->name);
                JS_ClearPendingException(context);
                if (str == NULL) {
                    return "[out of memory copying class name]";
                }
            } else {
                gjs_log_exception(context, NULL);
                return "[unknown object]";
            }
        } else {
            return "[unknown non-object]";
        }
    }

    g_assert(str != NULL);

    return JS_GetStringBytes(str);
}

void
gjs_log_object_props(JSContext      *context,
                     JSObject       *obj,
                     GjsDebugTopic   topic,
                     const char     *prefix)
{
    JSObject *props_iter;
    jsid prop_id;

    /* We potentially create new strings, plus the property iterator,
     * that could get collected as we go through this process. So
     * create a local root scope.
     */
    JS_EnterLocalRootScope(context);

    props_iter = JS_NewPropertyIterator(context, obj);
    if (props_iter == NULL) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to create property iterator for object props");
        goto done;
    }

    prop_id = JSVAL_VOID;
    if (!JS_NextProperty(context, props_iter, &prop_id))
        goto done;

    while (prop_id != JSVAL_VOID) {
        jsval nameval;
        const char *name;
        jsval propval;

        if (!JS_IdToValue(context, prop_id, &nameval))
            goto next;

        if (!gjs_get_string_id(nameval, &name))
            goto next;

        if (!gjs_object_get_property(context, obj, name, &propval))
            goto next;

        gjs_debug(topic,
                  "%s%s = '%s'",
                  prefix, name,
                  gjs_value_debug_string(context, propval));

    next:
        prop_id = JSVAL_VOID;
        if (!JS_NextProperty(context, props_iter, &prop_id))
            break;
    }

 done:
    JS_LeaveLocalRootScope(context);
}

void
gjs_explain_scope(JSContext  *context,
                  const char *title)
{
    JSContext *load_context;
    JSContext *call_context;
    JSObject *global;
    JSObject *parent;
    GString *chain;

    gjs_debug(GJS_DEBUG_SCOPE,
              "=== %s ===",
              title);

    load_context = gjs_runtime_peek_load_context(JS_GetRuntime(context));
    call_context = gjs_runtime_peek_call_context(JS_GetRuntime(context));

    JS_EnterLocalRootScope(context);

    gjs_debug(GJS_DEBUG_SCOPE,
              "  Context: %p %s",
              context,
              context == load_context ? "(LOAD CONTEXT)" :
              context == call_context ? "(CALL CONTEXT)" :
              "");

    global = JS_GetGlobalObject(context);
    gjs_debug(GJS_DEBUG_SCOPE,
              "  Global: %p %s",
              global, gjs_value_debug_string(context, OBJECT_TO_JSVAL(global)));

    parent = JS_GetScopeChain(context);
    chain = g_string_new(NULL);
    while (parent != NULL) {
        const char *debug;
        debug = gjs_value_debug_string(context, OBJECT_TO_JSVAL(parent));

        if (chain->len > 0)
            g_string_append(chain, ", ");

        g_string_append_printf(chain, "%p %s",
                               parent, debug);
        parent = JS_GetParent(context, parent);
    }
    gjs_debug(GJS_DEBUG_SCOPE,
              "  Chain: %s",
              chain->str);
    g_string_free(chain, TRUE);

    JS_LeaveLocalRootScope(context);
}

void
gjs_log_exception_props(JSContext *context,
                        jsval      exc)
{
    /* This is useful when the exception was never sent to an error reporter
     * due to JSOPTION_DONT_REPORT_UNCAUGHT, or if the exception was not
     * a normal Error object so jsapi didn't know how to report it sensibly.
     */
    if (JSVAL_IS_NULL(exc)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception was null");
    } else if (JSVAL_IS_OBJECT(exc)) {
        JSObject *exc_obj;

        exc_obj = JSVAL_TO_OBJECT(exc);

        /* I guess this is a SpiderMonkey bug.  If we don't get these
         * properties here, only 'message' shows up when we enumerate
         * all properties below. I did not debug in detail, so maybe
         * it's something wrong with our enumeration loop below. In
         * any case, if you remove this code block, check that "throw
         * Error()" still results in printing all four of these props.
         * For me right now, if you remove this block, only message
         * gets printed.
         */
        gjs_object_has_property(context, exc_obj, "stack");
        gjs_object_has_property(context, exc_obj, "fileName");
        gjs_object_has_property(context, exc_obj, "lineNumber");
        gjs_object_has_property(context, exc_obj, "message");

        gjs_log_object_props(context, exc_obj,
                                GJS_DEBUG_ERROR,
                                "  ");
    } else if (JSVAL_IS_STRING(exc)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception was a String");
    } else {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception had some strange type");
    }
}

static JSBool
log_and_maybe_keep_exception(JSContext  *context,
                             char      **message_p,
                             gboolean    keep)
{
    jsval exc = JSVAL_VOID;
    JSString *s;
    char *message;
    JSBool retval = JS_FALSE;

    if (message_p)
        *message_p = NULL;

    JS_AddRoot(context, &exc);
    if (!JS_GetPendingException(context, &exc))
        goto out;

    JS_ClearPendingException(context);

    s = JS_ValueToString(context, exc);

    if (s == NULL) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to convert exception to string");
        goto out; /* Exception should be thrown already */
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(s), &message)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to convert exception string to UTF-8");
        goto out; /* Error already set */
    }

    gjs_debug(GJS_DEBUG_ERROR,
              "Exception was: %s",
              message);

    if (message_p) {
        *message_p = message;
    } else {
        g_free(message);
    }

    gjs_log_exception_props(context, exc);

    /* We clear above and then set it back so any exceptions
     * from the logging process don't overwrite the original
     */
    if (keep)
        JS_SetPendingException(context, exc);

    retval = JS_TRUE;

 out:
    JS_RemoveRoot(context, &exc);

    return retval;
}

JSBool
gjs_log_exception(JSContext  *context,
                  char      **message_p)
{
    return log_and_maybe_keep_exception(context, message_p, FALSE);
}

JSBool
gjs_log_and_keep_exception(JSContext *context,
                           char     **message_p)
{
    return log_and_maybe_keep_exception(context, message_p, TRUE);
}

JSBool
gjs_move_exception(JSContext      *src_context,
                   JSContext      *dest_context)
{
    /* NOTE: src and dest could be the same. */
    jsval exc;
    if (JS_GetPendingException(src_context, &exc)) {
        if (src_context != dest_context) {
            JS_SetPendingException(dest_context, exc);
            JS_ClearPendingException(src_context);
        }
        return JS_TRUE;
    } else {
        return JS_FALSE;
    }
}

JSBool
gjs_call_function_value(JSContext      *context,
                        JSObject       *obj,
                        jsval           fval,
                        uintN           argc,
                        jsval          *argv,
                        jsval          *rval)
{
    JSBool result;
    JSContext *call_context;

    call_context = gjs_runtime_get_call_context(JS_GetRuntime(context));

    result = JS_CallFunctionValue(call_context, obj, fval,
                                  argc, argv, rval);
    gjs_move_exception(call_context, context);

    return result;
}

void
gjs_error_reporter(JSContext     *context,
                   const char    *message,
                   JSErrorReport *report)
{
    const char *warning;

    if ((report->flags & JSREPORT_WARNING) != 0) {
        /* We manually insert "WARNING" into the output instead of
         * having GJS_DEBUG_WARNING because it's convenient to
         * search for 'JS ERROR' to find all problems
         */
        warning = "WARNING: ";

        /* suppress bogus warnings. See mozilla/js/src/js.msg */
        switch (report->errorNumber) {
            /* 162, JSMSG_UNDEFINED_PROP: warns every time a lazy property
             * is resolved, since the property starts out
             * undefined. When this is a real bug it should usually
             * fail somewhere else anyhow.
             */
        case 162:
            return;
        }
    } else {
        warning = "REPORTED: ";
    }

    gjs_debug(GJS_DEBUG_ERROR,
              "%s'%s'",
              warning,
              message);

    gjs_debug(GJS_DEBUG_ERROR,
              "%sfile '%s' line %u exception %d number %d",
              warning,
              report->filename, report->lineno,
              (report->flags & JSREPORT_EXCEPTION) != 0,
              report->errorNumber);
}

static JSBool
log_prop(JSContext  *context,
         JSObject   *obj,
         jsval       id,
         jsval      *value_p,
         const char *what)
{
    if (JSVAL_IS_STRING(id)) {
        const char *name;

        name = gjs_string_get_ascii(id);
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %s: %s",
                  name, what);
    } else if (JSVAL_IS_INT(id)) {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %d: %s",
                  JSVAL_TO_INT(id), what);
    } else {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop not-sure-what: %s",
                  what);
    }

    return JS_TRUE;
}

JSBool
gjs_get_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "get");
}

JSBool
gjs_set_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "set");
}

JSBool
gjs_add_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "add");
}

JSBool
gjs_delete_prop_verbose_stub(JSContext *context,
                             JSObject  *obj,
                             jsval      id,
                             jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "delete");
}

/* get a debug string for type tag in jsval */
const char*
gjs_get_type_name(jsval value)
{
    if (JSVAL_IS_NULL(value)) {
        return "null";
    } else if (value == JSVAL_VOID) {
        return "undefined";
    } else if (JSVAL_IS_INT(value)) {
        return "integer";
    } else if (JSVAL_IS_DOUBLE(value)) {
        return "double";
    } else if (JSVAL_IS_BOOLEAN(value)) {
        return "boolean";
    } else if (JSVAL_IS_STRING(value)) {
        return "string";
    } else if (JSVAL_IS_OBJECT(value)) {
        return "object";
    } else {
        return "<unknown>";
    }
}
