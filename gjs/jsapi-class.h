/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017  Philip Chimento
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

#ifndef GJS_JSAPI_CLASS_H_
#define GJS_JSAPI_CLASS_H_

#include <config.h>

#include <glib-object.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gi/wrapperutils.h"  // IWYU pragma: keep
#include "gjs/global.h"       // IWYU pragma: keep
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_init_class_dynamic(
    JSContext* cx, JS::HandleObject in_object, JS::HandleObject parent_proto,
    const char* ns_name, const char* class_name, const JSClass* clasp,
    JSNative constructor_native, unsigned nargs, JSPropertySpec* ps,
    JSFunctionSpec* fs, JSPropertySpec* static_ps, JSFunctionSpec* static_fs,
    JS::MutableHandleObject prototype, JS::MutableHandleObject constructor);

GJS_USE
bool gjs_typecheck_instance(JSContext       *cx,
                            JS::HandleObject obj,
                            const JSClass   *static_clasp,
                            bool             throw_error);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_construct_object_dynamic(JSContext                  *cx,
                                       JS::HandleObject            proto,
                                       const JS::HandleValueArray& args);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_property_dynamic(JSContext       *cx,
                                 JS::HandleObject proto,
                                 const char      *prop_name,
                                 const char      *func_namespace,
                                 JSNative         getter,
                                 JSNative         setter,
                                 JS::HandleValue  private_slot,
                                 unsigned         flags);

/*
 * Helper methods to access private data:
 *
 * do_base_typecheck: checks that object has the right JSClass, and possibly
 *                    throw a TypeError exception if the check fails
 * priv_from_js: accesses the object private field; as a debug measure,
 *               it also checks that the object is of a compatible
 *               JSClass, but it doesn't raise an exception (it
 *               wouldn't be of much use, if subsequent code crashes on
 *               NULL)
 * priv_from_js_with_typecheck: a convenience function to call
 *                              do_base_typecheck and priv_from_js
 */
#define GJS_DEFINE_PRIV_FROM_JS(type, klass)                                   \
    GJS_ALWAYS_INLINE GJS_USE G_GNUC_UNUSED static inline bool                 \
    do_base_typecheck(JSContext* cx, JS::HandleObject obj, bool throw_error) { \
        return gjs_typecheck_instance(cx, obj, &klass, throw_error);           \
    }                                                                          \
    GJS_ALWAYS_INLINE GJS_USE static inline type* priv_from_js(                \
        JSContext* cx, JS::HandleObject obj) {                                 \
        return static_cast<type*>(                                             \
            JS_GetInstancePrivate(cx, obj, &klass, nullptr));                  \
    }                                                                          \
    G_GNUC_UNUSED GJS_USE static bool priv_from_js_with_typecheck(             \
        JSContext* cx, JS::HandleObject obj, type** out) {                     \
        if (!do_base_typecheck(cx, obj, false))                                \
            return false;                                                      \
        *out = priv_from_js(cx, obj);                                          \
        return true;                                                           \
    }

/*
 * GJS_GET_PRIV:
 * @cx: JSContext pointer passed into JSNative function
 * @argc: Number of arguments passed into JSNative function
 * @vp: Argument value array passed into JSNative function
 * @args: Name for JS::CallArgs variable defined by this code snippet
 * @to: Name for JS::RootedObject variable referring to function's this
 * @type: Type of private data
 * @priv: Name for private data variable defined by this code snippet
 *
 * A convenience macro for getting the private data from GJS classes using
 * priv_from_js().
 * Throws an error if the 'this' object is not the right type.
 * Use in any JSNative function.
 */
#define GJS_GET_PRIV(cx, argc, vp, args, to, type, priv)  \
    GJS_GET_THIS(cx, argc, vp, args, to);                 \
    if (!do_base_typecheck(cx, to, true))                 \
        return false;                                     \
    type *priv = priv_from_js(cx, to)

/* Helper for GJS_DEFINE_PROTO_* macros with no parent */
static inline JSObject* gjs_no_parent_get_proto(JSContext*) { return nullptr; }

/**
 * GJS_DEFINE_PROTO:
 * @tn: The name of the prototype, as a string
 * @cn: The name of the prototype, separated by _
 * @flags: additional JSClass flags, such as JSCLASS_BACKGROUND_FINALIZE
 *
 * A convenience macro for prototype implementations.
 */
#define GJS_DEFINE_PROTO(tn, cn, flags)                            \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn);                                \
_GJS_DEFINE_PROTO_FULL(tn, cn, gjs_##cn##_constructor, G_TYPE_NONE, flags)

/**
 * GJS_DEFINE_PROTO_ABSTRACT:
 * @tn: The name of the prototype, as a string
 * @cn: The name of the prototype, separated by _
 *
 * A convenience macro for prototype implementations.
 * Similar to GJS_DEFINE_PROTO but marks the prototype as abstract,
 * you won't be able to instantiate it using the new keyword
 */
#define GJS_DEFINE_PROTO_ABSTRACT(tn, cn, flags)                 \
_GJS_DEFINE_PROTO_FULL(tn, cn, no_parent, nullptr, G_TYPE_NONE,  \
                       flags)

#define GJS_DEFINE_PROTO_WITH_GTYPE(tn, cn, gtype, flags)          \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn);                                \
_GJS_DEFINE_PROTO_FULL(tn, cn, no_parent, gjs_##cn##_constructor,  \
                       gtype, flags)

#define GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE(tn, cn, gtype, flags)  \
_GJS_DEFINE_PROTO_FULL(tn, cn, no_parent, nullptr, gtype, flags)

#define GJS_DEFINE_PROTO_WITH_PARENT(tn, cn, parent_cn, flags)     \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn);                                \
_GJS_DEFINE_PROTO_FULL(tn, cn, parent_cn, gjs_##cn##_constructor,  \
                       G_TYPE_NONE, flags)

#define GJS_DEFINE_PROTO_ABSTRACT_WITH_PARENT(tn, cn, parent_cn, flags)  \
_GJS_DEFINE_PROTO_FULL(tn, cn, parent_cn, nullptr, G_TYPE_NONE, flags)

// clang-format off
#define _GJS_DEFINE_PROTO_FULL(type_name, cname, parent_cname, ctor, gtype, \
                               jsclass_flags)                               \
    extern JSPropertySpec gjs_##cname##_proto_props[];                      \
    extern JSFunctionSpec gjs_##cname##_proto_funcs[];                      \
    extern JSFunctionSpec gjs_##cname##_static_funcs[];                     \
    static void gjs_##cname##_finalize(JSFreeOp* fop, JSObject* obj);       \
    static const struct JSClassOps gjs_##cname##_class_ops = {              \
        nullptr,  /* addProperty */                                         \
        nullptr,  /* deleteProperty */                                      \
        nullptr,  /* enumerate */                                           \
        nullptr,  /* newEnumerate */                                        \
        nullptr,  /* resolve */                                             \
        nullptr,  /* mayResolve */                                          \
        gjs_##cname##_finalize                                              \
    };                                                                      \
    static struct JSClass gjs_##cname##_class = {                           \
        type_name,                                                          \
        JSCLASS_HAS_PRIVATE | jsclass_flags,                                \
        &gjs_##cname##_class_ops                                            \
    };                                                                      \
    _GJS_DEFINE_GET_PROTO(cname)                                            \
    _GJS_DEFINE_DEFINE_PROTO(cname, parent_cname, ctor, gtype)
// clang-format on

#define GJS_DEFINE_PROTO_FUNCS_WITH_PARENT(cname, parent_cname)  \
G_GNUC_UNUSED static                                             \
_GJS_DEFINE_GET_PROTO(cname)                                     \
G_GNUC_UNUSED static                                             \
_GJS_DEFINE_DEFINE_PROTO(cname, parent_cname,                    \
                         gjs_##cname##_constructor, G_TYPE_NONE)

#define GJS_DEFINE_PROTO_FUNCS(cname)  \
GJS_DEFINE_PROTO_FUNCS_WITH_PARENT(cname, no_parent)

#define _GJS_DEFINE_GET_PROTO(cname)                                           \
    GJS_USE JSObject* gjs_##cname##_get_proto(JSContext* cx) {                 \
        JS::RootedValue v_proto(                                               \
            cx, gjs_get_global_slot(cx, GJS_GLOBAL_SLOT_PROTOTYPE_##cname));   \
        g_assert(((void)"gjs_" #cname "_define_proto() must be called before " \
                        "gjs_" #cname "_get_proto()",                          \
                  !v_proto.isUndefined()));                                    \
        g_assert(((void)"Someone stored some weird value in a global slot",    \
                  v_proto.isObject()));                                        \
        return &v_proto.toObject();                                            \
    }

#define _GJS_DEFINE_DEFINE_PROTO(cname, parent_cname, ctor, type)              \
    GJS_JSAPI_RETURN_CONVENTION                                                \
    bool gjs_##cname##_define_proto(JSContext* cx, JS::HandleObject module,    \
                                    JS::MutableHandleObject proto) {           \
        /* If we've been here more than once, we already have the proto */     \
        JS::RootedValue v_proto(                                               \
            cx, gjs_get_global_slot(cx, GJS_GLOBAL_SLOT_PROTOTYPE_##cname));   \
        if (!v_proto.isUndefined()) {                                          \
            g_assert(                                                          \
                ((void)"Someone stored some weird value in a global slot",     \
                 v_proto.isObject()));                                         \
            proto.set(&v_proto.toObject());                                    \
            return true;                                                       \
        }                                                                      \
                                                                               \
        /* If module is not given, we are defining a global class */           \
        JS::RootedObject in_obj(cx, module);                                   \
        if (!in_obj)                                                           \
            in_obj = gjs_get_import_global(cx);                                \
                                                                               \
        /* Create the class, prototype, and constructor */                     \
        JS::RootedObject parent_proto(cx, gjs_##parent_cname##_get_proto(cx)); \
        proto.set(JS_InitClass(cx, in_obj, parent_proto, &gjs_##cname##_class, \
                               ctor, 0, gjs_##cname##_proto_props,             \
                               gjs_##cname##_proto_funcs, nullptr,             \
                               gjs_##cname##_static_funcs));                   \
        if (!proto)                                                            \
            return false;                                                      \
        gjs_set_global_slot(cx, GJS_GLOBAL_SLOT_PROTOTYPE_##cname,             \
                            JS::ObjectValue(*proto));                          \
                                                                               \
        /* Look up the constructor */                                          \
        JS::RootedObject ctor_obj(cx);                                         \
        JS::RootedId class_name(                                               \
            cx, gjs_intern_string_to_id(cx, gjs_##cname##_class.name));        \
        if (class_name == JSID_VOID)                                           \
            return false;                                                      \
        if (!gjs_object_require_property(cx, in_obj, #cname " constructor",    \
                                         class_name, &ctor_obj))               \
            return false;                                                      \
                                                                               \
        /* JS_InitClass defines the constructor as a property on the given     \
         * "global" object. If it's a module and not the real global object,   \
         * redefine it with different flags so it's enumerable; cairo copies   \
         * properties from cairoNative, for example */                         \
        if (module) {                                                          \
            if (!JS_DefinePropertyById(cx, module, class_name, ctor_obj,       \
                                       GJS_MODULE_PROP_FLAGS))                 \
                return false;                                                  \
        }                                                                      \
                                                                               \
        /* Define the GType value as a "$gtype" property on the constructor */ \
        if (type != G_TYPE_NONE) {                                             \
            if (!gjs_wrapper_define_gtype_prop(cx, ctor_obj, type))            \
                return false;                                                  \
        }                                                                      \
        gjs_debug(GJS_DEBUG_CONTEXT, "Initialized class %s prototype %p",      \
                  gjs_##cname##_class.name, proto.get());                      \
        return true;                                                           \
    }

/**
 * GJS_NATIVE_CONSTRUCTOR_DECLARE:
 * Prototype a constructor.
 */
#define GJS_NATIVE_CONSTRUCTOR_DECLARE(name)                          \
    GJS_JSAPI_RETURN_CONVENTION static bool gjs_##name##_constructor( \
        JSContext* context, unsigned argc, JS::Value* vp)

/**
 * GJS_NATIVE_CONSTRUCTOR_VARIABLES:
 * Declare variables necessary for the constructor; should
 * be at the very top.
 */
#define GJS_NATIVE_CONSTRUCTOR_VARIABLES(name)                      \
    JS::RootedObject object(context);                               \
    JS::CallArgs argv G_GNUC_UNUSED = JS::CallArgsFromVp(argc, vp);

/**
 * GJS_NATIVE_CONSTRUCTOR_PRELUDE:
 * Call after the initial variable declaration.
 */
#define GJS_NATIVE_CONSTRUCTOR_PRELUDE(name)                                  \
{                                                                             \
    if (!argv.isConstructing()) {                                             \
        gjs_throw_constructor_error(context);                                 \
        return false;                                                         \
    }                                                                         \
    object = JS_NewObjectForConstructor(context, &gjs_##name##_class, argv);  \
    if (!object)                                                              \
        return false;                                                         \
}

/**
 * GJS_NATIVE_CONSTRUCTOR_FINISH:
 * Call this at the end of a constructor when it's completed
 * successfully.
 */
#define GJS_NATIVE_CONSTRUCTOR_FINISH(name)             \
    argv.rval().setObject(*object);

/**
 * GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT:
 * Defines a constructor whose only purpose is to throw an error
 * and fail. To be used with classes that require a constructor (because they have
 * instances), but whose constructor cannot be used from JS code.
 */
#define GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(name)            \
    GJS_NATIVE_CONSTRUCTOR_DECLARE(name)                        \
    {                                                           \
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);       \
        gjs_throw_abstract_constructor_error(context, args);    \
        return false;                                           \
    }

GJS_USE
JS::Value gjs_dynamic_property_private_slot(JSObject *accessor_obj);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_in_prototype_chain(JSContext* cx, JS::HandleObject proto,
                                   JS::HandleObject check_obj,
                                   bool* is_in_chain);

#endif  // GJS_JSAPI_CLASS_H_
