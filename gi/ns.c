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

#include "ns.h"
#include "repo.h"
#include "param.h"
#include <gjs/gjs.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>

#include <string.h>

typedef struct {
    GIRepository *repo;
    char *namespace;

} Ns;

static struct JSClass gjs_ns_class;

GJS_DEFINE_PRIV_FROM_JS(Ns, gjs_ns_class)

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
ns_new_resolve(JSContext *context,
               JSObject  *obj,
               jsid       id,
               uintN      flags,
               JSObject **objp)
{
    Ns *priv;
    const char *name;
    GIRepository *repo;
    GIBaseInfo *info;
    JSContext *load_context;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0)
        return JS_TRUE;

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GNAMESPACE, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_TRUE; /* we are the prototype, or have the wrong class */

    load_context = gjs_runtime_get_load_context(JS_GetRuntime(context));
    JS_BeginRequest(load_context);

    repo = g_irepository_get_default();

    info = g_irepository_find_by_name(repo, priv->namespace, name);
    if (info == NULL) {
        /* Special-case fallback hack for GParamSpec */
        if (strcmp(name, "ParamSpec") == 0 &&
            strcmp(priv->namespace, "GLib") == 0) {
            gjs_define_param_class(load_context,
                                   obj,
                                   NULL);
            if (gjs_move_exception(load_context, context)) {
                JS_EndRequest(load_context);
                return JS_FALSE;
            } else {
                *objp = obj; /* we defined the property in this object */
                JS_EndRequest(load_context);
                return JS_TRUE;
            }
        } else {
            gjs_throw(context,
                      "No symbol '%s' in namespace '%s'",
                      name, priv->namespace);
            JS_EndRequest(load_context);
            return JS_FALSE;
        }
    }

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Found info type %s for '%s' in namespace '%s'",
              gjs_info_type_name(g_base_info_get_type(info)),
              g_base_info_get_name(info),
              g_base_info_get_namespace(info));

    if (gjs_define_info(load_context, obj, info)) {
        g_base_info_unref(info);
        *objp = obj; /* we defined the property in this object */
        JS_EndRequest(load_context);
        return JS_TRUE;
    } else {
        gjs_debug(GJS_DEBUG_GNAMESPACE,
                  "Failed to define info '%s'",
                  g_base_info_get_name(info));

        g_base_info_unref(info);

        if (!gjs_move_exception(load_context, context)) {
            /* set an exception if none was set */
            gjs_throw(context,
                         "Defining info failed but no exception set");
        }

        JS_EndRequest(load_context);
        return JS_FALSE;
    }
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
static JSBool
ns_constructor(JSContext *context,
               JSObject  *obj,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    Ns *priv;

    priv = g_slice_new0(Ns);

    GJS_INC_COUNTER(ns);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE, "ns constructor, obj %p priv %p", obj, priv);

    return JS_TRUE;
}

static void
ns_finalize(JSContext *context,
            JSObject  *obj)
{
    Ns *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    if (priv->namespace)
        g_free(priv->namespace);
    if (priv->repo)
        g_object_unref(priv->repo);

    GJS_DEC_COUNTER(ns);
    g_slice_free(Ns, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_ns_class = {
    "GIRepositoryNamespace",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) ns_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    ns_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static JSPropertySpec gjs_ns_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_ns_proto_funcs[] = {
    { NULL }
};

static JSObject*
ns_new(JSContext    *context,
       const char   *ns_name,
       GIRepository *repo)
{
    JSObject *ns;
    JSObject *global;
    Ns *priv;

    /* put constructor in the global namespace */
    global = JS_GetGlobalObject(context);

    if (!gjs_object_has_property(context, global, gjs_ns_class.name)) {
        JSObject *prototype;
        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 NULL,
                                 &gjs_ns_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 ns_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_ns_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_ns_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            gjs_fatal("Can't init class %s", gjs_ns_class.name);

        g_assert(gjs_object_has_property(context, global, gjs_ns_class.name));

        gjs_debug(GJS_DEBUG_GNAMESPACE, "Initialized class %s prototype %p",
                  gjs_ns_class.name, prototype);
    }

    ns = JS_ConstructObject(context, &gjs_ns_class, NULL, NULL);
    if (ns == NULL)
        gjs_fatal("No memory to create ns object");

    priv = priv_from_js(context, ns);
    priv->repo = g_object_ref(repo);
    priv->namespace = g_strdup(ns_name);

    return ns;
}

JSObject*
gjs_define_ns(JSContext    *context,
              JSObject     *in_object,
              const char   *ns_name,
              GIRepository *repo)
{
    JSObject *ns;

    ns = ns_new(context, ns_name, repo);

    if (!JS_DefineProperty(context, in_object,
                           ns_name, OBJECT_TO_JSVAL(ns),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        gjs_fatal("no memory to define ns property");

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Defined namespace '%s' %p in GIRepository %p", ns_name, ns, in_object);

    return ns;
}
