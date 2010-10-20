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

#include <string.h>

/* include first for logging related #define used in repo.h */
#include <util/log.h>

#include "union.h"
#include "arg.h"
#include "object.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include "repo.h"
#include "function.h"


#include <jsapi.h>

#include <girepository.h>

typedef struct {
    GIUnionInfo *info;
    void *gboxed; /* NULL if we are the prototype and not an instance */
} Union;

static Union unthreadsafe_template_for_constructor = { NULL, NULL };

static struct JSClass gjs_union_class;

GJS_DEFINE_DYNAMIC_PRIV_FROM_JS(Union, gjs_union_class)

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or boxed prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
union_new_resolve(JSContext *context,
                  JSObject  *obj,
                  jsid       id,
                  uintN      flags,
                  JSObject **objp)
{
    Union *priv;
    const char *name;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GBOXED, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE; /* wrong class */

    if (priv->gboxed == NULL) {
        /* We are the prototype, so look for methods and other class properties */
        GIFunctionInfo *method_info;

        method_info = g_union_info_find_method((GIUnionInfo*) priv->info,
                                               name);

        if (method_info != NULL) {
            JSObject *union_proto;
            const char *method_name;

#if GJS_VERBOSE_ENABLE_GI_USAGE
            _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

            method_name = g_base_info_get_name( (GIBaseInfo*) method_info);

            /* we do not define deprecated methods in the prototype */
            if (g_base_info_is_deprecated( (GIBaseInfo*) method_info)) {
                gjs_debug(GJS_DEBUG_GBOXED,
                          "Ignoring definition of deprecated method %s in prototype %s.%s",
                          method_name,
                          g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                          g_base_info_get_name( (GIBaseInfo*) priv->info));
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_TRUE;
            }

            gjs_debug(GJS_DEBUG_GBOXED,
                      "Defining method %s in prototype for %s.%s",
                      method_name,
                      g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                      g_base_info_get_name( (GIBaseInfo*) priv->info));

            union_proto = obj;

            if (gjs_define_function(context, union_proto, method_info) == NULL) {
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_FALSE;
            }

            *objp = union_proto; /* we defined the prop in object_proto */

            g_base_info_unref( (GIBaseInfo*) method_info);
        }
    } else {
        /* We are an instance, not a prototype, so look for
         * per-instance props that we want to define on the
         * JSObject. Generally we do not want to cache these in JS, we
         * want to always pull them from the C object, or JS would not
         * see any changes made from C. So we use the get/set prop
         * hooks, not this resolve hook.
         */
    }

    return JS_TRUE;
}

static void*
union_new(JSContext   *context,
          JSObject    *obj, /* "this" for constructor */
          GIUnionInfo *info)
{
    int n_methods;
    int i;

    /* Find a zero-args constructor and call it */

    n_methods = g_union_info_get_n_methods(info);

    for (i = 0; i < n_methods; ++i) {
        GIFunctionInfo *func_info;
        GIFunctionInfoFlags flags;

        func_info = g_union_info_get_method(info, i);

        flags = g_function_info_get_flags(func_info);
        if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0 &&
            g_callable_info_get_n_args((GICallableInfo*) func_info) == 0) {

            jsval rval;

            rval = JSVAL_NULL;
            gjs_invoke_c_function_uncached(context, func_info, obj,
                                           0, NULL, &rval);

            g_base_info_unref((GIBaseInfo*) func_info);

            /* We are somewhat wasteful here; invoke_c_function() above
             * creates a JSObject wrapper for the union that we immediately
             * discard.
             */
            if (JSVAL_IS_NULL(rval))
                return NULL;
            else
                return gjs_c_union_from_union(context, JSVAL_TO_OBJECT(rval));
        }

        g_base_info_unref((GIBaseInfo*) func_info);
    }

    gjs_throw(context, "Unable to construct union type %s since it has no zero-args <constructor>, can only wrap an existing one",
              g_base_info_get_name((GIBaseInfo*) info));

    return NULL;
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(union)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(union)
    Union *priv;
    Union *proto_priv;
    JSClass *obj_class;
    JSClass *proto_class;
    JSObject *proto;
    gboolean is_proto;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(union);

    priv = g_slice_new0(Union);

    GJS_INC_COUNTER(boxed);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "union constructor, obj %p priv %p",
                        object, priv);

    proto = JS_GetPrototype(context, object);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED, "union instance __proto__ is %p", proto);

    /* If we're constructing the prototype, its __proto__ is not the same
     * class as us, but if we're constructing an instance, the prototype
     * has the same class.
     */
    obj_class = JS_GET_CLASS(context, object);
    proto_class = JS_GET_CLASS(context, proto);

    is_proto = (obj_class != proto_class);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "union instance constructing proto %d, obj class %s proto class %s",
                        is_proto, obj_class->name, proto_class->name);

    if (!is_proto) {
        GType gtype;

        /* If we're the prototype, then post-construct we'll fill in priv->info.
         * If we are not the prototype, though, then we'll get ->info from the
         * prototype and then create a GObject if we don't have one already.
         */
        proto_priv = priv_from_js(context, proto);
        if (proto_priv == NULL) {
            gjs_debug(GJS_DEBUG_GBOXED,
                      "Bad prototype set on union? Must match JSClass of object. JS error should have been reported.");
            return JS_FALSE;
        }

        priv->info = proto_priv->info;
        g_base_info_ref( (GIBaseInfo*) priv->info);

        gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);

        /* Since gobject-introspection is always creating new info
         * objects, == is not meaningful on them, only comparison of
         * their names. We prefer to use the info that is already ref'd
         * by the prototype for the class.
         */
        g_assert(unthreadsafe_template_for_constructor.info == NULL ||
                 strcmp(g_base_info_get_name( (GIBaseInfo*) priv->info),
                        g_base_info_get_name( (GIBaseInfo*) unthreadsafe_template_for_constructor.info))
                 == 0);
        unthreadsafe_template_for_constructor.info = NULL;

        if (unthreadsafe_template_for_constructor.gboxed == NULL) {
            void *gboxed;

            /* union_new happens to be implemented by calling
             * gjs_invoke_c_function(), which returns a jsval.
             * The returned "gboxed" here is owned by that jsval,
             * not by us.
             */
            gboxed = union_new(context, object, priv->info);

            if (gboxed == NULL) {
                return JS_FALSE;
            }

            /* Because "gboxed" is owned by a jsval and will
             * be garbage colleced, we make a copy here to be
             * owned by us.
             */
            priv->gboxed = g_boxed_copy(gtype, gboxed);
        } else {
            priv->gboxed = g_boxed_copy(gtype, unthreadsafe_template_for_constructor.gboxed);
            unthreadsafe_template_for_constructor.gboxed = NULL;
        }

        gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                            "JSObject created with union instance %p type %s",
                            priv->gboxed, g_type_name(gtype));
    }

    GJS_NATIVE_CONSTRUCTOR_FINISH(union);

    return JS_TRUE;
}

static void
union_finalize(JSContext *context,
               JSObject  *obj)
{
    Union *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    if (priv->gboxed) {
        g_boxed_free(g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info),
                     priv->gboxed);
        priv->gboxed = NULL;
    }

    if (priv->info) {
        g_base_info_unref( (GIBaseInfo*) priv->info);
        priv->info = NULL;
    }

    GJS_DEC_COUNTER(boxed);
    g_slice_free(Union, priv);
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
static struct JSClass gjs_union_class = {
    NULL, /* dynamic class, no name here */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START |
    JSCLASS_CONSTRUCT_PROTOTYPE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) union_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    union_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};

static JSPropertySpec gjs_union_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_union_proto_funcs[] = {
    { NULL }
};

JSObject*
gjs_lookup_union_constructor(JSContext    *context,
                             GIUnionInfo  *info)
{
    JSObject *ns;
    JSObject *constructor;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    constructor = NULL;
    if (gjs_define_union_class(context, ns, info,
                               &constructor, NULL))
        return constructor;
    else
        return NULL;
}

JSObject*
gjs_lookup_union_prototype(JSContext    *context,
                           GIUnionInfo  *info)
{
    JSObject *ns;
    JSObject *proto;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    proto = NULL;
    if (gjs_define_union_class(context, ns, info, NULL, &proto))
        return proto;
    else
        return NULL;
}

JSClass*
gjs_lookup_union_class(JSContext    *context,
                       GIUnionInfo  *info)
{
    JSObject *prototype;

    prototype = gjs_lookup_union_prototype(context, info);

    return JS_GET_CLASS(context, prototype);
}

JSBool
gjs_define_union_class(JSContext    *context,
                       JSObject     *in_object,
                       GIUnionInfo  *info,
                       JSObject    **constructor_p,
                       JSObject    **prototype_p)
{
    const char *constructor_name;
    JSObject *prototype;
    jsval value;
    Union *priv;
    GType gtype;

    /* For certain unions, we may be able to relax this in the future by
     * directly allocating union memory, as we do for structures in boxed.c
     */
    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) info);
    if (gtype == G_TYPE_NONE) {
        gjs_throw(context, "Unions must currently be registered as boxed types");
        return JS_FALSE;
    }

    /* See the comment in gjs_define_object_class() for an
     * explanation of how this all works; Union is pretty much the
     * same as Object.
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    if (gjs_object_get_property(context, in_object, constructor_name, &value)) {
        JSObject *constructor;

        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Existing property '%s' does not look like a constructor",
                         constructor_name);
            return JS_FALSE;
        }

        constructor = JSVAL_TO_OBJECT(value);

        gjs_object_get_property(context, constructor, "prototype", &value);
        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "union %s prototype property does not appear to exist or has wrong type", constructor_name);
            return JS_FALSE;
        } else {
            if (prototype_p)
                *prototype_p = JSVAL_TO_OBJECT(value);
            if (constructor_p)
                *constructor_p = constructor;

            return JS_TRUE;
        }
    }

    prototype = gjs_init_class_dynamic(context, in_object,
                                       /* parent prototype JSObject* for
                                        * prototype; NULL for
                                        * Object.prototype
                                        */
                                       NULL,
                                       g_base_info_get_namespace( (GIBaseInfo*) info),
                                       constructor_name,
                                       &gjs_union_class,
                                       /* constructor for instances (NULL for
                                        * none - just name the prototype like
                                        * Math - rarely correct)
                                        */
                                       gjs_union_constructor,
                                       /* number of constructor args */
                                       0,
                                       /* props of prototype */
                                       &gjs_union_proto_props[0],
                                       /* funcs of prototype */
                                       &gjs_union_proto_funcs[0],
                                       /* props of constructor, MyConstructor.myprop */
                                       NULL,
                                       /* funcs of constructor, MyConstructor.myfunc() */
                                       NULL);
    if (prototype == NULL)
        gjs_fatal("Can't init class %s", constructor_name);

    g_assert(gjs_object_has_property(context, in_object, constructor_name));

    /* Put the info in the prototype */
    priv = priv_from_js(context, prototype);
    g_assert(priv != NULL);
    g_assert(priv->info == NULL);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GET_CLASS(context, prototype), in_object);

    if (constructor_p) {
        *constructor_p = NULL;
        gjs_object_get_property(context, in_object, constructor_name, &value);
        if (value != JSVAL_VOID) {
            if (!JSVAL_IS_OBJECT(value)) {
                gjs_throw(context, "Property '%s' does not look like a constructor",
                          constructor_name);
                return JS_FALSE;
            }
        }

        *constructor_p = JSVAL_TO_OBJECT(value);
    }

    if (prototype_p)
        *prototype_p = prototype;

    return JS_TRUE;
}

JSObject*
gjs_union_from_c_union(JSContext    *context,
                       GIUnionInfo  *info,
                       void         *gboxed)
{
    JSObject *proto;

    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping union %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    proto = gjs_lookup_union_prototype(context, (GIUnionInfo*) info);

    /* can't come up with a better approach... */
    unthreadsafe_template_for_constructor.info = (GIUnionInfo*) info;
    unthreadsafe_template_for_constructor.gboxed = gboxed;

    return gjs_construct_object_dynamic(context, proto,
                                        0, NULL);
}

void*
gjs_c_union_from_union(JSContext    *context,
                       JSObject     *obj)
{
    Union *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return NULL;

    if (priv->gboxed == NULL) {
        gjs_throw(context,
                  "Object is %s.%s.prototype, not an object instance - cannot convert to a union instance",
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));
        return NULL;
    }

    return priv->gboxed;
}
