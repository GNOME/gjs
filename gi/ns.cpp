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
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>
#include <girepository.h>

#include <string.h>

typedef struct {
    char *gi_namespace;
} Ns;

extern struct JSClass gjs_ns_class;

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
               JS::HandleObject obj,
               JS::HandleId id,
               unsigned flags,
               JS::MutableHandleObject objp)
{
    Ns *priv;
    char *name;
    GIRepository *repo;
    GIBaseInfo *info;
    JSBool ret = JS_FALSE;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0) {
        ret = JS_TRUE;
        goto out;
    }

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GNAMESPACE, "Resolve prop '%s' hook obj %p priv %p", name, *obj, priv);

    if (priv == NULL) {
        ret = JS_TRUE; /* we are the prototype, or have the wrong class */
        goto out;
    }

    JS_BeginRequest(context);

    repo = g_irepository_get_default();

    info = g_irepository_find_by_name(repo, priv->gi_namespace, name);
    if (info == NULL) {
        /* No property defined, but no error either, so return TRUE */
        JS_EndRequest(context);
        ret = JS_TRUE;
        goto out;
    }

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Found info type %s for '%s' in namespace '%s'",
              gjs_info_type_name(g_base_info_get_type(info)),
              g_base_info_get_name(info),
              g_base_info_get_namespace(info));

    if (gjs_define_info(context, obj, info)) {
        g_base_info_unref(info);
        objp.set(obj); /* we defined the property in this object */
        ret = JS_TRUE;
    } else {
        gjs_debug(GJS_DEBUG_GNAMESPACE,
                  "Failed to define info '%s'",
                  g_base_info_get_name(info));

        g_base_info_unref(info);
    }
    JS_EndRequest(context);

 out:
    g_free(name);
    return ret;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(ns)

static void
ns_finalize(JSFreeOp *fop,
            JSObject *obj)
{
    Ns *priv;

    priv = (Ns *)JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance */

    if (priv->gi_namespace)
        g_free(priv->gi_namespace);

    GJS_DEC_COUNTER(ns);
    g_slice_free(Ns, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_ns_class = {
    "GIRepositoryNamespace",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) ns_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    ns_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

JSPropertySpec gjs_ns_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_ns_proto_funcs[] = {
    { NULL }
};

static JSObject*
ns_new(JSContext    *context,
       const char   *ns_name)
{
    JSObject *ns;
    JSObject *global;
    Ns *priv;
    JSBool found;

    /* put constructor in the global namespace */
    global = gjs_get_import_global(context);

    if (!JS_HasProperty(context, global, gjs_ns_class.name, &found))
        return JS_FALSE;
    if (!found) {
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
                                 gjs_ns_constructor,
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
            g_error("Can't init class %s", gjs_ns_class.name);

        gjs_debug(GJS_DEBUG_GNAMESPACE, "Initialized class %s prototype %p",
                  gjs_ns_class.name, prototype);
    }

    ns = JS_NewObject(context, &gjs_ns_class, NULL, global);
    if (ns == NULL)
        g_error("No memory to create ns object");

    priv = g_slice_new0(Ns);

    GJS_INC_COUNTER(ns);

    g_assert(priv_from_js(context, ns) == NULL);
    JS_SetPrivate(ns, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE, "ns constructor, obj %p priv %p", ns, priv);

    priv = priv_from_js(context, ns);
    priv->gi_namespace = g_strdup(ns_name);
    return ns;
}

JSObject*
gjs_create_ns(JSContext    *context,
              const char   *ns_name)
{
    return ns_new(context, ns_name);
}
