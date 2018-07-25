/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2018  Philip Chimento <philip.chimento@gmail.com>
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

#include <glib-object.h>

#include <unordered_map>

#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"
#include "gobject.h"
#include "interface.h"
#include "object.h"
#include "param.h"
#include "private.h"
#include "repo.h"

/* gi/private.cpp - private "imports._gi" module with operations that we need
 * to use from JS in order to create GObject classes, but should not be exposed
 * to client code.
 */

static bool gjs_override_property(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsAutoJSChar name;
    JS::RootedObject type(cx);

    if (!gjs_parse_call_args(cx, "override_property", args, "so", "name", &name,
                             "type", &type))
        return false;

    GType gtype = gjs_gtype_get_actual_gtype(cx, type);
    if (gtype == G_TYPE_INVALID) {
        gjs_throw(cx, "Invalid parameter type was not a GType");
        return false;
    }

    GParamSpec* pspec;
    if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        auto* interface_type =
            static_cast<GTypeInterface*>(g_type_default_interface_ref(gtype));
        pspec = g_object_interface_find_property(interface_type, name);
        g_type_default_interface_unref(interface_type);
    } else {
        auto* class_type = static_cast<GTypeClass*>(g_type_class_ref(gtype));
        pspec = g_object_class_find_property(G_OBJECT_CLASS(class_type), name);
        g_type_class_unref(class_type);
    }

    if (!pspec) {
        gjs_throw(cx, "No such property '%s' to override on type '%s'",
                  name.get(), g_type_name(gtype));
        return false;
    }

    GjsAutoParam new_pspec = g_param_spec_override(name, pspec);

    g_param_spec_set_qdata(new_pspec, ObjectBase::custom_property_quark(),
                           GINT_TO_POINTER(1));

    args.rval().setObject(*gjs_param_from_g_param(cx, new_pspec.get()));

    return true;
}

static bool validate_interfaces_and_properties_args(JSContext* cx,
                                                    JS::HandleObject interfaces,
                                                    JS::HandleObject properties,
                                                    uint32_t* n_interfaces,
                                                    uint32_t* n_properties) {
    bool is_array;
    if (!JS_IsArrayObject(cx, interfaces, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter interfaces (expected Array)");
        return false;
    }

    uint32_t n_int;
    if (!JS_GetArrayLength(cx, interfaces, &n_int))
        return false;

    if (!JS_IsArrayObject(cx, properties, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter properties (expected Array)");
        return false;
    }

    uint32_t n_prop;
    if (!JS_GetArrayLength(cx, properties, &n_prop))
        return false;

    if (n_interfaces)
        *n_interfaces = n_int;
    if (n_properties)
        *n_properties = n_prop;
    return true;
}

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
        GType iface_type = gjs_gtype_get_actual_gtype(cx, iface);
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

static bool gjs_register_interface(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    GjsAutoJSChar name;
    JS::RootedObject interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_interface", args, "soo", "name",
                             &name, "interfaces", &interfaces, "properties",
                             &properties))
        return false;

    uint32_t n_interfaces, n_properties;
    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    GType* iface_types = g_newa(GType, n_interfaces);

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name.get());
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

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    if (!module)
        return false;  // error will have been thrown already

    JS::RootedObject constructor(cx);
    gjs_define_interface_class(cx, module, nullptr, interface_type,
                               &constructor);

    args.rval().setObject(*constructor);
    return true;
}

static inline void gjs_add_interface(GType instance_type,
                                     GType interface_type) {
    static GInterfaceInfo interface_vtable{nullptr, nullptr, nullptr};
    g_type_add_interface_static(instance_type, interface_type,
                                &interface_vtable);
}

static bool gjs_register_type(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    JSAutoRequest ar(cx);

    GjsAutoJSChar name;
    JS::RootedObject parent(cx), interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_type", argv, "osoo", "parent",
                             &parent, "name", &name, "interfaces", &interfaces,
                             "properties", &properties))
        return false;

    if (!parent)
        return false;

    if (!gjs_typecheck_is_object(cx, parent, true))
        return false;

    uint32_t n_interfaces, n_properties;
    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    auto* iface_types =
        static_cast<GType*>(g_alloca(sizeof(GType) * n_interfaces));

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name.get());
        return false;
    }

    auto* parent_priv = ObjectPrototype::for_js(cx, parent);
    /* We checked parent above, in gjs_typecheck_is_object() */
    g_assert(parent_priv);

    GTypeQuery query;
    parent_priv->type_query_dynamic_safe(&query);
    if (G_UNLIKELY(query.type == 0)) {
        gjs_throw(cx,
                  "Cannot inherit from a non-gjs dynamic type [bug 687184]");
        return false;
    }

    GTypeInfo type_info = gjs_gobject_class_info;
    type_info.class_size = query.class_size;
    type_info.instance_size = query.instance_size;

    GType instance_type = g_type_register_static(parent_priv->gtype(), name,
                                                 &type_info, GTypeFlags(0));

    g_type_set_qdata(instance_type, ObjectBase::custom_type_quark(),
                     GINT_TO_POINTER(1));

    if (!save_properties_for_class_init(cx, properties, n_properties,
                                        instance_type))
        return false;

    for (uint32_t ix = 0; ix < n_interfaces; ix++)
        gjs_add_interface(instance_type, iface_types[ix]);

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    JS::RootedObject constructor(cx), prototype(cx);
    gjs_define_object_class(cx, module, nullptr, instance_type, &constructor,
                            &prototype);

    auto* priv = ObjectPrototype::for_js(cx, prototype);
    priv->set_type_qdata();

    argv.rval().setObject(*constructor);

    return true;
}

static bool gjs_signal_new(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JSAutoRequest ar(cx);

    GjsAutoJSChar signal_name;
    int32_t flags, accumulator_enum;
    JS::RootedObject gtype_obj(cx), return_gtype_obj(cx), params_obj(cx);
    if (!gjs_parse_call_args(cx, "signal_new", args, "osiioo", "gtype",
                             &gtype_obj, "signal name", &signal_name, "flags",
                             &flags, "accumulator", &accumulator_enum,
                             "return gtype", &return_gtype_obj, "params",
                             &params_obj))
        return false;

    if (!gjs_typecheck_gtype(cx, gtype_obj, true))
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
            accumulator = NULL;
    }

    GType return_type = gjs_gtype_get_actual_gtype(cx, return_gtype_obj);

    if (accumulator == g_signal_accumulator_true_handled &&
        return_type != G_TYPE_BOOLEAN) {
        gjs_throw(cx,
                  "GObject.SignalAccumulator.TRUE_HANDLED can only be used "
                  "with boolean signals");
        return false;
    }

    uint32_t n_parameters;
    if (!JS_GetArrayLength(cx, params_obj, &n_parameters))
        return false;

    GType* params = g_newa(GType, n_parameters);
    JS::RootedValue gtype_val(cx);
    for (uint32_t ix = 0; ix < n_parameters; ix++) {
        if (!JS_GetElement(cx, params_obj, ix, &gtype_val) ||
            !gtype_val.isObject()) {
            gjs_throw(cx, "Invalid signal parameter number %d", ix);
            return false;
        }

        JS::RootedObject gjs_gtype(cx, &gtype_val.toObject());
        params[ix] = gjs_gtype_get_actual_gtype(cx, gjs_gtype);
    }

    GType gtype = gjs_gtype_get_actual_gtype(cx, gtype_obj);

    unsigned signal_id = g_signal_newv(
        signal_name, gtype, GSignalFlags(flags), nullptr, /* class closure */
        accumulator, nullptr,                             /* accu_data */
        g_cclosure_marshal_generic, return_type, n_parameters, params);

    // FIXME: what if ID is greater than int32 max?
    args.rval().setInt32(signal_id);
    return true;
}

static bool hook_up_vfunc_symbol_getter(JSContext* cx, unsigned argc,
                                        JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    args.rval().setSymbol(ObjectInstance::hook_up_vfunc_symbol(cx));
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FN("override_property", gjs_override_property, 2, GJS_MODULE_PROP_FLAGS),
    JS_FN("register_interface", gjs_register_interface, 3,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("register_type", gjs_register_type, 4, GJS_MODULE_PROP_FLAGS),
    JS_FN("signal_new", gjs_signal_new, 6, GJS_MODULE_PROP_FLAGS),
    JS_FS_END,
};

static JSPropertySpec module_props[] = {
    JS_PSG("hook_up_vfunc_symbol", hook_up_vfunc_symbol_getter,
           GJS_MODULE_PROP_FLAGS),
    JS_PS_END};

bool gjs_define_private_gi_stuff(JSContext* cx,
                                 JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, module_funcs) &&
           JS_DefineProperties(cx, module, module_props);
}
