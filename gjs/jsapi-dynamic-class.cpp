/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *               2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include <util/log.h>
#include <util/glib.h>
#include <util/misc.h>

#include "jsapi-class.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"

#include <string.h>
#include <math.h>

bool
gjs_init_class_dynamic(JSContext              *context,
                       JS::HandleObject        in_object,
                       JS::HandleObject        parent_proto,
                       const char             *ns_name,
                       const char             *class_name,
                       JSClass                *clasp,
                       JSNative                constructor_native,
                       unsigned                nargs,
                       JSPropertySpec         *proto_ps,
                       JSFunctionSpec         *proto_fs,
                       JSPropertySpec         *static_ps,
                       JSFunctionSpec         *static_fs,
                       JS::MutableHandleObject prototype,
                       JS::MutableHandleObject constructor)
{
    /* Force these variables on the stack, so the conservative GC will
       find them */
    JSFunction * volatile constructor_fun;
    char *full_function_name = NULL;
    bool res = false;

    /* Without a name, JS_NewObject fails */
    g_assert (clasp->name != NULL);

    g_assert(((void) "If you have a resolve hook, you also need mayResolve",
              (clasp->cOps->resolve == nullptr) == (clasp->cOps->mayResolve == nullptr)));

    /* gjs_init_class_dynamic only makes sense for instantiable classes,
       use JS_InitClass for static classes like Math */
    g_assert (constructor_native != NULL);

    JS_BeginRequest(context);

    /* Class initalization consists of three parts:
       - building a prototype
       - defining prototype properties and functions
       - building a constructor and definining it on the right object
       - defining constructor properties and functions
       - linking the constructor and the prototype, so that
         JS_NewObjectForConstructor can find it
    */

    if (parent_proto) {
        prototype.set(JS_NewObjectWithGivenProto(context, clasp, parent_proto));
    } else {
        /* JS_NewObject will use Object.prototype as the prototype if the
         * clasp's constructor is not a built-in class.
         */
        prototype.set(JS_NewObject(context, clasp));
    }
    if (!prototype)
        goto out;

    /* Bypass resolve hooks when defining the initial properties */
    if (clasp->cOps->resolve) {
        JSPropertySpec *ps_iter;
        JSFunctionSpec *fs_iter;
        for (ps_iter = proto_ps; ps_iter && ps_iter->name; ps_iter++)
            ps_iter->flags |= JSPROP_RESOLVING;
        for (fs_iter = proto_fs; fs_iter && fs_iter->name; fs_iter++)
            fs_iter->flags |= JSPROP_RESOLVING;
    }

    if (proto_ps && !JS_DefineProperties(context, prototype, proto_ps))
        goto out;
    if (proto_fs && !JS_DefineFunctions(context, prototype, proto_fs))
        goto out;

    full_function_name = g_strdup_printf("%s_%s", ns_name, class_name);
    constructor_fun = JS_NewFunction(context, constructor_native, nargs, JSFUN_CONSTRUCTOR,
                                     full_function_name);
    if (!constructor_fun)
        goto out;

    constructor.set(JS_GetFunctionObject(constructor_fun));

    if (static_ps && !JS_DefineProperties(context, constructor, static_ps))
        goto out;
    if (static_fs && !JS_DefineFunctions(context, constructor, static_fs))
        goto out;

    if (!clasp->cOps->resolve) {
        if (!JS_LinkConstructorAndPrototype(context, constructor, prototype))
            goto out;
    } else {
        /* Have to fake it with JSPROP_RESOLVING, otherwise it will trigger
         * the resolve hook */
        if (!gjs_object_define_property(context, constructor,
                                        GJS_STRING_PROTOTYPE, prototype,
                                        JSPROP_PERMANENT | JSPROP_READONLY | JSPROP_RESOLVING))
            goto out;
        if (!gjs_object_define_property(context, prototype,
                                        GJS_STRING_CONSTRUCTOR, constructor,
                                        JSPROP_RESOLVING))
            goto out;
    }

    /* The constructor defined by JS_InitClass has no property attributes, but this
       is a more useful default for gjs */
    if (!JS_DefineProperty(context, in_object, class_name, constructor,
                           GJS_MODULE_PROP_FLAGS))
        goto out;

    res = true;

    constructor_fun = NULL;

 out:
    JS_EndRequest(context);
    g_free(full_function_name);

    return res;
}

static const char*
format_dynamic_class_name (const char *name)
{
    if (g_str_has_prefix(name, "_private_"))
        return name + strlen("_private_");
    else
        return name;
}

bool
gjs_typecheck_instance(JSContext       *context,
                       JS::HandleObject obj,
                       const JSClass   *static_clasp,
                       bool             throw_error)
{
    if (!JS_InstanceOf(context, obj, static_clasp, NULL)) {
        if (throw_error) {
            const JSClass *obj_class = JS_GetClass(obj);

            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object %p is not a subclass of %s, it's a %s",
                             obj.get(), static_clasp->name,
                             format_dynamic_class_name(obj_class->name));
        }

        return false;
    }

    return true;
}

JSObject*
gjs_construct_object_dynamic(JSContext                  *context,
                             JS::HandleObject            proto,
                             const JS::HandleValueArray& args)
{
    JSAutoRequest ar(context);

    JS::RootedObject constructor(context);

    if (!gjs_object_require_property(context, proto, "prototype",
                                     GJS_STRING_CONSTRUCTOR,
                                     &constructor))
        return NULL;

    return JS_New(context, constructor, args);
}

/**
 * gjs_dynamic_class_default_may_resolve:
 * @names: SpiderMonkey internal, unused
 * @id: property ID being queried
 * @maybe_obj: object being queried, or nullptr if not known
 *
 * If you have a resolve hook on a dynamic class, you also need a mayResolve
 * hook. Otherwise, when gjs_init_class_dynamic() calls
 * JS_LinkConstructorAndPrototype(), the resolve hook will be called to resolve
 * the "constructor" and "prototype" properties first.
 *
 * If you don't have any other predefined properties or methods that you want
 * to prevent from being resolved, you can just use this function as the
 * mayResolve hook.
 */
bool
gjs_dynamic_class_default_may_resolve(const JSAtomState& names,
                                      jsid               id,
                                      JSObject          *maybe_obj)
{
    if (!JSID_IS_STRING(id))
        return false;
    JSFlatString *str = JSID_TO_FLAT_STRING(id);
    return !(JS_FlatStringEqualsAscii(str, "constructor") ||
             JS_FlatStringEqualsAscii(str, "prototype"));
}
