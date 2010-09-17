/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2009 Red Hat, Inc.
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
#include <util/misc.h>

#include "jsapi-util.h"
#include "context-jsapi.h"
#include "compat.h"
#include "jsapi-private.h"

#include <string.h>

GQuark
gjs_util_error_quark (void)
{
    return g_quark_from_static_string ("gjs-util-error-quark");
}

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

    return (JSContext*)gjs_context_get_native_context(context);
}

JSContext*
gjs_runtime_peek_load_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-load-context");
    if (context == NULL) {
        return NULL;
    } else {
        return (JSContext*)gjs_context_get_native_context(context);
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

    return (JSContext*)gjs_context_get_native_context(context);
}

static JSContext*
gjs_runtime_peek_call_context(JSRuntime *runtime)
{
    GjsContext *context;

    context = gjs_runtime_get_data(runtime, "gjs-call-context");
    if (context == NULL) {
        return NULL;
    } else {
        return (JSContext*)gjs_context_get_native_context(context);
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

    JS_BeginRequest(context);

    value = JSVAL_VOID;
    state = JS_SaveExceptionState(context);
    JS_GetProperty(context, obj, property_name, &value);
    JS_RestoreExceptionState(context, state);

    if (value_p)
        *value_p = value;

    JS_EndRequest(context);

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
                            const char      *obj_description,
                            const char      *property_name,
                            jsval           *value_p)
{
    jsval value;

    JS_BeginRequest(context);

    value = JSVAL_VOID;
    JS_GetProperty(context, obj, property_name, &value);

    if (value_p)
        *value_p = value;

    if (value != JSVAL_VOID) {
        JS_ClearPendingException(context); /* in case JS_GetProperty() was on crack */
        JS_EndRequest(context);
        return TRUE;
    } else {
        /* remember gjs_throw() is a no-op if JS_GetProperty()
         * already set an exception
         */
        if (obj_description)
            gjs_throw(context,
                      "No property '%s' in %s (or its value was undefined)",
                      property_name, obj_description);
        else
            gjs_throw(context,
                      "No property '%s' in object %p (or its value was undefined)",
                      property_name, obj);

        JS_EndRequest(context);
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
    JSContext *load_context;

    if (clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    JS_BeginRequest(context);

    /* We replace the passed-in context and global object with our
     * runtime-global permanent load context. Otherwise, in a
     * process with multiple contexts, we'd arbitrarily define
     * the class in whatever global object initialized the
     * class first, which is not desirable.
     */
    load_context = gjs_runtime_get_load_context(JS_GetRuntime(context));
    JS_BeginRequest(load_context);

    /* JS_InitClass() wants to define the constructor in the global object, so
     * we give it a private and namespaced name... passing in the namespace
     * object instead of global object seems to break JS_ConstructObject()
     * which then can't find the constructor for the class. I am probably
     * missing something.
     */
    private_name = g_strdup_printf("_private_%s_%s", ns_name, class_name);

    prototype = NULL;
    if (gjs_object_get_property(load_context, JS_GetGlobalObject(load_context),
                                private_name, &value) &&
        JSVAL_IS_OBJECT(value)) {
        jsval proto_val;

        g_free(private_name); /* don't need it anymore */

        if (!gjs_object_require_property(load_context, JSVAL_TO_OBJECT(value), NULL,
                                         "prototype", &proto_val) ||
            !JSVAL_IS_OBJECT(proto_val)) {
            gjs_throw(load_context, "prototype was not defined or not an object?");
            goto error;
        }
        prototype = JSVAL_TO_OBJECT(proto_val);
    } else {
        DynamicJSClass *class_copy;
        RuntimeData *rd;

        rd = get_data_from_context(load_context);

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

        prototype = JS_InitClass(load_context, JS_GetGlobalObject(load_context),
                                 parent_proto, &class_copy->base,
                                 constructor, nargs,
                                 ps, fs,
                                 static_ps, static_fs);

        /* Retrieve the property again so we can define it in
         * in_object
         */
        if (!gjs_object_require_property(load_context, JS_GetGlobalObject(load_context), NULL,
                                         class_copy->base.name, &value))
            goto error;
    }
    g_assert(value != JSVAL_VOID);
    g_assert(prototype != NULL);

    /* Now manually define our constructor with a sane name, in the
     * namespace object.
     */
    if (!JS_DefineProperty(load_context, in_object,
                           class_name,
                           value,
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        goto error;

    JS_EndRequest(load_context);
    JS_EndRequest(context);
    return prototype;

 error:
    /* Move the exception to the calling context from load context.
     */
    if (!gjs_move_exception(load_context, context)) {
        /* set an exception since none was set */
        gjs_throw(context, "No exception was set, but class initialize failed somehow");
    }

    JS_EndRequest(load_context);
    JS_EndRequest(context);
    return NULL;
}

gboolean
gjs_check_constructing(JSContext *context)
{
    JS_BeginRequest(context);
    if (!JS_IsConstructing(context)) {
        JS_EndRequest(context);
        gjs_throw(context,
                  "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
        return FALSE;
    }

    JS_EndRequest(context);
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
    void *instance;

    if (static_clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    JS_BeginRequest(context);

    obj_class = JS_GET_CLASS(context, obj);
    g_assert(obj_class != NULL);

    rd = get_data_from_context(context);
    g_assert(rd != NULL);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, obj_class) == NULL) {
        gjs_throw(context,
                  "Object %p proto %p doesn't have a dynamically-registered class, it has %s",
                  obj, JS_GetPrototype(context, obj), obj_class->name);
        JS_EndRequest(context);
        return NULL;
    }

    if (static_clasp != ((DynamicJSClass*) obj_class)->static_class) {
        gjs_throw(context, "Object is not a dynamically-registered class based on expected static class pointer");
        JS_EndRequest(context);
        return NULL;
    }

    instance = JS_GetInstancePrivate(context, obj, obj_class, argv);
    JS_EndRequest(context);

    return instance;
}

void*
gjs_get_instance_private_dynamic_with_typecheck(JSContext      *context,
                                                JSObject       *obj,
                                                JSClass        *static_clasp,
                                                jsval          *argv)
{
    RuntimeData *rd;
    JSClass *obj_class;
    void *instance;

    if (static_clasp->name != NULL) {
        g_warning("Dynamic class should not have a name in the JSClass struct");
        return NULL;
    }

    JS_BeginRequest(context);

    obj_class = JS_GET_CLASS(context, obj);
    g_assert(obj_class != NULL);

    rd = get_data_from_context(context);
    g_assert(rd != NULL);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, obj_class) == NULL) {
        JS_EndRequest(context);
        return NULL;
    }

    if (static_clasp != ((DynamicJSClass*) obj_class)->static_class) {
        JS_EndRequest(context);
        return NULL;
    }

    instance = JS_GetInstancePrivate(context, obj, obj_class, argv);
    JS_EndRequest(context);
    return instance;
}

JSObject*
gjs_construct_object_dynamic(JSContext      *context,
                             JSObject       *proto,
                             uintN           argc,
                             jsval          *argv)
{
    RuntimeData *rd;
    JSClass *proto_class;
    JSContext *load_context;
    JSObject *result;

    JS_BeginRequest(context);

    /* We replace the passed-in context and global object with our
     * runtime-global permanent load context. Otherwise, JS_ConstructObject
     * can't find the constructor in whatever random global object is set
     * on the passed-in context.
     */
    load_context = gjs_runtime_get_load_context(JS_GetRuntime(context));
    JS_BeginRequest(load_context);

    proto_class = JS_GET_CLASS(load_context, proto);

    rd = get_data_from_context(load_context);

    /* Check that it's safe to cast to DynamicJSClass */
    if (g_hash_table_lookup(rd->dynamic_classes, proto_class) == NULL) {
        gjs_throw(load_context, "Prototype is not for a dynamically-registered class");
        goto error;
    }

    gjs_debug_lifecycle(GJS_DEBUG_GREPO,
                        "Constructing instance of dynamic class %s %p from proto %p",
                        proto_class->name, proto_class, proto);

    if (argc > 0)
        result = JS_ConstructObjectWithArguments(load_context, proto_class, proto, NULL, argc, argv);
    else
        result = JS_ConstructObject(load_context, proto_class, proto, NULL);

    if (!result)
        goto error;

    JS_EndRequest(load_context);
    JS_EndRequest(context);
    return result;

 error:
    /* Move the exception to the calling context from load context.
     */
    if (!gjs_move_exception(load_context, context)) {
        /* set an exception since none was set */
        gjs_throw(context, "No exception was set, but object construction failed somehow");
    }

    JS_EndRequest(load_context);
    JS_EndRequest(context);
    return NULL;
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

    JS_BeginRequest(context);

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

    JS_EndRequest(context);
    return array;
}

const char*
gjs_value_debug_string(JSContext      *context,
                       jsval           value)
{
    JSString *str;
    const char *bytes;

    JS_BeginRequest(context);

    str = JS_ValueToString(context, value);

    if (str == NULL) {
        if (JSVAL_IS_OBJECT(value)) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            JSClass *klass;

            klass = JS_GET_CLASS(context, JSVAL_TO_OBJECT(value));
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

    bytes = JS_GetStringBytes(str);

    JS_EndRequest(context);

    return bytes;
}

void
gjs_log_object_props(JSContext      *context,
                     JSObject       *obj,
                     GjsDebugTopic   topic,
                     const char     *prefix)
{
    JSObject *props_iter;
    jsid prop_id;

    JS_BeginRequest(context);

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
    JS_EndRequest(context);
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

    JS_BeginRequest(context);
    JS_BeginRequest(load_context);
    JS_BeginRequest(call_context);

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

    JS_EndRequest(call_context);
    JS_EndRequest(load_context);
    JS_EndRequest(context);
}

void
gjs_log_exception_props(JSContext *context,
                        jsval      exc)
{
    JS_BeginRequest(context);

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
    JS_EndRequest(context);
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

    JS_BeginRequest(context);

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

    JS_EndRequest(context);

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

static void
try_to_chain_stack_trace(JSContext *src_context, JSContext *dst_context,
                         jsval src_exc) {
    /* append current stack of dst_context to stack trace for src_exc.
     * we bail if anything goes wrong, just using the src_exc unmodified
     * in that case. */
    jsval chained, src_stack, dst_stack, new_stack;
    JSString *new_stack_str;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dst_context);

    if (!JSVAL_IS_OBJECT(src_exc))
        goto out; // src_exc doesn't have a stack trace

    /* create a new exception in dst_context to get a stack trace */
    gjs_throw_literal(dst_context, "Chained exception");
    if (!(JS_GetPendingException(dst_context, &chained) &&
          JSVAL_IS_OBJECT(chained)))
        goto out; // gjs_throw_literal didn't work?!
    JS_ClearPendingException(dst_context);

    /* get stack trace for src_exc and chained */
    if (!(gjs_object_get_property(dst_context, JSVAL_TO_OBJECT(chained),
                                  "stack", &dst_stack) &&
          JSVAL_IS_STRING(dst_stack)))
        goto out; // couldn't get chained stack
    if (!(gjs_object_get_property(src_context, JSVAL_TO_OBJECT(src_exc),
                                  "stack", &src_stack) &&
          JSVAL_IS_STRING(src_stack)))
        goto out; // couldn't get source stack

    /* add chained exception's stack trace to src_exc */
    new_stack_str = JS_ConcatStrings
        (dst_context, JSVAL_TO_STRING(src_stack), JSVAL_TO_STRING(dst_stack));
    if (new_stack_str==NULL)
        goto out; // couldn't concatenate src and dst stacks?!
    new_stack = STRING_TO_JSVAL(new_stack_str);
    JS_SetProperty(dst_context, JSVAL_TO_OBJECT(src_exc), "stack", &new_stack);

 out:
    JS_EndRequest(dst_context);
    JS_EndRequest(src_context);
}

JSBool
gjs_move_exception(JSContext      *src_context,
                   JSContext      *dest_context)
{
    JSBool success;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dest_context);

    /* NOTE: src and dest could be the same. */
    jsval exc;
    if (JS_GetPendingException(src_context, &exc)) {
        if (src_context != dest_context) {
            /* try to add the current stack of dest_context to the
             * stack trace of exc */
            try_to_chain_stack_trace(src_context, dest_context, exc);
            /* move the exception to dest_context */
            JS_SetPendingException(dest_context, exc);
            JS_ClearPendingException(src_context);
        }
        success = JS_TRUE;
    } else {
        success = JS_FALSE;
    }

    JS_EndRequest(dest_context);
    JS_EndRequest(src_context);

    return success;
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

    JS_BeginRequest(context);

    call_context = gjs_runtime_get_call_context(JS_GetRuntime(context));
    JS_BeginRequest(call_context);

    result = JS_CallFunctionValue(call_context, obj, fval,
                                  argc, argv, rval);
    gjs_move_exception(call_context, context);

    JS_EndRequest(call_context);
    JS_EndRequest(context);
    return result;
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

jsval
gjs_date_from_time_t (JSContext *context, time_t time)
{
    JSObject *date;
    JSClass *date_class;
    JSObject *date_constructor;
    jsval date_prototype;
    jsval args[1];
    jsval result;

    JS_BeginRequest(context);

    if (!JS_EnterLocalRootScope(context))
        return JSVAL_VOID;

    if (!JS_GetClassObject(context, JS_GetGlobalObject(context), JSProto_Date,
                           &date_constructor))
        gjs_fatal("Failed to lookup Date prototype");

    if (!JS_GetProperty(context, date_constructor, "prototype", &date_prototype))
        gjs_fatal("Failed to get prototype from Date constructor");

    date_class = JS_GET_CLASS(context, JSVAL_TO_OBJECT (date_prototype));

    if (!JS_NewNumberValue(context, ((double) time) * 1000, &(args[0])))
        gjs_fatal("Failed to convert time_t to number");

    date = JS_ConstructObjectWithArguments(context, date_class,
                                           NULL, NULL, 1, args);

    result = OBJECT_TO_JSVAL(date);
    JS_LeaveLocalRootScope(context);
    JS_EndRequest(context);

    return result;
}

/**
 * gjs_parse_args:
 * @context:
 * @function_name: The name of the function being called
 * @format: Printf-like format specifier containing the expected arguments
 * @argc: Number of JavaScript arguments
 * @argv: JavaScript argument array
 * @Varargs: for each character in @format, a pair of a char * which is the name
 * of the argument, and a pointer to a location to store the value. The type of
 * value stored depends on the format character, as described below.
 *
 * This function is inspired by Python's PyArg_ParseTuple for those
 * familiar with it.  It takes a format specifier which gives the
 * types of the expected arguments, and a list of argument names and
 * value location pairs.  The currently accepted format specifiers are:
 *
 * s: A string, converted into UTF-8
 * z: Like 's', but may be null in JavaScript (which appears as NULL in C)
 * F: A string, converted into "filename encoding" (i.e. active locale)
 * i: A number, will be converted to a C "gint32"
 * u: A number, converted into a C "guint32"
 * o: A JavaScript object, as a "JSObject *"
 *
 * The '|' character introduces optional arguments.  All format specifiers
 * after a '|' when not specified, do not cause any changes in the C
 * value location.
 */
JSBool
gjs_parse_args (JSContext  *context,
                const char *function_name,
                const char *format,
                uintN      argc,
                jsval     *argv,
                ...)
{
    guint i;
    const char *fmt_iter;
    va_list args;
    GError *arg_error = NULL;
    guint n_unwind = 0;
#define MAX_UNWIND_STRINGS 16
    gpointer unwind_strings[MAX_UNWIND_STRINGS];
    guint n_required;
    guint n_total;
    guint consumed_args;

    JS_BeginRequest(context);

    va_start (args, argv);

    /* Check for optional argument specifier */
    fmt_iter = strchr (format, '|');
    if (fmt_iter) {
        /* Be sure there's not another '|' */
        g_return_val_if_fail (strchr (fmt_iter + 1, '|') == NULL, JS_FALSE);

        n_required = fmt_iter - format;
        n_total = n_required + strlen (fmt_iter + 1);
    } else {
        n_required = n_total = strlen (format);
    }

    if (argc < n_required || argc > n_total) {
        if (n_required == n_total) {
            gjs_throw(context, "Error invoking %s: Expected %d arguments, got %d", function_name,
                      n_required, argc);
        } else {
            gjs_throw(context, "Error invoking %s: Expected minimum %d arguments (and %d optional), got %d", function_name,
                      n_required, n_total - n_required, argc);
        }
        goto error_unwind;
    }

    /* We have 3 iteration variables here.
     * @i: The current integer position in fmt_args
     * @fmt_iter: A pointer to the character in fmt_args
     * @consumed_args: How many arguments we've taken from argv
     *
     * consumed_args can currently be different from 'i' because of the '|' character.
     */
    for (i = 0, consumed_args = 0, fmt_iter = format; *fmt_iter; fmt_iter++, i++) {
        const char *argname;
        gpointer arg_location;
        jsval js_value;
        const char *arg_error_message = NULL;

        if (*fmt_iter == '|')
            continue;

        if (consumed_args == argc) {
            break;
        }

        argname = va_arg (args, char *);
        arg_location = va_arg (args, gpointer);

        g_return_val_if_fail (argname != NULL, JS_FALSE);
        g_return_val_if_fail (arg_location != NULL, JS_FALSE);

        js_value = argv[consumed_args];

        switch (*fmt_iter) {
        case 'o': {
            if (!JSVAL_IS_OBJECT(js_value)) {
                arg_error_message = "Not an object";
            } else {
                JSObject **arg = arg_location;
                *arg = JSVAL_TO_OBJECT(js_value);
            }
        }
            break;
        case 's':
        case 'z': {
            char **arg = arg_location;

            if (*fmt_iter == 'z' && JSVAL_IS_NULL(js_value)) {
                *arg = NULL;
            } else {
                if (gjs_try_string_to_utf8 (context, js_value, arg, &arg_error)) {
                    unwind_strings[n_unwind++] = *arg;
                    g_assert(n_unwind < MAX_UNWIND_STRINGS);
                }
            }
        }
            break;
        case 'F': {
            char **arg = arg_location;

            if (gjs_try_string_to_filename (context, js_value, arg, &arg_error)) {
                unwind_strings[n_unwind++] = *arg;
                g_assert(n_unwind < MAX_UNWIND_STRINGS);
            }
        }
            break;
        case 'i': {
            if (!JS_ValueToInt32(context, js_value, (gint32*) arg_location)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to integer";
            }
        }
            break;
        case 'u': {
            gdouble num;
            if (!JS_ValueToNumber(context, js_value, &num)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to unsigned integer";
            } else if (num > G_MAXUINT32 || num < 0) {
                arg_error_message = "Value is out of range";
            } else {
                *((guint32*) arg_location) = num;
            }
        }
            break;
        case 'f': {
            double num;
            if (!JS_ValueToNumber(context, js_value, &num)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to double";
            } else {
                *((double*) arg_location) = num;
            }
        }
            break;
        default:
            g_assert_not_reached ();
        }

        if (arg_error != NULL)
            arg_error_message = arg_error->message;

        if (arg_error_message != NULL) {
            gjs_throw(context, "Error invoking %s, at argument %d (%s): %s", function_name,
                      consumed_args+1, argname, arg_error_message);
            g_clear_error (&arg_error);
            goto error_unwind;
        }

        consumed_args++;
    }

    va_end (args);

    JS_EndRequest(context);
    return JS_TRUE;

 error_unwind:
    va_end (args);
    /* We still own the strings in the error case, free any we converted */
    for (i = 0; i < n_unwind; i++) {
        g_free (unwind_strings[i]);
    }
    JS_EndRequest(context);
    return JS_FALSE;
}
