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

#include "keep-alive.h"

#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>
#include <util/glib.h>

typedef struct {
    GjsUnrootedFunc notify;
    JSObject *child;
    void *data;
} Child;

typedef struct {
    GHashTable *children;
    unsigned int inside_finalize : 1;
    unsigned int inside_trace : 1;
} KeepAlive;

extern struct JSClass gjs_keep_alive_class;

GJS_DEFINE_PRIV_FROM_JS(KeepAlive, gjs_keep_alive_class)

static guint
child_hash(gconstpointer  v)
{
    const Child *child = (const Child *) v;

    return
        GPOINTER_TO_UINT(child->notify) ^
        GPOINTER_TO_UINT(child->child) ^
        GPOINTER_TO_UINT(child->data);
}

static gboolean
child_equal (gconstpointer  v1,
             gconstpointer  v2)
{
    const Child *child1 = (const Child *) v1;
    const Child *child2 = (const Child *) v2;

    /* notify is most likely to be equal, so check it last */
    return child1->data == child2->data &&
        child1->child == child2->child &&
        child1->notify == child2->notify;
}

static void
child_free(void *data)
{
    Child *child = (Child *) data;
    g_slice_free(Child, child);
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(keep_alive)

static void
keep_alive_finalize(JSFreeOp *fop,
                    JSObject *obj)
{
    KeepAlive *priv;
    void *key;
    void *value;

    priv = (KeepAlive *) JS_GetPrivate(obj);

    gjs_debug_lifecycle(GJS_DEBUG_KEEP_ALIVE,
                        "keep_alive finalizing, obj %p priv %p", obj, priv);

    if (priv == NULL)
        return; /* we are the prototype, not a real instance */

    priv->inside_finalize = TRUE;

    while (gjs_g_hash_table_steal_one(priv->children,
                                      &key, &value)) {
        Child *child = (Child *) value;
        if (child->notify)
            (* child->notify) (child->child, child->data);

        child_free(child);
    }

    g_hash_table_destroy(priv->children);
    g_slice_free(KeepAlive, priv);
}

static void
trace_foreach(void *key,
              void *value,
              void *data)
{
    Child *child = (Child *) value;
    JSTracer *tracer = (JSTracer *) data;

    if (child->child != NULL) {
        jsval val;
        JS_SET_TRACING_DETAILS(tracer, NULL, "keep-alive", 0);
        val = OBJECT_TO_JSVAL(child->child);
        g_assert (JSVAL_TO_TRACEABLE (val));
        JS_CallValueTracer(tracer, &val, "keep-alive::val");
    }
}

static void
keep_alive_trace(JSTracer *tracer,
                 JSObject *obj)
{
    KeepAlive *priv;

    priv = (KeepAlive *) JS_GetPrivate(obj);

    if (priv == NULL) /* prototype */
        return;

    g_assert(!priv->inside_trace);
    priv->inside_trace = TRUE;
    g_hash_table_foreach(priv->children, trace_foreach, tracer);
    priv->inside_trace = FALSE;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_keep_alive_class = {
    "__private_GjsKeepAlive", /* means "new __private_GjsKeepAlive()" works */
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    keep_alive_finalize,
    NULL,
    NULL,
    NULL,
    NULL,
    keep_alive_trace,
};

JSPropertySpec gjs_keep_alive_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_keep_alive_proto_funcs[] = {
    { NULL }
};

JSObject*
gjs_keep_alive_new(JSContext *context)
{
    KeepAlive *priv;
    JSObject *keep_alive = NULL;
    JSObject *global;
    JSBool found;

    /* This function creates an unattached KeepAlive object; following our
     * general strategy, we have a single KeepAlive class with a constructor
     * stored on our single "load global" pseudo-global object, and we create
     * instances with the load global as parent.
     */

    g_assert(context != NULL);

    JS_BeginRequest(context);

    global = gjs_get_import_global(context);

    g_assert(global != NULL);

    if (!JS_HasProperty(context, global, gjs_keep_alive_class.name, &found))
        goto out;

    if (!found) {
        JSObject *prototype;

        gjs_debug(GJS_DEBUG_KEEP_ALIVE,
                  "Initializing keep-alive class in context %p global %p",
                  context, global);

        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 NULL,
                                 &gjs_keep_alive_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 gjs_keep_alive_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_keep_alive_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_keep_alive_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            g_error("Can't init class %s", gjs_keep_alive_class.name);

        gjs_debug(GJS_DEBUG_KEEP_ALIVE, "Initialized class %s prototype %p",
                  gjs_keep_alive_class.name, prototype);
    }

    gjs_debug(GJS_DEBUG_KEEP_ALIVE,
              "Creating new keep-alive object for context %p global %p",
              context, global);

    keep_alive = JS_NewObject(context, &gjs_keep_alive_class, NULL, global);
    if (keep_alive == NULL) {
        gjs_log_exception(context);
        g_error("Failed to create keep_alive object");
    }

    priv = g_slice_new0(KeepAlive);
    priv->children = g_hash_table_new_full(child_hash, child_equal, NULL, child_free);

    g_assert(priv_from_js(context, keep_alive) == NULL);
    JS_SetPrivate(keep_alive, priv);

    gjs_debug_lifecycle(GJS_DEBUG_KEEP_ALIVE,
                        "keep_alive constructor, obj %p priv %p", keep_alive, priv);

 out:
    JS_EndRequest(context);

    return keep_alive;
}

void
gjs_keep_alive_add_child(JSObject          *keep_alive,
                         GjsUnrootedFunc    notify,
                         JSObject          *obj,
                         void              *data)
{
    KeepAlive *priv;
    Child *child;

    g_assert(keep_alive != NULL);
    priv = (KeepAlive *) JS_GetPrivate(keep_alive);
    g_assert(priv != NULL);

    g_return_if_fail(!priv->inside_trace);
    g_return_if_fail(!priv->inside_finalize);

    child = g_slice_new0(Child);
    child->notify = notify;
    child->child = obj;
    child->data = data;

    /* this is sort of an expensive check, probably */
    g_return_if_fail(g_hash_table_lookup(priv->children, child) == NULL);

    /* this overwrites any identical-by-value previous child,
     * but there should not be one.
     */
    g_hash_table_replace(priv->children, child, child);
}

void
gjs_keep_alive_remove_child(JSObject          *keep_alive,
                            GjsUnrootedFunc    notify,
                            JSObject          *obj,
                            void              *data)
{
    KeepAlive *priv;
    Child child;

    g_assert(keep_alive != NULL);
    priv = (KeepAlive *) JS_GetPrivate(keep_alive);
    g_assert(priv != NULL);

    g_return_if_fail(!priv->inside_trace);
    g_return_if_fail(!priv->inside_finalize);

    child.notify = notify;
    child.child = obj;
    child.data = data;

    g_hash_table_remove(priv->children, &child);
}

static JSObject*
gjs_keep_alive_create(JSContext *context)
{
    JSObject *keep_alive;

    JS_BeginRequest(context);

    keep_alive = gjs_keep_alive_new(context);
    if (!keep_alive)
        g_error("could not create keep_alive on global object, no memory?");

    gjs_set_global_slot(context, GJS_GLOBAL_SLOT_KEEP_ALIVE, OBJECT_TO_JSVAL(keep_alive));

    JS_EndRequest(context);
    return keep_alive;
}

JSObject*
gjs_keep_alive_get_global_if_exists (JSContext *context)
{
    jsval keep_alive;

    keep_alive = gjs_get_global_slot(context, GJS_GLOBAL_SLOT_KEEP_ALIVE);

    if (G_LIKELY (JSVAL_IS_OBJECT(keep_alive)))
        return JSVAL_TO_OBJECT(keep_alive);
    
    return NULL;
}

JSObject*
gjs_keep_alive_get_global(JSContext *context)
{
    JSObject *keep_alive = gjs_keep_alive_get_global_if_exists(context);

    if (G_LIKELY(keep_alive))
        return keep_alive;

    return gjs_keep_alive_create(context);
}

void
gjs_keep_alive_add_global_child(JSContext         *context,
                                GjsUnrootedFunc  notify,
                                JSObject          *child,
                                void              *data)
{
    JSObject *keep_alive;

    JS_BeginRequest(context);

    keep_alive = gjs_keep_alive_get_global(context);

    gjs_keep_alive_add_child(keep_alive, notify, child, data);

    JS_EndRequest(context);
}

void
gjs_keep_alive_remove_global_child(JSContext         *context,
                                   GjsUnrootedFunc  notify,
                                   JSObject          *child,
                                   void              *data)
{
    JSObject *keep_alive;

    JS_BeginRequest(context);

    keep_alive = gjs_keep_alive_get_global(context);

    if (!keep_alive)
        g_error("no keep_alive property on the global object, have you "
                "previously added this child?");

    gjs_keep_alive_remove_child(keep_alive, notify, child, data);

    JS_EndRequest(context);
}

typedef struct {
    GHashTableIter hashiter;
} GjsRealKeepAliveIter;

void
gjs_keep_alive_iterator_init (GjsKeepAliveIter *iter,
                              JSObject         *keep_alive)
{
    GjsRealKeepAliveIter *real = (GjsRealKeepAliveIter*)iter;
    KeepAlive *priv = (KeepAlive *) JS_GetPrivate(keep_alive);
    g_assert(priv != NULL);
    g_hash_table_iter_init(&real->hashiter, priv->children);
}

gboolean
gjs_keep_alive_iterator_next (GjsKeepAliveIter  *iter,
                              GjsUnrootedFunc    notify_func,
                              JSObject         **out_child,
                              void             **out_data)
{
    GjsRealKeepAliveIter *real = (GjsRealKeepAliveIter*)iter;
    gpointer k, v;
    gboolean ret = FALSE;

    while (g_hash_table_iter_next(&real->hashiter, &k, &v)) {
        Child *child = (Child*)k;

        if (child->notify != notify_func)
            continue;

        ret = TRUE;
        *out_child = child->child;
        *out_data = child->data;
        break;
    }

    return ret;
}
