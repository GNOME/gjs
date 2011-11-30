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
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

typedef struct {
    GParamSpec *gparam; /* NULL if we are the prototype and not an instance */
} Param;

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
    char *name;
    const char *value_str;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not something we affect, but no error */

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GPARAM,
                     "Get prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL) {
        g_free(name);
        return JS_FALSE; /* wrong class */
    }

    value_str = NULL;
    if (strcmp(name, "name") == 0)
        value_str = g_param_spec_get_name(priv->gparam);
    else if (strcmp(name, "nick") == 0)
        value_str = g_param_spec_get_nick(priv->gparam);
    else if (strcmp(name, "blurb") == 0)
        value_str = g_param_spec_get_blurb(priv->gparam);

    g_free(name);

    if (value_str != NULL) {
        *value_p = STRING_TO_JSVAL(JS_NewStringCopyZ(context, value_str));
    }

    return JS_TRUE;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(param)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(param)
    GJS_NATIVE_CONSTRUCTOR_PRELUDE(param);
    GJS_INC_COUNTER(param);
    GJS_NATIVE_CONSTRUCTOR_FINISH(param);
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
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,
    JS_PropertyStub,
    param_get_prop,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
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

    ns = gjs_lookup_namespace_object_by_name(context, "GObject");

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
                                          "GObject",
                                          constructor_name,
                                          &gjs_param_class,
                                          /* constructor for instances (NULL for
                                           * none - just name the prototype like
                                           * Math - rarely correct)
                                           */
                                          gjs_param_constructor,
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
    Param *priv;

    if (gparam == NULL)
        return NULL;

    gjs_debug(GJS_DEBUG_GPARAM,
              "Wrapping %s '%s' on %s with JSObject",
              g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) gparam)),
              gparam->name,
              g_type_name(gparam->owner_type));

    proto = gjs_lookup_param_prototype(context);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GET_CLASS(context, proto), proto,
                                     gjs_get_import_global (context));

    priv = g_slice_new0(Param);
    JS_SetPrivate(context, obj, priv);
    priv->gparam = gparam;
    g_param_spec_ref (gparam);

    gjs_debug(GJS_DEBUG_GPARAM,
              "JSObject created with param instance %p type %s",
              priv->gparam, g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) priv->gparam)));

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
