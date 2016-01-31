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
#include "proxyutils.h"
#include "function.h"
#include "gtype.h"
#include <girepository.h>

typedef struct {
    GIUnionInfo *info;
    void *gboxed; /* NULL if we are the prototype and not an instance */
    GType gtype;
} Union;

extern struct JSClass gjs_union_class;

GJS_DEFINE_PRIV_FROM_JS(Union, gjs_union_class)

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
                  JS::HandleObject obj,
                  JS::HandleId id,
                  unsigned flags,
                  JS::MutableHandleObject objp)
{
    Union *priv;
    char *name;
    JSBool ret = JS_TRUE;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GBOXED, "Resolve prop '%s' hook obj %p priv %p",
                     name, (void *)obj, priv);

    if (priv == NULL) {
        ret = JS_FALSE; /* wrong class */
        goto out;
    }

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
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                method_name = g_base_info_get_name( (GIBaseInfo*) method_info);

                gjs_debug(GJS_DEBUG_GBOXED,
                          "Defining method %s in prototype for %s.%s",
                          method_name,
                          g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                          g_base_info_get_name( (GIBaseInfo*) priv->info));

                union_proto = obj;

                if (gjs_define_function(context, union_proto,
                                        g_registered_type_info_get_g_type(priv->info),
                                        method_info) == NULL) {
                    g_base_info_unref( (GIBaseInfo*) method_info);
                    ret = JS_FALSE;
                    goto out;
                }

                objp.set(union_proto); /* we defined the prop in object_proto */
            }

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

 out:
    g_free(name);
    return ret;
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

GJS_NATIVE_CONSTRUCTOR_DECLARE(union)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(union)
    Union *priv;
    Union *proto_priv;
    JSObject *proto;
    void *gboxed;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(union);

    priv = g_slice_new0(Union);

    GJS_INC_COUNTER(boxed);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "union constructor, obj %p priv %p",
                        object, priv);

    JS_GetPrototype(context, object, &proto);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED, "union instance __proto__ is %p", proto);

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
    priv->gtype = proto_priv->gtype;

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
     * be garbage collected, we make a copy here to be
     * owned by us.
     */
    priv->gboxed = g_boxed_copy(priv->gtype, gboxed);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "JSObject created with union instance %p type %s",
                        priv->gboxed, g_type_name(priv->gtype));

    GJS_NATIVE_CONSTRUCTOR_FINISH(union);

    return JS_TRUE;
}

static void
union_finalize(JSFreeOp *fop,
               JSObject *obj)
{
    Union *priv;

    priv = (Union*) JS_GetPrivate(obj);
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

static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JS::CallReceiver rec = JS::CallReceiverFromVp(vp);
    JSObject *obj = JSVAL_TO_OBJECT(rec.thisv());

    Union *priv;
    JSBool ret = JS_FALSE;
    jsval retval;

    if (!priv_from_js_with_typecheck(context, obj, &priv))
        goto out;
    
    if (!_gjs_proxy_to_string_func(context, obj, "union", (GIBaseInfo*)priv->info,
                                   priv->gtype, priv->gboxed, &retval))
        goto out;

    ret = JS_TRUE;
    rec.rval().set(retval);
 out:
    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_union_class = {
    "GObject_Union",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) union_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    union_finalize,
    NULL,
    NULL,
    NULL, NULL, NULL
};

JSPropertySpec gjs_union_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_union_proto_funcs[] = {
    { "toString", JSOP_WRAPPER((JSNative)to_string_func), 0, 0 },
    { NULL }
};

JSBool
gjs_define_union_class(JSContext    *context,
                       JSObject     *in_object,
                       GIUnionInfo  *info)
{
    const char *constructor_name;
    JSObject *prototype;
    jsval value;
    Union *priv;
    GType gtype;
    JSObject *constructor;

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

    if (!gjs_init_class_dynamic(context, in_object,
                                NULL,
                                g_base_info_get_namespace( (GIBaseInfo*) info),
                                constructor_name,
                                &gjs_union_class,
                                gjs_union_constructor, 0,
                                /* props of prototype */
                                &gjs_union_proto_props[0],
                                /* funcs of prototype */
                                &gjs_union_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                NULL,
                                &prototype,
                                &constructor)) {
        g_error("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Union);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = gtype;
    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

    value = OBJECT_TO_JSVAL(gjs_gtype_create_gtype_wrapper(context, gtype));
    JS_DefineProperty(context, constructor, "$gtype", value,
                      NULL, NULL, JSPROP_PERMANENT);

    return JS_TRUE;
}

JSObject*
gjs_union_from_c_union(JSContext    *context,
                       GIUnionInfo  *info,
                       void         *gboxed)
{
    JSObject *obj;
    JSObject *proto;
    Union *priv;
    GType gtype;

    if (gboxed == NULL)
        return NULL;

    /* For certain unions, we may be able to relax this in the future by
     * directly allocating union memory, as we do for structures in boxed.c
     */
    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) info);
    if (gtype == G_TYPE_NONE) {
        gjs_throw(context, "Unions must currently be registered as boxed types");
        return NULL;
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping union %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    proto = gjs_lookup_generic_prototype(context, (GIUnionInfo*) info);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Union);
    JS_SetPrivate(obj, priv);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo *) priv->info);
    priv->gtype = gtype;
    priv->gboxed = g_boxed_copy(gtype, gboxed);

    return obj;
}

void*
gjs_c_union_from_union(JSContext    *context,
                       JSObject     *obj)
{
    Union *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    return priv->gboxed;
}

JSBool
gjs_typecheck_union(JSContext     *context,
                    JSObject      *object,
                    GIStructInfo  *expected_info,
                    GType          expected_type,
                    JSBool         throw_error)
{
    Union *priv;
    JSBool result;

    if (!do_base_typecheck(context, object, throw_error))
        return JS_FALSE;

    priv = priv_from_js(context, object);

    if (priv->gboxed == NULL) {
        if (throw_error) {
            gjs_throw_custom(context, "TypeError",
                             "Object is %s.%s.prototype, not an object instance - cannot convert to a union instance",
                             g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                             g_base_info_get_name( (GIBaseInfo*) priv->info));
        }

        return JS_FALSE;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (priv->gtype, expected_type);
    else if (expected_info != NULL)
        result = g_base_info_equal((GIBaseInfo*) priv->info, (GIBaseInfo*) expected_info);
    else
        result = JS_TRUE;

    if (!result && throw_error) {
        if (expected_info != NULL) {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s.%s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_base_info_get_namespace((GIBaseInfo*) expected_info),
                             g_base_info_get_name((GIBaseInfo*) expected_info));
        } else {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_type_name(expected_type));
        }
    }

    return result;
}
