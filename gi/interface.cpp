/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2012  Red Hat, Inc.
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

#include "gi/function.h"
#include "gi/gtype.h"
#include "gi/interface.h"
#include "gi/object.h"
#include "gi/repo.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem-private.h"

#include <util/log.h>

#include <girepository.h>

InterfacePrototype::InterfacePrototype(GIInterfaceInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_vtable(
          static_cast<GTypeInterface*>(g_type_default_interface_ref(gtype))) {
    GJS_INC_COUNTER(interface);
}

InterfacePrototype::~InterfacePrototype(void) {
    g_clear_pointer(&m_vtable, g_type_default_interface_unref);
    GJS_DEC_COUNTER(interface);
}

// See GIWrapperBase::resolve().
bool InterfacePrototype::resolve_impl(JSContext* context, JS::HandleObject obj,
                                      JS::HandleId, const char* name,
                                      bool* resolved) {
    /* If we have no GIRepository information then this interface was defined
     * from within GJS. In that case, it has no properties that need to be
     * resolved from within C code, as interfaces cannot inherit. */
    if (is_custom_js_class()) {
        *resolved = false;
        return true;
    }

    GjsAutoFunctionInfo method_info =
        g_interface_info_find_method(m_info, name);

    if (method_info) {
        if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
            if (!gjs_define_function(context, obj, m_gtype, method_info))
                return false;

            *resolved = true;
        } else {
            *resolved = false;
        }
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

    InterfaceBase* priv = InterfaceBase::for_js_typecheck(cx, interface_proto);
    if (!priv)
        return false;

    return priv->to_prototype()->has_instance_impl(cx, args);
}

// See InterfaceBase::has_instance().
bool InterfacePrototype::has_instance_impl(JSContext* cx,
                                           const JS::CallArgs& args) {
    // This method is never called directly, so no need for error messages.
    g_assert(args.length() == 1);
    g_assert(args[0].isObject());
    JS::RootedObject instance(cx, &args[0].toObject());
    bool isinstance = ObjectBase::typecheck(cx, instance, nullptr, m_gtype,
                                            GjsTypecheckNoThrow());
    args.rval().setBoolean(isinstance);
    return true;
}

// clang-format off
const struct JSClassOps InterfaceBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    &InterfaceBase::resolve,
    nullptr,  // mayResolve
    &InterfaceBase::finalize,
};

const struct JSClass InterfaceBase::klass = {
    "GObject_Interface",
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
    &InterfaceBase::class_ops
};

JSFunctionSpec InterfaceBase::static_methods[] = {
    JS_SYM_FN(hasInstance, &InterfaceBase::has_instance, 1, 0),
    JS_FS_END
};
// clang-format on

bool
gjs_define_interface_class(JSContext              *context,
                           JS::HandleObject        in_object,
                           GIInterfaceInfo        *info,
                           GType                   gtype,
                           JS::MutableHandleObject constructor)
{
    JS::RootedObject prototype(context);

    if (!InterfacePrototype::create_class(context, in_object, info, gtype,
                                          constructor, &prototype))
        return false;

    /* If we have no GIRepository information, then this interface was defined
     * from within GJS and therefore has no C static methods to be defined. */
    if (info) {
        if (!gjs_define_static_methods<InfoType::Interface>(
                context, constructor, gtype, info))
            return false;
    }

    return true;
}

bool
gjs_lookup_interface_constructor(JSContext             *context,
                                 GType                  gtype,
                                 JS::MutableHandleValue value_p)
{
    JSObject *constructor;
    GIBaseInfo *interface_info;

    interface_info = g_irepository_find_by_gtype(NULL, gtype);

    if (interface_info == NULL) {
        gjs_throw(context, "Cannot expose non introspectable interface %s",
                  g_type_name(gtype));
        return false;
    }

    g_assert(g_base_info_get_type(interface_info) ==
             GI_INFO_TYPE_INTERFACE);

    constructor = gjs_lookup_generic_constructor(context, interface_info);
    if (G_UNLIKELY (constructor == NULL))
        return false;

    g_base_info_unref(interface_info);

    value_p.setObject(*constructor);
    return true;
}
