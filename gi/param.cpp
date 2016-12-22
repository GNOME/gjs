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
#include "function.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem.h"

#include <util/log.h>

typedef struct {
    GParamSpec *gparam; /* NULL if we are the prototype and not an instance */
} Param;

extern struct JSClass gjs_param_class;

GJS_DEFINE_PRIV_FROM_JS(Param, gjs_param_class)

/*
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static bool
param_new_resolve(JSContext *context,
                  JS::HandleObject obj,
                  JS::HandleId id,
                  JS::MutableHandleObject objp)
{
    GIObjectInfo *info = NULL;
    GIFunctionInfo *method_info;
    Param *priv;
    char *name;
    bool ret = false;

    if (!gjs_get_string_id(context, id, &name))
        return true; /* not resolved, but no error */

    priv = priv_from_js(context, obj);

    if (priv != NULL) {
        /* instance, not prototype */
        ret = true;
        goto out;
    }

    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), G_TYPE_PARAM);
    method_info = g_object_info_find_method(info, name);

    if (method_info == NULL) {
        ret = true;
        goto out;
    }
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

    if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for GObject.ParamSpec",
                  g_base_info_get_name( (GIBaseInfo*) method_info));

        if (gjs_define_function(context, obj, G_TYPE_PARAM, method_info) == NULL) {
            g_base_info_unref( (GIBaseInfo*) method_info);
            goto out;
        }

        objp.set(obj); /* we defined the prop in obj */
    }

    g_base_info_unref( (GIBaseInfo*) method_info);

    ret = true;
 out:
    g_free(name);
    if (info != NULL)
        g_base_info_unref( (GIBaseInfo*)info);

    return ret;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(param)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(param)
    GJS_NATIVE_CONSTRUCTOR_PRELUDE(param);
    GJS_INC_COUNTER(param);
    GJS_NATIVE_CONSTRUCTOR_FINISH(param);
    return true;
}

static void
param_finalize(JSFreeOp *fop,
               JSObject *obj)
{
    Param *priv;

    priv = (Param*) JS_GetPrivate(obj);
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
 */
struct JSClass gjs_param_class = {
    "GObject_ParamSpec",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_BACKGROUND_FINALIZE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) param_new_resolve,
    JS_ConvertStub,
    param_finalize
};

JSPropertySpec gjs_param_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_param_proto_funcs[] = {
    JS_FS_END
};

static JSFunctionSpec gjs_param_constructor_funcs[] = {
    JS_FS_END
};

static JSObject*
gjs_lookup_param_prototype(JSContext    *context)
{
    JS::RootedId gobject_name(context, gjs_intern_string_to_id(context, "GObject"));
    JS::RootedObject in_object(context,
        gjs_lookup_namespace_object_by_name(context, gobject_name));

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, "ParamSpec", &value))
        return NULL;

    if (G_UNLIKELY (!value.isObject()))
        return NULL;

    JS::RootedObject constructor(context, &value.toObject());
    g_assert(constructor != NULL);

    if (!gjs_object_get_property_const(context, constructor,
                                       GJS_STRING_PROTOTYPE, &value))
        return NULL;

    if (G_UNLIKELY (!value.isObjectOrNull()))
        return NULL;

    return value.toObjectOrNull();
}

void
gjs_define_param_class(JSContext       *context,
                       JS::HandleObject in_object)
{
    const char *constructor_name;
    JS::RootedObject prototype(context), constructor(context);
    GIObjectInfo *info;

    constructor_name = "ParamSpec";

    if (!gjs_init_class_dynamic(context, in_object,
                                JS::NullPtr(),
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
        g_error("Can't init class %s", constructor_name);
    }

    JS::RootedObject gtype_obj(context,
        gjs_gtype_create_gtype_wrapper(context, G_TYPE_PARAM));
    JS_DefineProperty(context, constructor, "$gtype", gtype_obj, JSPROP_PERMANENT);

    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), G_TYPE_PARAM);
    gjs_object_define_static_methods(context, constructor, G_TYPE_PARAM, info);
    g_base_info_unref( (GIBaseInfo*) info);

    gjs_debug(GJS_DEBUG_GPARAM, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype.get(), JS_GetClass(prototype),
              in_object.get());
}

JSObject*
gjs_param_from_g_param(JSContext    *context,
                       GParamSpec   *gparam)
{
    JSObject *obj;
    Param *priv;

    if (gparam == NULL)
        return NULL;

    gjs_debug(GJS_DEBUG_GPARAM,
              "Wrapping %s '%s' on %s with JSObject",
              g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) gparam)),
              gparam->name,
              g_type_name(gparam->owner_type));

    JS::RootedObject proto(context, gjs_lookup_param_prototype(context));
    JS::RootedObject global(context, gjs_get_import_global(context));

    obj = JS_NewObjectWithGivenProto(context, JS_GetClass(proto), proto, global);

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
gjs_g_param_from_param(JSContext       *context,
                       JS::HandleObject obj)
{
    Param *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    return priv->gparam;
}

bool
gjs_typecheck_param(JSContext       *context,
                    JS::HandleObject object,
                    GType            expected_type,
                    bool             throw_error)
{
    Param *priv;
    bool result;

    if (!do_base_typecheck(context, object, throw_error))
        return false;

    priv = priv_from_js(context, object);

    if (priv->gparam == NULL) {
        if (throw_error) {
            gjs_throw_custom(context, "TypeError", NULL,
                             "Object is GObject.ParamSpec.prototype, not an object instance - "
                             "cannot convert to a GObject.ParamSpec instance");
        }

        return false;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (G_TYPE_FROM_INSTANCE (priv->gparam), expected_type);
    else
        result = true;

    if (!result && throw_error) {
        gjs_throw_custom(context, "TypeError", NULL,
                         "Object is of type %s - cannot convert to %s",
                         g_type_name(G_TYPE_FROM_INSTANCE (priv->gparam)),
                         g_type_name(expected_type));
    }

    return result;
}
