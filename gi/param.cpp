/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <girepository.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>       // for JS_GetClass, JS_GetPropertyById
#include <jspubtd.h>     // for JSProto_TypeError

#include "gi/cwrapper.h"
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

extern struct JSClass gjs_param_class;

struct Param : GjsAutoParam {
    explicit Param(GParamSpec* param)
        : GjsAutoParam(param, GjsAutoTakeOwnership()) {}
};

[[nodiscard]] static GParamSpec* param_value(JSContext* cx,
                                             JS::HandleObject obj) {
    auto* priv = static_cast<Param*>(
        JS_GetInstancePrivate(cx, obj, &gjs_param_class, nullptr));
    return priv ? priv->get() : nullptr;
}

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
    if (!param_value(context, obj)) {
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

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_param_constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    if (!args.isConstructing()) {
        gjs_throw_constructor_error(cx);
        return false;
    }

    JS::RootedObject new_object(
        cx, JS_NewObjectForConstructor(cx, &gjs_param_class, args));
    if (!new_object)
        return false;

    GJS_INC_COUNTER(param);

    args.rval().setObject(*new_object);
    return true;
}

static void param_finalize(JSFreeOp*, JSObject* obj) {
    Param* priv = static_cast<Param*>(JS_GetPrivate(obj));
    gjs_debug_lifecycle(GJS_DEBUG_GPARAM, "finalize, obj %p priv %p", obj,
                        priv);
    if (!priv)
        return; /* wrong class? */

    GJS_DEC_COUNTER(param);
    JS_SetPrivate(obj, nullptr);
    delete priv;
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
    JS::RootedObject prototype(context), constructor(context);
    if (!gjs_init_class_dynamic(
            context, in_object, nullptr, "GObject", "ParamSpec",
            &gjs_param_class, gjs_param_constructor, 0,
            nullptr,  // props of prototype
            nullptr,  // funcs of prototype
            nullptr,  // props of constructor, MyConstructor.myprop
            nullptr,  // funcs of constructor
            &prototype, &constructor))
        return false;

    if (!gjs_wrapper_define_gtype_prop(context, constructor, G_TYPE_PARAM))
        return false;

    GjsAutoObjectInfo info = g_irepository_find_by_gtype(nullptr, G_TYPE_PARAM);
    if (!gjs_define_static_methods<InfoType::Object>(context, constructor,
                                                     G_TYPE_PARAM, info))
        return false;

    gjs_debug(GJS_DEBUG_GPARAM,
              "Defined class ParamSpec prototype is %p class %p in object %p",
              prototype.get(), &gjs_param_class, in_object.get());
    return true;
}

JSObject*
gjs_param_from_g_param(JSContext    *context,
                       GParamSpec   *gparam)
{
    JSObject *obj;

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
    auto* priv = new Param(gparam);
    JS_SetPrivate(obj, priv);

    gjs_debug(GJS_DEBUG_GPARAM,
              "JSObject created with param instance %p type %s", gparam,
              g_type_name(G_TYPE_FROM_INSTANCE(gparam)));

    return obj;
}

GParamSpec*
gjs_g_param_from_param(JSContext       *context,
                       JS::HandleObject obj)
{
    if (!obj)
        return nullptr;

    return param_value(context, obj);
}

bool
gjs_typecheck_param(JSContext       *context,
                    JS::HandleObject object,
                    GType            expected_type,
                    bool             throw_error)
{
    bool result;

    if (!gjs_typecheck_instance(context, object, &gjs_param_class, throw_error))
        return false;

    GParamSpec* param = param_value(context, object);
    if (!param) {
        if (throw_error) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is GObject.ParamSpec.prototype, not an object instance - "
                             "cannot convert to a GObject.ParamSpec instance");
        }

        return false;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a(G_TYPE_FROM_INSTANCE(param), expected_type);
    else
        result = true;

    if (!result && throw_error) {
        gjs_throw_custom(context, JSProto_TypeError, nullptr,
                         "Object is of type %s - cannot convert to %s",
                         g_type_name(G_TYPE_FROM_INSTANCE(param)),
                         g_type_name(expected_type));
    }

    return result;
}
