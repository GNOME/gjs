/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <girepository.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gi/function.h"
#include "gi/param.h"
#include "gi/repo.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "util/log.h"

typedef struct {
    GParamSpec* gparam;  // nullptr if we are the prototype and not an instance
} Param;

extern struct JSClass gjs_param_class;

GJS_DEFINE_PRIV_FROM_JS(Param, gjs_param_class)

/*
 * The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
param_resolve(JSContext       *context,
              JS::HandleObject obj,
              JS::HandleId     id,
              bool            *resolved)
{
    Param* priv = priv_from_js(context, obj);
    if (!priv) {
        /* instance, not prototype */
        *resolved = false;
        return true;
    }

    JS::UniqueChars name;
    if (!gjs_get_string_id(context, id, &name))
        return false;
    if (!name) {
        *resolved = false;
        return true; /* not resolved, but no error */
    }

    GjsAutoObjectInfo info = g_irepository_find_by_gtype(nullptr, G_TYPE_PARAM);
    GjsAutoFunctionInfo method_info =
        g_object_info_find_method(info, name.get());

    if (!method_info) {
        *resolved = false;
        return true;
    }
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(method_info);
#endif

    if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for GObject.ParamSpec",
                  method_info.name());

        if (!gjs_define_function(context, obj, G_TYPE_PARAM, method_info))
            return false;

        *resolved = true; /* we defined the prop in obj */
    }

    return true;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(param)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(param)
    GJS_NATIVE_CONSTRUCTOR_PRELUDE(param);
    GJS_INC_COUNTER(param);
    GJS_NATIVE_CONSTRUCTOR_FINISH(param);
    return true;
}

static void param_finalize(JSFreeOp*, JSObject* obj) {
    Param *priv;

    priv = (Param*) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GPARAM,
                        "finalize, obj %p priv %p", obj, priv);
    if (!priv)
        return; /* wrong class? */

    g_clear_pointer(&priv->gparam, g_param_spec_unref);

    GJS_DEC_COUNTER(param);
    g_slice_free(Param, priv);
}


/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_param_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    param_resolve,
    nullptr,  // mayResolve
    param_finalize};

struct JSClass gjs_param_class = {
    "GObject_ParamSpec",
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
    &gjs_param_class_ops
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

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
gjs_lookup_param_prototype(JSContext    *context)
{
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject in_object(
        context, gjs_lookup_namespace_object_by_name(context, atoms.gobject()));

    if (G_UNLIKELY (!in_object))
        return nullptr;

    JS::RootedValue value(context);
    if (!JS_GetPropertyById(context, in_object, atoms.param_spec(), &value) ||
        G_UNLIKELY(!value.isObject()))
        return nullptr;

    JS::RootedObject constructor(context, &value.toObject());
    g_assert(constructor);

    if (!JS_GetPropertyById(context, constructor, atoms.prototype(), &value) ||
        G_UNLIKELY(!value.isObjectOrNull()))
        return nullptr;

    return value.toObjectOrNull();
}

bool
gjs_define_param_class(JSContext       *context,
                       JS::HandleObject in_object)
{
    const char *constructor_name;
    JS::RootedObject prototype(context), constructor(context);

    constructor_name = "ParamSpec";

    if (!gjs_init_class_dynamic(
            context, in_object, nullptr, "GObject", constructor_name,
            &gjs_param_class, gjs_param_constructor, 0,
            gjs_param_proto_props,  // props of prototype
            gjs_param_proto_funcs,  // funcs of prototype
            nullptr,  // props of constructor, MyConstructor.myprop
            gjs_param_constructor_funcs,  // funcs of constructor
            &prototype, &constructor))
        return false;

    if (!gjs_wrapper_define_gtype_prop(context, constructor, G_TYPE_PARAM))
        return false;

    GjsAutoObjectInfo info = g_irepository_find_by_gtype(nullptr, G_TYPE_PARAM);
    if (!gjs_define_static_methods<InfoType::Object>(context, constructor,
                                                     G_TYPE_PARAM, info))
        return false;

    gjs_debug(GJS_DEBUG_GPARAM, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype.get(), JS_GetClass(prototype),
              in_object.get());
    return true;
}

JSObject*
gjs_param_from_g_param(JSContext    *context,
                       GParamSpec   *gparam)
{
    JSObject *obj;
    Param *priv;

    if (!gparam)
        return nullptr;

    gjs_debug(GJS_DEBUG_GPARAM,
              "Wrapping %s '%s' on %s with JSObject",
              g_type_name(G_TYPE_FROM_INSTANCE((GTypeInstance*) gparam)),
              gparam->name,
              g_type_name(gparam->owner_type));

    JS::RootedObject proto(context, gjs_lookup_param_prototype(context));

    obj = JS_NewObjectWithGivenProto(context, JS_GetClass(proto), proto);

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

    if (!obj)
        return nullptr;

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

    if (!priv->gparam) {
        if (throw_error) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
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
        gjs_throw_custom(context, JSProto_TypeError, nullptr,
                         "Object is of type %s - cannot convert to %s",
                         g_type_name(G_TYPE_FROM_INSTANCE (priv->gparam)),
                         g_type_name(expected_type));
    }

    return result;
}
