/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <stdint.h>

#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>  // for JS::GetArrayLength
#include <js/CallArgs.h>
#include <js/PropertyAndElement.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>  // for JS_NewPlainObject
#include <mozilla/Maybe.h>

#ifndef G_DISABLE_ASSERT
#    include <js/CallAndConstruct.h>  // for IsCallable
#endif

#include "gi/closure.h"
#include "gi/gobject.h"
#include "gi/gtype.h"
#include "gi/interface.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/private.h"
#include "gi/repo.h"
#include "gi/value.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

using mozilla::Nothing;

/* gi/private.cpp - private "imports._gi" module with operations that we need
 * to use from JS in order to create GObject classes, but should not be exposed
 * to client code.
 */

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_override_property(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars name;
    JS::RootedObject type(cx);

    if (!gjs_parse_call_args(cx, "override_property", args, "so", "name", &name,
                             "type", &type))
        return false;

    GType gtype;
    if (!gjs_gtype_get_actual_gtype(cx, type, &gtype))
        return false;
    if (gtype == G_TYPE_INVALID) {
        gjs_throw(cx, "Invalid parameter type was not a GType");
        return false;
    }

    GParamSpec* pspec;
    if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        auto* interface_type =
            static_cast<GTypeInterface*>(g_type_default_interface_ref(gtype));
        pspec = g_object_interface_find_property(interface_type, name.get());
        g_type_default_interface_unref(interface_type);
    } else {
        Gjs::AutoTypeClass<GObjectClass> class_type{gtype};
        pspec = g_object_class_find_property(class_type, name.get());
    }

    if (!pspec) {
        gjs_throw(cx, "No such property '%s' to override on type '%s'",
                  name.get(), g_type_name(gtype));
        return false;
    }

    Gjs::AutoParam new_pspec{g_param_spec_override(name.get(), pspec)};

    g_param_spec_set_qdata(new_pspec, ObjectBase::custom_property_quark(),
                           GINT_TO_POINTER(1));

    args.rval().setObject(*gjs_param_from_g_param(cx, new_pspec));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool validate_interfaces_and_properties_args(JSContext* cx,
                                                    JS::HandleObject interfaces,
                                                    JS::HandleObject properties,
                                                    uint32_t* n_interfaces,
                                                    uint32_t* n_properties) {
    bool is_array;
    if (!JS::IsArrayObject(cx, interfaces, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter interfaces (expected Array)");
        return false;
    }

    uint32_t n_int;
    if (!JS::GetArrayLength(cx, interfaces, &n_int))
        return false;

    if (!JS::IsArrayObject(cx, properties, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter properties (expected Array)");
        return false;
    }

    uint32_t n_prop;
    if (!JS::GetArrayLength(cx, properties, &n_prop))
        return false;

    if (n_interfaces)
        *n_interfaces = n_int;
    if (n_properties)
        *n_properties = n_prop;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool save_properties_for_class_init(JSContext* cx,
                                           JS::HandleObject properties,
                                           uint32_t n_properties, GType gtype) {
    AutoParamArray properties_native;
    JS::RootedValue prop_val(cx);
    JS::RootedObject prop_obj(cx);
    for (uint32_t i = 0; i < n_properties; i++) {
        if (!JS_GetElement(cx, properties, i, &prop_val))
            return false;

        if (!prop_val.isObject()) {
            gjs_throw(cx, "Invalid parameter, expected object");
            return false;
        }

        prop_obj = &prop_val.toObject();
        if (!gjs_typecheck_param(cx, prop_obj, G_TYPE_NONE, true))
            return false;

        properties_native.emplace_back(
            g_param_spec_ref(gjs_g_param_from_param(cx, prop_obj)));
    }
    push_class_init_properties(gtype, &properties_native);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool get_interface_gtypes(JSContext* cx, JS::HandleObject interfaces,
                                 uint32_t n_interfaces, GType* iface_types) {
    for (uint32_t ix = 0; ix < n_interfaces; ix++) {
        JS::RootedValue iface_val(cx);
        if (!JS_GetElement(cx, interfaces, ix, &iface_val))
            return false;

        if (!iface_val.isObject()) {
            gjs_throw(
                cx, "Invalid parameter interfaces (element %d was not a GType)",
                ix);
            return false;
        }

        JS::RootedObject iface(cx, &iface_val.toObject());
        GType iface_type;
        if (!gjs_gtype_get_actual_gtype(cx, iface, &iface_type))
            return false;
        if (iface_type == G_TYPE_INVALID) {
            gjs_throw(
                cx, "Invalid parameter interfaces (element %d was not a GType)",
                ix);
            return false;
        }

        iface_types[ix] = iface_type;
    }
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool create_wrapper_array(JSContext* cx, JS::HandleObject prototype,
                                 GType type, JS::MutableHandleValue rval) {
    JS::RootedObject gtype_wrapper(cx,
                                   gjs_gtype_create_gtype_wrapper(cx, type));
    if (!gtype_wrapper)
        return false;

    JS::RootedValueArray<2> tuple(cx);
    tuple[0].setObject(*prototype);
    tuple[1].setObject(*gtype_wrapper);

    JS::RootedObject array(cx, JS::NewArrayObject(cx, tuple));
    if (!array)
        return false;

    rval.setObject(*array);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_interface_impl(JSContext* cx, const char* name,
                                        JS::HandleObject interfaces,
                                        JS::HandleObject properties,
                                        GType* gtype) {
    uint32_t n_interfaces, n_properties;
    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    Gjs::AutoPointer<GType> iface_types{g_new(GType, n_interfaces)};

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name);
        return false;
    }

    GTypeInfo type_info = gjs_gobject_interface_info;
    GType interface_type = g_type_register_static(G_TYPE_INTERFACE, name,
                                                  &type_info, GTypeFlags(0));

    g_type_set_qdata(interface_type, ObjectBase::custom_type_quark(),
                     GINT_TO_POINTER(1));

    if (!save_properties_for_class_init(cx, properties, n_properties,
                                        interface_type))
        return false;

    for (uint32_t ix = 0; ix < n_interfaces; ix++)
        g_type_interface_add_prerequisite(interface_type, iface_types[ix]);

    *gtype = interface_type;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_interface(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars name;
    JS::RootedObject interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_interface", args, "soo", "name",
                             &name, "interfaces", &interfaces, "properties",
                             &properties))
        return false;

    GType interface_type;
    if (!gjs_register_interface_impl(cx, name.get(), interfaces, properties,
                                     &interface_type))
        return false;

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    if (!module)
        return false;  // error will have been thrown already

    JS::RootedObject constructor(cx), ignored_prototype(cx);
    if (!InterfacePrototype::create_class(cx, module, Nothing{}, interface_type,
                                          &constructor, &ignored_prototype))
        return false;

    args.rval().setObject(*constructor);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_interface_with_class(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars name;
    JS::RootedObject klass(cx), interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_interface_with_class", args, "osoo",
                             "class", &klass, "name", &name, "interfaces",
                             &interfaces, "properties", &properties))
        return false;

    GType interface_type;
    if (!gjs_register_interface_impl(cx, name.get(), interfaces, properties,
                                     &interface_type))
        return false;

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    if (!module)
        return false;  // error will have been thrown already

    JS::RootedObject prototype(cx);
    if (!InterfacePrototype::wrap_class(cx, module, Nothing{}, interface_type,
                                        klass, &prototype))
        return false;

    return create_wrapper_array(cx, prototype, interface_type, args.rval());
}

static inline void gjs_add_interface(GType instance_type,
                                     GType interface_type) {
    static GInterfaceInfo interface_vtable{nullptr, nullptr, nullptr};
    g_type_add_interface_static(instance_type, interface_type,
                                &interface_vtable);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_type_impl(JSContext* cx, const char* name,
                                   GTypeFlags type_flags,
                                   JS::HandleObject parent,
                                   JS::HandleObject interfaces,
                                   JS::HandleObject properties,
                                   GType** iface_types_out,
                                   uint32_t* n_interfaces_out, GType* gtype) {
    if (!parent)
        return false;

    /* Don't pass the argv to it, as otherwise we will log about the callee
     * while we only care about the parent object type. */
    ObjectBase* parent_priv;
    if (!ObjectBase::for_js_typecheck(cx, parent, &parent_priv))
        return false;

    uint32_t n_interfaces, n_properties;
    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    Gjs::AutoPointer<GType> iface_types{g_new(GType, n_interfaces)};

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name);
        return false;
    }

    /* We checked parent above, in ObjectBase::for_js_typecheck() */
    g_assert(parent_priv);

    GTypeQuery query;
    g_type_query(parent_priv->gtype(), &query);

    if (G_UNLIKELY(
            g_type_test_flags(parent_priv->gtype(), G_TYPE_FLAG_FINAL))) {
        gjs_throw(cx, "Cannot inherit from a final type");
        return false;
    }

    GTypeInfo type_info = gjs_gobject_class_info;
    type_info.class_size = query.class_size;
    type_info.instance_size = query.instance_size;

    GType instance_type = g_type_register_static(parent_priv->gtype(), name,
                                                 &type_info, type_flags);

    g_type_set_qdata(instance_type, ObjectBase::custom_type_quark(),
                     GINT_TO_POINTER(1));

    if (!save_properties_for_class_init(cx, properties, n_properties,
                                        instance_type))
        return false;

    for (uint32_t ix = 0; ix < n_interfaces; ix++)
        gjs_add_interface(instance_type, iface_types[ix]);

    *gtype = instance_type;
    *n_interfaces_out = n_interfaces;
    *iface_types_out = iface_types.release();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_type(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars name;
    GTypeFlags type_flags;
    JS::RootedObject parent(cx), interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_type", argv, "osioo", "parent",
                             &parent, "name", &name, "flags", &type_flags,
                             "interfaces", &interfaces, "properties",
                             &properties))
        return false;

    GType instance_type;
    Gjs::AutoPointer<GType> iface_types;
    uint32_t n_interfaces;
    if (!gjs_register_type_impl(cx, name.get(), type_flags, parent, interfaces,
                                properties, iface_types.out(), &n_interfaces,
                                &instance_type))
        return false;

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    JS::RootedObject constructor(cx), prototype(cx);
    if (!ObjectPrototype::define_class(cx, module, Nothing{}, instance_type,
                                       iface_types, n_interfaces, &constructor,
                                       &prototype))
        return false;

    auto* priv = ObjectPrototype::for_js(cx, prototype);
    priv->set_type_qdata();

    argv.rval().setObject(*constructor);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_register_type_with_class(JSContext* cx, unsigned argc,
                                         JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars name;
    GTypeFlags type_flags;
    JS::RootedObject klass(cx), parent(cx), interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_type_with_class", argv, "oosioo",
                             "class", &klass, "parent", &parent, "name", &name,
                             "flags", &type_flags, "interfaces", &interfaces,
                             "properties", &properties))
        return false;

    GType instance_type;
    uint32_t n_interfaces;
    Gjs::AutoPointer<GType> iface_types;
    if (!gjs_register_type_impl(cx, name.get(), type_flags, parent, interfaces,
                                properties, iface_types.out(), &n_interfaces,
                                &instance_type))
        return false;

    // create a custom JSClass
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    if (!module)
        return false;

    JS::RootedObject prototype(cx);
    ObjectPrototype* priv = ObjectPrototype::wrap_class(
        cx, module, Nothing{}, instance_type, klass, &prototype);
    if (!priv)
        return false;

    priv->set_interfaces(iface_types, n_interfaces);
    priv->set_type_qdata();

    return create_wrapper_array(cx, prototype, instance_type, argv.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_signal_new(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars signal_name;
    int32_t flags, accumulator_enum;
    JS::RootedObject gtype_obj(cx), return_gtype_obj(cx), params_obj(cx);
    if (!gjs_parse_call_args(cx, "signal_new", args, "osiioo", "gtype",
                             &gtype_obj, "signal name", &signal_name, "flags",
                             &flags, "accumulator", &accumulator_enum,
                             "return gtype", &return_gtype_obj, "params",
                             &params_obj))
        return false;

    /* we only support standard accumulators for now */
    GSignalAccumulator accumulator;
    switch (accumulator_enum) {
        case 1:
            accumulator = g_signal_accumulator_first_wins;
            break;
        case 2:
            accumulator = g_signal_accumulator_true_handled;
            break;
        case 0:
        default:
            accumulator = nullptr;
    }

    GType return_type;
    if (!gjs_gtype_get_actual_gtype(cx, return_gtype_obj, &return_type))
        return false;

    if (accumulator == g_signal_accumulator_true_handled &&
        return_type != G_TYPE_BOOLEAN) {
        gjs_throw(cx,
                  "GObject.SignalAccumulator.TRUE_HANDLED can only be used "
                  "with boolean signals");
        return false;
    }

    uint32_t n_parameters;
    if (!JS::GetArrayLength(cx, params_obj, &n_parameters))
        return false;

    Gjs::AutoPointer<GType> params{g_new(GType, n_parameters)};
    JS::RootedValue gtype_val(cx);
    for (uint32_t ix = 0; ix < n_parameters; ix++) {
        if (!JS_GetElement(cx, params_obj, ix, &gtype_val) ||
            !gtype_val.isObject()) {
            gjs_throw(cx, "Invalid signal parameter number %d", ix);
            return false;
        }

        JS::RootedObject gjs_gtype(cx, &gtype_val.toObject());
        if (!gjs_gtype_get_actual_gtype(cx, gjs_gtype, &params[ix]))
            return false;
    }

    GType gtype;
    if (!gjs_gtype_get_actual_gtype(cx, gtype_obj, &gtype))
        return false;

    unsigned signal_id = g_signal_newv(
        signal_name.get(), gtype, GSignalFlags(flags),
        /* class closure */ nullptr, accumulator, /* accu_data */ nullptr,
        /* c_marshaller */ nullptr, return_type, n_parameters, params);

    // FIXME: what if ID is greater than int32 max?
    args.rval().setInt32(signal_id);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_lookup_constructor(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject gtype_obj(cx);
    if (!gjs_parse_call_args(cx, "lookupConstructor", args, "o", "gtype",
                             &gtype_obj))
        return false;

    GType gtype;
    if (!gjs_gtype_get_actual_gtype(cx, gtype_obj, &gtype))
        return false;

    if (gtype == G_TYPE_NONE) {
        gjs_throw(cx, "Invalid GType for constructor lookup");
        return false;
    }

    return gjs_lookup_object_constructor(cx, gtype, args.rval());
}

template <GjsSymbolAtom GjsAtoms::*member>
GJS_JSAPI_RETURN_CONVENTION static bool symbol_getter(JSContext* cx,
                                                      unsigned argc,
                                                      JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    args.rval().setSymbol((atoms.*member)().toSymbol());
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_associate_closure(JSContext* context, unsigned argc,
                                  JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject func_obj(context);
    JS::RootedObject target_obj(context);
    Gjs::Closure::Ptr closure;
    Gjs::AutoGValue value(G_TYPE_CLOSURE);
    ObjectInstance* obj;

    if (!gjs_parse_call_args(context, "associateClosure", argv, "oo", "object",
                             &target_obj, "func", &func_obj))
        return false;

    g_assert(JS::IsCallable(func_obj) &&
             "associateClosure's function must be callable");

    obj = ObjectInstance::for_js(context, target_obj);
    if (!obj)
        return false;

    closure =
        Gjs::Closure::create_marshaled(context, func_obj, "wrapped", false);

    if (!obj->associate_closure(context, closure))
        return false;

    g_value_set_boxed(&value, closure);
    return gjs_value_from_g_value(context, argv.rval(), &value);
}

static JSFunctionSpec private_module_funcs[] = {
    JS_FN("override_property", gjs_override_property, 2, GJS_MODULE_PROP_FLAGS),
    JS_FN("register_interface", gjs_register_interface, 3,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("register_interface_with_class", gjs_register_interface_with_class, 4,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("register_type", gjs_register_type, 4, GJS_MODULE_PROP_FLAGS),
    JS_FN("register_type_with_class", gjs_register_type_with_class, 5,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("signal_new", gjs_signal_new, 6, GJS_MODULE_PROP_FLAGS),
    JS_FN("lookupConstructor", gjs_lookup_constructor, 1, 0),
    JS_FN("associateClosure", gjs_associate_closure, 2, GJS_MODULE_PROP_FLAGS),
    JS_FS_END,
};

static JSPropertySpec private_module_props[] = {
    JS_PSG("gobject_prototype_symbol",
           symbol_getter<&GjsAtoms::gobject_prototype>, GJS_MODULE_PROP_FLAGS),
    JS_PSG("hook_up_vfunc_symbol", symbol_getter<&GjsAtoms::hook_up_vfunc>,
           GJS_MODULE_PROP_FLAGS),
    JS_PSG("signal_find_symbol", symbol_getter<&GjsAtoms::signal_find>,
           GJS_MODULE_PROP_FLAGS),
    JS_PSG("signals_block_symbol", symbol_getter<&GjsAtoms::signals_block>,
           GJS_MODULE_PROP_FLAGS),
    JS_PSG("signals_unblock_symbol", symbol_getter<&GjsAtoms::signals_unblock>,
           GJS_MODULE_PROP_FLAGS),
    JS_PSG("signals_disconnect_symbol",
           symbol_getter<&GjsAtoms::signals_disconnect>, GJS_MODULE_PROP_FLAGS),
    JS_PS_END};

bool gjs_define_private_gi_stuff(JSContext* cx,
                                 JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, private_module_funcs) &&
           JS_DefineProperties(cx, module, private_module_props);
}
