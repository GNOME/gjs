/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Intel Corporation
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#include <config.h>

#include <string>  // for string methods

#include <girepository/girepository.h>
#include <glib.h>

#include <js/Class.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/GCHashTable.h>  // for WeakCache
#include <js/Object.h>       // for GetClass
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>       // for InformalValueTypeName, JS_NewObjectWithGivenP...
#include <mozilla/HashTable.h>

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/info.h"
#include "gi/repo.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-root.h"  // for WeakPtr methods
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

namespace JS {
class CallArgs;
}

using mozilla::Maybe, mozilla::Some;

FundamentalInstance::FundamentalInstance(FundamentalPrototype* prototype,
                                         JS::HandleObject obj)
    : GIWrapperInstance(prototype, obj) {
    GJS_INC_COUNTER(fundamental_instance);
}

/*
 * FundamentalInstance::associate_js_instance:
 *
 * Associates @gfundamental with @object so that @object can be retrieved in the
 * future if you have a pointer to @gfundamental. (Assuming @object has not been
 * garbage collected in the meantime.)
 */
bool FundamentalInstance::associate_js_instance(JSContext* cx, JSObject* object,
                                                void* gfundamental) {
    m_ptr = gfundamental;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    if (!gjs->fundamental_table().putNew(gfundamental, object)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    debug_lifecycle(object, "associated JSObject with fundamental");

    ref();
    return true;
}

/**/

/* Find the first constructor */
[[nodiscard]]
static Maybe<GI::AutoFunctionInfo> find_fundamental_constructor(
    const GI::ObjectInfo info) {
    for (GI::AutoFunctionInfo func_info : info.methods()) {
        if (func_info.is_constructor())
            return Some(func_info);
    }

    return {};
}

/**/

bool FundamentalPrototype::resolve_interface(JSContext* cx,
                                             JS::HandleObject obj,
                                             bool* resolved, const char* name) {
    bool ret;
    GType *interfaces;
    guint n_interfaces;
    guint i;

    ret = true;
    interfaces = g_type_interfaces(gtype(), &n_interfaces);
    GI::Repository repo;
    for (i = 0; i < n_interfaces; i++) {
        Maybe<GI::AutoInterfaceInfo> iface_info{
            repo.find_by_gtype<GI::InfoTag::INTERFACE>(interfaces[i])};
        if (!iface_info)
            continue;

        Maybe<GI::AutoFunctionInfo> method_info{iface_info->method(name)};

        if (method_info && method_info->is_method()) {
            if (gjs_define_function(cx, obj, gtype(), method_info.ref())) {
                *resolved = true;
            } else {
                ret = false;
            }
        }
    }

    g_free(interfaces);
    return ret;
}

// See GIWrapperBase::resolve().
bool FundamentalPrototype::resolve_impl(JSContext* cx, JS::HandleObject obj,
                                        JS::HandleId id, bool* resolved) {
    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(cx, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    /* We are the prototype, so look for methods and other class properties */
    Maybe<GI::AutoFunctionInfo> method_info{info().method(prop_name.get())};

    if (method_info) {
        method_info->log_usage();
        if (method_info->is_method()) {
            /* we do not define deprecated methods in the prototype */
            if (method_info->is_deprecated()) {
                gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                          "Ignoring definition of deprecated method %s in "
                          "prototype %s",
                          method_info->name(), format_name().c_str());
                *resolved = false;
                return true;
            }

            gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                      "Defining method %s in prototype for %s",
                      method_info->name(), format_name().c_str());

            if (!gjs_define_function(cx, obj, gtype(), *method_info))
                return false;

            *resolved = true;
        }
    } else {
        *resolved = false;
    }

    return resolve_interface(cx, obj, resolved, prop_name.get());
}

/*
 * FundamentalInstance::invoke_constructor:
 *
 * Finds the type's static constructor method (the static method given by
 * FundamentalPrototype::constructor_info()) and invokes it with the given
 * arguments.
 */
bool FundamentalInstance::invoke_constructor(JSContext* context,
                                             JS::HandleObject obj,
                                             const JS::CallArgs& args,
                                             GIArgument* rvalue) {
    Maybe<const GI::FunctionInfo> constructor_info =
        get_prototype()->constructor_info();
    if (!constructor_info) {
        gjs_throw(context, "Couldn't find a constructor for type %s",
                  format_name().c_str());
        return false;
    }

    return gjs_invoke_constructor_from_c(context, *constructor_info, obj, args,
                                         rvalue);
}

// See GIWrapperBase::constructor().
bool FundamentalInstance::constructor_impl(JSContext* cx,
                                           JS::HandleObject object,
                                           const JS::CallArgs& argv) {
    GIArgument ret_value;

    if (!invoke_constructor(cx, object, argv, &ret_value) ||
        !associate_js_instance(cx, object, gjs_arg_get<void*>(&ret_value)))
        return false;

    Maybe<const GI::FunctionInfo> constructor_info =
        get_prototype()->constructor_info();
    g_assert(constructor_info);

    GI::StackTypeInfo return_info;
    constructor_info->load_return_type(&return_info);

    return gjs_gi_argument_release(cx, constructor_info->caller_owns(),
                                   return_info, &ret_value);
}

FundamentalInstance::~FundamentalInstance(void) {
    if (m_ptr) {
        unref();
        m_ptr = nullptr;
    }
    GJS_DEC_COUNTER(fundamental_instance);
}

FundamentalPrototype::FundamentalPrototype(const GI::ObjectInfo info,
                                           GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_ref_function(info.ref_function_pointer()),
      m_unref_function(info.unref_function_pointer()),
      m_get_value_function(info.get_value_function_pointer()),
      m_set_value_function(info.set_value_function_pointer()),
      m_constructor_info(find_fundamental_constructor(info)) {
    GJS_INC_COUNTER(fundamental_prototype);
}

FundamentalPrototype::~FundamentalPrototype(void) {
    GJS_DEC_COUNTER(fundamental_prototype);
}

// clang-format off
const struct JSClassOps FundamentalBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    &FundamentalBase::resolve,
    nullptr,  // mayResolve
    &FundamentalBase::finalize,
    nullptr,  // call
    nullptr,  // construct
    &FundamentalBase::trace
};

const struct JSClass FundamentalBase::klass = {
    "GFundamental_Object",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
    &FundamentalBase::class_ops
};
// clang-format on

// FIXME: assume info is non-null on main? Is it possible to have hidden
// fundamental types?
GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_lookup_fundamental_prototype(JSContext* context,
                                                  const GI::ObjectInfo info) {
    JS::RootedObject in_object{context,
                               gjs_lookup_namespace_object(context, info)};
    const char* constructor_name = info.name();

    if (G_UNLIKELY (!in_object))
        return nullptr;

    bool found;
    if (!JS_HasProperty(context, in_object, constructor_name, &found))
        return nullptr;

    JS::RootedValue value(context);
    if (found && !JS_GetProperty(context, in_object, constructor_name, &value))
        return nullptr;

    JS::RootedObject constructor(context);
    if (value.isUndefined()) {
        /* In case we're looking for a private type, and we don't find it,
           we need to define it first.
        */
        if (!FundamentalPrototype::define_class(context, in_object, info,
                                                &constructor))
            return nullptr;
    } else {
        if (G_UNLIKELY(!value.isObject())) {
            gjs_throw(context,
                      "Fundamental constructor was not an object, it was a %s",
                      JS::InformalValueTypeName(value));
            return nullptr;
        }

        constructor = &value.toObject();
    }

    g_assert(constructor);

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject prototype(context);
    if (!gjs_object_require_property(context, constructor, "constructor object",
                                     atoms.prototype(), &prototype))
        return nullptr;

    return prototype;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_lookup_fundamental_prototype_from_gtype(JSContext* cx,
                                                             GType gtype) {
    Maybe<GI::AutoObjectInfo> info;
    GI::Repository repo;

    /* A given gtype might not have any definition in the introspection
     * data. If that's the case, try to look for a definition of any of the
     * parent type. */
    while (gtype != G_TYPE_INVALID &&
           !(info = repo.find_by_gtype<GI::InfoTag::OBJECT>(gtype)))
        gtype = g_type_parent(gtype);

    return gjs_lookup_fundamental_prototype(cx, info.ref());
}

// Overrides GIWrapperPrototype::get_parent_proto().
bool FundamentalPrototype::get_parent_proto(
    JSContext* cx, JS::MutableHandleObject proto) const {
    GType parent_gtype = g_type_parent(gtype());
    if (parent_gtype != G_TYPE_INVALID) {
        proto.set(
            gjs_lookup_fundamental_prototype_from_gtype(cx, parent_gtype));
        if (!proto)
            return false;
    }
    return true;
}

// Overrides GIWrapperPrototype::constructor_nargs().
unsigned FundamentalPrototype::constructor_nargs(void) const {
    if (m_constructor_info)
        return m_constructor_info->n_args();
    return 0;
}

/*
 * FundamentalPrototype::define_class:
 * @in_object: Object where the constructor is stored, typically a repo object.
 * @info: Introspection info for the fundamental class.
 * @constructor: Return location for the constructor object.
 *
 * Define a fundamental class constructor and prototype, including all the
 * necessary methods and properties. Provides the constructor object as an out
 * parameter, for convenience elsewhere.
 */
bool FundamentalPrototype::define_class(JSContext* cx,
                                        JS::HandleObject in_object,
                                        const GI::ObjectInfo info,
                                        JS::MutableHandleObject constructor) {
    JS::RootedObject prototype(cx);
    FundamentalPrototype* priv = FundamentalPrototype::create_class(
        cx, in_object, info, info.gtype(), constructor, &prototype);
    if (!priv)
        return false;

    if (info.fields().size() > 0) {
        gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                  "Fundamental type '%s' apparently has accessible fields. GJS "
                  "has no support for this yet, ignoring these.",
                  priv->format_name().c_str());
    }

    return true;
}

/*
 * FundamentalInstance::object_for_c_ptr:
 *
 * Given a pointer to a C fundamental object, returns a JS object. This JS
 * object may have been cached, or it may be newly created.
 */
JSObject* FundamentalInstance::object_for_c_ptr(JSContext* context,
                                                void* gfundamental) {
    if (!gfundamental) {
        gjs_throw(context, "Cannot get JSObject for null fundamental pointer");
        return nullptr;
    }

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    auto p = gjs->fundamental_table().lookup(gfundamental);
    if (p)
        return p->value();

    gjs_debug_marshal(GJS_DEBUG_GFUNDAMENTAL,
                      "Wrapping fundamental %p with JSObject", gfundamental);

    JS::RootedObject proto(context,
        gjs_lookup_fundamental_prototype_from_gtype(context,
                                                    G_TYPE_FROM_INSTANCE(gfundamental)));
    if (!proto)
        return nullptr;

    JS::RootedObject object(context, JS_NewObjectWithGivenProto(
                                         context, JS::GetClass(proto), proto));

    if (!object)
        return nullptr;

    auto* priv = FundamentalInstance::new_for_js_object(context, object);

    if (!priv->associate_js_instance(context, object, gfundamental))
        return nullptr;

    return object;
}

/*
 * FundamentalPrototype::for_gtype:
 *
 * Returns the FundamentalPrototype instance associated with the given GType.
 * Use this if you don't have the prototype object.
 */
FundamentalPrototype* FundamentalPrototype::for_gtype(JSContext* cx,
                                                      GType gtype) {
    JS::RootedObject proto(
        cx, gjs_lookup_fundamental_prototype_from_gtype(cx, gtype));
    if (!proto)
        return nullptr;

    return FundamentalPrototype::for_js(cx, proto);
}

bool FundamentalInstance::object_for_gvalue(
    JSContext* cx, const GValue* value, GType gtype,
    JS::MutableHandleObject object_out) {
    auto* proto_priv = FundamentalPrototype::for_gtype(cx, gtype);
    void* fobj = nullptr;

    if (!proto_priv->call_get_value_function(value, &fobj)) {
        if (!G_VALUE_HOLDS(value, gtype) || !g_value_fits_pointer(value)) {
            gjs_throw(cx,
                      "Failed to convert GValue of type %s to a fundamental %s "
                      "instance",
                      G_VALUE_TYPE_NAME(value), g_type_name(gtype));
            return false;
        }

        fobj = g_value_peek_pointer(value);
    }

    if (!fobj) {
        object_out.set(nullptr);
        return true;
    }

    object_out.set(FundamentalInstance::object_for_c_ptr(cx, fobj));
    return object_out.get() != nullptr;
}

bool FundamentalBase::to_gvalue(JSContext* cx, JS::HandleObject obj,
                                GValue* gvalue) {
    FundamentalBase* priv;
    if (!for_js_typecheck(cx, obj, &priv) ||
        !priv->check_is_instance(cx, "convert to GValue"))
        return false;

    auto* instance = priv->to_instance();
    if (!instance->set_value(gvalue)) {
        if (g_value_type_compatible(instance->gtype(), G_VALUE_TYPE(gvalue))) {
            g_value_set_instance(gvalue, instance->m_ptr);
            return true;
        } else if (g_value_type_transformable(instance->gtype(),
                                              G_VALUE_TYPE(gvalue))) {
            Gjs::AutoGValue instance_value;
            g_value_init(&instance_value, instance->gtype());
            g_value_set_instance(&instance_value, instance->m_ptr);
            if (g_value_transform(&instance_value, gvalue))
                return true;
        }

        gjs_throw(cx,
                  "Fundamental object of type %s does not support conversion "
                  "to a GValue of type %s",
                  instance->type_name(), G_VALUE_TYPE_NAME(gvalue));
        return false;
    }

    return true;
}

void* FundamentalInstance::copy_ptr(JSContext* cx, GType gtype,
                                    void* gfundamental) {
    auto* priv = FundamentalPrototype::for_gtype(cx, gtype);
    return priv->call_ref_function(gfundamental);
}
