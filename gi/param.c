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
#include "arg.h"
#include "object.h"
#include "repo.h"
#include "gtype.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <util/log.h>

typedef struct {
    GParamSpec *gparam; /* NULL if we are the prototype and not an instance */
} Param;

static struct JSClass gjs_param_class;

GJS_DEFINE_PRIV_FROM_JS(Param, gjs_param_class)

static GIFieldInfo *
find_field_info(GIObjectInfo *info,
                gchar        *name)
{
    int i;
    GIFieldInfo *field_info;

    /* GParamSpecs aren't very big. We could optimize this so that it isn't
     * O(N), but for the biggest GParamSpec, N=5, so it doesn't really matter. */
    for (i = 0; i < g_object_info_get_n_fields((GIObjectInfo*)info); i++) {
        field_info = g_object_info_get_field((GIObjectInfo*)info, i);
        if (g_str_equal(name, g_base_info_get_name((GIBaseInfo*)field_info)))
            return field_info;

        g_base_info_unref((GIBaseInfo*)field_info);
    }

    return NULL;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
param_get_prop(JSContext *context,
               JSObject **obj,
               jsid      *id,
               jsval     *value_p)
{
    JSBool success;
    Param *priv;
    GParamSpec *pspec;
    char *name;
    GType gtype;
    GIObjectInfo *info = NULL, *parent_info = NULL;
    GIFieldInfo *field_info = NULL;
    GITypeInfo *type_info = NULL;
    GIArgument arg;

    if (!gjs_get_string_id(context, *id, &name))
        return JS_TRUE; /* not something we affect, but no error */

    priv = priv_from_js(context, *obj);

    if (priv == NULL) {
        g_free(name);
        return JS_FALSE; /* wrong class */
    }

    success = JS_FALSE;
    pspec = priv->gparam;

    gtype = G_TYPE_FROM_INSTANCE(pspec);
    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), gtype);

    if (info == NULL) {
        /* We may have a non-introspectable GParamSpec subclass here. Just return VOID. */
        *value_p = JSVAL_VOID;
        success = JS_TRUE;
        goto out;
    }

    parent_info = g_object_info_get_parent(info);

    field_info = find_field_info(info, name);

    if (field_info == NULL) {
        /* Try it on the parent GParamSpec for generic GParamSpec properties. */
        field_info = find_field_info(parent_info, name);
    }

    if (field_info == NULL) {
        *value_p = JSVAL_VOID;
        success = JS_TRUE;
        goto out;
    }

    type_info = g_field_info_get_type(field_info);

    if (!g_field_info_get_field(field_info, priv->gparam, &arg)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  g_base_info_get_name(info),
                  g_base_info_get_name((GIBaseInfo*)field_info));
        goto out;
    }

    if (!gjs_value_from_g_argument(context, value_p, type_info, &arg, TRUE))
        goto out;

    success = JS_TRUE;

 out:
    if (field_info != NULL)
        g_base_info_unref((GIBaseInfo*)field_info);
    if (type_info != NULL)
        g_base_info_unref((GIBaseInfo*)type_info);
    if (info != NULL)
        g_base_info_unref((GIBaseInfo*)info);
    if (parent_info != NULL)
        g_base_info_unref((GIBaseInfo*)parent_info);
    g_free(name);

    return success;
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

static JSBool
param_new_internal(JSContext *cx,
                   unsigned   argc,
                   jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    GParamSpec *pspec = NULL;
    JSBool ret = JS_FALSE;
    gchar *method_name;

    gchar *prop_name;
    JSObject *prop_gtype_jsobj;
    GType prop_gtype;
    GType prop_type;
    gchar *nick;
    gchar *blurb;
    GParamFlags flags;

    if (!gjs_parse_args(cx, "GObject.ParamSpec._new_internal",
                        "!sossi", argc, argv,
                        "prop_name", &prop_name,
                        "prop_gtype", &prop_gtype_jsobj,
                        "nick", &nick,
                        "blurb", &blurb,
                        "flags", &flags))
        return JS_FALSE;

    prop_gtype = gjs_gtype_get_actual_gtype(cx, prop_gtype_jsobj);
    prop_type = G_TYPE_FUNDAMENTAL(prop_gtype);

    method_name = g_strdup_printf("GObject.ParamSpec.%s",
                                  g_type_name(prop_type));

    argv += 5;
    argc -= 5;

    switch (prop_type) {
    case G_TYPE_UCHAR:
    case G_TYPE_CHAR:
	{
	    gchar *minimum, *maximum, *default_value;

            if (!gjs_parse_args(cx, method_name,
                                "sss", argc, argv,
                                "minimum", &minimum,
                                "maximum", &maximum,
                                "default_value", &default_value))
                goto out;

            if (prop_type == G_TYPE_CHAR)
                pspec = g_param_spec_char(prop_name, nick, blurb,
                                          minimum[0], maximum[0], default_value[0],
                                          flags);
            else
                pspec = g_param_spec_uchar(prop_name, nick, blurb,
                                           minimum[0], maximum[0], default_value[0],
                                           flags);
 
            g_free(minimum);
            g_free(maximum);
            g_free(default_value);
	}
	break;
    case G_TYPE_INT:
    case G_TYPE_UINT:
    case G_TYPE_LONG:
    case G_TYPE_ULONG:
    case G_TYPE_INT64:
    case G_TYPE_UINT64:
        {
            gint64 minimum, maximum, default_value;

            if (!gjs_parse_args(cx, method_name,
                                "ttt", argc, argv,
                                "minimum", &minimum,
                                "maximum", &maximum,
                                "default_value", &default_value))
                goto out;

            switch (prop_type) {
            case G_TYPE_INT:
                pspec = g_param_spec_int(prop_name, nick, blurb,
                                         minimum, maximum, default_value, flags);
                break;
            case G_TYPE_UINT:
                pspec = g_param_spec_uint(prop_name, nick, blurb,
                                          minimum, maximum, default_value, flags);
                break;
            case G_TYPE_LONG:
                pspec = g_param_spec_long(prop_name, nick, blurb,
                                          minimum, maximum, default_value, flags);
                break;
            case G_TYPE_ULONG:
                pspec = g_param_spec_ulong(prop_name, nick, blurb,
                                           minimum, maximum, default_value, flags);
                break;
            case G_TYPE_INT64:
                pspec = g_param_spec_int64(prop_name, nick, blurb,
                                           minimum, maximum, default_value, flags);
                break;
            case G_TYPE_UINT64:
                pspec = g_param_spec_uint64(prop_name, nick, blurb,
                                            minimum, maximum, default_value, flags);
                break;
            }
        }
        break;
    case G_TYPE_BOOLEAN:
        {
            gboolean default_value;

            if (!gjs_parse_args(cx, method_name,
                                "b", argc, argv,
                                "default_value", &default_value))
                goto out;

            default_value = JSVAL_TO_BOOLEAN(argv[0]);

            pspec = g_param_spec_boolean(prop_name, nick, blurb,
                                         default_value, flags);
        }
        break;
    case G_TYPE_ENUM:
        {
            JSObject *gtype_jsobj;
            GType gtype;
            GIEnumInfo *info;
            gint64 default_value;

            if (!gjs_parse_args(cx, method_name,
                                "ot", argc, argv,
                                "gtype", &gtype_jsobj,
                                "default_value", &default_value))
                goto out;

            gtype = gjs_gtype_get_actual_gtype(cx, gtype_jsobj);
            if (gtype == G_TYPE_NONE) {
                gjs_throw(cx, "Passed invalid GType to GParamSpecEnum constructor");
                goto out;
            }

            info = g_irepository_find_by_gtype(g_irepository_get_default(), gtype);

            if (!_gjs_enum_value_is_valid(cx, info, default_value))
                goto out;

            pspec = g_param_spec_enum(prop_name, nick, blurb,
                                      gtype, default_value, flags);
        }
        break;
    case G_TYPE_FLAGS:
        {
            JSObject *gtype_jsobj;
            GType gtype;
            gint64 default_value;

            if (!gjs_parse_args(cx, method_name,
                                "ot", argc, argv,
                                "gtype", &gtype_jsobj,
                                "default_value", &default_value))
                goto out;

            gtype = gjs_gtype_get_actual_gtype(cx, gtype_jsobj);
            if (gtype == G_TYPE_NONE) {
                gjs_throw(cx, "Passed invalid GType to GParamSpecFlags constructor");
                goto out;
            }

            if (!_gjs_flags_value_is_valid(cx, gtype, default_value))
                goto out;

            pspec = g_param_spec_flags(prop_name, nick, blurb,
                                       gtype, default_value, flags);
        }
        break;
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE:
        {
	    gfloat minimum, maximum, default_value;

            if (!gjs_parse_args(cx, "GObject.ParamSpec.float",
                                "fff", argc, argv,
                                "minimum", &minimum,
                                "maximum", &maximum,
                                "default_value", &default_value))
                goto out;

            if (prop_type == G_TYPE_FLOAT)
                pspec = g_param_spec_float(prop_name, nick, blurb,
                                           minimum, maximum, default_value, flags);
            else
                pspec = g_param_spec_double(prop_name, nick, blurb,
                                            minimum, maximum, default_value, flags);
        }
        break;
    case G_TYPE_STRING:
        {
            gchar *default_value;

            if (!gjs_parse_args(cx, method_name,
                                "s", argc, argv,
                                "default_value", &default_value))
                goto out;

            pspec = g_param_spec_string(prop_name, nick, blurb,
                                        default_value, flags);

            g_free (default_value);
        }
        break;
    case G_TYPE_PARAM:
        pspec = g_param_spec_param(prop_name, nick, blurb, prop_type, flags);
        break;
    case G_TYPE_BOXED:
        pspec = g_param_spec_boxed(prop_name, nick, blurb, prop_type, flags);
        break;
    case G_TYPE_POINTER:
        pspec = g_param_spec_pointer(prop_name, nick, blurb, flags);
        break;
    case G_TYPE_OBJECT:
        pspec = g_param_spec_object(prop_name, nick, blurb, prop_type, flags);
        break;
    default:
        gjs_throw(cx,
                  "Could not create param spec for type '%s'",
                  g_type_name(prop_gtype));
        goto out;
    }

    ret = JS_TRUE;

    jsval foo = OBJECT_TO_JSVAL(gjs_param_from_g_param(cx, pspec));

    JS_SET_RVAL(cx, vp, foo);
 out:

    g_free(method_name);
    g_free(prop_name);
    g_free(nick);
    g_free(blurb);

    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static struct JSClass gjs_param_class = {
    "GObject_ParamSpec",
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

static JSFunctionSpec gjs_param_constructor_funcs[] = {
    { "_new_internal", (JSNative)param_new_internal, 0, 0 },
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
    JSObject *constructor;

    constructor_name = "ParamSpec";

    gjs_object_get_property(context, in_object, constructor_name, &value);
    if (!JSVAL_IS_VOID(value)) {
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

    if (!gjs_init_class_dynamic(context, in_object,
                                NULL,
                                "GObject",
                                constructor_name,
                                &gjs_param_class,
                                gjs_param_constructor, 0,
                                /* props of prototype */
                                &gjs_param_proto_props[0],
                                /* funcs of prototype */
                                &gjs_param_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                gjs_param_constructor_funcs,
                                &prototype,
                                &constructor)) {
        gjs_fatal("Can't init class %s", constructor_name);
    }

    value = OBJECT_TO_JSVAL(gjs_gtype_create_gtype_wrapper(context, G_TYPE_PARAM));
    JS_DefineProperty(context, constructor, "$gtype", value,
                      NULL, NULL, JSPROP_PERMANENT);
    
    if (prototype_p)
        *prototype_p = prototype;

    gjs_debug(GJS_DEBUG_GPARAM, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

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
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    GJS_INC_COUNTER(param);
    priv = g_slice_new0(Param);
    JS_SetPrivate(obj, priv);
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

    return priv->gparam;
}

JSBool
gjs_typecheck_param(JSContext     *context,
                    JSObject      *object,
                    GType          expected_type,
                    JSBool         throw)
{
    Param *priv;
    JSBool result;

    if (!do_base_typecheck(context, object, throw))
        return JS_FALSE;

    priv = priv_from_js(context, object);

    if (priv->gparam == NULL) {
        if (throw) {
            gjs_throw_custom(context, "TypeError",
                             "Object is GObject.ParamSpec.prototype, not an object instance - "
                             "cannot convert to a GObject.ParamSpec instance");
        }

        return JS_FALSE;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (G_TYPE_FROM_INSTANCE (priv->gparam), expected_type);
    else
        result = JS_TRUE;

    if (!result && throw) {
        gjs_throw_custom(context, "TypeError",
                         "Object is of type %s - cannot convert to %s",
                         g_type_name(G_TYPE_FROM_INSTANCE (priv->gparam)),
                         g_type_name(expected_type));
    }

    return result;
}
