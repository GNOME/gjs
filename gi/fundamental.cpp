/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013       Intel Corporation
 * Copyright (c) 2008-2010  litl, LLC
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

#include <gjs/gi.h>

#include "fundamental.h"
#include "arg.h"
#include "object.h"
#include "boxed.h"
#include "repo.h"
#include "function.h"
#include "gtype.h"
#include "proxyutils.h"

#include <gjs/gjs.h>

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>

/*
 * Structure allocated for prototypes.
 */
typedef struct _Fundamental {
    /* instance info */
    void                         *gfundamental;
    struct _Fundamental          *prototype;    /* NULL if prototype */

    /* prototype info */
    GIObjectInfo                 *info;
    GType                         gtype;
    GIObjectInfoRefFunction       ref_function;
    GIObjectInfoUnrefFunction     unref_function;
    GIObjectInfoGetValueFunction  get_value_function;
    GIObjectInfoSetValueFunction  set_value_function;

    jsid                          constructor_name;
    GICallableInfo               *constructor_info;
} Fundamental;

/*
 * Structure allocated for instances.
 */
typedef struct {
    void                         *gfundamental;
    struct _Fundamental          *prototype;
} FundamentalInstance;

extern struct JSClass gjs_fundamental_instance_class;

GJS_DEFINE_PRIV_FROM_JS(FundamentalInstance, gjs_fundamental_instance_class)

static GQuark
gjs_fundamental_table_quark (void)
{
    static GQuark val = 0;
    if (!val)
        val = g_quark_from_static_string("gjs::fundamental-table");

    return val;
}

static GHashTable *
_ensure_mapping_table(GjsContext *context)
{
    GHashTable *table =
        (GHashTable *) g_object_get_qdata ((GObject *) context,
                                           gjs_fundamental_table_quark());

    if (G_UNLIKELY(table == NULL)) {
        table = g_hash_table_new_full(g_direct_hash,
                                      g_direct_equal,
                                      NULL,
                                      NULL);
        g_object_set_qdata_full((GObject *) context,
                                gjs_fundamental_table_quark(),
                                table,
                                (GDestroyNotify) g_hash_table_unref);
    }

    return table;
}

static void
_fundamental_add_object(void *native_object, JSObject *js_object)
{
    GHashTable *table = _ensure_mapping_table(gjs_context_get_current());

    g_hash_table_insert(table, native_object, js_object);
}

static void
_fundamental_remove_object(void *native_object)
{
    GHashTable *table = _ensure_mapping_table(gjs_context_get_current());

    g_hash_table_remove(table, native_object);
}

static JSObject *
_fundamental_lookup_object(void *native_object)
{
    GHashTable *table = _ensure_mapping_table(gjs_context_get_current());

    return (JSObject *) g_hash_table_lookup(table, native_object);
}

/**/

static inline Fundamental *
proto_priv_from_js(JSContext *context,
                   JSObject  *obj)
{
    JSObject *proto;
    JS_GetPrototype(context, obj, &proto);
    return (Fundamental*) priv_from_js(context, proto);
}

static FundamentalInstance *
init_fundamental_instance(JSContext *context,
                          JSObject  *object)
{
    Fundamental *proto_priv;
    FundamentalInstance *priv;

    JS_BeginRequest(context);

    priv = g_slice_new0(FundamentalInstance);

    GJS_INC_COUNTER(fundamental);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNDAMENTAL,
                        "fundamental instance constructor, obj %p priv %p proto %p ",
                        object, priv, JS_GetPrototype (object));

    proto_priv = proto_priv_from_js(context, object);
    g_assert(proto_priv != NULL);

    priv->prototype = proto_priv;

    JS_EndRequest(context);

    return priv;
}

static void
associate_js_instance_to_fundamental(JSContext *context,
                                     JSObject  *object,
                                     void      *gfundamental,
                                     gboolean   owned_ref)
{
    FundamentalInstance *priv;

    priv = priv_from_js(context, object);
    priv->gfundamental = gfundamental;

    g_assert(_fundamental_lookup_object(gfundamental) == NULL);
    _fundamental_add_object(gfundamental, object);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNDAMENTAL,
                        "associated JSObject %p with fundamental %p",
                        object, gfundamental);

    if (!owned_ref)
        priv->prototype->ref_function(gfundamental);
}

/**/

/* Find the first constructor */
static GIFunctionInfo *
find_fundamental_constructor(JSContext    *context,
                             GIObjectInfo *info,
                             jsid         *constructor_name)
{
    int i, n_methods;

    n_methods = g_object_info_get_n_methods(info);

    for (i = 0; i < n_methods; ++i) {
        GIFunctionInfo *func_info;
        GIFunctionInfoFlags flags;

        func_info = g_object_info_get_method(info, i);

        flags = g_function_info_get_flags(func_info);
        if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0) {
            const char *name;

            name = g_base_info_get_name((GIBaseInfo *) func_info);
            *constructor_name = gjs_intern_string_to_id(context, name);

            return func_info;
        }

        g_base_info_unref((GIBaseInfo *) func_info);
    }

    return NULL;
}

/**/

static JSBool
fundamental_instance_new_resolve_interface(JSContext    *context,
                                           JS::HandleObject obj,
                                           JS::MutableHandleObject objp,
                                           Fundamental  *proto_priv,
                                           char         *name)
{
    GIFunctionInfo *method_info;
    JSBool ret;
    GType *interfaces;
    guint n_interfaces;
    guint i;

    ret = JS_TRUE;
    interfaces = g_type_interfaces(proto_priv->gtype, &n_interfaces);
    for (i = 0; i < n_interfaces; i++) {
        GIBaseInfo *base_info;
        GIInterfaceInfo *iface_info;

        base_info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                                interfaces[i]);

        if (base_info == NULL)
            continue;

        /* An interface GType ought to have interface introspection info */
        g_assert(g_base_info_get_type(base_info) == GI_INFO_TYPE_INTERFACE);

        iface_info = (GIInterfaceInfo *) base_info;

        method_info = g_interface_info_find_method(iface_info, name);

        g_base_info_unref(base_info);


        if (method_info != NULL) {
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                if (gjs_define_function(context, obj,
                                        proto_priv->gtype,
                                        (GICallableInfo *) method_info)) {
                    objp.set(obj);
                } else {
                    ret = JS_FALSE;
                }
            }

            g_base_info_unref((GIBaseInfo *) method_info);
        }
    }

    g_free(interfaces);
    return ret;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or fundamental prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
fundamental_instance_new_resolve(JSContext  *context,
                                 JS::HandleObject obj,
                                 JS::HandleId id,
                                 unsigned flags,
                                 JS::MutableHandleObject objp)
{
    FundamentalInstance *priv;
    char *name;
    JSBool ret = JS_FALSE;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GFUNDAMENTAL, "Resolve prop '%s' hook obj %p priv %p", name, *obj, priv);

    if (priv == NULL)
        goto out; /* wrong class */

    if (priv->prototype == NULL) {
        /* We are the prototype, so look for methods and other class properties */
        Fundamental *proto_priv = (Fundamental *) priv;
        GIFunctionInfo *method_info;

        method_info = g_object_info_find_method((GIStructInfo*) proto_priv->info,
                                                name);

        if (method_info != NULL) {
            const char *method_name;

#if GJS_VERBOSE_ENABLE_GI_USAGE
            _gjs_log_info_usage((GIBaseInfo *) method_info);
#endif
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                method_name = g_base_info_get_name((GIBaseInfo *) method_info);

                /* we do not define deprecated methods in the prototype */
                if (g_base_info_is_deprecated((GIBaseInfo *) method_info)) {
                    gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                              "Ignoring definition of deprecated method %s in prototype %s.%s",
                              method_name,
                              g_base_info_get_namespace((GIBaseInfo *) proto_priv->info),
                              g_base_info_get_name((GIBaseInfo *) proto_priv->info));
                    g_base_info_unref((GIBaseInfo *) method_info);
                    ret = JS_TRUE;
                    goto out;
                }

                gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                          "Defining method %s in prototype for %s.%s",
                          method_name,
                          g_base_info_get_namespace((GIBaseInfo *) proto_priv->info),
                          g_base_info_get_name((GIBaseInfo *) proto_priv->info));

                if (gjs_define_function(context, obj, proto_priv->gtype,
                                        method_info) == NULL) {
                    g_base_info_unref((GIBaseInfo *) method_info);
                    goto out;
                }

                objp.set(obj);
            }

            g_base_info_unref((GIBaseInfo *) method_info);
        }

        ret = fundamental_instance_new_resolve_interface(context, obj, objp,
                                                         proto_priv, name);
    } else {
        /* We are an instance, not a prototype, so look for
         * per-instance props that we want to define on the
         * JSObject. Generally we do not want to cache these in JS, we
         * want to always pull them from the C object, or JS would not
         * see any changes made from C. So we use the get/set prop
         * hooks, not this resolve hook.
         */
    }

    ret = JS_TRUE;
 out:
    g_free(name);
    return ret;
}

static JSBool
fundamental_invoke_constructor(FundamentalInstance *priv,
                               JSContext           *context,
                               JSObject            *obj,
                               unsigned             argc,
                               jsval               *argv,
                               GArgument           *rvalue)
{
    jsval js_constructor, js_constructor_func;
    jsid constructor_const;
    JSBool ret = JS_FALSE;

    constructor_const = gjs_context_get_const_string(context, GJS_STRING_CONSTRUCTOR);
    if (!gjs_object_require_property(context, obj, NULL,
                                     constructor_const, &js_constructor) ||
        priv->prototype->constructor_name == JSID_VOID) {
        gjs_throw (context,
                   "Couldn't find a constructor for type %s.%s",
                   g_base_info_get_namespace((GIBaseInfo*) priv->prototype->info),
                   g_base_info_get_name((GIBaseInfo*) priv->prototype->info));
        goto end;
    }

    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(js_constructor), NULL,
                                     priv->prototype->constructor_name, &js_constructor_func)) {
        gjs_throw (context,
                   "Couldn't find a constructor for type %s.%s",
                   g_base_info_get_namespace((GIBaseInfo*) priv->prototype->info),
                   g_base_info_get_name((GIBaseInfo*) priv->prototype->info));
        goto end;
    }

    ret = gjs_invoke_constructor_from_c(context, JSVAL_TO_OBJECT(js_constructor_func), obj, argc, argv, rvalue);

 end:
    return ret;
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(fundamental_instance)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(fundamental_instance)
    FundamentalInstance *priv;
    GArgument ret_value;
    GITypeInfo return_info;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(fundamental_instance);

    priv = init_fundamental_instance(context, object);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNDAMENTAL,
                        "fundamental constructor, obj %p priv %p",
                        object, priv);

    if (!fundamental_invoke_constructor(priv, context, object, argc, argv, &ret_value))
        return JS_FALSE;

    associate_js_instance_to_fundamental(context, object, ret_value.v_pointer, FALSE);

    g_callable_info_load_return_type((GICallableInfo*) priv->prototype->constructor_info, &return_info);

    if (!gjs_g_argument_release (context,
                                 g_callable_info_get_caller_owns((GICallableInfo*) priv->prototype->constructor_info),
                                 &return_info,
                                 &ret_value))
        return JS_FALSE;

    GJS_NATIVE_CONSTRUCTOR_FINISH(fundamental_instance);

    return JS_TRUE;
}

static void
fundamental_finalize(JSFreeOp  *fop,
                     JSObject  *obj)
{
    FundamentalInstance *priv;

    priv = (FundamentalInstance *) JS_GetPrivate(obj);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNDAMENTAL,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    if (priv->prototype) {
        if (priv->gfundamental) {
            _fundamental_remove_object(priv->gfundamental);
            priv->prototype->unref_function(priv->gfundamental);
            priv->gfundamental = NULL;
        }

        g_slice_free(FundamentalInstance, priv);
        GJS_DEC_COUNTER(fundamental);
    } else {
        Fundamental *proto_priv = (Fundamental *) priv;

        /* Only unref infos when freeing the prototype */
        if (proto_priv->constructor_info)
            g_base_info_unref (proto_priv->constructor_info);
        proto_priv->constructor_info = NULL;
        if (proto_priv->info)
            g_base_info_unref((GIBaseInfo *) proto_priv->info);
        proto_priv->info = NULL;

        g_slice_free(Fundamental, proto_priv);
    }
}

static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    FundamentalInstance *priv;
    JSBool ret = JS_FALSE;
    jsval retval;

    if (!priv_from_js_with_typecheck(context, obj, &priv))
        goto out;

    if (!priv->prototype) {
        Fundamental *proto_priv = (Fundamental *) priv;

        if (!_gjs_proxy_to_string_func(context, obj, "fundamental",
                                       (GIBaseInfo *) proto_priv->info,
                                       proto_priv->gtype,
                                       proto_priv->gfundamental,
                                       &retval))
            goto out;
    } else {
        if (!_gjs_proxy_to_string_func(context, obj, "fundamental",
                                       (GIBaseInfo *) priv->prototype->info,
                                       priv->prototype->gtype,
                                       priv->gfundamental,
                                       &retval))
            goto out;
    }

    ret = JS_TRUE;
    JS_SET_RVAL(context, vp, retval);
 out:
    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 *
 * We allocate 1 reserved slot; this is typically unused, but if the
 * fundamental is for a nested structure inside a parent structure, the
 * reserved slot is used to hold onto the parent Javascript object and
 * make sure it doesn't get freed.
 */
struct JSClass gjs_fundamental_instance_class = {
    "GFundamental_Object",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) fundamental_instance_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    fundamental_finalize,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static JSPropertySpec gjs_fundamental_instance_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_fundamental_instance_proto_funcs[] = {
    { "toString", JSOP_WRAPPER((JSNative)to_string_func), 0, 0 },
    { NULL }
};

static JSObject *
gjs_lookup_fundamental_prototype(JSContext    *context,
                                 GIObjectInfo *info,
                                 GType         gtype)
{
    JSObject *in_object;
    JSObject *constructor;
    const char *constructor_name;
    jsval value;

    if (info) {
        in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);
        constructor_name = g_base_info_get_name((GIBaseInfo*) info);
    } else {
        in_object = gjs_lookup_private_namespace(context);
        constructor_name = g_type_name(gtype);
    }

    if (G_UNLIKELY (!in_object))
        return NULL;

    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    if (JSVAL_IS_VOID(value)) {
        /* In case we're looking for a private type, and we don't find it,
           we need to define it first.
        */
        gjs_define_fundamental_class(context, in_object, info, &constructor, NULL);
    } else {
        if (G_UNLIKELY (!JSVAL_IS_OBJECT(value) || JSVAL_IS_NULL(value)))
            return NULL;

        constructor = JSVAL_TO_OBJECT(value);
    }

    g_assert(constructor != NULL);

    if (!gjs_object_get_property_const(context, constructor,
                                       GJS_STRING_PROTOTYPE, &value))
        return NULL;

    if (G_UNLIKELY (!JSVAL_IS_OBJECT(value)))
        return NULL;

    return JSVAL_TO_OBJECT(value);
}

static JSObject*
gjs_lookup_fundamental_prototype_from_gtype(JSContext *context,
                                            GType      gtype)
{
    GIObjectInfo *info;
    JSObject *proto;

    info = (GIObjectInfo *) g_irepository_find_by_gtype(g_irepository_get_default(),
                                                        gtype);
    proto = gjs_lookup_fundamental_prototype(context, info, gtype);
    if (info)
        g_base_info_unref((GIBaseInfo*)info);

    return proto;
}

JSBool
gjs_define_fundamental_class(JSContext     *context,
                             JSObject      *in_object,
                             GIObjectInfo  *info,
                             JSObject     **constructor_p,
                             JSObject     **prototype_p)
{
    const char *constructor_name;
    JSObject *prototype;
    jsval value;
    jsid js_constructor_name = JSID_VOID;
    JSObject *parent_proto;
    Fundamental *priv;
    JSObject *constructor;
    GType parent_gtype;
    GType gtype;
    GIFunctionInfo *constructor_info;
    /* See the comment in gjs_define_object_class() for an explanation
     * of how this all works; Fundamental is pretty much the same as
     * Object.
     */

    constructor_name = g_base_info_get_name((GIBaseInfo *) info);
    constructor_info = find_fundamental_constructor(context, info,
                                                    &js_constructor_name);

    gtype = g_registered_type_info_get_g_type (info);
    parent_gtype = g_type_parent(gtype);
    parent_proto = NULL;
    if (parent_gtype != G_TYPE_INVALID)
        parent_proto = gjs_lookup_fundamental_prototype_from_gtype(context,
                                                                   parent_gtype);

    if (!gjs_init_class_dynamic(context, in_object,
                                /* parent prototype JSObject* for
                                 * prototype; NULL for
                                 * Object.prototype
                                 */
                                parent_proto,
                                g_base_info_get_namespace((GIBaseInfo *) info),
                                constructor_name,
                                &gjs_fundamental_instance_class,
                                gjs_fundamental_instance_constructor,
                                /* number of constructor args (less can be passed) */
                                constructor_info != NULL ? g_callable_info_get_n_args((GICallableInfo *) constructor_info) : 0,
                                /* props of prototype */
                                parent_proto ? NULL : &gjs_fundamental_instance_proto_props[0],
                                /* funcs of prototype */
                                parent_proto ? NULL : &gjs_fundamental_instance_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                NULL,
                                &prototype,
                                &constructor)) {
        gjs_log_exception(context);
        g_error("Can't init class %s", constructor_name);
    }

    /* Put the info in the prototype */
    priv = g_slice_new0(Fundamental);
    g_assert(priv != NULL);
    g_assert(priv->info == NULL);
    priv->info = g_base_info_ref((GIBaseInfo *) info);
    priv->gtype = gtype;
    priv->constructor_name = js_constructor_name;
    priv->constructor_info = constructor_info;
    priv->ref_function = g_object_info_get_ref_function_pointer(info);
    g_assert(priv->ref_function != NULL);
    priv->unref_function = g_object_info_get_unref_function_pointer(info);
    g_assert(priv->unref_function != NULL);
    priv->set_value_function = g_object_info_get_set_value_function_pointer(info);
    g_assert(priv->set_value_function != NULL);
    priv->get_value_function = g_object_info_get_get_value_function_pointer(info);
    g_assert(priv->get_value_function != NULL);
    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
              "Defined class %s prototype is %p class %p in object %p constructor %s.%s.%s",
              constructor_name, prototype, JS_GetClass(prototype),
              in_object,
              constructor_info != NULL ? g_base_info_get_namespace(constructor_info) : "unknown",
              constructor_info != NULL ? g_base_info_get_name(g_base_info_get_container(constructor_info)) : "unknown",
              constructor_info != NULL ? g_base_info_get_name(constructor_info) : "unknown");

    if (g_object_info_get_n_fields(priv->info) > 0) {
        gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                  "Fundamental type '%s.%s' apparently has accessible fields. "
                  "Gjs has no support for this yet, ignoring these.",
                  g_base_info_get_namespace((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)priv->info));
    }

    gjs_object_define_static_methods(context, constructor, gtype, info);

    value = OBJECT_TO_JSVAL(gjs_gtype_create_gtype_wrapper(context, gtype));
    JS_DefineProperty(context, constructor, "$gtype", value,
                      NULL, NULL, JSPROP_PERMANENT);

    if (constructor_p)
        *constructor_p = constructor;
    if (prototype_p)
        *prototype_p = prototype;

    return JS_TRUE;
}

JSObject*
gjs_object_from_g_fundamental(JSContext    *context,
                              GIObjectInfo *info,
                              void         *gfundamental)
{
    JSObject *proto;
    JSObject *object;

    if (gfundamental == NULL)
        return NULL;

    object = _fundamental_lookup_object(gfundamental);
    if (object)
        return object;

    gjs_debug_marshal(GJS_DEBUG_GFUNDAMENTAL,
                      "Wrapping fundamental %s.%s %p with JSObject",
                      g_base_info_get_namespace((GIBaseInfo *) info),
                      g_base_info_get_name((GIBaseInfo *) info),
                      gfundamental);

    proto = gjs_lookup_fundamental_prototype_from_gtype(context,
                                                        G_TYPE_FROM_INSTANCE(gfundamental));

    object = JS_NewObjectWithGivenProto(context,
                                        JS_GetClass(proto), proto,
                                        gjs_get_import_global(context));

    if (object == NULL)
        goto out;

    init_fundamental_instance(context, object);

    associate_js_instance_to_fundamental(context, object, gfundamental, FALSE);

 out:
    return object;
}

JSObject *
gjs_fundamental_from_g_value(JSContext    *context,
                             const GValue *value,
                             GType         gtype)
{
    JSObject *proto;
    Fundamental *proto_priv;
    void *fobj;

    proto = gjs_lookup_fundamental_prototype_from_gtype(context, gtype);
    if (!proto)
        return NULL;

    proto_priv = (Fundamental *) priv_from_js(context, proto);

    fobj = proto_priv->get_value_function(value);
    if (!fobj) {
        gjs_throw(context,
                  "Failed to convert GValue to a fundamental instance");
        return NULL;
    }

    return gjs_object_from_g_fundamental(context, proto_priv->info, fobj);
}

void*
gjs_g_fundamental_from_object(JSContext *context,
                              JSObject  *obj)
{
    FundamentalInstance *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    if (priv == NULL) {
        gjs_throw(context,
                  "No introspection information for %p", obj);
        return NULL;
    }

    if (priv->gfundamental == NULL) {
        gjs_throw(context,
                  "Object is %s.%s.prototype, not an object instance - cannot convert to a fundamental instance",
                  g_base_info_get_namespace((GIBaseInfo *) priv->prototype->info),
                  g_base_info_get_name((GIBaseInfo *) priv->prototype->info));
        return NULL;
    }

    return priv->gfundamental;
}

JSBool
gjs_typecheck_is_fundamental(JSContext     *context,
                             JSObject      *object,
                             JSBool         throw_error)
{
    return do_base_typecheck(context, object, throw_error);
}

JSBool
gjs_typecheck_fundamental(JSContext *context,
                          JSObject  *object,
                          GType      expected_gtype,
                          JSBool     throw_error)
{
    FundamentalInstance *priv;
    JSBool result;

    if (!do_base_typecheck(context, object, throw_error))
        return JS_FALSE;

    priv = priv_from_js(context, object);
    g_assert(priv != NULL);

    if (priv->gfundamental == NULL) {
        if (throw_error) {
            Fundamental *proto_priv = (Fundamental *) priv;
            gjs_throw(context,
                      "Object is %s.%s.prototype, not an fundamental instance - cannot convert to void*",
                      proto_priv->info ? g_base_info_get_namespace((GIBaseInfo *) proto_priv->info) : "",
                      proto_priv->info ? g_base_info_get_name((GIBaseInfo *) proto_priv->info) : g_type_name(proto_priv->gtype));
        }

        return JS_FALSE;
    }

    if (expected_gtype != G_TYPE_NONE)
        result = g_type_is_a(priv->prototype->gtype, expected_gtype);
    else
        result = JS_TRUE;

    if (!result && throw_error) {
        if (priv->prototype->info) {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo *) priv->prototype->info),
                             g_base_info_get_name((GIBaseInfo *) priv->prototype->info),
                             g_type_name(expected_gtype));
        } else {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s - cannot convert to %s",
                             g_type_name(priv->prototype->gtype),
                             g_type_name(expected_gtype));
        }
    }

    return result;
}

void *
gjs_fundamental_ref(JSContext     *context,
                    void          *gfundamental)
{
    JSObject *proto;
    Fundamental *proto_priv;

    proto = gjs_lookup_fundamental_prototype_from_gtype(context,
                                                        G_TYPE_FROM_INSTANCE(gfundamental));

    proto_priv = (Fundamental *) priv_from_js(context, proto);

    return proto_priv->ref_function(gfundamental);
}

void
gjs_fundamental_unref(JSContext    *context,
                      void         *gfundamental)
{
    JSObject *proto;
    Fundamental *proto_priv;

    proto = gjs_lookup_fundamental_prototype_from_gtype(context,
                                                        G_TYPE_FROM_INSTANCE(gfundamental));

    proto_priv = (Fundamental *) priv_from_js(context, proto);

    proto_priv->unref_function(gfundamental);
}
