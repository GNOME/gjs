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

#include "function.h"
#include "gtype.h"
#include "interface.h"
#include "object.h"
#include "repo.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem.h"

#include <util/log.h>

#include <girepository.h>

typedef struct {
    GIInterfaceInfo *info;
    GType gtype;
    /* the GTypeInterface vtable wrapped by this JS Object (only used for
       prototypes) */
    GTypeInterface *vtable;
} Interface;

extern struct JSClass gjs_interface_class;

GJS_DEFINE_PRIV_FROM_JS(Interface, gjs_interface_class)

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(interface)

static void
interface_finalize(JSFreeOp *fop,
                   JSObject *obj)
{
    Interface *priv;

    priv = (Interface*) JS_GetPrivate(obj);

    if (priv == NULL)
        return;

    if (priv->info != NULL)
        g_base_info_unref((GIBaseInfo*)priv->info);

    g_clear_pointer(&priv->vtable, (GDestroyNotify)g_type_default_interface_unref);

    GJS_DEC_COUNTER(interface);
    g_slice_free(Interface, priv);
}

static bool
gjs_define_static_methods(JSContext       *context,
                          JS::HandleObject constructor,
                          GType            gtype,
                          GIInterfaceInfo *info)
{
    int i;
    int n_methods;

    n_methods = g_interface_info_get_n_methods(info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfo *meth_info;
        GIFunctionInfoFlags flags;

        meth_info = g_interface_info_get_method (info, i);
        flags = g_function_info_get_flags (meth_info);

        /* Anything that isn't a method we put on the prototype of the
         * constructor.  This includes <constructor> introspection
         * methods, as well as the forthcoming "static methods"
         * support.  We may want to change this to use
         * GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the
         * like in the near future.
         */
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            gjs_define_function(context, constructor, gtype,
                                (GICallableInfo *)meth_info);
        }

        g_base_info_unref((GIBaseInfo*) meth_info);
    }
    return true;
}

static bool
interface_resolve(JSContext       *context,
                  JS::HandleObject obj,
                  JS::HandleId     id,
                  bool            *resolved)
{
    Interface *priv;
    GIFunctionInfo *method_info;

    priv = priv_from_js(context, obj);

    if (priv == nullptr)
        return false;

    /* If we have no GIRepository information then this interface was defined
     * from within GJS. In that case, it has no properties that need to be
     * resolved from within C code, as interfaces cannot inherit. */
    if (priv->info == NULL) {
        *resolved = false;
        return true;
    }

    GjsAutoJSChar name;
    if (!gjs_get_string_id(context, id, &name)) {
        *resolved = false;
        return true;
    }

    method_info = g_interface_info_find_method((GIInterfaceInfo*) priv->info, name);

    if (method_info != NULL) {
        if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
            if (gjs_define_function(context, obj,
                                    priv->gtype,
                                    (GICallableInfo*)method_info) == NULL) {
                g_base_info_unref((GIBaseInfo*)method_info);
                return false;
            }

            *resolved = true;
        } else {
            *resolved = false;
        }

        g_base_info_unref((GIBaseInfo*)method_info);
    } else {
        *resolved = false;
    }

    return true;
}

static bool
interface_has_instance_func(JSContext *cx,
                            unsigned   argc,
                            JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    /* This method is not called directly, so no need for error messages */

    JS::RootedValue interface(cx, args.computeThis(cx));
    g_assert(interface.isObject());
    JS::RootedObject interface_constructor(cx, &interface.toObject());
    JS::RootedObject interface_proto(cx);
    gjs_object_require_property(cx, interface_constructor,
                                "interface constructor",
                                GJS_STRING_PROTOTYPE, &interface_proto);

    Interface *priv;
    if (!priv_from_js_with_typecheck(cx, interface_proto, &priv))
        return false;

    g_assert(args.length() == 1);
    g_assert(args[0].isObject());
    JS::RootedObject instance(cx, &args[0].toObject());
    bool isinstance = gjs_typecheck_object(cx, instance, priv->gtype, false);
    args.rval().setBoolean(isinstance);
    return true;
}

static const struct JSClassOps gjs_interface_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    interface_resolve,
    nullptr,  // mayResolve
    interface_finalize};

struct JSClass gjs_interface_class = {
    "GObject_Interface",
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
    &gjs_interface_class_ops
};

JSPropertySpec gjs_interface_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_interface_proto_funcs[] = {
    JS_FS_END
};

JSFunctionSpec gjs_interface_static_funcs[] = {
    JS_SYM_FN(hasInstance, interface_has_instance_func, 1, 0),
    JS_FS_END
};

bool
gjs_define_interface_class(JSContext              *context,
                           JS::HandleObject        in_object,
                           GIInterfaceInfo        *info,
                           GType                   gtype,
                           JS::MutableHandleObject constructor)
{
    Interface *priv;
    const char *constructor_name;
    const char *ns;
    JS::RootedObject prototype(context);

    ns = gjs_get_names_from_gtype_and_gi_info(gtype, (GIBaseInfo *) info,
                                              &constructor_name);

    if (!gjs_init_class_dynamic(context, in_object, nullptr, ns,
                                constructor_name,
                                &gjs_interface_class,
                                gjs_interface_constructor, 0,
                                /* props of prototype */
                                &gjs_interface_proto_props[0],
                                /* funcs of prototype */
                                &gjs_interface_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                gjs_interface_static_funcs,
                                &prototype,
                                constructor)) {
        g_error("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(interface);
    priv = g_slice_new0(Interface);
    priv->info = info == NULL ? NULL : g_base_info_ref((GIBaseInfo *) info);
    priv->gtype = gtype;
    priv->vtable = (GTypeInterface *) g_type_default_interface_ref(gtype);
    JS_SetPrivate(prototype, priv);

    /* If we have no GIRepository information, then this interface was defined
     * from within GJS and therefore has no C static methods to be defined. */
    if (priv->info)
        gjs_define_static_methods(context, constructor, priv->gtype, priv->info);

    JS::RootedObject gtype_obj(context,
        gjs_gtype_create_gtype_wrapper(context, priv->gtype));
    JS_DefineProperty(context, constructor, "$gtype", gtype_obj, JSPROP_PERMANENT);

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
