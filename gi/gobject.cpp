/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <unordered_map>
#include <utility>  // for move, pair

#include <glib-object.h>
#include <glib.h>

#include <js/CallAndConstruct.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>  // for JS_NewPlainObject
#include <mozilla/Maybe.h>

#include "gi/gobject.h"
#include "gi/object.h"
#include "gi/value.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

static std::unordered_map<GType, AutoParamArray> class_init_properties;

[[nodiscard]] static JSContext* current_js_context() {
    GjsContext* gjs = gjs_context_get_current();
    return static_cast<JSContext*>(gjs_context_get_native_context(gjs));
}

void push_class_init_properties(GType gtype, AutoParamArray* params) {
    class_init_properties[gtype] = std::move(*params);
}

bool pop_class_init_properties(GType gtype, AutoParamArray* params_out) {
    auto found = class_init_properties.find(gtype);
    if (found == class_init_properties.end())
        return false;

    *params_out = std::move(found->second);
    class_init_properties.erase(found);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool jsobj_set_gproperty(JSContext* cx, JS::HandleObject object,
                                const GValue* value, GParamSpec* pspec) {
    JS::RootedValue jsvalue(cx);
    if (!gjs_value_from_g_value(cx, &jsvalue, value))
        return false;

    Gjs::AutoChar underscore_name{gjs_hyphen_to_underscore(pspec->name)};

    if (pspec->flags & G_PARAM_CONSTRUCT_ONLY) {
        unsigned flags = GJS_MODULE_PROP_FLAGS | JSPROP_READONLY;
        Gjs::AutoChar camel_name{gjs_hyphen_to_camel(pspec->name)};

        if (g_param_spec_get_qdata(pspec, ObjectBase::custom_property_quark())) {
            JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> jsprop(cx);
            JS::RootedObject holder(cx);
            JS::RootedObject getter(cx);

            // Ensure to call any associated setter method
            if (!g_str_equal(underscore_name.get(), pspec->name)) {
                if (!JS_GetPropertyDescriptor(cx, object, underscore_name,
                                              &jsprop, &holder)) {
                    return false;
                }

                if (jsprop.isSome() && jsprop->setter() &&
                    !JS_SetProperty(cx, object, underscore_name, jsvalue)) {
                    return false;
                }
                if (jsprop.isSome() && jsprop->getter())
                    getter.set(jsprop->getter());
            }

            if (!g_str_equal(camel_name.get(), pspec->name)) {
                if (!JS_GetPropertyDescriptor(cx, object, camel_name, &jsprop,
                                              &holder)) {
                    return false;
                }

                if (jsprop.isSome() && jsprop.value().setter() &&
                    !JS_SetProperty(cx, object, camel_name, jsvalue)) {
                    return false;
                }
                if (!getter && jsprop.isSome() && jsprop->getter())
                    getter.set(jsprop->getter());
            }

            if (!JS_GetPropertyDescriptor(cx, object, pspec->name, &jsprop,
                                          &holder))
                return false;
            if (jsprop.isSome() && jsprop.value().setter() &&
                !JS_SetProperty(cx, object, pspec->name, jsvalue))
                return false;
            if (!getter && jsprop.isSome() && jsprop->getter())
                getter.set(jsprop->getter());

            // If a getter is found, redefine the property with that getter
            // and no setter.
            if (getter)
                return JS_DefineProperty(cx, object, underscore_name, getter,
                                         nullptr, GJS_MODULE_PROP_FLAGS) &&
                       JS_DefineProperty(cx, object, camel_name, getter,
                                         nullptr, GJS_MODULE_PROP_FLAGS) &&
                       JS_DefineProperty(cx, object, pspec->name, getter,
                                         nullptr, GJS_MODULE_PROP_FLAGS);
        }

        return JS_DefineProperty(cx, object, underscore_name, jsvalue, flags) &&
               JS_DefineProperty(cx, object, camel_name, jsvalue, flags) &&
               JS_DefineProperty(cx, object, pspec->name, jsvalue, flags);
    }

    return JS_SetProperty(cx, object, underscore_name, jsvalue);
}

static void gjs_object_base_init(void* klass) {
    auto* priv = ObjectPrototype::for_gtype(G_OBJECT_CLASS_TYPE(klass));
    if (priv)
        priv->ref_vfuncs();
}

static void gjs_object_base_finalize(void* klass) {
    auto* priv = ObjectPrototype::for_gtype(G_OBJECT_CLASS_TYPE(klass));
    if (priv)
        priv->unref_vfuncs();
}

static GObject* gjs_object_constructor(
    GType type, unsigned n_construct_properties,
    GObjectConstructParam* construct_properties) {
    JSContext* cx = current_js_context();
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);

    if (!gjs->object_init_list().empty()) {
        GType parent_type = g_type_parent(type);

        /* The object is being constructed from JS:
         * Simply chain up to the first non-gjs constructor
         */
        while (G_OBJECT_CLASS(g_type_class_peek(parent_type))->constructor ==
               gjs_object_constructor)
            parent_type = g_type_parent(parent_type);

        return G_OBJECT_CLASS(g_type_class_peek(parent_type))
            ->constructor(type, n_construct_properties, construct_properties);
    }

    /* The object is being constructed from native code (e.g. GtkBuilder):
     * Construct the JS object from the constructor, then use the GObject
     * that was associated in gjs_object_custom_init()
     */
    Gjs::AutoMainRealm ar{gjs};

    JS::RootedValue constructor{cx};
    if (!gjs_lookup_object_constructor(cx, type, &constructor))
        return nullptr;

    JS::RootedObject object(cx);
    if (n_construct_properties) {
        JS::RootedObject props_hash(cx, JS_NewPlainObject(cx));

        for (unsigned i = 0; i < n_construct_properties; i++)
            if (!jsobj_set_gproperty(cx, props_hash,
                                     construct_properties[i].value,
                                     construct_properties[i].pspec))
                return nullptr;

        JS::RootedValueArray<1> args(cx);
        args[0].set(JS::ObjectValue(*props_hash));

        if (!JS::Construct(cx, constructor, args, &object))
            return nullptr;
    } else if (!JS::Construct(cx, constructor, JS::HandleValueArray::empty(),
                              &object)) {
        return nullptr;
    }

    auto* priv = ObjectBase::for_js_nocheck(object);
    /* Should have been set in init_impl() and pushed into object_init_list,
     * then popped from object_init_list in gjs_object_custom_init() */
    g_assert(priv);
    /* We only hold a toggle ref at this point, add back a ref that the
     * native code can own.
     */
    return G_OBJECT(g_object_ref(priv->to_instance()->ptr()));
}

static void gjs_object_set_gproperty(GObject* object,
                                     unsigned property_id [[maybe_unused]],
                                     const GValue* value, GParamSpec* pspec) {
    auto* priv = ObjectInstance::for_gobject(object);
    if (!priv || !priv->wrapper()) {
        g_warning("Wrapper for GObject %p was disposed, cannot set property %s",
                  object, g_param_spec_get_name(pspec));
        return;
    }

    JSContext* cx = current_js_context();

    JS::RootedObject js_obj(cx, priv->wrapper());
    JSAutoRealm ar(cx, js_obj);

    if (!jsobj_set_gproperty(cx, js_obj, value, pspec))
        gjs_log_exception_uncaught(cx);
}

static void gjs_object_get_gproperty(GObject* object,
                                     unsigned property_id [[maybe_unused]],
                                     GValue* value, GParamSpec* pspec) {
    auto* priv = ObjectInstance::for_gobject(object);
    if (!priv || !priv->wrapper()) {
        g_warning("Wrapper for GObject %p was disposed, cannot get property %s",
                  object, g_param_spec_get_name(pspec));
        return;
    }

    JSContext* cx = current_js_context();

    JS::RootedObject js_obj(cx, priv->wrapper());
    JS::RootedValue jsvalue(cx);
    JSAutoRealm ar(cx, js_obj);

    Gjs::AutoChar underscore_name{gjs_hyphen_to_underscore(pspec->name)};
    if (!JS_GetProperty(cx, js_obj, underscore_name, &jsvalue)) {
        gjs_log_exception_uncaught(cx);
        return;
    }
    if (!gjs_value_to_g_value(cx, jsvalue, value))
        gjs_log_exception(cx);
}

static void gjs_object_class_init(void* class_pointer, void*) {
    GObjectClass* klass = G_OBJECT_CLASS(class_pointer);
    GType gtype = G_OBJECT_CLASS_TYPE(klass);

    klass->constructor = gjs_object_constructor;
    klass->set_property = gjs_object_set_gproperty;
    klass->get_property = gjs_object_get_gproperty;

    AutoParamArray properties;
    if (!pop_class_init_properties(gtype, &properties))
        return;

    unsigned i = 0;
    for (Gjs::AutoParam& pspec : properties) {
        g_param_spec_set_qdata(pspec, ObjectBase::custom_property_quark(),
                               GINT_TO_POINTER(1));
        g_object_class_install_property(klass, ++i, pspec);
    }
}

static void gjs_object_custom_init(GTypeInstance* instance,
                                   void* g_class [[maybe_unused]]) {
    JSContext* cx = current_js_context();
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);

    if (gjs->object_init_list().empty())
        return;

    JS::RootedObject object(cx, gjs->object_init_list().back());
    auto* priv_base = ObjectBase::for_js_nocheck(object);
    g_assert(priv_base);  // Should have been set in init_impl()
    ObjectInstance* priv = priv_base->to_instance();

    if (priv_base->gtype() != G_TYPE_FROM_INSTANCE(instance)) {
        /* This is not the most derived instance_init function,
           do nothing.
         */
        return;
    }

    gjs->object_init_list().popBack();

    if (!priv->init_custom_class_from_gobject(cx, object, G_OBJECT(instance)))
        gjs_log_exception_uncaught(cx);
}

static void gjs_interface_init(void* g_iface, void*) {
    GType gtype = G_TYPE_FROM_INTERFACE(g_iface);

    AutoParamArray properties;
    if (!pop_class_init_properties(gtype, &properties))
        return;

    for (Gjs::AutoParam& pspec : properties) {
        g_param_spec_set_qdata(pspec, ObjectBase::custom_property_quark(),
                               GINT_TO_POINTER(1));
        g_object_interface_install_property(g_iface, pspec);
    }
}

constexpr GTypeInfo gjs_gobject_class_info = {
    0,  // class_size

    gjs_object_base_init,
    gjs_object_base_finalize,

    gjs_object_class_init,
    GClassFinalizeFunc(nullptr),
    nullptr,  // class_data

    0,  // instance_size
    0,  // n_preallocs
    gjs_object_custom_init,
};

constexpr GTypeInfo gjs_gobject_interface_info = {
    sizeof(GTypeInterface),  // class_size

    GBaseInitFunc(nullptr),
    GBaseFinalizeFunc(nullptr),

    gjs_interface_init,
    GClassFinalizeFunc(nullptr),
    nullptr,  // class_data

    0,        // instance_size
    0,        // n_preallocs
    nullptr,  // instance_init
};
