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

#include "param.h"
#include "repo.h"
#include <gjs/gjs.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

typedef struct {
    GParamSpec *gparam; /* NULL if we are the prototype and not an instance */
} Param;

static Param unthreadsafe_template_for_constructor = { NULL };

static struct JSClass gjs_param_class;

GJS_DEFINE_DYNAMIC_PRIV_FROM_JS(Param, gjs_param_class)

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
param_get_prop(JSContext *context,
               JSObject  *obj,
               jsid       id,
               jsval     *value_p)
{
    Param *priv;
    const char *name;
    const char *value_str;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not something we affect, but no error */

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GPARAM,
                     "Get prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE; /* wrong class */

    value_str = NULL;
    if (strcmp(name, "name") == 0)
        value_str = g_param_spec_get_name(priv->gparam);
    else if (strcmp(name, "nick") == 0)
        value_str = g_param_spec_get_nick(priv->gparam);
    else if (strcmp(name, "blurb") == 0)
        value_str = g_param_spec_get_blurb(priv->gparam);

    if (value_str != NULL) {
        *value_p = STRING_TO_JSVAL(JS_NewStringCopyZ(context, value_str));
    }

    return JS_TRUE;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or param prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
param_new_resolve(JSContext *context,
                  JSObject  *obj,
                  jsid       id,
                  uintN      flags,
                  JSObject **objp)
{
    Param *priv;
    const char *name;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GPARAM, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE; /* wrong class */

    if (priv->gparam == NULL) {
        /* We are the prototype, so implement any methods or other class properties */

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

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
static JSBool
param_constructor(JSContext *context,
                  JSObject  *obj,
                  uintN      argc,
                  jsval     *argv,
                  jsval     *retval)
{
    Param *priv;
    Param *proto_priv;
    JSClass *obj_class;
    JSClass *proto_class;
    JSObject *proto;
    gboolean is_proto;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    priv = g_slice_new0(Param);

    GJS_INC_COUNTER(param);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GPARAM,
                        "param constructor, obj %p priv %p", obj, priv);

    proto = JS_GetPrototype(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GPARAM, "param instance __proto__ is %p", proto);

    /* If we're constructing the prototype, its __proto__ is not the same
     * class as us, but if we're constructing an instance, the prototype
     * has the same class.
     */
    obj_class = JS_GET_CLASS(context, obj);
    proto_class = JS_GET_CLASS(context, proto);

    is_proto = (obj_class != proto_class);

    gjs_debug_lifecycle(GJS_DEBUG_GPARAM,
                        "param instance constructing proto %d, obj class %s proto class %s",
                        is_proto, obj_class->name, proto_class->name);

    if (!is_proto) {
        /* If we're the prototype, then post-construct we'll fill in priv->info.
         * If we are not the prototype, though, then we'll get ->info from the
         * prototype and then create a GObject if we don't have one already.
         */
        proto_priv = priv_from_js(context, proto);
        if (proto_priv == NULL) {
            gjs_debug(GJS_DEBUG_GPARAM,
                      "Bad prototype set on object? Must match JSClass of object. JS error should have been reported.");
            return JS_FALSE;
        }

        if (unthreadsafe_template_for_constructor.gparam == NULL) {
            /* To construct these we'd have to wrap all the annoying subclasses.
             * Since we only bind ParamSpec for purposes of the GObject::notify signal,
             * there isn't much point.
             */
            gjs_throw(context, "Unable to construct ParamSpec, can only wrap an existing one");
            return JS_FALSE;
        } else {
            priv->gparam = g_param_spec_ref(unthreadsafe_template_for_constructor.gparam);
            unthreadsafe_template_for_constructor.gparam = NULL;
        }

        gjs_debug(GJS_DEBUG_GPARAM,
                  "JSObject created with param instance %p type %s",
                  priv->gparam, g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) priv->gparam)));
    }

    return JS_TRUE;
}

static void
param_finalize(JSContext *context,
               JSObject  *obj)
{
    Param *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GPARAM,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    if (priv->gparam) {
        g_param_spec_unref(priv->gparam);
        priv->gparam = NULL;
    }

    GJS_DEC_COUNTER(param);
    g_slice_free(Param, priv);
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
static struct JSClass gjs_param_class = {
    NULL, /* dynamic */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START |
    JSCLASS_CONSTRUCT_PROTOTYPE,
    JS_PropertyStub,
    JS_PropertyStub,
    param_get_prop,
    JS_PropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) param_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    param_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};

static JSPropertySpec gjs_param_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_param_proto_funcs[] = {
    { NULL }
};

JSObject*
gjs_lookup_param_prototype(JSContext    *context)
{
    JSObject *ns;
    JSObject *proto;

    ns = gjs_lookup_namespace_object_by_name(context, "GLib");

    if (ns == NULL)
        return NULL;

    if (gjs_define_param_class(context, ns, &proto))
        return proto;
    else
        return NULL;
}

JSBool
gjs_define_param_class(JSContext    *context,
                       JSObject     *in_object,
                       JSObject    **prototype_p)
{
    const char *constructor_name;
    JSObject *prototype;
    jsval value;

    constructor_name = "ParamSpec";

    gjs_object_get_property(context, in_object, constructor_name, &value);
    if (value != JSVAL_VOID) {
        JSObject *constructor;

        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Existing property '%s' does not look like a constructor",
                      constructor_name);
            return JS_FALSE;
        }

        constructor = JSVAL_TO_OBJECT(value);

        gjs_object_get_property(context, constructor, "prototype", &value);
        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "prototype property does not appear to exist or has wrong type");
            return JS_FALSE;
        } else {
            if (prototype_p)
                *prototype_p = JSVAL_TO_OBJECT(value);

            return JS_TRUE;
        }

        return JS_TRUE;
    }

    /* we could really just use JS_InitClass for this since we have one class instead of
     * N classes on-demand. But, this deals with namespacing and such for us.
     */
    prototype = gjs_init_class_dynamic(context, in_object,
                                          /* parent prototype JSObject* for
                                           * prototype; NULL for
                                           * Object.prototype
                                           */
                                          NULL,
                                          "GLib",
                                          constructor_name,
                                          &gjs_param_class,
                                          /* constructor for instances (NULL for
                                           * none - just name the prototype like
                                           * Math - rarely correct)
                                           */
                                          param_constructor,
                                          /* number of constructor args */
                                          0,
                                          /* props of prototype */
                                          &gjs_param_proto_props[0],
                                          /* funcs of prototype */
                                          &gjs_param_proto_funcs[0],
                                          /* props of constructor, MyConstructor.myprop */
                                          NULL,
                                          /* funcs of constructor, MyConstructor.myfunc() */
                                          NULL);
    if (prototype == NULL)
        gjs_fatal("Can't init class %s", constructor_name);

    g_assert(gjs_object_has_property(context, in_object, constructor_name));

    if (prototype_p)
        *prototype_p = prototype;

    gjs_debug(GJS_DEBUG_GPARAM, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GET_CLASS(context, prototype), in_object);

    return JS_TRUE;
}

JSObject*
gjs_param_from_g_param(JSContext    *context,
                       GParamSpec   *gparam)
{
    JSObject *obj;
    JSObject *proto;

    if (gparam == NULL)
        return NULL;

    gjs_debug(GJS_DEBUG_GPARAM,
              "Wrapping %s '%s' on %s with JSObject",
              g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) gparam)),
              gparam->name,
              g_type_name(gparam->owner_type));

    proto = gjs_lookup_param_prototype(context);

    /* can't come up with a better approach... */
    unthreadsafe_template_for_constructor.gparam = gparam;

    obj = gjs_construct_object_dynamic(context, proto,
                                       0, NULL);

    return obj;
}

GParamSpec*
gjs_g_param_from_param(JSContext    *context,
                       JSObject     *obj)
{
    Param *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return NULL;

    if (priv->gparam == NULL) {
        gjs_throw(context,
                  "Object is a prototype, not an object instance - cannot convert to a paramspec instance");
        return NULL;
    }

    return priv->gparam;
}
