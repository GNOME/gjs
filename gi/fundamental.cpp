/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013       Intel Corporation
 * Copyright (c) 2008-2010  litl, LLC
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

#include "fundamental.h"

#include "arg.h"
#include "boxed.h"
#include "function.h"
#include "gi/gtype.h"
#include "gi/object.h"
#include "gi/repo.h"
#include "gi/wrapperutils.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem-private.h"

#include <gjs/context.h>
#include <util/log.h>
#include <girepository.h>

FundamentalInstance::FundamentalInstance(JSContext* cx, JS::HandleObject obj)
    : GIWrapperInstance(cx, obj) {
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
GJS_USE
static bool find_fundamental_constructor(
    JSContext* context, GIObjectInfo* info,
    JS::MutableHandleId constructor_name,
    GjsAutoFunctionInfo* constructor_info) {
    int i, n_methods;

    n_methods = g_object_info_get_n_methods(info);

    for (i = 0; i < n_methods; ++i) {
        GIFunctionInfo *func_info;
        GIFunctionInfoFlags flags;

        func_info = g_object_info_get_method(info, i);

        flags = g_function_info_get_flags(func_info);
        if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0) {
            const char *name;

            name = g_base_info_get_name((GIBaseInfo *) func_info);
            constructor_name.set(gjs_intern_string_to_id(context, name));
            if (constructor_name == JSID_VOID)
                return false;

            constructor_info->reset(func_info);
            return true;
        }

        g_base_info_unref((GIBaseInfo *) func_info);
    }

    return true;
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
    for (i = 0; i < n_interfaces; i++) {
        GjsAutoInterfaceInfo iface_info =
            g_irepository_find_by_gtype(nullptr, interfaces[i]);

        if (!iface_info)
            continue;

        GjsAutoFunctionInfo method_info =
            g_interface_info_find_method(iface_info, name);

        if (method_info &&
            g_function_info_get_flags(method_info) & GI_FUNCTION_IS_METHOD) {
            if (gjs_define_function(cx, obj, gtype(), method_info)) {
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
                                        JS::HandleId id, const char* prop_name,
                                        bool* resolved) {
    /* We are the prototype, so look for methods and other class properties */
    GjsAutoFunctionInfo method_info =
        g_object_info_find_method(info(), prop_name);

    if (method_info) {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        _gjs_log_info_usage(method_info);
#endif
        if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
            /* we do not define deprecated methods in the prototype */
            if (g_base_info_is_deprecated(method_info)) {
                gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                          "Ignoring definition of deprecated method %s in "
                          "prototype %s.%s",
                          method_info.name(), ns(), name());
                *resolved = false;
                return true;
            }

            gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                      "Defining method %s in prototype for %s.%s",
                      method_info.name(), ns(), name());

            if (!gjs_define_function(cx, obj, gtype(), method_info))
                return false;

            *resolved = true;
        }
    } else {
        *resolved = false;
    }

    return resolve_interface(cx, obj, resolved, prop_name);
}

/*
 * FundamentalInstance::invoke_constructor:
 *
 * Finds the type's static constructor method (the static method given by
 * FundamentalPrototype::constructor_name()) and invokes it with the given
 * arguments.
 */
bool FundamentalInstance::invoke_constructor(JSContext* context,
                                             JS::HandleObject obj,
                                             const JS::HandleValueArray& args,
                                             GIArgument* rvalue) {
    JS::RootedObject js_constructor(context);
    JS::RootedId constructor_name(context, get_prototype()->constructor_name());

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (!gjs_object_require_property(context, obj, NULL, atoms.constructor(),
                                     &js_constructor) ||
        constructor_name == JSID_VOID) {
        gjs_throw(context, "Couldn't find a constructor for type %s.%s", ns(),
                  name());
        return false;
    }

    JS::RootedObject constructor(context);
    if (!gjs_object_require_property(context, js_constructor, NULL,
                                     constructor_name, &constructor)) {
        gjs_throw(context, "Couldn't find a constructor for type %s.%s", ns(),
                  name());
        return false;
    }

    return gjs_invoke_constructor_from_c(context, constructor, obj, args, rvalue);
}

// See GIWrapperBase::constructor().
bool FundamentalInstance::constructor_impl(JSContext* cx,
                                           JS::HandleObject object,
                                           const JS::CallArgs& argv) {
    GArgument ret_value;
    GITypeInfo return_info;

    if (!invoke_constructor(cx, object, argv, &ret_value) ||
        !associate_js_instance(cx, object, ret_value.v_pointer))
        return false;

    GICallableInfo* constructor_info = get_prototype()->constructor_info();
    g_callable_info_load_return_type(constructor_info, &return_info);

    return gjs_g_argument_release(
        cx, g_callable_info_get_caller_owns(constructor_info), &return_info,
        &ret_value);
}

FundamentalInstance::~FundamentalInstance(void) {
    if (m_ptr) {
        unref();
        m_ptr = nullptr;
    }
    GJS_DEC_COUNTER(fundamental_instance);
}

FundamentalPrototype::FundamentalPrototype(GIObjectInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_ref_function(g_object_info_get_ref_function_pointer(info)),
      m_unref_function(g_object_info_get_unref_function_pointer(info)),
      m_get_value_function(g_object_info_get_get_value_function_pointer(info)),
      m_set_value_function(g_object_info_get_set_value_function_pointer(info)) {
    g_assert(m_ref_function);
    g_assert(m_unref_function);
    g_assert(m_set_value_function);
    g_assert(m_get_value_function);
}

// Overrides GIWrapperPrototype::init().
bool FundamentalPrototype::init(JSContext* cx) {
    JS::RootedId constructor_name(cx);
    GjsAutoFunctionInfo constructor_info;
    if (!find_fundamental_constructor(cx, info(), &constructor_name,
                                      &constructor_info))
        return false;

    m_constructor_name = constructor_name;
    m_constructor_info = constructor_info.release();
    return true;
}

FundamentalPrototype::~FundamentalPrototype(void) {
    g_clear_pointer(&m_constructor_info, g_base_info_unref);
    GJS_DEC_COUNTER(fundamental_prototype);
}

// Overrides GIWrapperPrototype::trace_impl().
void FundamentalPrototype::trace_impl(JSTracer* trc) {
    JS::TraceEdge<jsid>(trc, &m_constructor_name,
                        "Fundamental::constructor_name");
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
    nullptr,  // hasInstance
    nullptr,  // construct
    &FundamentalBase::trace
};

const struct JSClass FundamentalBase::klass = {
    "GFundamental_Object",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &FundamentalBase::class_ops
};
// clang-format on

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
gjs_lookup_fundamental_prototype(JSContext    *context,
                                 GIObjectInfo *info,
                                 GType         gtype)
{
    JS::RootedObject in_object(context);
    const char *constructor_name;

    if (info) {
        in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);
        constructor_name = g_base_info_get_name((GIBaseInfo*) info);
    } else {
        in_object = gjs_lookup_private_namespace(context);
        constructor_name = g_type_name(gtype);
    }

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    JS::RootedObject constructor(context);
    if (value.isUndefined()) {
        /* In case we're looking for a private type, and we don't find it,
           we need to define it first.
        */
        JS::RootedObject ignored(context);
        if (!gjs_define_fundamental_class(context, in_object, info,
                                          &constructor, &ignored))
            return nullptr;
    } else {
        if (G_UNLIKELY(!value.isObject())) {
            gjs_throw(context,
                      "Fundamental constructor was not an object, it was a %s",
                      JS::InformalValueTypeName(value));
            return NULL;
        }

        constructor = &value.toObject();
    }

    g_assert(constructor);

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject prototype(context);
    if (!gjs_object_require_property(context, constructor, "constructor object",
                                     atoms.prototype(), &prototype))
        return NULL;

    return prototype;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
gjs_lookup_fundamental_prototype_from_gtype(JSContext *context,
                                            GType      gtype)
{
    GjsAutoObjectInfo info;

    /* A given gtype might not have any definition in the introspection
     * data. If that's the case, try to look for a definition of any of the
     * parent type. */
    while (gtype != G_TYPE_INVALID &&
           !(info = g_irepository_find_by_gtype(nullptr, gtype)))
        gtype = g_type_parent(gtype);

    return gjs_lookup_fundamental_prototype(context, info, gtype);
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
    return g_callable_info_get_n_args(m_constructor_info);
}

bool
gjs_define_fundamental_class(JSContext              *context,
                             JS::HandleObject        in_object,
                             GIObjectInfo           *info,
                             JS::MutableHandleObject constructor,
                             JS::MutableHandleObject prototype)
{
    GType gtype;

    gtype = g_registered_type_info_get_g_type (info);

    FundamentalPrototype* priv = FundamentalPrototype::create_class(
        context, in_object, info, gtype, constructor, prototype);
    if (!priv)
        return false;

    if (g_object_info_get_n_fields(info) > 0) {
        gjs_debug(GJS_DEBUG_GFUNDAMENTAL,
                  "Fundamental type '%s.%s' apparently has accessible fields. "
                  "Gjs has no support for this yet, ignoring these.",
                  priv->ns(), priv->name());
    }

    return gjs_define_static_methods<InfoType::Object>(context, constructor,
                                                       gtype, info);
}

JSObject*
gjs_object_from_g_fundamental(JSContext    *context,
                              GIObjectInfo *info,
                              void         *gfundamental)
{
    if (gfundamental == NULL)
        return NULL;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    auto p = gjs->fundamental_table().lookup(gfundamental);
    if (p)
        return p->value();

    gjs_debug_marshal(GJS_DEBUG_GFUNDAMENTAL,
                      "Wrapping fundamental %s.%s %p with JSObject",
                      g_base_info_get_namespace((GIBaseInfo *) info),
                      g_base_info_get_name((GIBaseInfo *) info),
                      gfundamental);

    JS::RootedObject proto(context,
        gjs_lookup_fundamental_prototype_from_gtype(context,
                                                    G_TYPE_FROM_INSTANCE(gfundamental)));
    if (!proto)
        return NULL;

    JS::RootedObject object(context, JS_NewObjectWithGivenProto(
                                         context, JS_GetClass(proto), proto));

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

JSObject *
gjs_fundamental_from_g_value(JSContext    *context,
                             const GValue *value,
                             GType         gtype)
{
    void *fobj;

    auto* proto_priv = FundamentalPrototype::for_gtype(context, gtype);

    fobj = proto_priv->call_get_value_function(value);
    if (!fobj) {
        gjs_throw(context,
                  "Failed to convert GValue to a fundamental instance");
        return NULL;
    }

    return gjs_object_from_g_fundamental(context, proto_priv->info(), fobj);
}

void*
gjs_g_fundamental_from_object(JSContext       *context,
                              JS::HandleObject obj)
{
    if (!obj)
        return NULL;

    auto* priv = FundamentalBase::for_js(context, obj);

    if (priv == NULL) {
        gjs_throw(context,
                  "No introspection information for %p", obj.get());
        return NULL;
    }

    if (!priv->check_is_instance(context, "convert to a fundamental instance"))
        return NULL;

    return priv->to_instance()->ptr();
}

bool
gjs_typecheck_fundamental(JSContext       *context,
                          JS::HandleObject object,
                          GType            expected_gtype,
                          bool             throw_error)
{
    if (throw_error)
        return FundamentalBase::typecheck(context, object, nullptr,
                                          expected_gtype);
    return FundamentalBase::typecheck(context, object, nullptr, expected_gtype,
                                      FundamentalBase::TypecheckNoThrow());
}

void *
gjs_fundamental_ref(JSContext     *context,
                    void          *gfundamental)
{
    auto* priv = FundamentalPrototype::for_gtype(
        context, G_TYPE_FROM_INSTANCE(gfundamental));
    return priv->call_ref_function(gfundamental);
}

void
gjs_fundamental_unref(JSContext    *context,
                      void         *gfundamental)
{
    auto* priv = FundamentalPrototype::for_gtype(
        context, G_TYPE_FROM_INSTANCE(gfundamental));
    priv->call_unref_function(gfundamental);
}
