/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#ifndef GI_WRAPPERUTILS_H_
#define GI_WRAPPERUTILS_H_

#include <config.h>

#include <stdint.h>

#include <new>  // for operator new
#include <string>
#include <type_traits>

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>  // for JSEXN_TYPEERR
#include <js/GCVector.h>     // for MutableHandleIdVector
#include <js/Id.h>
#include <js/MemoryFunctions.h>
#include <js/Object.h>
#include <js/PropertyAndElement.h>  // for JS_DefineFunctionById
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_GetPrototype
#include <mozilla/Maybe.h>

#include "gi/arg-inl.h"
#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/profiler-private.h"
#include "util/log.h"

struct JSFunctionSpec;
struct JSPropertySpec;
class JSTracer;

GJS_JSAPI_RETURN_CONVENTION
bool gjs_wrapper_to_string_func(JSContext* cx, JSObject* this_obj,
                                const char* objtype,
                                mozilla::Maybe<const GI::BaseInfo> info,
                                GType gtype, const void* native_address,
                                JS::MutableHandleValue ret);

GJS_JSAPI_RETURN_CONVENTION
static inline bool gjs_wrapper_to_string_func(JSContext* cx, JSObject* this_obj,
                                              const char* objtype,
                                              const GI::BaseInfo info,
                                              GType gtype,
                                              const void* native_address,
                                              JS::MutableHandleValue ret) {
    return gjs_wrapper_to_string_func(
        cx, this_obj, objtype, mozilla::Some(info), gtype, native_address, ret);
}

bool gjs_wrapper_throw_nonexistent_field(JSContext* cx, GType gtype,
                                         const char* field_name);

bool gjs_wrapper_throw_readonly_field(JSContext* cx, GType gtype,
                                      const char* field_name);

namespace MemoryUse {
constexpr JS::MemoryUse GObjectInstanceStruct = JS::MemoryUse::Embedding1;
}

struct GjsTypecheckNoThrow {};

// Some types of introspected wrapper permit creating a new type from JS (e.g.,
// objects, interfaces.) These JS-created types do not have introspection info
// and so their GIWrapperPrototype::info() methods return Maybe<const FooInfo>.
// Others do not permit creating a new type from JS (e.g., enums, boxeds.) These
// have GIWrapperPrototype::info() methods that return const FooInfo directly.
// Sometimes we need to have different code for the two cases.
template <typename>
struct is_maybe : std::false_type {};
template <typename T>
struct is_maybe<mozilla::Maybe<T>> : std::true_type {};

/*
 * gjs_define_static_methods:
 *
 * Defines all static methods from @info on @constructor. Also includes class
 * methods for GI::ObjectInfo, and interface methods for GI::InterfaceInfo.
 */
template <GI::InfoTag TAG>
GJS_JSAPI_RETURN_CONVENTION bool gjs_define_static_methods(
    JSContext*, JS::HandleObject constructor, GType,
    const GI::UnownedInfo<TAG>);

template <GI::InfoTag TAG>
GJS_JSAPI_RETURN_CONVENTION inline bool gjs_define_static_methods(
    JSContext* cx, JS::HandleObject constructor, GType gtype,
    const GI::OwnedInfo<TAG>& info) {
    return gjs_define_static_methods(cx, constructor, gtype,
                                     GI::UnownedInfo<TAG>{info});
}

/*
 * GIWrapperBase:
 *
 * In most different kinds of C pointer that we expose to JS through GObject
 * Introspection (boxed, fundamental, gerror, interface, object, union), we want
 * to have different private structures for the prototype JS object and the JS
 * objects representing instances. Both should inherit from a base structure for
 * their common functionality.
 *
 * This is mainly for memory reasons. We need to keep track of the GIBaseInfo*
 * and GType for each dynamically created class, but we don't need to duplicate
 * that information (16 bytes on x64 systems) for every instance. In some cases
 * there can also be other information that's only used on the prototype.
 *
 * So, to conserve memory, we split the private structures in FooInstance and
 * FooPrototype, which both inherit from FooBase. All the repeated code in these
 * structures lives in GIWrapperBase, GIWrapperPrototype, and GIWrapperInstance.
 *
 * The m_proto member needs a bit of explanation, as this is used to implement
 * an unusual form of polymorphism. Sadly, we cannot have virtual methods in
 * FooBase, because SpiderMonkey can be compiled with or without RTTI, so we
 * cannot count on being able to cast FooBase to FooInstance or FooPrototype
 * with dynamic_cast<>, and the vtable would take up just as much space anyway.
 * Instead, we use the CRTP technique, and distinguish between FooInstance and
 * FooPrototype using the m_proto member, which will be null for FooPrototype.
 * Instead of casting, we have the to_prototype() and to_instance() methods
 * which will give you a pointer if the FooBase is of the correct type (and
 * assert if not.)
 *
 * The CRTP requires inheriting classes to declare themselves friends of the
 * parent class, so that the parent class can call their private methods.
 *
 * For more information about the CRTP, the Wikipedia article is informative:
 * https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 */
template <class Base, class Prototype, class Instance>
class GIWrapperBase : public CWrapperPointerOps<Base> {
 protected:
    // nullptr if this Base is a Prototype; points to the corresponding
    // Prototype if this Base is an Instance.
    Prototype* m_proto;

    explicit GIWrapperBase(Prototype* proto = nullptr) : m_proto(proto) {}

    // These three can be overridden in subclasses. See define_jsclass().
    static constexpr JSPropertySpec* proto_properties = nullptr;
    static constexpr JSPropertySpec* static_properties = nullptr;
    static constexpr JSFunctionSpec* proto_methods = nullptr;
    static constexpr JSFunctionSpec* static_methods = nullptr;

 public:
    // Methods implementing our CRTP polymorphism scheme follow below. We don't
    // use standard C++ polymorphism because that would occupy another 8 bytes
    // for a vtable.

    /*
     * GIWrapperBase::is_prototype:
     *
     * Returns whether this Base is actually a Prototype (true) or an Instance
     * (false).
     */
    [[nodiscard]] bool is_prototype() const { return !m_proto; }

    /*
     * GIWrapperBase::to_prototype:
     * GIWrapperBase::to_instance:
     *
     * These methods assert that this Base is of the correct subclass. If you
     * don't want to assert, then either check beforehand with is_prototype(),
     * or use get_prototype().
     */
    [[nodiscard]] Prototype* to_prototype() {
        g_assert(is_prototype());
        return reinterpret_cast<Prototype*>(this);
    }
    [[nodiscard]] const Prototype* to_prototype() const {
        g_assert(is_prototype());
        return reinterpret_cast<const Prototype*>(this);
    }
    [[nodiscard]] Instance* to_instance() {
        g_assert(!is_prototype());
        return reinterpret_cast<Instance*>(this);
    }
    [[nodiscard]] const Instance* to_instance() const {
        g_assert(!is_prototype());
        return reinterpret_cast<const Instance*>(this);
    }

    /*
     * GIWrapperBase::get_prototype:
     *
     * get_prototype() doesn't assert. If you call it on a Prototype, it returns
     * you the same object cast to the correct type; if you call it on an
     * Instance, it returns you the Prototype belonging to the corresponding JS
     * prototype.
     */
    [[nodiscard]] [[gnu::const]] Prototype* get_prototype() {
        return is_prototype() ? to_prototype() : m_proto;
    }
    [[nodiscard]] const Prototype* get_prototype() const {
        return is_prototype() ? to_prototype() : m_proto;
    }

    // Accessors for Prototype members follow below. Both Instance and Prototype
    // should be able to access the GIFooInfo and the GType, but for space
    // reasons we store them only on Prototype.

    [[nodiscard]] auto info() const { return get_prototype()->info(); }
    [[nodiscard]] GType gtype() const { return get_prototype()->gtype(); }

    // The next three methods are operations derived from the GIFooInfo.

    [[nodiscard]] const char* type_name() const { return g_type_name(gtype()); }
    [[nodiscard]] const char* ns() const {
        if constexpr (Prototype::may_not_have_info) {
            const auto i = info();
            return i ? i->ns() : "";
        } else {
            return info().ns();
        }
    }
    [[nodiscard]] const char* name() const {
        if constexpr (Prototype::may_not_have_info) {
            const auto i = info();
            return i ? i->name() : type_name();
        } else {
            return info().name();
        }
    }

    [[nodiscard]] std::string format_name() const {
        std::string retval = ns();
        if (!retval.empty())
            retval += '.';
        retval += name();
        return retval;
    }

 private:
    // Accessor for Instance member. Used only in debug methods and toString().
    [[nodiscard]] const void* ptr_addr() const {
        return is_prototype() ? nullptr : to_instance()->ptr();
    }

    // Debug methods

 protected:
    void debug_lifecycle(const char* message GJS_USED_VERBOSE_LIFECYCLE) const {
        gjs_debug_lifecycle(Base::DEBUG_TOPIC,
                            "[%p: %s pointer %p - %s (%s)] %s", this,
                            Base::DEBUG_TAG, ptr_addr(), format_name().c_str(),
                            type_name(), message);
    }
    void debug_lifecycle(const void* obj GJS_USED_VERBOSE_LIFECYCLE,
                         const char* message GJS_USED_VERBOSE_LIFECYCLE) const {
        gjs_debug_lifecycle(Base::DEBUG_TOPIC,
                            "[%p: %s pointer %p - JS wrapper %p - %s (%s)] %s",
                            this, Base::DEBUG_TAG, ptr_addr(), obj,
                            format_name().c_str(), type_name(), message);
    }
    void debug_jsprop(const char* message GJS_USED_VERBOSE_PROPS,
                      const char* id GJS_USED_VERBOSE_PROPS,
                      const void* obj GJS_USED_VERBOSE_PROPS) const {
        gjs_debug_jsprop(
            Base::DEBUG_TOPIC,
            "[%p: %s pointer %p - JS wrapper %p - %s (%s)] %s '%s'", this,
            Base::DEBUG_TAG, ptr_addr(), obj, format_name().c_str(),
            type_name(), message, id);
    }
    void debug_jsprop(const char* message, jsid id, const void* obj) const {
        if constexpr (GJS_VERBOSE_ENABLE_PROPS)
            debug_jsprop(message, gjs_debug_id(id).c_str(), obj);
    }
    void debug_jsprop(const char* message, JSString* id,
                      const void* obj) const {
        if constexpr (GJS_VERBOSE_ENABLE_PROPS)
            debug_jsprop(message, gjs_debug_string(id).c_str(), obj);
    }
    static void debug_jsprop_static(const char* message GJS_USED_VERBOSE_PROPS,
                                    jsid id GJS_USED_VERBOSE_PROPS,
                                    const void* obj GJS_USED_VERBOSE_PROPS) {
        gjs_debug_jsprop(Base::DEBUG_TOPIC,
                         "[%s JS wrapper %p] %s '%s', no instance associated",
                         Base::DEBUG_TAG, obj, message,
                         gjs_debug_id(id).c_str());
    }

    // JS class operations, used only in the JSClassOps struct

    /*
     * GIWrapperBase::new_enumerate:
     *
     * Include this in the Base::klass vtable if the class should support
     * lazy enumeration (listing all of the lazy properties that can be defined
     * in resolve().) If it is included, then there must be a corresponding
     * Prototype::new_enumerate_impl() method.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool new_enumerate(JSContext* cx, JS::HandleObject obj,
                              JS::MutableHandleIdVector properties,
                              bool only_enumerable) {
        Base* priv = Base::for_js(cx, obj);

        priv->debug_jsprop("Enumerate hook", "(all)", obj);

        if (!priv->is_prototype()) {
            // Instances don't have any methods or properties.
            // Spidermonkey will call new_enumerate on the prototype next.
            return true;
        }

        return priv->to_prototype()->new_enumerate_impl(cx, obj, properties,
                                                        only_enumerable);
    }

 private:
    /*
     * GIWrapperBase::id_is_never_lazy:
     *
     * Returns true if @id should never be treated as a lazy property. The
     * JSResolveOp for an instance is called for every property not defined,
     * even if it's one of the functions or properties we're adding to the
     * prototype manually, such as toString().
     *
     * Override this and chain up if you have Base::resolve in your JSClassOps
     * vtable, and have overridden Base::proto_properties or
     * Base::proto_methods. You should add any identifiers in the override that
     * you have added to the prototype object.
     */
    [[nodiscard]] static bool id_is_never_lazy(jsid id, const GjsAtoms& atoms) {
        // toString() is always defined somewhere on the prototype chain, so it
        // is never a lazy property.
        return id == atoms.to_string();
    }

 protected:
    /**
     * GIWrapperBase::resolve_prototype:
     */
    [[nodiscard]] static Prototype* resolve_prototype(JSContext* cx,
                                                      JS::HandleObject proto) {
        if (JS::GetClass(proto) == &Base::klass)
            return Prototype::for_js(cx, proto);

        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

        bool has_property = false;
        if (!JS_HasOwnPropertyById(cx, proto, atoms.gobject_prototype(),
                                   &has_property))
            return nullptr;

        if (!has_property) {
            gjs_throw(cx, "Tried to construct an object without a GType");
            return nullptr;
        }

        JS::RootedValue gobject_proto(cx);
        if (!JS_GetPropertyById(cx, proto, atoms.gobject_prototype(),
                                &gobject_proto))
            return nullptr;

        if (!gobject_proto.isObject()) {
            gjs_throw(cx, "Tried to construct an object without a GType");
            return nullptr;
        }

        JS::RootedObject obj(cx, &gobject_proto.toObject());
        // gobject_prototype is an internal symbol so we can assert that it is
        // only assigned to objects with &Base::klass definitions
        g_assert(JS::GetClass(obj) == &Base::klass);

        return Prototype::for_js(cx, obj);
    }

    /*
     * GIWrapperBase::resolve:
     *
     * Include this in the Base::klass vtable if the class should support lazy
     * properties. If it is included, then there must be a corresponding
     * Prototype::resolve_impl() method.
     *
     * The *resolved out parameter, on success, should be false to indicate that
     * id was not resolved; and true if id was resolved.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                        bool* resolved) {
        Base* priv = Base::for_js(cx, obj);

        if (!priv) {
            // This catches a case in Object where the private struct isn't set
            // until the initializer is called, so just defer to prototype
            // chains in this case.
            //
            // This isn't too bad: either you get undefined if the field doesn't
            // exist on any of the prototype chains, or whatever code will run
            // afterwards will fail because of the "!priv" check there.
            debug_jsprop_static("Resolve hook", id, obj);
            *resolved = false;
            return true;
        }

        priv->debug_jsprop("Resolve hook", id, obj);

        if (!priv->is_prototype()) {
            // We are an instance, not a prototype, so look for per-instance
            // props that we want to define on the JSObject. Generally we do not
            // want to cache these in JS, we want to always pull them from the C
            // object, or JS would not see any changes made from C. So we use
            // the property accessors, not this resolve hook.
            *resolved = false;
            return true;
        }

        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (id_is_never_lazy(id, atoms)) {
            *resolved = false;
            return true;
        }

        return priv->to_prototype()->resolve_impl(cx, obj, id, resolved);
    }

    /*
     * GIWrapperBase::finalize:
     *
     * This should always be included in the Base::klass vtable. The destructors
     * of Prototype and Instance will be called in the finalize hook. It is not
     * necessary to include a finalize_impl() function in Prototype or Instance.
     * Any needed finalization should be done in ~Prototype() and ~Instance().
     */
    static void finalize(JS::GCContext* gcx, JSObject* obj) {
        Base* priv = Base::for_js_nocheck(obj);
        if (!priv)
            return;  // construction didn't finish

        // Call only GIWrapperBase's original method here, not any overrides;
        // e.g., we don't want to deal with a read barrier in ObjectInstance.
        static_cast<GIWrapperBase*>(priv)->debug_lifecycle(obj, "Finalize");

        if (priv->is_prototype())
            priv->to_prototype()->finalize_impl(gcx, obj);
        else
            priv->to_instance()->finalize_impl(gcx, obj);

        Base::unset_private(obj);
    }

    /*
     * GIWrapperBase::trace:
     *
     * This should be included in the Base::klass vtable if any of the Base,
     * Prototype or Instance structures contain any members that the JS garbage
     * collector must trace. Each struct containing such members must override
     * GIWrapperBase::trace_impl(), GIWrapperPrototype::trace_impl(), and/or
     * GIWrapperInstance::trace_impl() in order to perform the trace.
     */
    static void trace(JSTracer* trc, JSObject* obj) {
        Base* priv = Base::for_js_nocheck(obj);
        if (!priv)
            return;

        // Don't log in trace(). That would overrun even the most verbose logs.

        if (priv->is_prototype())
            priv->to_prototype()->trace_impl(trc);
        else
            priv->to_instance()->trace_impl(trc);

        priv->trace_impl(trc);
    }

    /*
     * GIWrapperBase::trace_impl:
     * Override if necessary. See trace().
     */
    void trace_impl(JSTracer*) {}

    // JSNative methods

    /*
     * GIWrapperBase::constructor:
     *
     * C++ implementation of the JS constructor passed to JS_InitClass(). Only
     * called on instances, never on prototypes. This method contains the
     * functionality common to all GI wrapper classes. There must be a
     * corresponding Instance::constructor_impl method containing the rest of
     * the functionality.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

        if (!args.isConstructing()) {
            gjs_throw_constructor_error(cx);
            return false;
        }
        JS::RootedObject obj(
            cx, JS_NewObjectForConstructor(cx, &Base::klass, args));
        if (!obj)
            return false;

        JS::RootedObject proto(cx);
        if (!JS_GetPrototype(cx, obj, &proto))
            return false;

        Prototype* prototype = resolve_prototype(cx, proto);
        if (!prototype)
            return false;

        args.rval().setUndefined();

        Instance* priv = Instance::new_for_js_object(prototype, obj);

        {
            std::string full_name{
                GJS_PROFILER_DYNAMIC_STRING(cx, priv->format_name())};
            AutoProfilerLabel label{cx, "constructor", full_name};

            if (!priv->constructor_impl(cx, obj, args))
                return false;
        }

        static_cast<GIWrapperBase*>(priv)->debug_lifecycle(obj,
                                                           "JSObject created");
        gjs_debug_lifecycle(Base::DEBUG_TOPIC, "m_proto is %p",
                            priv->get_prototype());

        // We may need to return a value different from obj (for example because
        // we delegate to another constructor)
        if (args.rval().isUndefined())
            args.rval().setObject(*obj);
        return true;
    }

    /*
     * GIWrapperBase::to_string:
     *
     * JSNative method connected to the toString() method in JS.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp) {
        GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, Base, priv);
        return gjs_wrapper_to_string_func(cx, obj, Base::DEBUG_TAG,
                                          priv->info(), priv->gtype(),
                                          priv->ptr_addr(), args.rval());
    }

    // Helper methods

 public:
    /*
     * GIWrapperBase::check_is_instance:
     * @for_what: string used in the exception message if an exception is thrown
     *
     * Used in JSNative methods to ensure the passed-in JS object is an instance
     * and not the prototype. Throws a JS exception if the prototype is passed
     * in.
     */
    GJS_JSAPI_RETURN_CONVENTION
    bool check_is_instance(JSContext* cx, const char* for_what) const {
        if (!is_prototype())
            return true;
        gjs_throw(cx, "Can't %s on %s.prototype; only on instances", for_what,
                  format_name().c_str());
        return false;
    }

    /*
     * GIWrapperBase::to_c_ptr:
     *
     * Returns the underlying C pointer of the wrapped object, or throws a JS
     * exception if that is not possible (for example, the passed-in JS object
     * is the prototype.)
     *
     * Includes a JS typecheck (but without any extra typecheck of the GType or
     * introspection info that you would get from GIWrapperBase::typecheck(), so
     * if you want that you still have to do the typecheck before calling this
     * method.)
     */
    template <typename T = void>
    GJS_JSAPI_RETURN_CONVENTION static T* to_c_ptr(JSContext* cx,
                                                   JS::HandleObject obj) {
        Base* priv;
        if (!Base::for_js_typecheck(cx, obj, &priv) ||
            !priv->check_is_instance(cx, "get a C pointer"))
            return nullptr;

        return static_cast<T*>(priv->to_instance()->ptr());
    }

    /*
     * GIWrapperBase::transfer_to_gi_argument:
     * @arg: #GIArgument to fill with the value from @obj
     * @transfer_direction: Either %GI_DIRECTION_IN or %GI_DIRECTION_OUT
     * @transfer_ownership: #GITransfer value specifying whether @arg should
     *   copy or acquire a reference to the value or not
     * @expected_gtype: #GType to perform a typecheck with
     * @expected_info: Introspection info to perform a typecheck with
     *
     * Prepares @arg for passing the value from @obj into C code. It will get a
     * C pointer from @obj and assign it to @arg's pointer field, taking a
     * reference with GIWrapperInstance::copy_ptr() if @transfer_direction and
     * @transfer_ownership indicate that it should.
     *
     * Includes a typecheck using GIWrapperBase::typecheck(), to which
     * @expected_gtype and @expected_info are passed.
     *
     * If returning false, then @arg's pointer field is null.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                        GIArgument* arg,
                                        GIDirection transfer_direction,
                                        GITransfer transfer_ownership,
                                        GType expected_gtype) {
        g_assert(transfer_direction != GI_DIRECTION_INOUT &&
                 "transfer_to_gi_argument() must choose between in or out");

        if (expected_gtype != G_TYPE_NONE &&
            !Base::typecheck(cx, obj, expected_gtype)) {
            gjs_arg_unset(arg);
            return false;
        }

        gjs_arg_set(arg, Base::to_c_ptr(cx, obj));
        if (!gjs_arg_get<void*>(arg))
            return false;

        if ((transfer_direction == GI_DIRECTION_IN &&
             transfer_ownership != GI_TRANSFER_NOTHING) ||
            (transfer_direction == GI_DIRECTION_OUT &&
             transfer_ownership == GI_TRANSFER_EVERYTHING)) {
            gjs_arg_set(arg, Instance::copy_ptr(cx, expected_gtype,
                                                gjs_arg_get<void*>(arg)));
            if (!gjs_arg_get<void*>(arg))
                return false;
        }

        return true;
    }

    // Public typecheck API

    /*
     * GIWrapperBase::typecheck:
     * @expected_info: (nullable): GI info to check
     * @expected_type: (nullable): GType to check
     *
     * Checks not only that the JS object is of the correct JSClass (like
     * CWrapperPointerOps::typecheck() does); but also that the object is an
     * instance, not the prototype; and that the instance's wrapped pointer is
     * of the correct GType or GI info.
     *
     * The overload with a GjsTypecheckNoThrow parameter will not throw a JS
     * exception if the prototype is passed in or the typecheck fails.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool typecheck(JSContext* cx, JS::HandleObject object,
                          const GI::BaseInfo expected_info) {
        Base* priv;
        if (!Base::for_js_typecheck(cx, object, &priv) ||
            !priv->check_is_instance(cx, "convert to pointer"))
            return false;

        if (priv->to_instance()->typecheck_impl(expected_info))
            return true;

        gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                         "Object is of type %s - cannot convert to %s.%s",
                         priv->format_name().c_str(),
                         expected_info.ns(), expected_info.name());
        return false;
    }
    GJS_JSAPI_RETURN_CONVENTION
    static bool typecheck(JSContext* cx, JS::HandleObject object,
                          GType expected_gtype) {
        Base* priv;
        if (!Base::for_js_typecheck(cx, object, &priv) ||
            !priv->check_is_instance(cx, "convert to pointer"))
            return false;

        if (priv->to_instance()->typecheck_impl(expected_gtype))
            return true;

        gjs_throw_custom(cx, JSEXN_TYPEERR, nullptr,
                         "Object is of type %s - cannot convert to %s",
                         priv->format_name().c_str(),
                         g_type_name(expected_gtype));
        return false;
    }
    template <typename T>
    [[nodiscard]]
    static bool typecheck(JSContext* cx, JS::HandleObject object,
                          T expected, GjsTypecheckNoThrow) {
        Base* priv = Base::for_js(cx, object);
        if (!priv || priv->is_prototype())
            return false;

        return priv->to_instance()->typecheck_impl(expected);
    }

    // Deleting these constructors and assignment operators will also delete
    // them from derived classes.
    GIWrapperBase(const GIWrapperBase& other) = delete;
    GIWrapperBase(GIWrapperBase&& other) = delete;
    GIWrapperBase& operator=(const GIWrapperBase& other) = delete;
    GIWrapperBase& operator=(GIWrapperBase&& other) = delete;
};

/*
 * GIWrapperPrototype:
 *
 * The specialization of GIWrapperBase which becomes the private data of JS
 * prototype objects. For example, it is the parent class of BoxedPrototype.
 *
 * Classes inheriting from GIWrapperPrototype must declare "friend class
 * GIWrapperBase" as well as the normal CRTP requirement of "friend class
 * GIWrapperPrototype", because of the unusual polymorphism scheme, in order for
 * Base to call methods such as trace_impl().
 */
template <class Base, class Prototype, class Instance, typename OwnedInfo,
          typename UnownedInfo>
class GIWrapperPrototype : public Base {
    using GjsAutoPrototype =
        Gjs::AutoPointer<Prototype, void, g_atomic_rc_box_release>;

 protected:
    // m_info may be null in the case of JS-defined types, or internal types
    // not exposed through introspection, such as GLocalFile. Not all subclasses
    // of GIWrapperPrototype support this. Object and Interface support it in
    // any case.
    OwnedInfo m_info;
    GType m_gtype;

    explicit GIWrapperPrototype(const UnownedInfo info, GType gtype)
        : Base(), m_info(info), m_gtype(gtype) {
        Base::debug_lifecycle("Prototype constructor");
    }

    /*
     * GIWrapperPrototype::init:
     *
     * Performs any initialization that cannot be done in the constructor of
     * GIWrapperPrototype, either because it can fail, or because it can cause a
     * garbage collection.
     *
     * This default implementation does nothing. Override in a subclass if
     * necessary.
     */
    GJS_JSAPI_RETURN_CONVENTION
    bool init(JSContext*) { return true; }

    // The following four methods are private because they are used only in
    // create_class().

 private:
    /*
     * GIWrapperPrototype::parent_proto:
     *
     * Returns in @proto the parent class's prototype object, or nullptr if
     * there is none.
     *
     * This default implementation is for GObject introspection types that can't
     * inherit in JS, like Boxed and Union. Override this if the type can
     * inherit in JS.
     */
    GJS_JSAPI_RETURN_CONVENTION
    bool get_parent_proto(JSContext*, JS::MutableHandleObject proto) const {
        proto.set(nullptr);
        return true;
    }

    /*
     * GIWrapperPrototype::constructor_nargs:
     *
     * Override this if the type's constructor takes other than 1 argument.
     */
    [[nodiscard]] unsigned constructor_nargs() const { return 1; }

    /*
     * GIWrapperPrototype::define_jsclass:
     * @in_object: JSObject on which to define the class constructor as a
     *   property
     * @parent_proto: (nullable): prototype of the prototype
     * @constructor: return location for the constructor function object
     * @prototype: return location for the prototype object
     *
     * Defines a JS class with constructor and prototype, and optionally defines
     * properties and methods on the prototype object, and methods on the
     * constructor object.
     *
     * By default no properties or methods are defined, but derived classes can
     * override the GIWrapperBase::proto_properties,
     * GIWrapperBase::proto_methods, and GIWrapperBase::static_methods members.
     * Static properties would also be possible but are not used anywhere in GJS
     * so are not implemented yet.
     *
     * Note: no prototype methods are defined if @parent_proto is null.
     *
     * Here is a refresher comment on the difference between __proto__ and
     * prototype that has been in the GJS codebase since forever:
     *
     * https://web.archive.org/web/20100716231157/http://egachine.berlios.de/embedding-sm-best-practice/apa.html
     * https://www.sitepoint.com/javascript-inheritance/
     * http://www.cs.rit.edu/~atk/JavaScript/manuals/jsobj/
     *
     * What we want is: repoobj.Gtk.Window is constructor for a GtkWindow
     * wrapper JSObject (gjs_define_object_class() is supposed to define Window
     * in Gtk.)
     *
     * Window.prototype contains the methods on Window, e.g. set_default_size()
     * mywindow.__proto__ is Window.prototype
     * mywindow.__proto__.__proto__ is Bin.prototype
     * mywindow.__proto__.__proto__.__proto__ is Container.prototype
     *
     * Because Window.prototype is an instance of Window in a sense,
     * Window.prototype.__proto__ is Window.prototype, just as
     * mywindow.__proto__ is Window.prototype
     *
     * If we do "mywindow = new Window()" then we should get:
     *     mywindow.__proto__ == Window.prototype
     * which means "mywindow instanceof Window" is true.
     *
     * Remember "Window.prototype" is "the __proto__ of stuff constructed with
     * new Window()"
     *
     * __proto__ is used to search for properties if you do "this.foo", while
     * .prototype is only relevant for constructors and is used to set __proto__
     * on new'd objects. So .prototype only makes sense on constructors.
     *
     * JS_SetPrototype() and JS_GetPrototype() are for __proto__. To set/get
     * .prototype, just use the normal property accessors, or JS_InitClass()
     * sets it up automatically.
     */
    GJS_JSAPI_RETURN_CONVENTION
    bool define_jsclass(JSContext* cx, JS::HandleObject in_object,
                        JS::HandleObject parent_proto,
                        JS::MutableHandleObject constructor,
                        JS::MutableHandleObject prototype) {
        // The GI namespace is only used to set the JSClass->name field (exposed
        // by Object.prototype.toString, for example). We can safely set
        // "unknown" if this is a custom or internal JS class with no GI
        // namespace, as in that case the name is already globally unique (it's
        // a GType name).
        const char* gi_namespace;
        if constexpr (may_not_have_info)
            gi_namespace = Base::info() ? Base::ns() : "unknown";
        else
            gi_namespace = Base::ns();

        unsigned nargs = static_cast<Prototype*>(this)->constructor_nargs();

        if (!gjs_init_class_dynamic(
                cx, in_object, parent_proto, gi_namespace, Base::name(),
                &Base::klass, &Base::constructor, nargs, Base::proto_properties,
                parent_proto ? nullptr : Base::proto_methods,
                Base::static_properties, Base::static_methods, prototype,
                constructor))
            return false;

        gjs_debug(Base::DEBUG_TOPIC,
                  "Defined class for %s (%s), prototype %p, "
                  "JSClass %p, in object %p",
                  Base::name(), Base::type_name(), prototype.get(),
                  JS::GetClass(prototype), in_object.get());

        return true;
    }

    /*
     * GIWrapperPrototype::define_static_methods:
     *
     * Defines all introspectable static methods on @constructor, including
     * class methods for objects, and interface methods for interfaces. See
     * gjs_define_static_methods() for details.
     */
    GJS_JSAPI_RETURN_CONVENTION
    bool define_static_methods(JSContext* cx, JS::HandleObject constructor) {
        if constexpr (may_not_have_info) {
            if (!info())
                return true;  // no introspection means no methods to define
            return gjs_define_static_methods(cx, constructor, m_gtype,
                                             info().value());
        } else {
            return gjs_define_static_methods(cx, constructor, m_gtype, m_info);
        }
    }

    GJS_JSAPI_RETURN_CONVENTION
    static Prototype* create_prototype(const UnownedInfo info, GType gtype) {
        g_assert(gtype != G_TYPE_INVALID);

        // We have to keep the Prototype in an arcbox because some of its
        // members are needed in some Instance destructors, e.g. m_gtype to
        // figure out how to free the Instance's m_ptr, and m_info to figure out
        // how many bytes to free if it is allocated directly. Storing a
        // refcount on the prototype is cheaper than storing pointers to m_info
        // and m_gtype on each instance.
        Prototype* priv = g_atomic_rc_box_new0(Prototype);
        new (priv) Prototype(info, gtype);

        return priv;
    }

 public:
    /**
     * GIWrapperPrototype::create_class:
     * @in_object: JSObject on which to define the class constructor as a
     *   property
     * @info: (nullable): Introspection info for the class, or null if the class
     *   has been defined in JS
     * @gtype: GType for the class
     * @constructor: return location for the constructor function object
     * @prototype: return location for the prototype object
     *
     * Creates a JS class that wraps a GI pointer, by defining its constructor
     * function and prototype object. The prototype object is given an instance
     * of GIWrapperPrototype as its private data, which is also returned.
     * Basically treat this method as the public constructor.
     *
     * Also defines all the requested methods and properties on the prototype
     * and constructor objects (see define_jsclass()), as well as a `$gtype`
     * property and a toString() method.
     *
     * This method can be overridden and chained up to if the derived class
     * needs to define more properties on the constructor or prototype objects,
     * e.g. eager GI properties.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static Prototype* create_class(JSContext* cx, JS::HandleObject in_object,
                                   const UnownedInfo info, GType gtype,
                                   JS::MutableHandleObject constructor,
                                   JS::MutableHandleObject prototype) {
        g_assert(in_object);

        GjsAutoPrototype priv = create_prototype(info, gtype);
        if (!priv->init(cx))
            return nullptr;

        JS::RootedObject parent_proto(cx);
        if (!priv->get_parent_proto(cx, &parent_proto) ||
            !priv->define_jsclass(cx, in_object, parent_proto, constructor,
                                  prototype))
            return nullptr;

        // Init the private variable of @private before we do anything else. If
        // a garbage collection or error happens subsequently, then this object
        // might be traced and we would end up dereferencing a null pointer.
        Prototype* proto = priv.release();
        Prototype::init_private(prototype, proto);

        if (!gjs_wrapper_define_gtype_prop(cx, constructor, gtype))
            return nullptr;

        // Every class has a toString() with C++ implementation, so define that
        // without requiring it to be listed in Base::proto_methods
        if (!parent_proto) {
            const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
            if (!JS_DefineFunctionById(cx, prototype, atoms.to_string(),
                                       &Base::to_string, 0,
                                       GJS_MODULE_PROP_FLAGS))
                return nullptr;
        }

        if (!proto->define_static_methods(cx, constructor))
            return nullptr;

        return proto;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static Prototype* wrap_class(JSContext* cx, JS::HandleObject in_object,
                                 const UnownedInfo info, GType gtype,
                                 JS::HandleObject constructor,
                                 JS::MutableHandleObject prototype) {
        g_assert(in_object);

        GjsAutoPrototype priv = create_prototype(info, gtype);
        if (!priv->init(cx))
            return nullptr;

        JS::RootedObject parent_proto(cx);
        if (!priv->get_parent_proto(cx, &parent_proto))
            return nullptr;

        if (parent_proto) {
            prototype.set(
                JS_NewObjectWithGivenProto(cx, &Base::klass, parent_proto));
        } else {
            prototype.set(JS_NewObject(cx, &Base::klass));
        }

        if (!prototype)
            return nullptr;

        Prototype* proto = priv.release();
        Prototype::init_private(prototype, proto);

        if (!proto->define_static_methods(cx, constructor))
            return nullptr;

        Gjs::AutoChar class_name{g_strdup_printf("%s", proto->name())};
        if (!JS_DefineProperty(cx, in_object, class_name, constructor,
                               GJS_MODULE_PROP_FLAGS))
            return nullptr;

        return proto;
    }

    // Methods to get an existing Prototype

    /*
     * GIWrapperPrototype::for_js:
     *
     * Like Base::for_js(), but asserts that the returned private struct is a
     * Prototype and not an Instance.
     */
    [[nodiscard]] static Prototype* for_js(JSContext* cx,
                                           JS::HandleObject wrapper) {
        return Base::for_js(cx, wrapper)->to_prototype();
    }

    /*
     * GIWrapperPrototype::for_js_prototype:
     *
     * Gets the Prototype private data from to @wrapper.prototype. Cannot return
     * null, and asserts so.
     */
    [[nodiscard]] static Prototype* for_js_prototype(JSContext* cx,
                                                     JS::HandleObject wrapper) {
        JS::RootedObject proto(cx);
        JS_GetPrototype(cx, wrapper, &proto);
        Base* retval = Base::for_js(cx, proto);
        g_assert(retval);
        return retval->to_prototype();
    }

    // Accessors

    static constexpr bool may_not_have_info = is_maybe<UnownedInfo>::value;
    [[nodiscard]] const UnownedInfo info() const { return m_info; }
    [[nodiscard]] GType gtype() const { return m_gtype; }

    // Helper methods

 private:
    static void destroy_notify(void* ptr) {
        static_cast<Prototype*>(ptr)->~Prototype();
    }

 public:
    Prototype* acquire(void) {
        g_atomic_rc_box_acquire(this);
        return static_cast<Prototype*>(this);
    }

    void release(void) { g_atomic_rc_box_release_full(this, &destroy_notify); }

    // JSClass operations

 protected:
    void finalize_impl(JS::GCContext*, JSObject*) { release(); }

    // Override if necessary
    void trace_impl(JSTracer*) {}
};

using GIWrappedUnowned = void;
namespace Gjs {
template <>
struct SmartPointer<GIWrappedUnowned>
    : AutoPointer<GIWrappedUnowned, void, nullptr> {
    using AutoPointer::AutoPointer;
};
}  // namespace Gjs

/*
 * GIWrapperInstance:
 *
 * The specialization of GIWrapperBase which becomes the private data of JS
 * instance objects. For example, it is the parent class of BoxedInstance.
 *
 * Classes inheriting from GIWrapperInstance must declare "friend class
 * GIWrapperBase" as well as the normal CRTP requirement of "friend class
 * GIWrapperInstance", because of the unusual polymorphism scheme, in order for
 * Base to call methods such as trace_impl().
 */
template <class Base, class Prototype, class Instance,
          typename Wrapped = GIWrappedUnowned>
class GIWrapperInstance : public Base {
 protected:
    Gjs::SmartPointer<Wrapped> m_ptr;

    explicit GIWrapperInstance(Prototype* prototype, JS::HandleObject obj)
        : Base(prototype), m_ptr(nullptr) {
        Base::m_proto->acquire();
        Base::GIWrapperBase::debug_lifecycle(obj, "Instance constructor");
    }

    ~GIWrapperInstance(void) { Base::m_proto->release(); }

 public:
    /*
     * GIWrapperInstance::new_for_js_object:
     *
     * Creates a GIWrapperInstance and associates it with @obj as its private
     * data. This is called by the JS constructor.
     */
    [[nodiscard]] static Instance* new_for_js_object(JSContext* cx,
                                                     JS::HandleObject obj) {
        Prototype* prototype = Prototype::for_js_prototype(cx, obj);
        auto* priv = new Instance(prototype, obj);

        // Init the private variable before we do anything else. If a garbage
        // collection happens when calling the constructor, then this object
        // might be traced and we would end up dereferencing a null pointer.
        Instance::init_private(obj, priv);

        return priv;
    }

    [[nodiscard]] static Instance* new_for_js_object(Prototype* prototype,
                                                     JS::HandleObject obj) {
        auto* priv = new Instance(prototype, obj);

        Instance::init_private(obj, priv);

        return priv;
    }

    // Method to get an existing Instance

    /*
     * GIWrapperInstance::for_js:
     *
     * Like Base::for_js(), but asserts that the returned private struct is an
     * Instance and not a Prototype.
     */
    [[nodiscard]] static Instance* for_js(JSContext* cx,
                                          JS::HandleObject wrapper) {
        return Base::for_js(cx, wrapper)->to_instance();
    }

    // Accessors

    [[nodiscard]] Wrapped* ptr() const { return m_ptr; }
    /*
     * GIWrapperInstance::raw_ptr:
     *
     * Like ptr(), but returns a byte pointer for use in byte arithmetic.
     */
    [[nodiscard]] uint8_t* raw_ptr() const {
        return reinterpret_cast<uint8_t*>(ptr());
    }

    // JSClass operations

 protected:
    void finalize_impl(JS::GCContext*, JSObject*) {
        delete static_cast<Instance*>(this);
    }

    // Override if necessary
    void trace_impl(JSTracer*) {}

    // Helper methods

    /*
     * GIWrapperInstance::typecheck_impl:
     *
     * See GIWrapperBase::typecheck(). Checks that the instance's wrapped
     * pointer is of the correct GType or GI info. Does not throw a JS
     * exception.
     *
     * It's possible to override typecheck_impl() if you need an extra step in
     * the check.
     */
    [[nodiscard]]
    bool typecheck_impl(const GI::BaseInfo expected_info) const {
        if constexpr (Prototype::may_not_have_info) {
            if (Base::info())
                return Base::info().ref() == expected_info;
        } else {
            return Base::info() == expected_info;
        }
        return true;
    }
    [[nodiscard]]
    bool typecheck_impl(GType expected_gtype) const {
        g_assert(expected_gtype != G_TYPE_NONE &&
                 "should not call typecheck_impl() without a real GType");
        return g_type_is_a(Base::gtype(), expected_gtype);
    }
};

#endif  // GI_WRAPPERUTILS_H_
