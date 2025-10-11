/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <assert.h>
#include <stddef.h>  // for size_t

#include <string>       // for string methods
#include <type_traits>  // for integral_constant

#include <glib-object.h>  // for GType

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ErrorReport.h>  // for JSEXN_TYPEERR
#include <js/GCVector.h>     // for MutableHandleIdVector
#include <js/GlobalObject.h>  // for CurrentGlobalOrNull
#include <js/Id.h>
#include <js/Object.h>  // for GetClass
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JSFUN_CONSTRUCTOR, JS_NewPlainObject, JS_GetFuncti...
#include <jspubtd.h>  // for JSProto_Object, JSProtoKey

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

struct JSFunctionSpec;
struct JSPropertySpec;

// gi/cwrapper.h - template implementing a JS object that wraps a C pointer.
// This template is used for many of the special objects in GJS. It contains
// functionality such as storing the class's prototype in a global slot, where
// it can be easily retrieved in order to create new objects.

/*
 * GJS_CHECK_WRAPPER_PRIV:
 * @cx: JSContext pointer passed into JSNative function
 * @argc: Number of arguments passed into JSNative function
 * @vp: Argument value array passed into JSNative function
 * @args: Name for JS::CallArgs variable defined by this code snippet
 * @thisobj: Name for JS::RootedObject variable referring to function's this
 * @type: Type of private data
 * @priv: Name for private data variable defined by this code snippet
 *
 * A convenience macro for getting the private data from GJS classes using
 * CWrapper or GIWrapper.
 * Throws an error and returns false if the 'this' object is not the right type.
 * Use in any JSNative function.
 */
#define GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, thisobj, type, priv) \
    GJS_GET_THIS(cx, argc, vp, args, thisobj);                          \
    type* priv;                                                         \
    if (!type::for_js_typecheck(cx, thisobj, &priv, &args))             \
        return false;

GJS_JSAPI_RETURN_CONVENTION
bool gjs_wrapper_define_gtype_prop(JSContext* cx, JS::HandleObject constructor,
                                   GType gtype);

/*
 * CWrapperPointerOps:
 *
 * This class contains methods that are common to both CWrapper and
 * GIWrapperBase, for retrieving the wrapped C pointer out of the JS object.
 */
template <class Base, typename Wrapped = Base>
class CWrapperPointerOps {
 public:
    /*
     * CWrapperPointerOps::for_js:
     *
     * Gets the wrapped C pointer belonging to a particular JS object wrapper.
     * Checks that the wrapper object has the right JSClass (Base::klass).
     * A null return value means either that the object didn't have the right
     * class, or that no private data has been set yet on the wrapper. To
     * distinguish between these two cases, use for_js_typecheck().
     */
    [[nodiscard]] static Wrapped* for_js(JSContext* cx,
                                         JS::HandleObject wrapper) {
        if (!JS_InstanceOf(cx, wrapper, &Base::klass, nullptr))
            return nullptr;

        return JS::GetMaybePtrFromReservedSlot<Wrapped>(wrapper, POINTER);
    }

    /*
     * CWrapperPointerOps::typecheck:
     *
     * Checks if the given wrapper object has the right JSClass (Base::klass).
     */
    [[nodiscard]] static bool typecheck(JSContext* cx, JS::HandleObject wrapper,
                                        JS::CallArgs* args = nullptr) {
        return JS_InstanceOf(cx, wrapper, &Base::klass, args);
    }

    /*
     * CWrapperPointerOps::for_js_typecheck:
     *
     * Like for_js(), only throws a JS exception if the wrapper object has the
     * wrong class. Use in JSNative functions, where you have access to a
     * JS::CallArgs. The exception message will mention args.callee.
     *
     * The second overload can be used when you don't have access to an
     * instance of JS::CallArgs. The exception message will be generic.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool for_js_typecheck(JSContext* cx, JS::HandleObject wrapper,
                                 Wrapped** out, JS::CallArgs* args) {
        if (!typecheck(cx, wrapper, args))
            return false;
        *out = for_js_nocheck(wrapper);
        return true;
    }
    GJS_JSAPI_RETURN_CONVENTION
    static bool for_js_typecheck(JSContext* cx, JS::HandleObject wrapper,
                                 Wrapped** out) {
        if (!typecheck(cx, wrapper)) {
            const JSClass* obj_class = JS::GetClass(wrapper);
            gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                             "Object %p is not a subclass of %s, it's a %s",
                             wrapper.get(), Base::klass.name, obj_class->name);
            return false;
        }
        *out = for_js_nocheck(wrapper);
        return true;
    }

    /*
     * CWrapperPointerOps::for_js_nocheck:
     *
     * Use when you don't have a JSContext* available. This method is infallible
     * and cannot trigger a GC, so it's safe to use from finalize() and trace().
     * (It can return null if no private data has been set yet on the wrapper.)
     */
    [[nodiscard]] static Wrapped* for_js_nocheck(JSObject* wrapper) {
        return JS::GetMaybePtrFromReservedSlot<Wrapped>(wrapper, POINTER);
    }

 protected:
    // The first reserved slot always stores the private pointer.
    static const size_t POINTER = 0;

    /*
     * CWrapperPointerOps::has_private:
     *
     * Returns true if a private C pointer has already been associated with the
     * wrapper object.
     */
    [[nodiscard]] static bool has_private(JSObject* wrapper) {
        return !!JS::GetMaybePtrFromReservedSlot<Wrapped>(wrapper, POINTER);
    }

    /*
     * CWrapperPointerOps::init_private:
     *
     * Call this to initialize the wrapper object's private C pointer. The
     * pointer should not be null. This should not be called twice, without
     * calling unset_private() in between.
     */
    static void init_private(JSObject* wrapper, Wrapped* ptr) {
        assert(!has_private(wrapper) &&
               "wrapper object should be a fresh object");
        assert(ptr && "private pointer should not be null, use unset_private");
        JS::SetReservedSlot(wrapper, POINTER, JS::PrivateValue(ptr));
    }

    /*
     * CWrapperPointerOps::unset_private:
     *
     * Call this to remove the wrapper object's private C pointer. After calling
     * this, it's okay to call init_private() again.
     */
    static void unset_private(JSObject* wrapper) {
        JS::SetReservedSlot(wrapper, POINTER, JS::UndefinedValue());
    }
};

/*
 * CWrapper:
 *
 * This template implements a JS object that wraps a C pointer, stores its
 * prototype in a global slot, and includes some optional functionality.
 *
 * If you derive from this class, you must implement:
 *  - static constexpr GjsGlobalSlot PROTOTYPE_SLOT: global slot that the
 *    prototype will be stored in
 *  - static constexpr GjsDebugTopic DEBUG_TOPIC: debug log domain
 *  - static constexpr JSClass klass: see documentation in SpiderMonkey; the
 *    class may have JSClassOps (see below under CWrapper::class_ops) but must
 *    at least have its js::ClassSpec member set. The members of js::ClassSpec
 *    are createConstructor, createPrototype, constructorFunctions,
 *    constructorProperties, prototypeFunctions, prototypeProperties,
 *    finishInit, and flags.
 *  - static Wrapped* constructor_impl(JSContext*, const JS::CallArgs&): custom
 *    constructor functionality. If your JS object doesn't need a constructor
 *    (i.e. user code can't use the `new` operator on it) then you can skip this
 *    one, and include js::ClassSpec::DontDefineConstructor in your
 *    class_spec's flags member.
 *  - static constexpr unsigned constructor_nargs: number of arguments that the
 *    constructor takes. If you implement constructor_impl() then also add this.
 *  - void finalize_impl(JS::GCContext*, Wrapped*): called when the JS object is
 *    garbage collected, use this to free the C pointer and do any other cleanup
 *
 * Add optional functionality by setting members of class_spec:
 *  - createConstructor: the default is to create a constructor function that
 *    calls constructor_impl(), unless flags includes DontDefineConstructor. If
 *    you need something else, set this member.
 *  - createPrototype: the default is to use a plain object as the prototype. If
 *    you need something else, set this member.
 *  - constructorFunctions: If the class has static methods, set this member.
 *  - constructorProperties: If the class has static properties, set this
 *    member.
 *  - prototypeFunctions: If the class has methods, set this member.
 *  - prototypeProperties: If the class has properties, set this member.
 *  - finishInit: If you need to do any other initialization on the prototype or
 *    the constructor object, set this member.
 *  - flags: Specify DontDefineConstructor here if you don't want a user-visible
 *    constructor.
 *
 * You may override CWrapper::class_ops if you want to opt in to more JSClass
 * operations. In that case, CWrapper includes some optional functionality:
 *  - resolve: include &resolve in your class_ops, and implement
 *    bool resolve_impl(JSContext*, JS::HandleObject, JS::HandleId, bool*).
 *  - new enumerate: include &new_enumerate in your class_ops, and implement
 *    bool new_enumerate_impl(JSContext*, JS::HandleObject,
 *    JS::MutableHandleIdVector, bool).
 *
 * This template uses the Curiously Recurring Template Pattern (CRTP), which
 * requires inheriting classes to declare themselves friends of the parent
 * class, so that the parent class can call their private methods.
 *
 * For more information about the CRTP, the Wikipedia article is informative:
 * https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 */
template <class Base, typename Wrapped = Base>
class CWrapper : public CWrapperPointerOps<Base, Wrapped> {
    GJS_JSAPI_RETURN_CONVENTION
    static bool constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        if (!args.isConstructing()) {
            gjs_throw_constructor_error(cx);
            return false;
        }
        JS::RootedObject object(
            cx, JS_NewObjectForConstructor(cx, &Base::klass, args));
        if (!object)
            return false;

        Wrapped* priv = Base::constructor_impl(cx, args);
        if (!priv)
            return false;
        CWrapperPointerOps<Base, Wrapped>::init_private(object, priv);

        args.rval().setObject(*object);
        return true;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool abstract_constructor(JSContext* cx, unsigned argc,
                                     JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        gjs_throw_abstract_constructor_error(cx, args);
        return false;
    }

    // Debug methods, no-op unless verbose logging is compiled in

 protected:
    static void debug_lifecycle(
        const void* wrapped_ptr GJS_USED_VERBOSE_LIFECYCLE,
        const void* obj GJS_USED_VERBOSE_LIFECYCLE,
        const char* message GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(Base::DEBUG_TOPIC, "[%p: JS wrapper %p] %s",
                            wrapped_ptr, obj, message);
    }
    void debug_jsprop(const char* message GJS_USED_VERBOSE_PROPS,
                      const char* id GJS_USED_VERBOSE_PROPS,
                      const void* obj GJS_USED_VERBOSE_PROPS) const {
        gjs_debug_jsprop(Base::DEBUG_TOPIC, "[%p: JS wrapper %p] %s prop %s",
                         this, obj, message, id);
    }
    void debug_jsprop(const char* message, jsid id, const void* obj) const {
        if constexpr (GJS_VERBOSE_ENABLE_PROPS)
            debug_jsprop(message, gjs_debug_id(id).c_str(), obj);
    }

    static void finalize(JS::GCContext* gcx, JSObject* obj) {
        Wrapped* priv = Base::for_js_nocheck(obj);

        // Call only CWrapper's original method here, not any overrides; e.g.,
        // we don't want to deal with a read barrier.
        CWrapper::debug_lifecycle(priv, obj, "Finalize");

        Base::finalize_impl(gcx, priv);

        CWrapperPointerOps<Base, Wrapped>::unset_private(obj);
    }

    static constexpr JSClassOps class_ops = {
        nullptr,  // addProperty
        nullptr,  // deleteProperty
        nullptr,  // enumerate
        nullptr,  // newEnumerate
        nullptr,  // resolve
        nullptr,  // mayResolve
        &CWrapper::finalize,
    };

    /*
     * CWrapper::create_abstract_constructor:
     *
     * This function can be used as the createConstructor member of class_ops.
     * It creates a constructor that always throws if it is the new.target. Use
     * it if you do need a constructor object to exist (for example, if it has
     * static methods) but you don't want it to be able to be called.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_abstract_constructor(JSContext* cx, JSProtoKey) {
        return JS_GetFunctionObject(
            JS_NewFunction(cx, &Base::abstract_constructor, 0,
                           JSFUN_CONSTRUCTOR, Base::klass.name));
    }

    /*
     * CWrapper::define_gtype_prop:
     *
     * This function can be used as the finishInit member of class_ops. It
     * defines a '$gtype' property on the constructor. If you use it, you must
     * implement a gtype() static method that returns the GType to define.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_gtype_prop(JSContext* cx, JS::HandleObject ctor,
                                  JS::HandleObject proto [[maybe_unused]]) {
        return gjs_wrapper_define_gtype_prop(cx, ctor, Base::gtype());
    }

    // Used to get the prototype when it is guaranteed to have already been
    // created
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* prototype(JSContext* cx) {
        JSObject* global = JS::CurrentGlobalOrNull(cx);
        assert(global && "Must be in a realm to call prototype()");
        JS::RootedValue v_proto(
            cx, gjs_get_global_slot(global, Base::PROTOTYPE_SLOT));
        assert(!v_proto.isUndefined() &&
               "create_prototype() must be called before prototype()");
        assert(v_proto.isObject() &&
               "Someone stored some weird value in a global slot");
        return &v_proto.toObject();
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                        bool* resolved) {
        Wrapped* priv = CWrapperPointerOps<Base, Wrapped>::for_js(cx, obj);
        assert(priv && "resolve called on wrong object");
        priv->debug_jsprop("Resolve hook", id, obj);
        return priv->resolve_impl(cx, obj, id, resolved);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool new_enumerate(JSContext* cx, JS::HandleObject obj,
                              JS::MutableHandleIdVector properties,
                              bool only_enumerable) {
        Wrapped* priv = CWrapperPointerOps<Base, Wrapped>::for_js(cx, obj);
        assert(priv && "enumerate called on wrong object");
        priv->debug_jsprop("Enumerate hook", "(all)", obj);
        return priv->new_enumerate_impl(cx, obj, properties, only_enumerable);
    }

 public:
    /*
     * CWrapper::create_prototype:
     * @module: Object on which to define the constructor as a property, or
     *   the global object if not given
     *
     * Create the class's prototype and store it in the global slot, or
     * retrieve it if it has already been created.
     *
     * Unless DontDefineConstructor is in class_ops.flags, also create the
     * class's constructor, and define it as a property on @module.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create_prototype(JSContext* cx,
                                      JS::HandleObject module = nullptr) {
        JSObject* global = JS::CurrentGlobalOrNull(cx);
        assert(global && "Must be in a realm to call create_prototype()");

        // If we've been here more than once, we already have the proto
        JS::RootedValue v_proto(
            cx, gjs_get_global_slot(global, Base::PROTOTYPE_SLOT));
        if (!v_proto.isUndefined()) {
            assert(v_proto.isObject() &&
                   "Someone stored some weird value in a global slot");
            return &v_proto.toObject();
        }

        // Workaround for bogus warning
        // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=94554
        // Note that the corresponding function pointers in the js::ClassSpec
        // must be initialized as nullptr, not the default initializer! (see
        // e.g. CairoPath::class_spec.finishInit)
        using NullOpType =
            std::integral_constant<js::ClassObjectCreationOp, nullptr>;
        using CreateConstructorType =
            std::integral_constant<js::ClassObjectCreationOp,
                                   Base::klass.spec->createConstructor>;
        using CreatePrototypeType =
            std::integral_constant<js::ClassObjectCreationOp,
                                   Base::klass.spec->createPrototype>;
        using NullFuncsType =
            std::integral_constant<const JSFunctionSpec*, nullptr>;
        using ConstructorFuncsType =
            std::integral_constant<const JSFunctionSpec*,
                                   Base::klass.spec->constructorFunctions>;
        using PrototypeFuncsType =
            std::integral_constant<const JSFunctionSpec*,
                                   Base::klass.spec->prototypeFunctions>;
        using NullPropsType =
            std::integral_constant<const JSPropertySpec*, nullptr>;
        using ConstructorPropsType =
            std::integral_constant<const JSPropertySpec*,
                                   Base::klass.spec->constructorProperties>;
        using PrototypePropsType =
            std::integral_constant<const JSPropertySpec*,
                                   Base::klass.spec->prototypeProperties>;
        using NullFinishOpType =
            std::integral_constant<js::FinishClassInitOp, nullptr>;
        using FinishInitType =
            std::integral_constant<js::FinishClassInitOp,
                                   Base::klass.spec->finishInit>;

        // Create the prototype. If no createPrototype function is provided,
        // then the default is to create a plain object as the prototype.
        JS::RootedObject proto(cx);
        if constexpr (!std::is_same_v<CreatePrototypeType, NullOpType>) {
            proto = Base::klass.spec->createPrototype(cx, JSProto_Object);
        } else {
            proto = JS_NewPlainObject(cx);
        }
        if (!proto)
            return nullptr;

        if constexpr (!std::is_same_v<PrototypePropsType, NullPropsType>) {
            if (!JS_DefineProperties(cx, proto,
                                     Base::klass.spec->prototypeProperties))
                return nullptr;
        }
        if constexpr (!std::is_same_v<PrototypeFuncsType, NullFuncsType>) {
            if (!JS_DefineFunctions(cx, proto,
                                    Base::klass.spec->prototypeFunctions))
                return nullptr;
        }

        gjs_set_global_slot(global, Base::PROTOTYPE_SLOT,
                            JS::ObjectValue(*proto));

        // Create the constructor. If no createConstructor function is provided,
        // then the default is to call CWrapper::constructor() which calls
        // Base::constructor_impl().
        JS::RootedObject ctor_obj(cx);
        if constexpr (!(Base::klass.spec->flags &
                        js::ClassSpec::DontDefineConstructor)) {
            if constexpr (!std::is_same_v<CreateConstructorType, NullOpType>) {
                ctor_obj =
                    Base::klass.spec->createConstructor(cx, JSProto_Object);
            } else {
                JSFunction* ctor = JS_NewFunction(
                    cx, &Base::constructor, Base::constructor_nargs,
                    JSFUN_CONSTRUCTOR, Base::klass.name);
                ctor_obj = JS_GetFunctionObject(ctor);
            }
            if (!ctor_obj ||
                !JS_LinkConstructorAndPrototype(cx, ctor_obj, proto))
                return nullptr;
            if constexpr (!std::is_same_v<ConstructorPropsType,
                                          NullPropsType>) {
                if (!JS_DefineProperties(
                        cx, ctor_obj, Base::klass.spec->constructorProperties))
                    return nullptr;
            }
            if constexpr (!std::is_same_v<ConstructorFuncsType,
                                          NullFuncsType>) {
                if (!JS_DefineFunctions(cx, ctor_obj,
                                        Base::klass.spec->constructorFunctions))
                    return nullptr;
            }
        }

        if constexpr (!std::is_same_v<FinishInitType, NullFinishOpType>) {
            if (!Base::klass.spec->finishInit(cx, ctor_obj, proto))
                return nullptr;
        }

        // Put the constructor, if one exists, as a property on the module
        // object. If module is not given, we are defining a global class.
        if (ctor_obj) {
            JS::RootedObject in_obj(cx, module);
            if (!in_obj)
                in_obj = global;
            JS::RootedId class_name(
                cx, gjs_intern_string_to_id(cx, Base::klass.name));
            if (class_name.isVoid() ||
                !JS_DefinePropertyById(cx, in_obj, class_name, ctor_obj,
                                       GJS_MODULE_PROP_FLAGS))
                return nullptr;
        }

        gjs_debug(GJS_DEBUG_CONTEXT, "Initialized class %s prototype %p",
                  Base::klass.name, proto.get());
        return proto;
    }

    /*
     * CWrapper::from_c_ptr():
     *
     * Create a new CWrapper JS object from the given C pointer. The pointer
     * is copied using copy_ptr(), so you must implement that if you use this
     * function.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* from_c_ptr(JSContext* cx, Wrapped* ptr) {
        JS::RootedObject proto(cx, Base::prototype(cx));
        if (!proto)
            return nullptr;

        JS::RootedObject wrapper(
            cx, JS_NewObjectWithGivenProto(cx, &Base::klass, proto));
        if (!wrapper)
            return nullptr;

        CWrapperPointerOps<Base, Wrapped>::init_private(wrapper,
                                                        Base::copy_ptr(ptr));

        debug_lifecycle(ptr, wrapper, "from_c_ptr");

        return wrapper;
    }
};
