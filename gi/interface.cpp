/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>

#include <js/Class.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/GCVector.h>     // for MutableHandleIdVector
#include <js/Id.h>           // for PropertyKey, jsid
#include <js/PropertySpec.h>  // for JSFunctionSpec, JS_FS_END
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars

#include "gi/function.h"
#include "gi/info.h"
#include "gi/interface.h"
#include "gi/object.h"
#include "gi/repo.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"

using mozilla::Maybe, mozilla::Nothing;

InterfacePrototype::InterfacePrototype(Maybe<const GI::InterfaceInfo> info,
                                       GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_vtable(
          static_cast<GTypeInterface*>(g_type_default_interface_ref(gtype))) {
    GJS_INC_COUNTER(interface);
}

InterfacePrototype::~InterfacePrototype(void) {
    g_clear_pointer(&m_vtable, g_type_default_interface_unref);
    GJS_DEC_COUNTER(interface);
}

bool InterfacePrototype::new_enumerate_impl(
    JSContext* cx, JS::HandleObject, JS::MutableHandleIdVector properties,
    bool only_enumerable [[maybe_unused]]) {
    if (!info())
        return true;

    GI::InterfaceInfo::MethodsIterator methods = info()->methods();
    int n_methods = methods.size();
    if (!properties.reserve(properties.length() + n_methods)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    for (GI::AutoFunctionInfo meth_info : methods) {
        if (meth_info.is_method()) {
            const char* name = meth_info.name();
            jsid id = gjs_intern_string_to_id(cx, name);
            if (id.isVoid())
                return false;
            properties.infallibleAppend(id);
        }
    }

    return true;
}

// See GIWrapperBase::resolve().
bool InterfacePrototype::resolve_impl(JSContext* context, JS::HandleObject obj,
                                      JS::HandleId id, bool* resolved) {
    /* If we have no GIRepository information then this interface was defined
     * from within GJS. In that case, it has no properties that need to be
     * resolved from within C code, as interfaces cannot inherit. */
    if (!info()) {
        *resolved = false;
        return true;
    }

    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(context, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    Maybe<GI::AutoFunctionInfo> method_info{m_info->method(prop_name.get())};

    if (method_info && method_info->is_method()) {
        if (!gjs_define_function(context, obj, m_gtype, method_info.ref()))
            return false;

        *resolved = true;
    } else {
        *resolved = false;
    }

    return true;
}

/*
 * InterfaceBase::has_instance:
 *
 * JSNative implementation of `[Symbol.hasInstance]()`. This method is never
 * called directly, but instead is called indirectly by the JS engine as part of
 * an `instanceof` expression.
 */
bool InterfaceBase::has_instance(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, interface_constructor);

    JS::RootedObject interface_proto(cx);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!gjs_object_require_property(cx, interface_constructor,
                                     "interface constructor", atoms.prototype(),
                                     &interface_proto))
        return false;

    InterfaceBase* priv;
    if (!for_js_typecheck(cx, interface_proto, &priv))
        return false;

    return priv->to_prototype()->has_instance_impl(cx, args);
}

// See InterfaceBase::has_instance().
bool InterfacePrototype::has_instance_impl(JSContext* cx,
                                           const JS::CallArgs& args) {
    // This method is never called directly, so no need for error messages.
    g_assert(args.length() == 1);

    if (!args[0].isObject()) {
        args.rval().setBoolean(false);
        return true;
    }

    JS::RootedObject instance(cx, &args[0].toObject());
    bool isinstance =
        ObjectBase::typecheck(cx, instance, m_gtype, GjsTypecheckNoThrow{});
    args.rval().setBoolean(isinstance);
    return true;
}

// clang-format off
const struct JSClassOps InterfaceBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    &InterfaceBase::new_enumerate,
    &InterfaceBase::resolve,
    nullptr,  // mayResolve
    &InterfaceBase::finalize,
};

const struct JSClass InterfaceBase::klass = {
    "GObject_Interface",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
    &InterfaceBase::class_ops
};

JSFunctionSpec InterfaceBase::static_methods[] = {
    JS_SYM_FN(hasInstance, &InterfaceBase::has_instance, 1, 0),
    JS_FS_END
};
// clang-format on

bool
gjs_lookup_interface_constructor(JSContext             *context,
                                 GType                  gtype,
                                 JS::MutableHandleValue value_p)
{
    GI::Repository repo;
    Maybe<GI::AutoRegisteredTypeInfo> interface_info{repo.find_by_gtype(gtype)};
    if (!interface_info) {
        gjs_throw(context, "Cannot expose non introspectable interface %s",
                  g_type_name(gtype));
        return false;
    }

    JSObject* constructor =
        gjs_lookup_generic_constructor(context, interface_info.ref());
    if (G_UNLIKELY(!constructor))
        return false;

    value_p.setObject(*constructor);
    return true;
}
