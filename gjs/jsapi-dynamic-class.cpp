/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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
#include "jsapi-private.h"

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

    /* gjs_init_class_dynamic only makes sense for instantiable classes,
       use JS_InitClass for static classes like Math */
    g_assert (constructor_native != NULL);

    JS_BeginRequest(context);

    JS::RootedObject global(context, gjs_get_import_global(context));

    /* Class initalization consists of three parts:
       - building a prototype
       - defining prototype properties and functions
       - building a constructor and definining it on the right object
       - defining constructor properties and functions
       - linking the constructor and the prototype, so that
         JS_NewObjectForConstructor can find it
    */

    if (parent_proto) {
        prototype.set(JS_NewObjectWithGivenProto(context, clasp,
                                                 parent_proto, global));
    } else {
        /* JS_NewObject will try to search for clasp prototype in the
         * global object, which is wrong, but it's not a problem because
         * it will fallback to Object.prototype if the clasp's
         * constructor is not found (and it won't be found, because we
         * never call JS_InitClass).
         */
        prototype.set(JS_NewObject(context, clasp, global));
    }
    if (!prototype)
        goto out;

    if (proto_ps && !JS_DefineProperties(context, prototype, proto_ps))
        goto out;
    if (proto_fs && !JS_DefineFunctions(context, prototype, proto_fs))
        goto out;

    full_function_name = g_strdup_printf("%s_%s", ns_name, class_name);
    constructor_fun = JS_NewFunction(context, constructor_native, nargs, JSFUN_CONSTRUCTOR,
                                     global, full_function_name);
    if (!constructor_fun)
        goto out;

    constructor.set(JS_GetFunctionObject(constructor_fun));

    if (static_ps && !JS_DefineProperties(context, constructor, static_ps))
        goto out;
    if (static_fs && !JS_DefineFunctions(context, constructor, static_fs))
        goto out;

    if (!JS_LinkConstructorAndPrototype(context, constructor, prototype))
        goto out;

    /* The constructor defined by JS_InitClass has no property attributes, but this
       is a more useful default for gjs */
    if (!JS_DefineProperty(context, in_object, class_name, constructor,
                           GJS_MODULE_PROP_FLAGS, JS_STUBGETTER, JS_STUBSETTER))
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

            gjs_throw_custom(context, "TypeError", NULL,
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
