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

#include <config.h>

#include <string.h>

#include <girepository.h>

/* include first for logging related #define used in repo.h */
#include <util/log.h>

#include "arg.h"
#include "gi/function.h"
#include "gi/gtype.h"
#include "gi/object.h"
#include "gi/union.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem-private.h"
#include "repo.h"

UnionPrototype::UnionPrototype(GIUnionInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype) {
    GJS_INC_COUNTER(union_prototype);
}

UnionPrototype::~UnionPrototype(void) { GJS_DEC_COUNTER(union_prototype); }

UnionInstance::UnionInstance(JSContext* cx, JS::HandleObject obj)
    : GIWrapperInstance(cx, obj) {
    GJS_INC_COUNTER(union_instance);
}

UnionInstance::~UnionInstance(void) {
    if (m_ptr) {
        g_boxed_free(g_registered_type_info_get_g_type(info()), m_ptr);
        m_ptr = nullptr;
    }
    GJS_DEC_COUNTER(union_instance);
}

// See GIWrapperBase::resolve().
bool UnionPrototype::resolve_impl(JSContext* context, JS::HandleObject obj,
                                  JS::HandleId id, const char* prop_name,
                                  bool* resolved) {
    // Look for methods and other class properties
    GjsAutoFunctionInfo method_info =
        g_union_info_find_method(info(), prop_name);

    if (method_info) {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        _gjs_log_info_usage(method_info);
#endif
        if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
            gjs_debug(GJS_DEBUG_GBOXED,
                      "Defining method %s in prototype for %s.%s",
                      method_info.name(), ns(), name());

            /* obj is union proto */
            if (!gjs_define_function(context, obj,
                                     g_registered_type_info_get_g_type(info()),
                                     method_info))
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
static void*
union_new(JSContext       *context,
          JS::HandleObject obj, /* "this" for constructor */
          GIUnionInfo     *info)
{
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

            JS::RootedValue rval(context, JS::NullValue());

            if (!gjs_invoke_c_function_uncached(context, func_info, obj,
                                                JS::HandleValueArray::empty(),
                                                &rval))
                return nullptr;

            /* We are somewhat wasteful here; invoke_c_function() above
             * creates a JSObject wrapper for the union that we immediately
             * discard.
             */
            if (rval.isNull()) {
                gjs_throw(context,
                          "Unable to construct union type %s as its"
                          "constructor function returned NULL",
                          g_base_info_get_name(info));
                return NULL;
            } else {
                JS::RootedObject rval_obj(context, &rval.toObject());
                return UnionBase::to_c_ptr(context, rval_obj);
            }
        }
    }

    gjs_throw(context, "Unable to construct union type %s since it has no zero-args <constructor>, can only wrap an existing one",
              g_base_info_get_name((GIBaseInfo*) info));

    return NULL;
}

// See GIWrapperBase::constructor().
bool UnionInstance::constructor_impl(JSContext* context,
                                     JS::HandleObject object,
                                     const JS::CallArgs& args) {
    /* union_new happens to be implemented by calling
     * gjs_invoke_c_function(), which returns a JS::Value.
     * The returned "gboxed" here is owned by that JS::Value,
     * not by us.
     */
    void* gboxed = union_new(context, object, info());

    if (gboxed == NULL) {
        return false;
    }

    /* Because "gboxed" is owned by a JS::Value and will
     * be garbage collected, we make a copy here to be
     * owned by us.
     */
    copy_union(gboxed);

    return true;
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
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
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

    if (gboxed == NULL)
        return NULL;

    /* For certain unions, we may be able to relax this in the future by
     * directly allocating union memory, as we do for structures in boxed.c
     */
    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) info);
    if (gtype == G_TYPE_NONE) {
        gjs_throw(context, "Unions must currently be registered as boxed types");
        return NULL;
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

bool
gjs_typecheck_union(JSContext       *context,
                    JS::HandleObject object,
                    GIStructInfo    *expected_info,
                    GType            expected_type,
                    bool             throw_error)
{
    if (throw_error)
        return UnionBase::typecheck(context, object, expected_info,
                                    expected_type);
    return UnionBase::typecheck(context, object, expected_info, expected_type,
                                UnionBase::TypecheckNoThrow());
}
