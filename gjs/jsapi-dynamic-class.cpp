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

#include "gjs/context-private.h"
#include "jsapi-class.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"

#include <string.h>
#include <math.h>

/* Reserved slots of JSNative accessor wrappers */
enum {
    DYNAMIC_PROPERTY_PRIVATE_SLOT,
};

bool gjs_init_class_dynamic(JSContext* context, JS::HandleObject in_object,
                            JS::HandleObject parent_proto, const char* ns_name,
                            const char* class_name, const JSClass* clasp,
                            JSNative constructor_native, unsigned nargs,
                            JSPropertySpec* proto_ps, JSFunctionSpec* proto_fs,
                            JSPropertySpec* static_ps,
                            JSFunctionSpec* static_fs,
                            JS::MutableHandleObject prototype,
                            JS::MutableHandleObject constructor) {
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

    /* Class initalization consists of five parts:
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
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        if (!JS_DefinePropertyById(
                context, constructor, atoms.prototype(), prototype,
                JSPROP_PERMANENT | JSPROP_READONLY | JSPROP_RESOLVING))
            goto out;
        if (!JS_DefinePropertyById(context, prototype, atoms.constructor(),
                                   constructor, JSPROP_RESOLVING))
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

GJS_USE
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

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject constructor(context);

    if (!gjs_object_require_property(context, proto, "prototype",
                                     atoms.constructor(), &constructor))
        return NULL;

    return JS_New(context, constructor, args);
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
define_native_accessor_wrapper(JSContext      *cx,
                               JSNative        call,
                               unsigned        nargs,
                               const char     *func_name,
                               JS::HandleValue private_slot)
{
    JSFunction *func = js::NewFunctionWithReserved(cx, call, nargs, 0, func_name);
    if (!func)
        return nullptr;

    JSObject *func_obj = JS_GetFunctionObject(func);
    js::SetFunctionNativeReserved(func_obj, DYNAMIC_PROPERTY_PRIVATE_SLOT,
                                  private_slot);
    return func_obj;
}

/**
 * gjs_define_property_dynamic:
 * @cx: the #JSContext
 * @proto: the prototype of the object, on which to define the property
 * @prop_name: name of the property or field in GObject, visible to JS code
 * @func_namespace: string from which the internal names for the getter and
 *   setter functions are built, not visible to JS code
 * @getter: getter function
 * @setter: setter function
 * @private_slot: private data in the form of a #JS::Value that the getter and
 *   setter will have access to
 * @flags: additional flags to define the property with (other than the ones
 *   required for a property with native getter/setter)
 *
 * When defining properties in a GBoxed or GObject, we can't have a separate
 * getter and setter for each one, since the properties are defined dynamically.
 * Therefore we must have one getter and setter for all the properties we define
 * on all the types. In order to have that, we must provide the getter and
 * setter with private data, e.g. the field index for GBoxed, in a "reserved
 * slot" for which we must unfortunately use the jsfriendapi.
 *
 * Returns: %true on success, %false if an exception is pending on @cx.
 */
bool
gjs_define_property_dynamic(JSContext       *cx,
                            JS::HandleObject proto,
                            const char      *prop_name,
                            const char      *func_namespace,
                            JSNative         getter,
                            JSNative         setter,
                            JS::HandleValue  private_slot,
                            unsigned         flags)
{
    GjsAutoChar getter_name = g_strconcat(func_namespace, "_get::", prop_name, nullptr);
    GjsAutoChar setter_name = g_strconcat(func_namespace, "_set::", prop_name, nullptr);

    JS::RootedObject getter_obj(cx,
        define_native_accessor_wrapper(cx, getter, 0, getter_name, private_slot));
    if (!getter_obj)
        return false;

    JS::RootedObject setter_obj(cx,
        define_native_accessor_wrapper(cx, setter, 1, setter_name, private_slot));
    if (!setter_obj)
        return false;

    flags |= JSPROP_GETTER | JSPROP_SETTER;

    return JS_DefineProperty(
        cx, proto, prop_name, JS_DATA_TO_FUNC_PTR(JSNative, getter_obj.get()),
        JS_DATA_TO_FUNC_PTR(JSNative, setter_obj.get()), flags);
}

/**
 * gjs_dynamic_property_private_slot:
 * @accessor_obj: the getter or setter as a function object, i.e.
 *   `&args.callee()` in the #JSNative function
 *
 * For use in dynamic property getters and setters (see
 * gjs_define_property_dynamic()) to retrieve the private data passed there.
 *
 * Returns: the JS::Value that was passed to gjs_define_property_dynamic().
 */
JS::Value
gjs_dynamic_property_private_slot(JSObject *accessor_obj)
{
    return js::GetFunctionNativeReserved(accessor_obj,
                                         DYNAMIC_PROPERTY_PRIVATE_SLOT);
}
