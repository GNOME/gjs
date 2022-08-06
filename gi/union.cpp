/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <girepository.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Warnings.h>

#include "gi/arg-inl.h"
#include "gi/function.h"
#include "gi/repo.h"
#include "gi/union.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

UnionPrototype::UnionPrototype(GIUnionInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype) {
    GJS_INC_COUNTER(union_prototype);
}

UnionPrototype::~UnionPrototype(void) { GJS_DEC_COUNTER(union_prototype); }

UnionInstance::UnionInstance(UnionPrototype* prototype, JS::HandleObject obj)
    : GIWrapperInstance(prototype, obj) {
    GJS_INC_COUNTER(union_instance);
}

UnionInstance::~UnionInstance(void) {
    if (m_ptr) {
        g_boxed_free(gtype(), m_ptr);
        m_ptr = nullptr;
    }
    GJS_DEC_COUNTER(union_instance);
}

// See GIWrapperBase::resolve().
bool UnionPrototype::resolve_impl(JSContext* context, JS::HandleObject obj,
                                  JS::HandleId id, bool* resolved) {
    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(context, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    // Look for methods and other class properties
    GjsAutoFunctionInfo method_info =
        g_union_info_find_method(info(), prop_name.get());

    if (method_info) {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        _gjs_log_info_usage(method_info);
#endif
        if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
            gjs_debug(GJS_DEBUG_GBOXED,
                      "Defining method %s in prototype for %s.%s",
                      method_info.name(), ns(), name());

            /* obj is union proto */
            if (!gjs_define_function(context, obj, gtype(), method_info))
                return false;

            *resolved = true; /* we defined the prop in object_proto */
        } else {
            *resolved = false;
        }
    } else {
        *resolved = false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static void* union_new(JSContext* context, JS::HandleObject this_obj,
                       const JS::CallArgs& args, GIUnionInfo* info) {
    int n_methods;
    int i;

    /* Find a zero-args constructor and call it */

    n_methods = g_union_info_get_n_methods(info);

    for (i = 0; i < n_methods; ++i) {
        GIFunctionInfoFlags flags;

        GjsAutoFunctionInfo func_info = g_union_info_get_method(info, i);

        flags = g_function_info_get_flags(func_info);
        if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0 &&
            g_callable_info_get_n_args((GICallableInfo*) func_info) == 0) {
            GIArgument rval;
            if (!gjs_invoke_constructor_from_c(context, func_info, this_obj,
                                               args, &rval))
                return nullptr;

            if (!gjs_arg_get<void*>(&rval)) {
                gjs_throw(context,
                          "Unable to construct union type %s as its"
                          "constructor function returned null",
                          g_base_info_get_name(info));
                return nullptr;
            }

            return gjs_arg_get<void*>(&rval);
        }
    }

    gjs_throw(context, "Unable to construct union type %s since it has no zero-args <constructor>, can only wrap an existing one",
              g_base_info_get_name((GIBaseInfo*) info));

    return nullptr;
}

// See GIWrapperBase::constructor().
bool UnionInstance::constructor_impl(JSContext* context,
                                     JS::HandleObject object,
                                     const JS::CallArgs& args) {
    if (args.length() > 0 &&
        !JS::WarnUTF8(context, "Arguments to constructor of %s ignored",
                      name()))
        return false;

    m_ptr = union_new(context, object, args, info());
    return !!m_ptr;
}

// clang-format off
const struct JSClassOps UnionBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    &UnionBase::resolve,
    nullptr,  // mayResolve
    &UnionBase::finalize,
};

const struct JSClass UnionBase::klass = {
    "GObject_Union",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
    &UnionBase::class_ops
};
// clang-format on

bool
gjs_define_union_class(JSContext       *context,
                       JS::HandleObject in_object,
                       GIUnionInfo     *info)
{
    GType gtype;
    JS::RootedObject prototype(context), constructor(context);

    /* For certain unions, we may be able to relax this in the future by
     * directly allocating union memory, as we do for structures in boxed.c
     */
    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) info);
    if (gtype == G_TYPE_NONE) {
        gjs_throw(context, "Unions must currently be registered as boxed types");
        return false;
    }

    return !!UnionPrototype::create_class(context, in_object, info, gtype,
                                          &constructor, &prototype);
}

JSObject*
gjs_union_from_c_union(JSContext    *context,
                       GIUnionInfo  *info,
                       void         *gboxed)
{
    GType gtype;

    if (!gboxed)
        return nullptr;

    /* For certain unions, we may be able to relax this in the future by
     * directly allocating union memory, as we do for structures in boxed.c
     */
    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) info);
    if (gtype == G_TYPE_NONE) {
        gjs_throw(context, "Unions must currently be registered as boxed types");
        return nullptr;
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping union %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    JS::RootedObject obj(context,
                         gjs_new_object_with_generic_prototype(context, info));
    if (!obj)
        return nullptr;

    UnionInstance* priv = UnionInstance::new_for_js_object(context, obj);
    priv->copy_union(gboxed);

    return obj;
}

void* UnionInstance::copy_ptr(JSContext* cx, GType gtype, void* ptr) {
    if (g_type_is_a(gtype, G_TYPE_BOXED))
        return g_boxed_copy(gtype, ptr);

    gjs_throw(cx,
              "Can't transfer ownership of a union type not registered as "
              "boxed");
    return nullptr;
}
