/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>

#include <config.h>

#include <string.h>  // for strlen

#include <glib.h>

#include <js/CallAndConstruct.h>
#include <js/CallArgs.h>  // for JSNative
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>         // for JSEXN_TYPEERR
#include <js/Object.h>              // for GetClass
#include <js/PropertyAndElement.h>  // for JS_DefineFunctions, JS_DefinePro...
#include <js/Realm.h>  // for GetRealmObjectPrototype
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>        // for JS_GetFunctionObject, JS_GetPrototype
#include <jsfriendapi.h>  // for GetFunctionNativeReserved, NewFun...

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"  // IWYU pragma: associated
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

struct JSFunctionSpec;
struct JSPropertySpec;

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
    /* Without a name, JS_NewObject fails */
    g_assert (clasp->name != NULL);

    /* gjs_init_class_dynamic only makes sense for instantiable classes,
       use JS_InitClass for static classes like Math */
    g_assert (constructor_native != NULL);

    /* Class initialization consists of five parts:
       - building a prototype
       - defining prototype properties and functions
       - building a constructor and defining it on the right object
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
        return false;

    if (proto_ps && !JS_DefineProperties(context, prototype, proto_ps))
        return false;
    if (proto_fs && !JS_DefineFunctions(context, prototype, proto_fs))
        return false;

    Gjs::AutoChar full_function_name{
        g_strdup_printf("%s_%s", ns_name, class_name)};
    JSFunction* constructor_fun =
        JS_NewFunction(context, constructor_native, nargs, JSFUN_CONSTRUCTOR,
                       full_function_name);
    if (!constructor_fun)
        return false;

    constructor.set(JS_GetFunctionObject(constructor_fun));

    if (static_ps && !JS_DefineProperties(context, constructor, static_ps))
        return false;
    if (static_fs && !JS_DefineFunctions(context, constructor, static_fs))
        return false;

    if (!JS_LinkConstructorAndPrototype(context, constructor, prototype))
        return false;

    /* The constructor defined by JS_InitClass has no property attributes, but this
       is a more useful default for gjs */
    return JS_DefineProperty(context, in_object, class_name, constructor,
                             GJS_MODULE_PROP_FLAGS);
}

[[nodiscard]] static const char* format_dynamic_class_name(const char* name) {
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
            const JSClass* obj_class = JS::GetClass(obj);

            gjs_throw_custom(context, JSEXN_TYPEERR, nullptr,
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
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject constructor(context);

    if (!gjs_object_require_property(context, proto, "prototype",
                                     atoms.constructor(), &constructor))
        return NULL;

    JS::RootedValue v_constructor(context, JS::ObjectValue(*constructor));
    JS::RootedObject object(context);
    if (!JS::Construct(context, v_constructor, args, &object))
        return nullptr;

    return object;
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
bool gjs_define_property_dynamic(JSContext* cx, JS::HandleObject proto,
                                 const char* prop_name, JS::HandleId id,
                                 const char* func_namespace, JSNative getter,
                                 JS::HandleValue getter_slot, JSNative setter,
                                 JS::HandleValue setter_slot, unsigned flags) {
    Gjs::AutoChar getter_name{
        g_strconcat(func_namespace, "_get::", prop_name, nullptr)};
    Gjs::AutoChar setter_name{
        g_strconcat(func_namespace, "_set::", prop_name, nullptr)};

    JS::RootedObject getter_obj(
        cx, define_native_accessor_wrapper(cx, getter, 0, getter_name,
                                           getter_slot));
    if (!getter_obj)
        return false;

    JS::RootedObject setter_obj(
        cx, define_native_accessor_wrapper(cx, setter, 1, setter_name,
                                           setter_slot));
    if (!setter_obj)
        return false;

    if (id.isVoid()) {
        return JS_DefineProperty(cx, proto, prop_name, getter_obj, setter_obj,
                                 flags);
    }

    return JS_DefinePropertyById(cx, proto, id, getter_obj, setter_obj, flags);
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

/**
 * gjs_object_in_prototype_chain:
 * @cx:
 * @proto: The prototype which we are checking if @check_obj has in its chain
 * @check_obj: The object to check
 * @is_in_chain: (out): Whether @check_obj has @proto in its prototype chain
 *
 * Similar to JS_HasInstance() but takes into account abstract classes defined
 * with JS_InitClass(), which JS_HasInstance() does not. Abstract classes don't
 * have constructors, and JS_HasInstance() requires a constructor.
 *
 * Returns: false if an exception was thrown, true otherwise.
 */
bool gjs_object_in_prototype_chain(JSContext* cx, JS::HandleObject proto,
                                   JS::HandleObject check_obj,
                                   bool* is_in_chain) {
    JS::RootedObject object_prototype(cx, JS::GetRealmObjectPrototype(cx));
    if (!object_prototype)
        return false;

    JS::RootedObject proto_iter(cx);
    if (!JS_GetPrototype(cx, check_obj, &proto_iter))
        return false;
    while (proto_iter != object_prototype) {
        if (proto_iter == proto) {
            *is_in_chain = true;
            return true;
        }
        if (!JS_GetPrototype(cx, proto_iter, &proto_iter))
            return false;
    }
    *is_in_chain = false;
    return true;
}
