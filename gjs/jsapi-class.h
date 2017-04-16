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

#ifndef GJS_JSAPI_CLASS_H
#define GJS_JSAPI_CLASS_H

#include "jsapi-util.h"
#include "jsapi-wrapper.h"

G_BEGIN_DECLS

bool gjs_init_class_dynamic(JSContext              *cx,
                            JS::HandleObject        in_object,
                            JS::HandleObject        parent_proto,
                            const char             *ns_name,
                            const char             *class_name,
                            JSClass                *clasp,
                            JSNative                constructor_native,
                            unsigned                nargs,
                            JSPropertySpec         *ps,
                            JSFunctionSpec         *fs,
                            JSPropertySpec         *static_ps,
                            JSFunctionSpec         *static_fs,
                            JS::MutableHandleObject prototype,
                            JS::MutableHandleObject constructor);

bool gjs_typecheck_instance(JSContext       *cx,
                            JS::HandleObject obj,
                            const JSClass   *static_clasp,
                            bool             throw_error);

JSObject *gjs_construct_object_dynamic(JSContext                  *cx,
                                       JS::HandleObject            proto,
                                       const JS::HandleValueArray& args);

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
#define GJS_DEFINE_PRIV_FROM_JS(type, klass)                          \
    G_GNUC_UNUSED static inline bool                                    \
    do_base_typecheck(JSContext       *context,                         \
                      JS::HandleObject object,                          \
                      bool             throw_error)                     \
    {                                                                   \
        return gjs_typecheck_instance(context, object, &klass, throw_error);  \
    }                                                                   \
    static inline type *                                                \
    priv_from_js(JSContext       *context,                              \
                 JS::HandleObject object)                               \
    {                                                                   \
        type *priv;                                                     \
        JS_BeginRequest(context);                                       \
        priv = (type*) JS_GetInstancePrivate(context, object, &klass, NULL);  \
        JS_EndRequest(context);                                         \
        return priv;                                                    \
    }                                                                   \
    G_GNUC_UNUSED static bool                                           \
    priv_from_js_with_typecheck(JSContext       *context,               \
                                JS::HandleObject object,                \
                                type           **out)                   \
    {                                                                   \
        if (!do_base_typecheck(context, object, false))                 \
            return false;                                               \
        *out = priv_from_js(context, object);                           \
        return true;                                                    \
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
_GJS_DEFINE_PROTO_FULL(tn, cn, nullptr, G_TYPE_NONE, flags)

#define GJS_DEFINE_PROTO_WITH_GTYPE(tn, cn, gtype, flags)          \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn);                                \
_GJS_DEFINE_PROTO_FULL(tn, cn, gjs_##cn##_constructor, gtype, flags)

#define GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE(tn, cn, gtype, flags)  \
_GJS_DEFINE_PROTO_FULL(tn, cn, nullptr, gtype, flags)

#define GJS_DEFINE_PROTO_WITH_PARENT(tn, cn, flags)     \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn);                     \
_GJS_DEFINE_PROTO_FULL(tn, cn, gjs_##cn##_constructor, G_TYPE_NONE, flags)

#define GJS_DEFINE_PROTO_ABSTRACT_WITH_PARENT(tn, cn, flags)  \
_GJS_DEFINE_PROTO_FULL(tn, cn, nullptr, G_TYPE_NONE, flags)

#define _GJS_DEFINE_PROTO_FULL(type_name, cname, ctor, gtype, jsclass_flags) \
extern JSPropertySpec gjs_##cname##_proto_props[];                           \
extern JSFunctionSpec gjs_##cname##_proto_funcs[];                           \
static void gjs_##cname##_finalize(JSFreeOp *fop, JSObject *obj);            \
static JS::PersistentRootedObject gjs_##cname##_prototype;                   \
static struct JSClass gjs_##cname##_class = {                                \
    type_name,                                                               \
    JSCLASS_HAS_PRIVATE | jsclass_flags,                                     \
    nullptr,  /* addProperty */                                              \
    nullptr,  /* deleteProperty */                                           \
    nullptr,  /* getProperty */                                              \
    nullptr,  /* setProperty */                                              \
    nullptr,  /* enumerate */                                                \
    nullptr,  /* resolve */                                                  \
    nullptr,  /* convert */                                                  \
    gjs_##cname##_finalize                                                   \
};                                                                           \
JSObject *                                                                   \
gjs_##cname##_create_proto(JSContext       *cx,                              \
                           JS::HandleObject module,                          \
                           const char      *proto_name,                      \
                           JS::HandleObject parent)                          \
{                                                                            \
    JS::RootedObject rval(cx);                                               \
    JS::RootedObject global(cx, gjs_get_import_global(cx));                  \
    JS::RootedId class_name(cx,                                              \
        gjs_intern_string_to_id(cx, gjs_##cname##_class.name));              \
    bool found = false;                                                      \
    if (!JS_AlreadyHasOwnPropertyById(cx, global, class_name, &found))       \
        return nullptr;                                                      \
    if (!found) {                                                            \
        gjs_##cname##_prototype.init(cx);                                    \
        gjs_##cname##_prototype =                                            \
            JS_InitClass(cx, global, parent, &gjs_##cname##_class, ctor,     \
                         0, &gjs_##cname##_proto_props[0],                   \
                         &gjs_##cname##_proto_funcs[0],                      \
                         nullptr, nullptr);                                  \
        if (!gjs_##cname##_prototype)                                        \
            return nullptr;                                                  \
    }                                                                        \
    if (!gjs_object_require_property(cx, global, nullptr,                    \
                                     class_name, &rval))                     \
        return nullptr;                                                      \
    if (found)                                                               \
        return rval;                                                         \
    if (!JS_DefineProperty(cx, module, proto_name,                           \
                           rval, GJS_MODULE_PROP_FLAGS))                     \
        return nullptr;                                                      \
    if (gtype != G_TYPE_NONE) {                                              \
        JS::RootedObject gtype_obj(cx,                                       \
            gjs_gtype_create_gtype_wrapper(cx, gtype));                      \
        JS_DefineProperty(cx, rval, "$gtype", gtype_obj,                     \
                          JSPROP_PERMANENT);                                 \
    }                                                                        \
    return rval;                                                             \
}

/**
 * GJS_NATIVE_CONSTRUCTOR_DECLARE:
 * Prototype a constructor.
 */
#define GJS_NATIVE_CONSTRUCTOR_DECLARE(name)            \
static bool                                             \
gjs_##name##_constructor(JSContext  *context,           \
                         unsigned    argc,              \
                         JS::Value  *vp)

/**
 * GJS_NATIVE_CONSTRUCTOR_VARIABLES:
 * Declare variables necessary for the constructor; should
 * be at the very top.
 */
#define GJS_NATIVE_CONSTRUCTOR_VARIABLES(name)                      \
    JS::RootedObject object(context, NULL);                         \
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
    if (object == NULL)                                                       \
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

G_END_DECLS

#endif /* GJS_JSAPI_CLASS_H */
