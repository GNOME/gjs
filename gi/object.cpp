/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <stdint.h>
#include <string.h>  // for memset, strcmp

#include <algorithm>  // for find
#include <array>
#include <functional>  // for mem_fn
#include <limits>
#include <memory>  // for make_unique, unique_ptr
#include <string>
#include <tuple>  // for tie
#include <type_traits>
#include <unordered_set>
#include <utility>  // for move, pair
#include <vector>

#include <girepository/girepository.h>
#include <girepository/girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallAndConstruct.h>  // for IsCallable, JS_CallFunctionValue
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>         // for JS_ReportOutOfMemory
#include <js/Exception.h>           // for JS_ClearPendingException
#include <js/GCAPI.h>               // for JS_AddWeakPointerCompartmentCallback
#include <js/GCVector.h>            // for MutableWrappedPtrOperations
#include <js/HeapAPI.h>
#include <js/MemoryFunctions.h>     // for AddAssociatedMemory, RemoveAssoci...
#include <js/ObjectWithStashedPointer.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_READONLY
#include <js/PropertySpec.h>        // for JS_FN, JSFunctionSpec, JSPropertySpec
#include <js/String.h>
#include <js/Symbol.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>        // for JS_GetFunctionObject, IdVector
#include <jsfriendapi.h>  // for JS_GetObjectFunction, GetFunctionNativeReserved
#include <mozilla/Maybe.h>
#include <mozilla/Result.h>
#include <mozilla/Span.h>
#include <mozilla/Try.h>
#include <mozilla/Unused.h>

#include "gi/arg-inl.h"
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/closure.h"
#include "gi/cwrapper.h"
#include "gi/function.h"
#include "gi/gjs_gi_trace.h"
#include "gi/info.h"
#include "gi/js-value-inl.h"  // for Relaxed, c_value_to_js_checked
#include "gi/object.h"
#include "gi/repo.h"
#include "gi/toggle.h"
#include "gi/utils-inl.h"  // for gjs_int_to_pointer
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "gjs/profiler-private.h"
#include "util/log.h"

class JSTracer;

using mozilla::Err, mozilla::Maybe, mozilla::Nothing, mozilla::Ok,
    mozilla::Result, mozilla::Some;

/* This is a trick to print out the sizes of the structs at compile time, in
 * an error message. */
// template <int s> struct Measure;
// Measure<sizeof(ObjectInstance)> instance_size;
// Measure<sizeof(ObjectPrototype)> prototype_size;

#if defined(__x86_64__) && defined(__clang__)
/* This isn't meant to be comprehensive, but should trip on at least one CI job
 * if sizeof(ObjectInstance) is increased. */
static_assert(sizeof(ObjectInstance) <= 64,
              "Think very hard before increasing the size of ObjectInstance. "
              "There can be tens of thousands of them alive in a typical "
              "gnome-shell run.");
#endif  // x86-64 clang

bool ObjectInstance::s_weak_pointer_callback = false;
decltype(ObjectInstance::s_wrapped_gobject_list)
    ObjectInstance::s_wrapped_gobject_list;

static const auto DISPOSED_OBJECT = std::numeric_limits<uintptr_t>::max();

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_lookup_object_prototype_from_info(
    JSContext*, Maybe<const GI::BaseInfo>, GType);

// clang-format off
G_DEFINE_QUARK(gjs::custom-type, ObjectBase::custom_type)
G_DEFINE_QUARK(gjs::custom-property, ObjectBase::custom_property)
G_DEFINE_QUARK(gjs::instance-strings, ObjectBase::instance_strings)
G_DEFINE_QUARK(gjs::disposed, ObjectBase::disposed)
// clang-format on

[[nodiscard]] static GQuark gjs_object_priv_quark() {
    static GQuark val = 0;
    if (G_UNLIKELY (!val))
        val = g_quark_from_static_string ("gjs::private");

    return val;
}

bool ObjectBase::is_custom_js_class() {
    return !!g_type_get_qdata(gtype(), ObjectBase::custom_type_quark());
}

void ObjectInstance::link() {
    auto [_, done] = s_wrapped_gobject_list.insert(this);
    g_assert(done);
    mozilla::Unused << done;
}

void ObjectInstance::unlink() { s_wrapped_gobject_list.erase(this); }

const void* ObjectBase::jsobj_addr(void) const {
    if (is_prototype())
        return nullptr;
    return to_instance()->m_wrapper.debug_addr();
}

bool ObjectInstance::check_gobject_disposed_or_finalized(
    const char* for_what) const {
    if (!m_gobj_disposed)
        return true;

    g_critical(
        "Object %s (%p), has been already %s — impossible to %s it. This might "
        "be caused by the object having been destroyed from C code using "
        "something such as destroy(), dispose(), or remove() vfuncs.\n%s",
        format_name().c_str(), m_ptr.get(),
        m_gobj_finalized ? "finalized" : "disposed", for_what,
        gjs_dumpstack_string().c_str());
    return false;
}

bool ObjectInstance::check_gobject_finalized(const char* for_what) const {
    if (check_gobject_disposed_or_finalized(for_what))
        return true;

    return !m_gobj_finalized;
}

ObjectInstance *
ObjectInstance::for_gobject(GObject *gobj)
{
    auto priv = static_cast<ObjectInstance *>(g_object_get_qdata(gobj,
                                                                 gjs_object_priv_quark()));

    if (priv)
        priv->check_js_object_finalized();

    return priv;
}

void
ObjectInstance::check_js_object_finalized(void)
{
    if (!m_uses_toggle_ref)
        return;
    if (G_UNLIKELY(m_wrapper_finalized)) {
        g_critical(
            "Object %p (a %s) resurfaced after the JS wrapper was finalized. "
            "This is some library doing dubious memory management inside "
            "dispose()",
            m_ptr.get(), type_name());
        m_wrapper_finalized = false;
        g_assert(!m_wrapper);  /* should associate again with a new wrapper */
    }
}

ObjectPrototype* ObjectPrototype::for_gtype(GType gtype) {
    return static_cast<ObjectPrototype*>(
        g_type_get_qdata(gtype, gjs_object_priv_quark()));
}

void ObjectPrototype::set_type_qdata(void) {
    g_type_set_qdata(m_gtype, gjs_object_priv_quark(), this);
}

void
ObjectInstance::set_object_qdata(void)
{
    g_object_set_qdata_full(
        m_ptr, gjs_object_priv_quark(), this, [](void* object) {
            auto* self = static_cast<ObjectInstance*>(object);
            if (G_UNLIKELY(!self->m_gobj_disposed)) {
                g_warning(
                    "Object %p (a %s) was finalized but we didn't track "
                    "its disposal",
                    self->m_ptr.get(), g_type_name(self->gtype()));
                self->m_gobj_disposed = true;
            }
            self->m_gobj_finalized = true;
            gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                                "Wrapped GObject %p finalized",
                                self->m_ptr.get());
        });
}

void
ObjectInstance::unset_object_qdata(void)
{
    auto priv_quark = gjs_object_priv_quark();
    if (g_object_get_qdata(m_ptr, priv_quark) == this)
        g_object_steal_qdata(m_ptr, priv_quark);
}

GParamSpec* ObjectPrototype::find_param_spec_from_id(
    JSContext* cx, Gjs::AutoTypeClass<GObjectClass> const& object_class,
    JS::HandleString key) {
    /* First check for the ID in the cache */

    JS::UniqueChars js_prop_name(JS_EncodeStringToUTF8(cx, key));
    if (!js_prop_name)
        return nullptr;

    Gjs::AutoChar gname{gjs_hyphen_from_camel(js_prop_name.get())};
    GParamSpec* pspec = g_object_class_find_property(object_class, gname);

    if (!pspec) {
        gjs_wrapper_throw_nonexistent_field(cx, m_gtype, js_prop_name.get());
        return nullptr;
    }

    return pspec;
}

/* A hook on adding a property to an object. This is called during a set
 * property operation after all the resolve hooks on the prototype chain have
 * failed to resolve. We use this to mark an object as needing toggle refs when
 * custom state is set on it, because we need to keep the JS GObject wrapper
 * alive in order not to lose custom "expando" properties.
 */
bool ObjectBase::add_property(JSContext* cx, JS::HandleObject obj,
                              JS::HandleId id, JS::HandleValue value) {
    auto* priv = ObjectBase::for_js(cx, obj);

    /* priv is null during init: property is not being added from JS */
    if (!priv) {
        debug_jsprop_static("Add property hook", id, obj);
        return true;
    }
    if (priv->is_prototype())
        return true;

    return priv->to_instance()->add_property_impl(cx, obj, id, value);
}

bool ObjectInstance::add_property_impl(JSContext* cx, JS::HandleObject obj,
                                       JS::HandleId id, JS::HandleValue) {
    debug_jsprop("Add property hook", id, obj);

    if (is_custom_js_class())
        return true;

    ensure_uses_toggle_ref(cx);
    return true;
}

template <typename TAG>
bool ObjectBase::prop_getter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    auto* pspec = static_cast<GParamSpec*>(
        gjs_dynamic_property_private_slot(&args.callee()).toPrivate());

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + pspec->name + "\"]")};
    AutoProfilerLabel label{cx, "property getter", full_name};

    priv->debug_jsprop("Property getter", pspec->name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->to_instance()->prop_getter_impl<TAG>(cx, pspec, args.rval());
}

template <typename TAG>
bool ObjectInstance::prop_getter_impl(JSContext* cx, GParamSpec* param,
                                      JS::MutableHandleValue rval) {
    if (!check_gobject_finalized("get any property from")) {
        rval.setUndefined();
        return true;
    }

    if (param->flags & G_PARAM_DEPRECATED) {
        _gjs_warn_deprecated_once_per_callsite(cx, DeprecatedGObjectProperty,
                                               {format_name(), param->name});
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Accessing GObject property %s",
                     param->name);

    Gjs::AutoGValue gvalue(G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(m_ptr, param->name, &gvalue);

    if constexpr (!std::is_same_v<TAG, void>) {
        if (Gjs::c_value_to_js_checked<TAG>(cx, Gjs::gvalue_get<TAG>(&gvalue),
                                            rval))
            return true;

        gjs_throw(cx, "Can't convert value %s got from %s::%s property",
                  Gjs::gvalue_to_string<TAG>(&gvalue).c_str(),
                  format_name().c_str(), param->name);
        return false;
    } else {
        return gjs_value_from_g_value(cx, rval, &gvalue);
    }
}

bool ObjectBase::prop_getter_write_only(JSContext*, unsigned argc,
                                        JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    args.rval().setUndefined();
    return true;
}

class ObjectPropertyInfoCaller {
 public:
    GI::AutoFunctionInfo func_info;
    void* native_address;

    explicit ObjectPropertyInfoCaller(const GI::FunctionInfo info)
        : func_info(info), native_address(nullptr) {}

    Gjs::GErrorResult<> init() {
        GIFunctionInvoker invoker;
        MOZ_TRY(func_info.prep_invoker(&invoker));
        native_address = invoker.native_address;
        gi_function_invoker_clear(&invoker);
        return Ok{};
    }
};

bool ObjectBase::prop_getter_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject pspec_obj{
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject()};
    auto* info_caller =
        JS::ObjectGetStashedPointer<ObjectPropertyInfoCaller>(cx, pspec_obj);

    const GI::AutoFunctionInfo& func_info = info_caller->func_info;
    GI::AutoPropertyInfo property_info{func_info.property().value()};
    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + property_info.name() + "\"]")};
    AutoProfilerLabel label{cx, "property getter", full_name};

    priv->debug_jsprop("Property getter", property_info.name(), obj);

    // Ignore silently; note that this is different from what we do for
    // boxed types, for historical reasons
    if (priv->is_prototype())
        return true;

    return priv->to_instance()->prop_getter_impl(cx, info_caller, args);
}

template <typename TAG>
[[nodiscard]]
static bool simple_getter_caller(GObject* obj, void* native_address,
                                 GIArgument* out_arg) {
    using T = Gjs::Tag::RealT<TAG>;
    using FuncType = T (*)(GObject*);
    FuncType func = reinterpret_cast<FuncType>(native_address);

    gjs_arg_set<TAG>(out_arg, func(obj));
    return true;
}

[[nodiscard]]
static bool simple_getters_caller(const GI::TypeInfo type_info, GObject* obj,
                                  void* native_address, GIArgument* out_arg) {
    switch (type_info.tag()) {
        case GI_TYPE_TAG_VOID:
            if (type_info.is_pointer())
                return simple_getter_caller<void*>(obj, native_address,
                                                   out_arg);
            return false;
        case GI_TYPE_TAG_BOOLEAN:
            return simple_getter_caller<Gjs::Tag::GBoolean>(obj, native_address,
                                                            out_arg);
        case GI_TYPE_TAG_INT8:
            return simple_getter_caller<int8_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_UINT8:
            return simple_getter_caller<uint8_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_INT16:
            return simple_getter_caller<int16_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_UINT16:
            return simple_getter_caller<uint16_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_INT32:
            return simple_getter_caller<int32_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_UINT32:
            return simple_getter_caller<uint32_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_INT64:
            return simple_getter_caller<int64_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_UINT64:
            return simple_getter_caller<uint64_t>(obj, native_address, out_arg);
        case GI_TYPE_TAG_FLOAT:
            return simple_getter_caller<float>(obj, native_address, out_arg);
        case GI_TYPE_TAG_DOUBLE:
            return simple_getter_caller<double>(obj, native_address, out_arg);
        case GI_TYPE_TAG_GTYPE:
            return simple_getter_caller<Gjs::Tag::GType>(obj, native_address,
                                                         out_arg);
        case GI_TYPE_TAG_UNICHAR:
            return simple_getter_caller<gunichar>(obj, native_address, out_arg);

        case GI_TYPE_TAG_INTERFACE:
            {
                GI::AutoBaseInfo interface_info{type_info.interface()};

                if (interface_info.is_enum_or_flags()) {
                    return simple_getter_caller<Gjs::Tag::Enum>(obj, native_address, out_arg);
                }
                return simple_getter_caller<void*>(obj, native_address, out_arg);
            }

        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            return simple_getter_caller<void*>(obj, native_address, out_arg);
    }

    return false;
}

bool ObjectInstance::prop_getter_impl(JSContext* cx,
                                      ObjectPropertyInfoCaller* info_caller,
                                      JS::CallArgs const& args) {
    if (!check_gobject_finalized("get any property from")) {
        args.rval().setUndefined();
        return true;
    }

    const GI::AutoFunctionInfo& getter = info_caller->func_info;
    GI::AutoPropertyInfo property_info{getter.property().value()};

    if (property_info.has_deprecated_param_flag() ||
        property_info.is_deprecated() || getter.is_deprecated()) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, DeprecatedGObjectProperty,
            {format_name(), property_info.name()});
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Accessing GObject property %s",
                     property_info.name());

    GIArgument ret;
    std::array<GIArgument, 1> gi_args;
    gjs_arg_set(&gi_args[0], m_ptr.get());

    GI::StackTypeInfo type_info;
    getter.load_return_type(&type_info);
    if (!simple_getters_caller(type_info, m_ptr, info_caller->native_address,
                               &ret)) {
        const std::string& class_name = format_name();
        gjs_throw(cx, "Wrong type for %s::%s getter", class_name.c_str(),
                  property_info.name());
        return false;
    }

    GITransfer transfer = getter.caller_owns();

    if (!gjs_value_from_gi_argument(cx, args.rval(), type_info,
                                    GJS_ARGUMENT_RETURN_VALUE, transfer,
                                    &ret)) {
        // Unlikely to happen, but we fallback to gvalue mode, just in case
        JS_ClearPendingException(cx);
        Gjs::AutoTypeClass<GObjectClass> klass{gtype()};
        GParamSpec* pspec =
            g_object_class_find_property(klass, property_info.name());
        if (!pspec) {
            const std::string& class_name = format_name();
            gjs_throw(cx, "Error converting value got from %s::%s getter",
                      class_name.c_str(), property_info.name());
            return false;
        }
        return prop_getter_impl<void>(cx, pspec, args[0]);
    }

    return gjs_gi_argument_release(cx, transfer, type_info,
                                   GjsArgumentFlags::ARG_OUT, &ret);
}

class ObjectPropertyPspecCaller {
 public:
    GParamSpec* pspec;
    void* native_address;

    explicit ObjectPropertyPspecCaller(GParamSpec* param)
        : pspec(param), native_address(nullptr) {}

    Gjs::GErrorResult<> init(const GI::FunctionInfo info) {
        GIFunctionInvoker invoker;
        MOZ_TRY(info.prep_invoker(&invoker));
        native_address = invoker.native_address;
        gi_function_invoker_clear(&invoker);
        return Ok{};
    }
};

template <typename TAG, GITransfer TRANSFER>
bool ObjectBase::prop_getter_simple_type_func(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject pspec_obj(
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject());
    auto* caller =
        JS::ObjectGetStashedPointer<ObjectPropertyPspecCaller>(cx, pspec_obj);

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + caller->pspec->name + "\"]")};
    AutoProfilerLabel label{cx, "property getter", full_name};

    priv->debug_jsprop("Property getter",
                       gjs_intern_string_to_id(cx, caller->pspec->name), obj);

    // Ignore silently; note that this is different from what we do for
    // boxed types, for historical reasons
    if (priv->is_prototype())
        return true;

    return priv->to_instance()->prop_getter_impl<TAG, TRANSFER>(cx, caller,
                                                                args);
}

template <typename TAG, GITransfer TRANSFER>
bool ObjectInstance::prop_getter_impl(JSContext* cx,
                                      ObjectPropertyPspecCaller* pspec_caller,
                                      JS::CallArgs const& args) {
    if (!check_gobject_finalized("get any property from")) {
        args.rval().setUndefined();
        return true;
    }

    if (pspec_caller->pspec->flags & G_PARAM_DEPRECATED) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, DeprecatedGObjectProperty,
            {format_name(), pspec_caller->pspec->name});
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Accessing GObject property %s",
                     pspec_caller->pspec->name);

    using T = Gjs::Tag::RealT<TAG>;
    using FuncType = T (*)(GObject*);
    FuncType func = reinterpret_cast<FuncType>(pspec_caller->native_address);
    T retval = func(m_ptr);
    if (!Gjs::c_value_to_js_checked<TAG>(cx, retval, args.rval()))
        return false;

    if constexpr (TRANSFER != GI_TRANSFER_NOTHING) {
        static_assert(std::is_same_v<T, char*>, "Unexpected type to release");
        g_free(retval);
    }

    return true;
}

[[nodiscard]]
static Maybe<GI::AutoFieldInfo> lookup_field_info(const GI::ObjectInfo info,
                                                  const char* name) {
    for (GI::AutoFieldInfo retval : info.fields()) {
        if (strcmp(name, retval.name()) == 0)
            return Some(retval);
    }
    return {};
}

bool ObjectBase::field_getter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject field_info_obj{
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject()};
    auto const& field_info =
        *JS::ObjectGetStashedPointer<GI::AutoFieldInfo>(cx, field_info_obj);

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + field_info.name() + "\"]")};
    AutoProfilerLabel label{cx, "field getter", full_name};

    priv->debug_jsprop("Field getter", field_info.name(), obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->to_instance()->field_getter_impl(cx, field_info, args.rval());
}

bool ObjectInstance::field_getter_impl(JSContext* cx,
                                       GI::AutoFieldInfo const& field,
                                       JS::MutableHandleValue rval) {
    if (!check_gobject_finalized("get any property from"))
        return true;

    GIArgument arg = { 0 };

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Overriding %s with GObject field",
                     field.name());

    GI::AutoTypeInfo type{field.type_info()};
    switch (type.tag()) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_INTERFACE:
            gjs_throw(cx,
                      "Can't get field %s; GObject introspection supports only "
                      "fields with simple types, not %s",
                      field.name(), type.display_string());
            return false;

        default:
            break;
    }

    if (field.read(m_ptr, &arg).isErr()) {
        gjs_throw(cx, "Error getting field %s from object", field.name());
        return false;
    }

    return gjs_value_from_gi_argument(cx, rval, type, GJS_ARGUMENT_FIELD,
                                      GI_TRANSFER_EVERYTHING, &arg);
    /* transfer is irrelevant because g_field_info_get_field() doesn't
     * handle boxed types */
}

/* Dynamic setter for GObject properties. Returns false on OOM/exception.
 * args.rval() becomes the "stored value" for the property. */
template <typename TAG>
bool ObjectBase::prop_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    auto* pspec = static_cast<GParamSpec*>(
        gjs_dynamic_property_private_slot(&args.callee()).toPrivate());

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + pspec->name + "\"]")};
    AutoProfilerLabel label{cx, "property setter", full_name};

    priv->debug_jsprop("Property setter", pspec->name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* Clear the JS stored value, to avoid keeping additional references */
    args.rval().setUndefined();

    return priv->to_instance()->prop_setter_impl<TAG>(cx, pspec, args[0]);
}

template <typename TAG>
bool ObjectInstance::prop_setter_impl(JSContext* cx, GParamSpec* param_spec,
                                      JS::HandleValue value) {
    if (!check_gobject_finalized("set any property on"))
        return true;

    if (param_spec->flags & G_PARAM_DEPRECATED) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, DeprecatedGObjectProperty, {format_name(), param_spec->name});
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Setting GObject prop %s",
                     param_spec->name);

    Gjs::AutoGValue gvalue(G_PARAM_SPEC_VALUE_TYPE(param_spec));

    using T = Gjs::Tag::RealT<TAG>;
    if constexpr (std::is_same_v<T, void>) {
        if (!gjs_value_to_g_value(cx, value, &gvalue))
            return false;
    } else if constexpr (std::is_arithmetic_v<  // NOLINT(readability/braces)
                             T> &&
                         !Gjs::type_has_js_getter<TAG>()) {
        bool out_of_range = false;

        Gjs::Tag::JSValuePackT<TAG> val{};
        using HolderTag = Gjs::Tag::JSValuePackTag<TAG>;
        if (!Gjs::js_value_to_c_checked<T, HolderTag>(cx, value, &val,
                                                      &out_of_range)) {
            gjs_throw(cx, "Can't convert value %s to set %s::%s property",
                      gjs_debug_value(value).c_str(), format_name().c_str(),
                      param_spec->name);
            return false;
        }

        if (out_of_range) {
            gjs_throw(cx, "value %s is out of range for %s (type %s)",
                      std::to_string(val).c_str(), param_spec->name,
                      Gjs::static_type_name<TAG>());
            return false;
        }

        Gjs::gvalue_set<TAG>(&gvalue, val);
    } else {
        T native_value;
        if (!Gjs::js_value_to_c<TAG>(cx, value, &native_value)) {
            gjs_throw(cx, "Can't convert %s value to set %s::%s property",
                      gjs_debug_value(value).c_str(), format_name().c_str(),
                      param_spec->name);
            return false;
        }

        if constexpr (std::is_pointer_v<T>) {
            Gjs::gvalue_take<TAG>(&gvalue, g_steal_pointer(&native_value));
        } else {
            Gjs::gvalue_set<TAG>(&gvalue, native_value);
        }
    }

    g_object_set_property(m_ptr, param_spec->name, &gvalue);

    return true;
}

bool ObjectBase::prop_setter_read_only(JSContext* cx, unsigned argc,
                                       JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    auto* pspec = static_cast<GParamSpec*>(
        gjs_dynamic_property_private_slot(&args.callee()).toPrivate());
    // Prevent setting the property even in JS
    return gjs_wrapper_throw_readonly_field(cx, priv->to_instance()->gtype(),
                                            pspec->name);
}

bool ObjectBase::prop_setter_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject func_obj{
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject()};
    auto* info_caller =
        JS::ObjectGetStashedPointer<ObjectPropertyInfoCaller>(cx, func_obj);

    const GI::AutoFunctionInfo& func_info = info_caller->func_info;
    GI::AutoPropertyInfo property_info{func_info.property().value()};
    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + property_info.name() + "\"]")};
    AutoProfilerLabel label{cx, "property setter", full_name};

    priv->debug_jsprop("Property setter", property_info.name(), obj);

    // Ignore silently; note that this is different from what we do for
    // boxed types, for historical reasons
    if (priv->is_prototype())
        return true;

    return priv->to_instance()->prop_setter_impl(cx, info_caller, args);
}

template <typename TAG>
[[nodiscard]]
static bool simple_setter_caller(GIArgument* arg, GObject* obj,
                                 void* native_address) {
    using FuncType = void (*)(GObject*, Gjs::Tag::RealT<TAG>);
    FuncType func = reinterpret_cast<FuncType>(native_address);

    func(obj, gjs_arg_get<TAG>(arg));
    return true;
}

[[nodiscard]]
static bool simple_setters_caller(const GI::TypeInfo type_info, GIArgument* arg,
                                  GObject* obj, void* native_address) {
    switch (type_info.tag()) {
        case GI_TYPE_TAG_VOID:
            if (type_info.is_pointer())
                return simple_setter_caller<void*>(arg, obj, native_address);
            return false;
        case GI_TYPE_TAG_BOOLEAN:
            return simple_setter_caller<Gjs::Tag::GBoolean>(arg, obj,
                                                            native_address);
        case GI_TYPE_TAG_INT8:
            return simple_setter_caller<int8_t>(arg, obj, native_address);
        case GI_TYPE_TAG_UINT8:
            return simple_setter_caller<uint8_t>(arg, obj, native_address);
        case GI_TYPE_TAG_INT16:
            return simple_setter_caller<int16_t>(arg, obj, native_address);
        case GI_TYPE_TAG_UINT16:
            return simple_setter_caller<uint16_t>(arg, obj, native_address);
        case GI_TYPE_TAG_INT32:
            return simple_setter_caller<int32_t>(arg, obj, native_address);
        case GI_TYPE_TAG_UINT32:
            return simple_setter_caller<uint32_t>(arg, obj, native_address);
        case GI_TYPE_TAG_INT64:
            return simple_setter_caller<int64_t>(arg, obj, native_address);
        case GI_TYPE_TAG_UINT64:
            return simple_setter_caller<uint64_t>(arg, obj, native_address);
        case GI_TYPE_TAG_FLOAT:
            return simple_setter_caller<float>(arg, obj, native_address);
        case GI_TYPE_TAG_DOUBLE:
            return simple_setter_caller<double>(arg, obj, native_address);
        case GI_TYPE_TAG_GTYPE:
            return simple_setter_caller<Gjs::Tag::GType>(arg, obj,
                                                         native_address);
        case GI_TYPE_TAG_UNICHAR:
            return simple_setter_caller<gunichar>(arg, obj, native_address);

        case GI_TYPE_TAG_INTERFACE:
            {
                GI::AutoBaseInfo interface_info{type_info.interface()};

                if (interface_info.is_enum_or_flags()) {
                    return simple_setter_caller<Gjs::Tag::Enum>(arg, obj, native_address);
                }
                return simple_setter_caller<void*>(arg, obj, native_address);
            }

        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            return simple_setter_caller<void*>(arg, obj, native_address);
    }

    return false;
}

bool ObjectInstance::prop_setter_impl(JSContext* cx,
                                      ObjectPropertyInfoCaller* info_caller,
                                      JS::CallArgs const& args) {
    if (!check_gobject_finalized("set any property on"))
        return true;

    const GI::AutoFunctionInfo& setter = info_caller->func_info;
    GI::AutoPropertyInfo property_info{setter.property().value()};

    if (property_info.has_deprecated_param_flag() ||
        property_info.is_deprecated() || setter.is_deprecated()) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, DeprecatedGObjectProperty,
            {format_name(), property_info.name()});
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Setting GObject prop via setter %s",
                     property_info.name());

    GI::StackArgInfo arg_info;
    setter.load_arg(0, &arg_info);
    GI::StackTypeInfo type_info;
    arg_info.load_type(&type_info);
    GITransfer transfer = arg_info.ownership_transfer();
    JS::RootedValue value{cx, args[0]};
    GIArgument arg;

    if (!gjs_value_to_gi_argument(cx, value, type_info, property_info.name(),
                                  GJS_ARGUMENT_ARGUMENT, transfer,
                                  GjsArgumentFlags::ARG_IN, &arg)) {
        // Unlikely to happen, but we fallback to gvalue mode, just in case
        JS_ClearPendingException(cx);
        Gjs::AutoTypeClass<GObjectClass> klass{gtype()};
        GParamSpec* pspec =
            g_object_class_find_property(klass, property_info.name());
        if (!pspec) {
            const std::string& class_name = format_name();
            gjs_throw(cx, "Error converting value to call %s::%s setter",
                      class_name.c_str(), property_info.name());
            return false;
        }
        return prop_setter_impl<void>(cx, pspec, value);
    }

    if (!simple_setters_caller(type_info, &arg, m_ptr,
                               info_caller->native_address)) {
        const std::string& class_name = format_name();
        gjs_throw(cx, "Wrong type for %s::%s setter", class_name.c_str(),
                  property_info.name());
        return false;
    }

    return gjs_gi_argument_release_in_arg(cx, transfer, type_info, &arg);
}

template <typename TAG, GITransfer TRANSFER>
bool ObjectBase::prop_setter_simple_type_func(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject pspec_obj(
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject());
    auto* caller =
        JS::ObjectGetStashedPointer<ObjectPropertyPspecCaller>(cx, pspec_obj);

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[" + caller->pspec->name + "]")};
    AutoProfilerLabel label{cx, "property setter", full_name};

    priv->debug_jsprop("Property setter", caller->pspec->name, obj);

    // Ignore silently; note that this is different from what we do for
    // boxed types, for historical reasons
    if (priv->is_prototype())
        return true;

    return priv->to_instance()->prop_setter_impl<TAG, TRANSFER>(cx, caller,
                                                                args);
}

template <typename TAG, GITransfer TRANSFER>
bool ObjectInstance::prop_setter_impl(JSContext* cx,
                                      ObjectPropertyPspecCaller* pspec_caller,
                                      JS::CallArgs const& args) {
    if (!check_gobject_finalized("set any property on"))
        return true;

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Setting GObject prop via setter %s",
                     pspec_caller->pspec->name);

    if (pspec_caller->pspec->flags & G_PARAM_DEPRECATED) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, DeprecatedGObjectProperty,
            {format_name(), pspec_caller->pspec->name});
    }

    using T = Gjs::Tag::RealT<TAG>;
    using FuncType = void (*)(GObject*, T);
    FuncType func = reinterpret_cast<FuncType>(pspec_caller->native_address);

    if constexpr (std::is_arithmetic_v<T> && !Gjs::type_has_js_getter<TAG>()) {
        bool out_of_range = false;

        Gjs::Tag::JSValuePackT<TAG> native_value{};
        using HolderTag = Gjs::Tag::JSValuePackTag<TAG>;
        if (!Gjs::js_value_to_c_checked<T, HolderTag>(
                cx, args[0], &native_value, &out_of_range))
            return false;

        if (out_of_range) {
            gjs_throw(cx, "value %s is out of range for %s (type %s)",
                      std::to_string(native_value).c_str(),
                      pspec_caller->pspec->name, Gjs::static_type_name<TAG>());
            return false;
        }

        func(m_ptr, native_value);
    } else {
        T native_value;
        if (!Gjs::js_value_to_c<TAG>(cx, args[0], &native_value))
            return false;

        func(m_ptr, native_value);

        if constexpr (TRANSFER == GI_TRANSFER_NOTHING && std::is_pointer_v<T>) {
            static_assert(std::is_same_v<T, char*>,
                          "Unexpected type to release");
            g_free(native_value);
        }
    }

    return true;
}

bool ObjectBase::field_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedObject field_info_obj{
        cx, &gjs_dynamic_property_private_slot(&args.callee()).toObject()};
    auto const& field_info =
        *JS::ObjectGetStashedPointer<GI::AutoFieldInfo>(cx, field_info_obj);

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        cx, priv->format_name() + "[\"" + field_info.name() + "\"]")};
    AutoProfilerLabel label{cx, "field setter", full_name};

    priv->debug_jsprop("Field setter", field_info.name(), obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* We have to update args.rval(), because JS caches it as the property's "stored
     * value" (https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/Stored_value)
     * and so subsequent gets would get the stored value instead of accessing
     * the field */
    args.rval().setUndefined();

    return priv->to_instance()->field_setter_not_impl(cx, field_info);
}

bool ObjectInstance::field_setter_not_impl(JSContext* cx,
                                           GI::AutoFieldInfo const& field) {
    if (!check_gobject_finalized("set GObject field on"))
        return true;

    /* As far as I know, GI never exposes GObject instance struct fields as
     * writable, so no need to implement this for the time being */
    if (field.is_writable()) {
        g_message(
            "Field %s of a GObject is writable, but setting it is not "
            "implemented",
            field.name());
        return true;
    }

    return gjs_wrapper_throw_readonly_field(cx, gtype(), field.name());
}

bool ObjectPrototype::is_vfunc_unchanged(const GI::VFuncInfo info) const {
    GType ptype = g_type_parent(m_gtype);

    Gjs::GErrorResult<void*> addr1 = info.address(m_gtype);
    if (addr1.isErr())
        return false;

    Gjs::GErrorResult<void*> addr2 = info.address(ptype);
    if (addr2.isErr())
        return false;

    return addr1.unwrap() == addr2.unwrap();
}

[[nodiscard]]
static Maybe<GI::AutoVFuncInfo> find_vfunc_on_parents(
    const GI::ObjectInfo info, const char* name, bool* out_defined_by_parent) {
    bool defined_by_parent = false;

    /* ref the first info so that we don't destroy
     * it when unrefing parents later */
    Maybe<GI::AutoObjectInfo> parent{Some(info)};

    /* Since it isn't possible to override a vfunc on
     * an interface without reimplementing it, we don't need
     * to search the parent types when looking for a vfunc. */
    Maybe<GI::AutoVFuncInfo> vfunc =
        parent->find_vfunc_using_interfaces(name).map(
            [](auto&& pair) { return std::move(pair.first); });
    while (!vfunc && parent) {
        parent = parent->parent();
        if (parent)
            vfunc = parent->vfunc(name);

        defined_by_parent = true;
    }

    if (out_defined_by_parent)
        *out_defined_by_parent = defined_by_parent;

    return vfunc;
}

/* Taken from GLib */
static void canonicalize_key(const Gjs::AutoChar& key) {
    for (char* p = key; *p != 0; p++) {
        char c = *p;

        if (c != '-' && (c < '0' || c > '9') && (c < 'A' || c > 'Z') &&
            (c < 'a' || c > 'z'))
            *p = '-';
    }
}

/* @name must already be canonicalized */
[[nodiscard]]
static Maybe<GI::AutoPropertyInfo> get_ginterface_property_by_name(
    const GI::InterfaceInfo info, const char* name) {
    for (GI::AutoPropertyInfo prop_info : info.properties()) {
        if (strcmp(name, prop_info.name()) == 0)
            return Some(std::move(prop_info));
    }

    return {};
}

[[nodiscard]]
static Maybe<GI::AutoPropertyInfo> get_gobject_property_info(
    const GI::ObjectInfo info, const char* name) {
    for (GI::AutoPropertyInfo prop_info : info.properties()) {
        if (strcmp(name, prop_info.name()) == 0)
            return Some(std::move(prop_info));
    }

    for (GI::AutoInterfaceInfo iface_info : info.interfaces()) {
        if (Maybe<GI::AutoPropertyInfo> prop_info =
                get_ginterface_property_by_name(iface_info, name))
            return prop_info;
    }
    return {};
}

[[nodiscard]]
static JSNative get_getter_for_type(const GI::TypeInfo type_info,
                                    GITransfer transfer) {
    switch (type_info.tag()) {
        case GI_TYPE_TAG_BOOLEAN:
            return ObjectBase::prop_getter_simple_type_func<Gjs::Tag::GBoolean>;
        case GI_TYPE_TAG_INT8:
            return ObjectBase::prop_getter_simple_type_func<int8_t>;
        case GI_TYPE_TAG_UINT8:
            return ObjectBase::prop_getter_simple_type_func<uint8_t>;
        case GI_TYPE_TAG_INT16:
            return ObjectBase::prop_getter_simple_type_func<int16_t>;
        case GI_TYPE_TAG_UINT16:
            return ObjectBase::prop_getter_simple_type_func<uint16_t>;
        case GI_TYPE_TAG_INT32:
            return ObjectBase::prop_getter_simple_type_func<int32_t>;
        case GI_TYPE_TAG_UINT32:
            return ObjectBase::prop_getter_simple_type_func<uint32_t>;
        case GI_TYPE_TAG_INT64:
            return ObjectBase::prop_getter_simple_type_func<int64_t>;
        case GI_TYPE_TAG_UINT64:
            return ObjectBase::prop_getter_simple_type_func<uint64_t>;
        case GI_TYPE_TAG_FLOAT:
            return ObjectBase::prop_getter_simple_type_func<float>;
        case GI_TYPE_TAG_DOUBLE:
            return ObjectBase::prop_getter_simple_type_func<double>;
        case GI_TYPE_TAG_GTYPE:
            return ObjectBase::prop_getter_simple_type_func<Gjs::Tag::GType>;
        case GI_TYPE_TAG_UNICHAR:
            return ObjectBase::prop_getter_simple_type_func<gunichar>;
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING) {
                return ObjectBase::prop_getter_simple_type_func<
                    const char*, GI_TRANSFER_NOTHING>;
            } else {
                return ObjectBase::prop_getter_simple_type_func<
                    char*, GI_TRANSFER_EVERYTHING>;
            }
        default:
            return nullptr;
    }
}

[[nodiscard]] static JSNative get_setter_for_type(const GI::TypeInfo type_info,
                                                  GITransfer transfer) {
    switch (type_info.tag()) {
        case GI_TYPE_TAG_BOOLEAN:
            return ObjectBase::prop_setter_simple_type_func<Gjs::Tag::GBoolean>;
        case GI_TYPE_TAG_INT8:
            return ObjectBase::prop_setter_simple_type_func<int8_t>;
        case GI_TYPE_TAG_UINT8:
            return ObjectBase::prop_setter_simple_type_func<uint8_t>;
        case GI_TYPE_TAG_INT16:
            return ObjectBase::prop_setter_simple_type_func<int16_t>;
        case GI_TYPE_TAG_UINT16:
            return ObjectBase::prop_setter_simple_type_func<uint16_t>;
        case GI_TYPE_TAG_INT32:
            return ObjectBase::prop_setter_simple_type_func<int32_t>;
        case GI_TYPE_TAG_UINT32:
            return ObjectBase::prop_setter_simple_type_func<uint32_t>;
        case GI_TYPE_TAG_INT64:
            return ObjectBase::prop_setter_simple_type_func<int64_t>;
        case GI_TYPE_TAG_UINT64:
            return ObjectBase::prop_setter_simple_type_func<uint64_t>;
        case GI_TYPE_TAG_FLOAT:
            return ObjectBase::prop_setter_simple_type_func<float>;
        case GI_TYPE_TAG_DOUBLE:
            return ObjectBase::prop_setter_simple_type_func<double>;
        case GI_TYPE_TAG_GTYPE:
            return ObjectBase::prop_setter_simple_type_func<Gjs::Tag::GType>;
        case GI_TYPE_TAG_UNICHAR:
            return ObjectBase::prop_setter_simple_type_func<gunichar>;
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING) {
                return ObjectBase::prop_setter_simple_type_func<
                    char*, GI_TRANSFER_NOTHING>;
            } else {
                return ObjectBase::prop_setter_simple_type_func<
                    char*, GI_TRANSFER_EVERYTHING>;
            }
        default:
            return nullptr;
    }
}

// Wrap a call to JS::NewObjectWithStashedPointer() while ensuring the pointer
// is properly deleted if the call fails.
template <typename T, typename... Ts>
GJS_JSAPI_RETURN_CONVENTION static inline JSObject*
new_object_with_stashed_pointer(JSContext* cx, Ts... args) {
    std::unique_ptr<T> data = std::make_unique<T>(args...);
    JSObject* obj = JS::NewObjectWithStashedPointer(
        cx, data.get(), [](T* data) { delete data; });
    if (obj)
        data.release();
    return obj;
}

GJS_JSAPI_RETURN_CONVENTION
static JSNative create_getter_invoker(JSContext* cx, GParamSpec* pspec,
                                      const GI::FunctionInfo getter,
                                      const GI::TypeInfo type,
                                      JS::MutableHandleValue wrapper_out) {
    JS::RootedObject wrapper{cx};

    GITransfer transfer = getter.caller_owns();
    JSNative js_getter = get_getter_for_type(type, transfer);

    Gjs::GErrorResult<> init_result{Ok{}};
    if (js_getter) {
        wrapper = new_object_with_stashed_pointer<ObjectPropertyPspecCaller>(
            cx, pspec);
        if (!wrapper)
            return nullptr;
        auto* caller =
            JS::ObjectGetStashedPointer<ObjectPropertyPspecCaller>(cx, wrapper);
        init_result = caller->init(getter);
    } else {
        wrapper = new_object_with_stashed_pointer<ObjectPropertyInfoCaller>(
            cx, getter);
        if (!wrapper)
            return nullptr;
        js_getter = &ObjectBase::prop_getter_func;
        auto* caller =
            JS::ObjectGetStashedPointer<ObjectPropertyInfoCaller>(cx, wrapper);
        init_result = caller->init();
    }

    if (init_result.isErr()) {
        gjs_throw(cx, "Impossible to create invoker for %s: %s", getter.name(),
                  init_result.inspectErr()->message);
        return nullptr;
    }

    wrapper_out.setObject(*wrapper);
    return js_getter;
}

// We cannot use g_base_info_equal because the GITypeInfo of properties is
// not marked as a pointer in GIR files, while it is marked as a pointer in the
// return type of the associated getter, or the argument type of the associated
// setter. Also, there isn't a GParamSpec for integers of specific widths, there
// is only int and long, whereas the corresponding getter may return a specific
// width of integer.
[[nodiscard]]
static bool type_info_compatible(const GI::TypeInfo func_type,
                                 const GI::TypeInfo prop_type) {
    GITypeTag tag = prop_type.tag();
    GITypeTag func_tag = func_type.tag();

    if (GI_TYPE_TAG_IS_BASIC(tag)) {
        if (func_type.is_pointer() != prop_type.is_pointer())
            return false;
    }
    switch (tag) {
        case GI_TYPE_TAG_VOID:     // g_param_spec_param
        case GI_TYPE_TAG_BOOLEAN:  // g_param_spec_boolean
        case GI_TYPE_TAG_INT8:     // g_param_spec_char
        case GI_TYPE_TAG_DOUBLE:   // g_param_spec_double
        case GI_TYPE_TAG_FLOAT:    // g_param_spec_float
        case GI_TYPE_TAG_GTYPE:    // g_param_spec_gtype
        case GI_TYPE_TAG_UINT8:    // g_param_spec_uchar
        case GI_TYPE_TAG_UNICHAR:  // g_param_spec_unichar
        case GI_TYPE_TAG_ERROR:    // would be g_param_spec_boxed?
            return func_tag == tag;
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_INT64:
            // g_param_spec_int, g_param_spec_long, or g_param_spec_int64
            return func_tag == GI_TYPE_TAG_INT8 ||
                   func_tag == GI_TYPE_TAG_INT16 ||
                   func_tag == GI_TYPE_TAG_INT32 ||
                   func_tag == GI_TYPE_TAG_INT64;
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_UINT64:
            // g_param_spec_uint, g_param_spec_ulong, or g_param_spec_uint64
            return func_tag == GI_TYPE_TAG_UINT8 ||
                   func_tag == GI_TYPE_TAG_UINT16 ||
                   func_tag == GI_TYPE_TAG_UINT32 ||
                   func_tag == GI_TYPE_TAG_UINT64;
        case GI_TYPE_TAG_UTF8:  // g_param_spec_string
            return func_tag == tag || func_tag == GI_TYPE_TAG_FILENAME;
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_FILENAME:
            g_return_val_if_reached(false);  // never occurs as GParamSpec type
        // everything else
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
            return func_tag == tag &&
                   func_type.element_type() == prop_type.element_type();
        case GI_TYPE_TAG_ARRAY:
            return func_tag == tag &&
                   func_type.element_type() == prop_type.element_type() &&
                   func_type.is_zero_terminated() ==
                       prop_type.is_zero_terminated() &&
                   func_type.array_fixed_size() ==
                       prop_type.array_fixed_size() &&
                   func_type.array_type() == prop_type.array_type();
        case GI_TYPE_TAG_GHASH:
            return func_tag == tag &&
                   func_type.key_type() == prop_type.key_type() &&
                   func_type.value_type() == prop_type.value_type();
        case GI_TYPE_TAG_INTERFACE:
            return func_tag == tag &&
                   func_type.interface() == prop_type.interface();
    }
    g_return_val_if_reached(false);
}

GJS_JSAPI_RETURN_CONVENTION
static JSNative get_getter_for_property(
    JSContext* cx, GParamSpec* pspec, Maybe<GI::AutoPropertyInfo> property_info,
    JS::MutableHandleValue priv_out) {
    if (!(pspec->flags & G_PARAM_READABLE)) {
        priv_out.setUndefined();
        return &ObjectBase::prop_getter_write_only;
    }

    if (property_info) {
        Maybe<GI::AutoFunctionInfo> prop_getter{property_info->getter()};

        if (prop_getter && prop_getter->is_method() &&
            prop_getter->n_args() == 0 && !prop_getter->skip_return()) {
            GI::StackTypeInfo return_type;
            prop_getter->load_return_type(&return_type);
            GI::AutoTypeInfo prop_type{property_info->type_info()};

            if (G_LIKELY(type_info_compatible(return_type, prop_type))) {
                return create_getter_invoker(cx, pspec, *prop_getter,
                                             return_type, priv_out);
            } else {
                Maybe<GI::BaseInfo> container = prop_getter->container();
                g_warning(
                    "Type %s of property %s.%s::%s does not match return type "
                    "%s of getter %s. Falling back to slow path",
                    prop_type.type_string(), container->ns(), container->name(),
                    property_info->name(), return_type.type_string(),
                    prop_getter->name());
                // fall back to GValue below
            }
        }
    }

    priv_out.setPrivate(pspec);
    switch (pspec->value_type) {
        case G_TYPE_BOOLEAN:
            return &ObjectBase::prop_getter<Gjs::Tag::GBoolean>;
        case G_TYPE_INT:
            return &ObjectBase::prop_getter<int>;
        case G_TYPE_UINT:
            return &ObjectBase::prop_getter<unsigned int>;
        case G_TYPE_CHAR:
            return &ObjectBase::prop_getter<signed char>;
        case G_TYPE_UCHAR:
            return &ObjectBase::prop_getter<unsigned char>;
        case G_TYPE_INT64:
            return &ObjectBase::prop_getter<int64_t>;
        case G_TYPE_UINT64:
            return &ObjectBase::prop_getter<uint64_t>;
        case G_TYPE_FLOAT:
            return &ObjectBase::prop_getter<float>;
        case G_TYPE_DOUBLE:
            return &ObjectBase::prop_getter<double>;
        case G_TYPE_STRING:
            return &ObjectBase::prop_getter<char*>;
        case G_TYPE_LONG:
            return &ObjectBase::prop_getter<Gjs::Tag::Long>;
        case G_TYPE_ULONG:
            return &ObjectBase::prop_getter<Gjs::Tag::UnsignedLong>;
        default:
            return &ObjectBase::prop_getter<>;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static JSNative create_setter_invoker(JSContext* cx, GParamSpec* pspec,
                                      const GI::FunctionInfo setter,
                                      const GI::ArgInfo value_arg,
                                      const GI::TypeInfo type,
                                      JS::MutableHandleValue wrapper_out) {
    JS::RootedObject wrapper{cx};

    GITransfer transfer = value_arg.ownership_transfer();
    JSNative js_setter = get_setter_for_type(type, transfer);

    Gjs::GErrorResult<> init_result{Ok{}};
    if (js_setter) {
        wrapper = new_object_with_stashed_pointer<ObjectPropertyPspecCaller>(
            cx, pspec);
        if (!wrapper)
            return nullptr;
        auto* caller =
            JS::ObjectGetStashedPointer<ObjectPropertyPspecCaller>(cx, wrapper);
        init_result = caller->init(setter);
    } else {
        wrapper = new_object_with_stashed_pointer<ObjectPropertyInfoCaller>(
            cx, setter);
        if (!wrapper)
            return nullptr;
        js_setter = &ObjectBase::prop_setter_func;
        auto* caller =
            JS::ObjectGetStashedPointer<ObjectPropertyInfoCaller>(cx, wrapper);
        init_result = caller->init();
    }

    if (init_result.isErr()) {
        gjs_throw(cx, "Impossible to create invoker for %s: %s", setter.name(),
                  init_result.inspectErr()->message);
        return nullptr;
    }

    wrapper_out.setObject(*wrapper);
    return js_setter;
}

GJS_JSAPI_RETURN_CONVENTION
static JSNative get_setter_for_property(
    JSContext* cx, GParamSpec* pspec, Maybe<GI::AutoPropertyInfo> property_info,
    JS::MutableHandleValue priv_out) {
    if (!(pspec->flags & G_PARAM_WRITABLE)) {
        priv_out.setPrivate(pspec);
        return &ObjectBase::prop_setter_read_only;
    }

    if (property_info) {
        Maybe<GI::AutoFunctionInfo> prop_setter{property_info->setter()};

        if (prop_setter && prop_setter->is_method() &&
            prop_setter->n_args() == 1) {
            GI::StackArgInfo value_arg;
            prop_setter->load_arg(0, &value_arg);
            GI::StackTypeInfo type_info;
            value_arg.load_type(&type_info);
            GI::AutoTypeInfo prop_type{property_info->type_info()};

            if (G_LIKELY(type_info_compatible(type_info, prop_type))) {
                return create_setter_invoker(cx, pspec, *prop_setter, value_arg,
                                             type_info, priv_out);
            } else {
                Maybe<GI::BaseInfo> container = prop_setter->container();
                g_warning(
                    "Type %s of property %s.%s::%s does not match type %s of "
                    "first argument of setter %s. Falling back to slow path",
                    prop_type.type_string(), container->ns(), container->name(),
                    property_info->name(), type_info.type_string(),
                    prop_setter->name());
                // fall back to GValue below
            }
        }
    }

    priv_out.setPrivate(pspec);
    switch (pspec->value_type) {
        case G_TYPE_BOOLEAN:
            return &ObjectBase::prop_setter<Gjs::Tag::GBoolean>;
        case G_TYPE_INT:
            return &ObjectBase::prop_setter<int>;
        case G_TYPE_UINT:
            return &ObjectBase::prop_setter<unsigned int>;
        case G_TYPE_CHAR:
            return &ObjectBase::prop_setter<signed char>;
        case G_TYPE_UCHAR:
            return &ObjectBase::prop_setter<unsigned char>;
        case G_TYPE_INT64:
            return &ObjectBase::prop_setter<int64_t>;
        case G_TYPE_UINT64:
            return &ObjectBase::prop_setter<uint64_t>;
        case G_TYPE_FLOAT:
            return &ObjectBase::prop_setter<float>;
        case G_TYPE_DOUBLE:
            return &ObjectBase::prop_setter<double>;
        case G_TYPE_STRING:
            return &ObjectBase::prop_setter<char*>;
        case G_TYPE_LONG:
            return &ObjectBase::prop_setter<Gjs::Tag::Long>;
        case G_TYPE_ULONG:
            return &ObjectBase::prop_setter<Gjs::Tag::UnsignedLong>;
        default:
            return &ObjectBase::prop_setter<>;
    }
}

bool ObjectPrototype::lazy_define_gobject_property(
    JSContext* cx, JS::HandleObject obj, JS::HandleId id, GParamSpec* pspec,
    bool* resolved, const char* name,
    Maybe<const GI::AutoPropertyInfo> property_info) {
    JS::RootedId canonical_id{cx};
    JS::Rooted<JS::PropertyDescriptor> canonical_desc{cx};

    // Make property configurable so that interface properties can be
    // overridden by GObject.ParamSpec.override in the class that
    // implements them
    unsigned flags = GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT;

    if (!g_str_equal(pspec->name, name)) {
        canonical_id = gjs_intern_string_to_id(cx, pspec->name);

        JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> desc{cx};
        if (!JS_GetOwnPropertyDescriptorById(cx, obj, canonical_id, &desc))
            return false;

        if (desc.isSome()) {
            debug_jsprop("Defining alias GObject property", id, obj);
            canonical_desc = *desc;
            if (!JS_DefinePropertyById(cx, obj, id, canonical_desc))
                return false;

            *resolved = true;
            return true;
        }
    }

    debug_jsprop("Defining lazy GObject property", id, obj);

    if (!(pspec->flags & (G_PARAM_WRITABLE | G_PARAM_READABLE))) {
        if (!JS_DefinePropertyById(cx, obj, id, JS::UndefinedHandleValue,
                                   flags))
            return false;

        if (!canonical_id.isVoid() &&
            !JS_DefinePropertyById(cx, obj, canonical_id,
                                   JS::UndefinedHandleValue, flags))
            return false;

        *resolved = true;
        return true;
    }

    // Do not fetch JS overridden properties from GObject, to avoid
    // infinite recursion.
    if (g_param_spec_get_qdata(pspec, ObjectBase::custom_property_quark())) {
        *resolved = false;
        return true;
    }

    JS::RootedValue getter_priv{cx};
    JSNative js_getter =
        get_getter_for_property(cx, pspec, property_info, &getter_priv);
    if (!js_getter)
        return false;

    JS::RootedValue setter_priv{cx};
    JSNative js_setter =
        get_setter_for_property(cx, pspec, property_info, &setter_priv);
    if (!js_setter)
        return false;

    if (!gjs_define_property_dynamic(cx, obj, name, id, "gobject_prop",
                                     js_getter, getter_priv, js_setter,
                                     setter_priv, flags))
        return false;

    if G_UNLIKELY (!canonical_id.isVoid()) {
        debug_jsprop("Defining alias GObject property", canonical_id, obj);

        if (!JS_DefinePropertyById(cx, obj, canonical_id, canonical_desc))
            return false;
    }

    *resolved = true;
    return true;
}

// An object shared by the getter and setter to store the interface' prototype
// and overrides.
static constexpr size_t ACCESSOR_SLOT = 0;

static bool interface_getter(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedValue v_accessor(
        cx, js::GetFunctionNativeReserved(&args.callee(), ACCESSOR_SLOT));
    g_assert(v_accessor.isObject() && "accessor must be an object");
    JS::RootedObject accessor(cx, &v_accessor.toObject());

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

    // Check if an override value has been set
    bool has_override_symbol = false;
    if (!JS_HasPropertyById(cx, accessor, atoms.override(),
                            &has_override_symbol))
        return false;

    if (has_override_symbol) {
        JS::RootedValue v_override_symbol(cx);
        if (!JS_GetPropertyById(cx, accessor, atoms.override(),
                                &v_override_symbol))
            return false;
        g_assert(v_override_symbol.isSymbol() &&
                 "override symbol must be a symbol");
        JS::RootedSymbol override_symbol(cx, v_override_symbol.toSymbol());
        JS::RootedId override_id(cx, JS::PropertyKey::Symbol(override_symbol));

        JS::RootedObject this_obj(cx);
        if (!args.computeThis(cx, &this_obj))
            return false;

        bool has_override = false;
        if (!JS_HasPropertyById(cx, this_obj, override_id, &has_override))
            return false;

        if (has_override)
            return JS_GetPropertyById(cx, this_obj, override_id, args.rval());
    }

    JS::RootedValue v_prototype(cx);
    if (!JS_GetPropertyById(cx, accessor, atoms.prototype(), &v_prototype))
        return false;
    g_assert(v_prototype.isObject() && "prototype must be an object");

    JS::RootedObject prototype(cx, &v_prototype.toObject());
    JS::RootedFunction fn_obj{cx, JS_GetObjectFunction(&args.callee())};
    JS::RootedString fn_name{cx};
    if (!JS_GetFunctionId(cx, fn_obj, &fn_name))
        return false;
    JS::RootedId id{cx, JS::PropertyKey::NonIntAtom(fn_name)};
    return JS_GetPropertyById(cx, prototype, id, args.rval());
}

static bool interface_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedValue v_accessor(
        cx, js::GetFunctionNativeReserved(&args.callee(), ACCESSOR_SLOT));
    JS::RootedObject accessor(cx, &v_accessor.toObject());
    JS::RootedString description(
        cx, JS_AtomizeAndPinString(cx, "Private interface function setter"));
    JS::RootedSymbol symbol(cx, JS::NewSymbol(cx, description));
    JS::RootedValue v_symbol(cx, JS::SymbolValue(symbol));

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_SetPropertyById(cx, accessor, atoms.override(), v_symbol))
        return false;

    args.rval().setUndefined();

    JS::RootedObject this_obj(cx);
    if (!args.computeThis(cx, &this_obj))
        return false;
    JS::RootedId override_id(cx, JS::PropertyKey::Symbol(symbol));

    return JS_SetPropertyById(cx, this_obj, override_id, args[0]);
}

static bool resolve_on_interface_prototype(JSContext* cx,
                                           const GI::InterfaceInfo iface_info,
                                           JS::HandleId identifier,
                                           JS::HandleObject class_prototype,
                                           bool* found) {
    JS::RootedObject interface_prototype(
        cx, gjs_lookup_object_prototype_from_info(cx, Some(iface_info),
                                                  iface_info.gtype()));
    if (!interface_prototype)
        return false;

    bool exists = false;
    if (!JS_HasPropertyById(cx, interface_prototype, identifier, &exists))
        return false;

    // If the property doesn't exist on the interface prototype, we don't need
    // to perform this trick.
    if (!exists) {
        *found = false;
        return true;
    }

    // Lazily define a property on the class prototype if a property
    // of that name is present on an interface prototype that the class
    // implements.
    //
    // Define a property of the same name on the class prototype, with a
    // getter and setter. This is so that e.g. file.dup() calls the _current_
    // value of Gio.File.prototype.dup(), not the original, so that it can be
    // overridden (or monkeypatched).
    //
    // The setter (interface_setter() above) marks the property as overridden if
    // it is set from user code. The getter (interface_getter() above) proxies
    // the interface prototype's property, unless it was marked as overridden.
    //
    // Store the identifier in the getter and setter function's ID slots for
    // to enable looking up the original value on the interface prototype.
    JS::RootedObject getter(
        cx, JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
                cx, interface_getter, 0, 0, identifier)));
    if (!getter)
        return false;

    JS::RootedObject setter(
        cx, JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
                cx, interface_setter, 1, 0, identifier)));
    if (!setter)
        return false;

    JS::RootedObject accessor(cx, JS_NewPlainObject(cx));
    if (!accessor)
        return false;

    js::SetFunctionNativeReserved(setter, ACCESSOR_SLOT,
                                  JS::ObjectValue(*accessor));
    js::SetFunctionNativeReserved(getter, ACCESSOR_SLOT,
                                  JS::ObjectValue(*accessor));

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue v_prototype(cx, JS::ObjectValue(*interface_prototype));
    if (!JS_SetPropertyById(cx, accessor, atoms.prototype(), v_prototype))
        return false;

    // Create a new descriptor with our getter and setter, that is configurable
    // and enumerable, because GObject may need to redefine it later.
    JS::PropertyAttributes attrs{JS::PropertyAttribute::Configurable,
                                 JS::PropertyAttribute::Enumerable};
    JS::Rooted<JS::PropertyDescriptor> desc(
        cx, JS::PropertyDescriptor::Accessor(getter, setter, attrs));

    if (!JS_DefinePropertyById(cx, class_prototype, identifier, desc))
        return false;

    *found = true;
    return true;
}

bool ObjectPrototype::resolve_no_info(JSContext* cx, JS::HandleObject obj,
                                      JS::HandleId id, bool* resolved,
                                      const char* name,
                                      ResolveWhat resolve_props) {
    Gjs::AutoChar canonical_name;
    if (resolve_props == ConsiderMethodsAndProperties) {
        // Optimization: GObject property names must start with a letter
        if (g_ascii_isalpha(name[0])) {
            canonical_name = gjs_hyphen_from_camel(name);
            canonicalize_key(canonical_name);
        }
    }

    mozilla::Span<const GI::InterfaceInfo> interfaces =
        GI::Repository{}.object_get_gtype_interfaces(m_gtype);

    /* Fallback to GType system for non custom GObjects with no GI information
     */
    if (canonical_name && G_TYPE_IS_CLASSED(m_gtype) && !is_custom_js_class()) {
        Gjs::AutoTypeClass<GObjectClass> oclass{m_gtype};

        if (GParamSpec* pspec =
                g_object_class_find_property(oclass, canonical_name))
            return lazy_define_gobject_property(cx, obj, id, pspec, resolved,
                                                name);
    }

    for (const GI::InterfaceInfo& iface_info : interfaces) {
        Maybe<GI::AutoFunctionInfo> method_info{iface_info.method(name)};
        if (method_info && method_info->is_method()) {
            bool found = false;
            if (!resolve_on_interface_prototype(cx, iface_info, id, obj,
                                                &found))
                return false;

            // Fallback to defining the function from type info...
            if (!found &&
                !gjs_define_function(cx, obj, m_gtype, method_info.ref()))
                return false;

            *resolved = true;
            return true;
        }

        if (!resolve_on_interface_prototype(cx, iface_info, id, obj, resolved))
            return false;
        if (*resolved)
            return true;
    }

    *resolved = false;
    return true;
}

[[nodiscard]]
static Maybe<GI::AutoPropertyInfo> find_gobject_property_info(
    const GI::ObjectInfo info, const char* name) {
    // Optimization: GObject property names must start with a letter
    if (!g_ascii_isalpha(name[0]))
        return {};

    Gjs::AutoChar canonical_name{gjs_hyphen_from_camel(name)};
    canonicalize_key(canonical_name);

    return get_gobject_property_info(info, canonical_name);
}

// Override of GIWrapperBase::id_is_never_lazy()
bool ObjectBase::id_is_never_lazy(jsid name, const GjsAtoms& atoms) {
    // Keep this list in sync with ObjectBase::proto_properties and
    // ObjectBase::proto_methods. However, explicitly do not include
    // connect() in it, because there are a few cases where the lazy property
    // should override the predefined one, such as Gio.Cancellable.connect().
    return name == atoms.init() || name == atoms.connect_after() ||
           name == atoms.emit();
}

bool ObjectPrototype::resolve_impl(JSContext* context, JS::HandleObject obj,
                                   JS::HandleId id, bool* resolved) {
    if (m_unresolvable_cache.has(id)) {
        *resolved = false;
        return true;
    }

    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(context, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    if (!uncached_resolve(context, obj, id, prop_name.get(), resolved))
        return false;

    if (!*resolved && !m_unresolvable_cache.putNew(id)) {
        JS_ReportOutOfMemory(context);
        return false;
    }

    return true;
}

bool ObjectPrototype::uncached_resolve(JSContext* context, JS::HandleObject obj,
                                       JS::HandleId id, const char* name,
                                       bool* resolved) {
    bool found = false;
    if (!JS_AlreadyHasOwnPropertyById(context, obj, id, &found))
        return false;

    if (found) {
        // Already defined, so *resolved = false because we didn't just define
        // it
        *resolved = false;
        return true;
    }

    // If we have no GIRepository information (we're a JS GObject subclass or an
    // internal non-introspected class such as GLocalFile), we need to look at
    // exposing interfaces. Look up our interfaces through GType data, and then
    // hope that *those* are introspectable.
    if (!info())
        return resolve_no_info(context, obj, id, resolved, name,
                               ConsiderMethodsAndProperties);

    if (g_str_has_prefix(name, "vfunc_")) {
        /* The only time we find a vfunc info is when we're the base
         * class that defined the vfunc. If we let regular prototype
         * chaining resolve this, we'd have the implementation for the base's
         * vfunc on the base class, without any other "real" implementations
         * in the way. If we want to expose a "real" vfunc implementation,
         * we need to go down to the parent infos and look at their VFuncInfos.
         *
         * This is good, but it's memory-hungry -- we would define every
         * possible vfunc on every possible object, even if it's the same
         * "real" vfunc underneath. Instead, only expose vfuncs that are
         * different from their parent, and let prototype chaining do the
         * rest.
         */

        const char *name_without_vfunc_ = &(name[6]);  /* lifetime tied to name */
        bool defined_by_parent;
        Maybe<GI::AutoVFuncInfo> vfunc{find_vfunc_on_parents(
            m_info.ref(), name_without_vfunc_, &defined_by_parent)};
        if (vfunc) {
            /* In the event that the vfunc is unchanged, let regular
             * prototypal inheritance take over. */
            if (defined_by_parent && is_vfunc_unchanged(vfunc.ref())) {
                *resolved = false;
                return true;
            }

            if (!gjs_define_function(context, obj, m_gtype, vfunc.ref()))
                return false;

            *resolved = true;
            return true;
        }

        /* If the vfunc wasn't found, fall through, back to normal
         * method resolution. */
    }

    if (Maybe<GI::AutoPropertyInfo> property_info =
            find_gobject_property_info(m_info.ref(), name)) {
        Gjs::AutoTypeClass<GObjectClass> gobj_class{m_gtype};
        if (GParamSpec* pspec =
                g_object_class_find_property(gobj_class, property_info->name()))
            return lazy_define_gobject_property(context, obj, id, pspec,
                                                resolved, name, property_info);
    }

    Maybe<GI::AutoFieldInfo> field_info{lookup_field_info(m_info.ref(), name)};
    if (field_info) {
        debug_jsprop("Defining lazy GObject field", id, obj);

        unsigned flags = GJS_MODULE_PROP_FLAGS;
        if (!field_info->is_writable())
            flags |= JSPROP_READONLY;

        JS::RootedObject rooted_field{
            context, new_object_with_stashed_pointer<GI::AutoFieldInfo>(
                         context, field_info.extract())};
        JS::RootedValue private_value{context, JS::ObjectValue(*rooted_field)};
        if (!gjs_define_property_dynamic(
                context, obj, name, id, "gobject_field",
                &ObjectBase::field_getter, &ObjectBase::field_setter,
                private_value, flags))
            return false;

        *resolved = true;
        return true;
    }

    /* find_method does not look at methods on parent classes,
     * we rely on javascript to walk up the __proto__ chain
     * and find those and define them in the right prototype.
     *
     * Note that if it isn't a method on the object, since JS
     * lacks multiple inheritance, we're sticking the iface
     * methods in the object prototype, which means there are many
     * copies of the iface methods (one per object class node that
     * introduces the iface)
     */

    auto result = m_info->find_method_using_interfaces(name);

    /**
     * Search through any interfaces implemented by the GType;
     * See https://bugzilla.gnome.org/show_bug.cgi?id=632922
     * for background on why we need to do this.
     */
    if (!result)
        return resolve_no_info(context, obj, id, resolved, name,
                               ConsiderOnlyMethods);

    GI::AutoFunctionInfo method_info{result->first};
    GI::AutoRegisteredTypeInfo implementor_info{result->second};

    method_info.log_usage();

    if (method_info.is_method()) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for %s (%s)",
                  method_info.name(), type_name(), format_name().c_str());
        if (auto iface_info = implementor_info.as<GI::InfoTag::INTERFACE>()) {
            bool found = false;
            if (!resolve_on_interface_prototype(context, iface_info.value(), id,
                                                obj, &found))
                return false;

            // If the method was not found fallback to defining the function
            // from type info...
            if (!found &&
                !gjs_define_function(context, obj, m_gtype, method_info)) {
                return false;
            }
        } else if (!gjs_define_function(context, obj, m_gtype, method_info)) {
            return false;
        }

        *resolved = true; /* we defined the prop in obj */
    }

    return true;
}

bool ObjectPrototype::new_enumerate_impl(JSContext* cx, JS::HandleObject,
                                         JS::MutableHandleIdVector properties,
                                         bool only_enumerable
                                         [[maybe_unused]]) {
    unsigned n_interfaces;
    GType* interfaces = g_type_interfaces(gtype(), &n_interfaces);
    GI::Repository repo;

    for (unsigned k = 0; k < n_interfaces; k++) {
        Maybe<GI::AutoInterfaceInfo> iface_info{
            repo.find_by_gtype<GI::InfoTag::INTERFACE>(interfaces[k])};
        if (!iface_info)
            continue;

        GI::InterfaceInfo::MethodsIterator meth_iter = iface_info->methods();
        GI::InterfaceInfo::PropertiesIterator props_iter =
            iface_info->properties();
        if (!properties.reserve(properties.length() + meth_iter.size() +
                                props_iter.size())) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        // Methods
        for (GI::AutoFunctionInfo meth_info : meth_iter) {
            if (meth_info.is_method()) {
                const char* name = meth_info.name();
                jsid id = gjs_intern_string_to_id(cx, name);
                if (id.isVoid())
                    return false;
                properties.infallibleAppend(id);
            }
        }

        // Properties
        for (GI::AutoPropertyInfo prop_info : props_iter) {
            Gjs::AutoChar js_name{gjs_hyphen_to_underscore(prop_info.name())};

            jsid id = gjs_intern_string_to_id(cx, js_name);
            if (id.isVoid())
                return false;
            properties.infallibleAppend(id);
        }
    }

    g_free(interfaces);

    if (info()) {
        GI::ObjectInfo::MethodsIterator meth_iter = info()->methods();
        GI::ObjectInfo::PropertiesIterator props_iter = info()->properties();
        if (!properties.reserve(properties.length() + meth_iter.size() +
                                props_iter.size())) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        // Methods
        for (GI::AutoFunctionInfo meth_info : meth_iter) {
            if (meth_info.is_method()) {
                const char* name = meth_info.name();
                jsid id = gjs_intern_string_to_id(cx, name);
                if (id.isVoid())
                    return false;
                properties.infallibleAppend(id);
            }
        }

        // Properties
        for (GI::AutoPropertyInfo prop_info : props_iter) {
            Gjs::AutoChar js_name{gjs_hyphen_to_underscore(prop_info.name())};
            jsid id = gjs_intern_string_to_id(cx, js_name);
            if (id.isVoid())
                return false;
            properties.infallibleAppend(id);
        }
    }

    return true;
}

/* Set properties from args to constructor (args[0] is supposed to be
 * a hash) */
bool ObjectPrototype::props_to_g_parameters(
    JSContext* context, Gjs::AutoTypeClass<GObjectClass> const& object_class,
    JS::HandleObject props, std::vector<const char*>* names,
    AutoGValueVector* values) {
    size_t ix, length;
    JS::RootedId prop_id(context);
    JS::RootedValue value(context);
    JS::Rooted<JS::IdVector> ids(context, context);
    std::unordered_set<GParamSpec*> visited_params;
    if (!JS_Enumerate(context, props, &ids)) {
        gjs_throw(context, "Failed to create property iterator for object props hash");
        return false;
    }

    values->reserve(ids.length());
    for (ix = 0, length = ids.length(); ix < length; ix++) {
        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        prop_id = ids[ix];

        if (!prop_id.isString())
            return gjs_wrapper_throw_nonexistent_field(
                context, m_gtype, gjs_debug_id(prop_id).c_str());

        JS::RootedString js_prop_name(context, prop_id.toString());
        GParamSpec* param_spec =
            find_param_spec_from_id(context, object_class, js_prop_name);
        if (!param_spec)
            return false;

        if (visited_params.find(param_spec) != visited_params.end())
            continue;
        visited_params.insert(param_spec);

        if (!JS_GetPropertyById(context, props, prop_id, &value))
            return false;
        if (value.isUndefined()) {
            gjs_throw(context, "Invalid value 'undefined' for property %s in "
                      "object initializer.", param_spec->name);
            return false;
        }

        if (!(param_spec->flags & G_PARAM_WRITABLE))
            return gjs_wrapper_throw_readonly_field(context, m_gtype,
                                                    param_spec->name);
            /* prevent setting the prop even in JS */

        Gjs::AutoGValue& gvalue =
            values->emplace_back(G_PARAM_SPEC_VALUE_TYPE(param_spec));
        if (!gjs_value_to_g_value(context, value, &gvalue))
            return false;

        names->push_back(param_spec->name);  // owned by GParamSpec
    }

    return true;
}

void ObjectInstance::wrapped_gobj_dispose_notify(
    void* data, GObject* where_the_object_was GJS_USED_VERBOSE_LIFECYCLE) {
    auto *priv = static_cast<ObjectInstance *>(data);
    priv->gobj_dispose_notify();
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Wrapped GObject %p disposed",
                        where_the_object_was);
}

void ObjectInstance::track_gobject_finalization() {
    auto quark = ObjectBase::disposed_quark();
    g_object_steal_qdata(m_ptr, quark);
    g_object_set_qdata_full(m_ptr, quark, this, [](void* data) {
        auto* self = static_cast<ObjectInstance*>(data);
        self->m_gobj_finalized = true;
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Wrapped GObject %p finalized",
                            self->m_ptr.get());
    });
}

void ObjectInstance::ignore_gobject_finalization() {
    auto quark = ObjectBase::disposed_quark();
    if (g_object_get_qdata(m_ptr, quark) == this) {
        g_object_steal_qdata(m_ptr, quark);
        g_object_set_qdata(m_ptr, quark, gjs_int_to_pointer(DISPOSED_OBJECT));
    }
}

void
ObjectInstance::gobj_dispose_notify(void)
{
    m_gobj_disposed = true;

    unset_object_qdata();
    track_gobject_finalization();

    if (m_uses_toggle_ref) {
        g_object_ref(m_ptr.get());
        g_object_remove_toggle_ref(m_ptr, wrapped_gobj_toggle_notify, this);
        ToggleQueue::get_default()->cancel(this);
        wrapped_gobj_toggle_notify(this, m_ptr, TRUE);
        m_uses_toggle_ref = false;
    }

    if (GjsContextPrivate::from_current_context()->is_owner_thread())
        discard_wrapper();
}

void ObjectInstance::remove_wrapped_gobjects_if(
    const ObjectInstance::Predicate& predicate,
    const ObjectInstance::Action& action) {
    for (auto link = s_wrapped_gobject_list.begin(),
              last = s_wrapped_gobject_list.end();
         link != last;) {
        if (predicate(*link)) {
            action(*link);
            link = s_wrapped_gobject_list.erase(link);
            continue;
        }
        ++link;
    }
}

/*
 * ObjectInstance::context_dispose_notify:
 *
 * Callback called when the #GjsContext is disposed. It just calls
 * handle_context_dispose() on every ObjectInstance.
 */
void ObjectInstance::context_dispose_notify(void*, GObject* where_the_object_was
                                            [[maybe_unused]]) {
    std::for_each(s_wrapped_gobject_list.begin(), s_wrapped_gobject_list.end(),
        std::mem_fn(&ObjectInstance::handle_context_dispose));
}

/*
 * ObjectInstance::handle_context_dispose:
 *
 * Called on each existing ObjectInstance when the #GjsContext is disposed.
 */
void ObjectInstance::handle_context_dispose(void) {
    if (wrapper_is_rooted()) {
        debug_lifecycle("Was rooted, but unrooting due to GjsContext dispose");
        discard_wrapper();
    }
}

void
ObjectInstance::toggle_down(void)
{
    debug_lifecycle("Toggle notify DOWN");

    /* Change to weak ref so the wrapper-wrappee pair can be
     * collected by the GC
     */
    if (wrapper_is_rooted()) {
        debug_lifecycle("Unrooting wrapper");
        GjsContextPrivate* gjs = GjsContextPrivate::from_current_context();
        switch_to_unrooted(gjs->context());

        /* During a GC, the collector asks each object which other
         * objects that it wants to hold on to so if there's an entire
         * section of the heap graph that's not connected to anything
         * else, and not reachable from the root set, then it can be
         * trashed all at once.
         *
         * GObjects, however, don't work like that, there's only a
         * reference count but no notion of who owns the reference so,
         * a JS object that's wrapping a GObject is unconditionally held
         * alive as long as the GObject has >1 references.
         *
         * Since we cannot know how many more wrapped GObjects are going
         * be marked for garbage collection after the owner is destroyed,
         * always queue a garbage collection when a toggle reference goes
         * down.
         */
        if (!gjs->destroying())
            gjs->schedule_gc();
    }
}

void
ObjectInstance::toggle_up(void)
{
    if (G_UNLIKELY(!m_ptr || m_gobj_disposed || m_gobj_finalized)) {
        if (m_ptr) {
            gjs_debug_lifecycle(
                GJS_DEBUG_GOBJECT,
                "Avoid to toggle up a wrapper for a %s object: %p (%s)",
                m_gobj_finalized ? "finalized" : "disposed", m_ptr.get(),
                g_type_name(gtype()));
        } else {
            gjs_debug_lifecycle(
                GJS_DEBUG_GOBJECT,
                "Avoid to toggle up a wrapper for a released %s object (%p)",
                g_type_name(gtype()), this);
        }
        return;
    }

    /* We need to root the JSObject associated with the passed in GObject so it
     * doesn't get garbage collected (and lose any associated javascript state
     * such as custom properties).
     */
    if (!has_wrapper()) /* Object already GC'd */
        return;

    debug_lifecycle("Toggle notify UP");

    /* Change to strong ref so the wrappee keeps the wrapper alive
     * in case the wrapper has data in it that the app cares about
     */
    if (!wrapper_is_rooted()) {
        // FIXME: thread the context through somehow. Maybe by looking up the
        // realm that obj belongs to.
        debug_lifecycle("Rooting wrapper");
        auto* cx = GjsContextPrivate::from_current_context()->context();
        switch_to_rooted(cx);
    }
}

static void toggle_handler(ObjectInstance* self,
                           ToggleQueue::Direction direction) {
    switch (direction) {
        case ToggleQueue::UP:
            self->toggle_up();
            break;
        case ToggleQueue::DOWN:
            self->toggle_down();
            break;
        default:
            g_assert_not_reached();
    }
}

void ObjectInstance::wrapped_gobj_toggle_notify(void* instance, GObject*,
                                                gboolean is_last_ref) {
    bool is_main_thread;
    bool toggle_up_queued, toggle_down_queued;
    auto* self = static_cast<ObjectInstance*>(instance);

    GjsContextPrivate* gjs = GjsContextPrivate::from_current_context();
    if (gjs->destroying()) {
        /* Do nothing here - we're in the process of disassociating
         * the objects.
         */
        return;
    }

    /* We only want to touch javascript from one thread.
     * If we're not in that thread, then we need to defer processing
     * to it.
     * In case we're toggling up (and thus rooting the JS object) we
     * also need to take care if GC is running. The marking side
     * of it is taken care by JS::Heap, which we use in GjsMaybeOwned,
     * so we're safe. As for sweeping, it is too late: the JS object
     * is dead, and attempting to keep it alive would soon crash
     * the process. Plus, if we touch the JSAPI from another thread, libmozjs
     * aborts in most cases when in debug mode.
     * Thus, we drain the toggle queue when GC starts, in order to
     * prevent this from happening.
     * In practice, a toggle up during JS finalize can only happen
     * for temporary refs/unrefs of objects that are garbage anyway,
     * because JS code is never invoked while the finalizers run
     * and C code needs to clean after itself before it returns
     * from dispose()/finalize().
     * On the other hand, toggling down is a lot simpler, because
     * we're creating more garbage. So we just unroot the object, make it a
     * weak pointer, and wait for the next GC cycle.
     *
     * Note that one would think that toggling up only happens
     * in the main thread (because toggling up is the result of
     * the JS object, previously visible only to JS code, becoming
     * visible to the refcounted C world), but because of weird
     * weak singletons like g_bus_get_sync() objects can see toggle-ups
     * from different threads too.
     */
    is_main_thread = gjs->is_owner_thread();

    auto toggle_queue = ToggleQueue::get_default();
    std::tie(toggle_down_queued, toggle_up_queued) =
        toggle_queue->is_queued(self);
    bool anything_queued = toggle_up_queued || toggle_down_queued;

    if (is_last_ref) {
        /* We've transitions from 2 -> 1 references,
         * The JSObject is rooted and we need to unroot it so it
         * can be garbage collected
         */
        if (is_main_thread && !anything_queued) {
            self->toggle_down();
        } else {
            toggle_queue->enqueue(self, ToggleQueue::DOWN, toggle_handler);
        }
    } else {
        /* We've transitioned from 1 -> 2 references.
         *
         * The JSObject associated with the gobject is not rooted,
         * but it needs to be. We'll root it.
         */
        if (is_main_thread && !anything_queued &&
            !JS::RuntimeHeapIsCollecting()) {
            self->toggle_up();
        } else {
            toggle_queue->enqueue(self, ToggleQueue::UP, toggle_handler);
        }
    }
}

void
ObjectInstance::release_native_object(void)
{
    static GType gdksurface_type = 0;

    discard_wrapper();

    if (m_gobj_finalized) {
        g_critical(
            "Object %p of type %s has been finalized while it was still "
            "owned by gjs, this is due to invalid memory management.",
            m_ptr.get(), g_type_name(gtype()));
        m_ptr.release();
        return;
    }

    if (m_ptr)
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Releasing native object %s %p",
                            g_type_name(gtype()), m_ptr.get());

    if (m_gobj_disposed)
        ignore_gobject_finalization();

    if (m_uses_toggle_ref && !m_gobj_disposed) {
        g_object_remove_toggle_ref(m_ptr.release(), wrapped_gobj_toggle_notify,
                                   this);
        return;
    }

    // Unref the object. Handle any special cases for destruction here
    if (m_ptr->ref_count == 1) {
        // Quickest way to check for GdkSurface if Gdk has been loaded?
        // surface_type may be 0 if Gdk not loaded. The type may be a private
        // type and not have introspection info.
        if (!gdksurface_type)
            gdksurface_type = g_type_from_name("GdkSurface");
        if (gdksurface_type && g_type_is_a(gtype(), gdksurface_type)) {
            GObject* ptr = m_ptr.release();

            // Workaround for https://gitlab.gnome.org/GNOME/gtk/-/issues/6289
            GI::Repository repo;
            GI::AutoObjectInfo surface_info{
                repo.find_by_gtype<GI::InfoTag::OBJECT>(gdksurface_type)
                    .value()};
            GI::AutoFunctionInfo destroy_func{
                surface_info.method("destroy").value()};
            GIArgument destroy_args;
            gjs_arg_set(&destroy_args, ptr);
            GIArgument unused_return;

            auto result =
                destroy_func.invoke({{destroy_args}}, {}, &unused_return);
            if (result.isErr())
                g_critical("Error destroying GdkSurface %p: %s", ptr,
                           result.inspectErr()->message);
        }
    }

    m_ptr = nullptr;
}

/* At shutdown, we need to ensure we've cleared the context of any
 * pending toggle references.
 */
void
gjs_object_clear_toggles(void)
{
    ToggleQueue::get_default()->handle_all_toggles(toggle_handler);
}

void
gjs_object_shutdown_toggle_queue(void)
{
    ToggleQueue::get_default()->shutdown();
}

/*
 * ObjectInstance::prepare_shutdown:
 *
 * Called when the #GjsContext is disposed, in order to release all GC roots of
 * JSObjects that are held by GObjects.
 */
void ObjectInstance::prepare_shutdown(void) {
    /* We iterate over all of the objects, breaking the JS <-> C
     * association.  We avoid the potential recursion implied in:
     *   toggle ref removal -> gobj dispose -> toggle ref notify
     * by emptying the toggle queue earlier in the shutdown sequence. */
    ObjectInstance::remove_wrapped_gobjects_if(
        std::mem_fn(&ObjectInstance::wrapper_is_rooted),
        std::mem_fn(&ObjectInstance::release_native_object));
}

ObjectInstance::ObjectInstance(ObjectPrototype* prototype,
                               JS::HandleObject object)
    : GIWrapperInstance(prototype, object),
      m_wrapper_finalized(false),
      m_gobj_disposed(false),
      m_gobj_finalized(false),
      m_uses_toggle_ref(false) {
    GTypeQuery query;
    g_type_query(gtype(), &query);
    if (G_LIKELY(query.type))
        JS::AddAssociatedMemory(object, query.instance_size,
                                MemoryUse::GObjectInstanceStruct);

    GJS_INC_COUNTER(object_instance);
}

ObjectPrototype::ObjectPrototype(Maybe<GI::ObjectInfo> info, GType gtype)
    : GIWrapperPrototype(info, gtype) {
    g_type_class_ref(gtype);

    GJS_INC_COUNTER(object_prototype);
}

/*
 * ObjectInstance::update_heap_wrapper_weak_pointers:
 *
 * Private callback, called after the JS engine finishes garbage collection, and
 * notifies when weak pointers need to be either moved or swept.
 */
void ObjectInstance::update_heap_wrapper_weak_pointers(JSTracer* trc,
                                                       JS::Compartment*,
                                                       void*) {
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Weak pointer update callback, "
                        "%zu wrapped GObject(s) to examine",
                        ObjectInstance::num_wrapped_gobjects());

    // Take a lock on the queue till we're done with it, so that we don't
    // risk that another thread will queue something else while sweeping
    auto locked_queue = ToggleQueue::get_default();

    ObjectInstance::remove_wrapped_gobjects_if(
        [&trc](ObjectInstance* instance) -> bool {
            return instance->weak_pointer_was_finalized(trc);
        },
        std::mem_fn(&ObjectInstance::disassociate_js_gobject));
}

bool ObjectInstance::weak_pointer_was_finalized(JSTracer* trc) {
    if (has_wrapper() && !wrapper_is_rooted()) {
        bool toggle_down_queued, toggle_up_queued;

        auto toggle_queue = ToggleQueue::get_default();
        std::tie(toggle_down_queued, toggle_up_queued) =
            toggle_queue->is_queued(this);

        if (!toggle_down_queued && toggle_up_queued)
            return false;

        if (!update_after_gc(trc))
            return false;

        if (toggle_down_queued)
            toggle_queue->cancel(this);

        /* Ouch, the JS object is dead already. Disassociate the
         * GObject and hope the GObject dies too. (Remove it from
         * the weak pointer list first, since the disassociation
         * may also cause it to be erased.)
         */
        debug_lifecycle("Found GObject weak pointer whose JS wrapper is about "
                        "to be finalized");
        return true;
    }
    return false;
}

/*
 * ObjectInstance::ensure_weak_pointer_callback:
 *
 * Private method called when adding a weak pointer for the first time.
 */
void ObjectInstance::ensure_weak_pointer_callback(JSContext* cx) {
    if (!s_weak_pointer_callback) {
        JS_AddWeakPointerCompartmentCallback(
            cx, &ObjectInstance::update_heap_wrapper_weak_pointers, nullptr);
        s_weak_pointer_callback = true;
    }
}

void
ObjectInstance::associate_js_gobject(JSContext       *context,
                                     JS::HandleObject object,
                                     GObject         *gobj)
{
    g_assert(!wrapper_is_rooted());

    m_uses_toggle_ref = false;
    m_ptr = gobj;
    set_object_qdata();
    m_wrapper = object;
    m_gobj_disposed = !!g_object_get_qdata(gobj, ObjectBase::disposed_quark());

    ensure_weak_pointer_callback(context);
    link();

    if (!G_UNLIKELY(m_gobj_disposed))
        g_object_weak_ref(gobj, wrapped_gobj_dispose_notify, this);
}

void ObjectInstance::ensure_uses_toggle_ref(JSContext* cx) {
    if (m_uses_toggle_ref)
        return;

    if (!check_gobject_disposed_or_finalized("add toggle reference on"))
        return;

    debug_lifecycle("Switching object instance to toggle ref");

    g_assert(!wrapper_is_rooted());

    /* OK, here is where things get complicated. We want the
     * wrapped gobj to keep the JSObject* wrapper alive, because
     * people might set properties on the JSObject* that they care
     * about. Therefore, whenever the refcount on the wrapped gobj
     * is >1, i.e. whenever something other than the wrapper is
     * referencing the wrapped gobj, the wrapped gobj has a strong
     * ref (gc-roots the wrapper). When the refcount on the
     * wrapped gobj is 1, then we change to a weak ref to allow
     * the wrapper to be garbage collected (and thus unref the
     * wrappee).
     */
    m_uses_toggle_ref = true;
    switch_to_rooted(cx);
    g_object_add_toggle_ref(m_ptr, wrapped_gobj_toggle_notify, this);

    /* We now have both a ref and a toggle ref, we only want the toggle ref.
     * This may immediately remove the GC root we just added, since refcount
     * may drop to 1. */
    g_object_unref(m_ptr);
}

template <typename T>
static void invalidate_closure_collection(T* closures, void* data,
                                          GClosureNotify notify_func) {
    g_assert(closures);
    g_assert(notify_func);

    for (auto it = closures->begin(); it != closures->end();) {
        // This will also free the closure data, through the closure
        // invalidation mechanism, but adding a temporary reference to
        // ensure that the closure is still valid when calling invalidation
        // notify callbacks
        Gjs::AutoGClosure closure{*it, Gjs::TakeOwnership{}};
        it = closures->erase(it);

        // Only call the invalidate notifiers that won't touch this vector
        g_closure_remove_invalidate_notifier(closure, data, notify_func);
        g_closure_invalidate(closure);
    }

    g_assert(closures->empty());
}

// Note: m_wrapper (the JS object) may already be null when this is called, if
// it was finalized while the GObject was toggled down.
void
ObjectInstance::disassociate_js_gobject(void)
{
    bool had_toggle_down, had_toggle_up;

    std::tie(had_toggle_down, had_toggle_up) =
        ToggleQueue::get_default()->cancel(this);
    if (had_toggle_up && !had_toggle_down) {
        g_error(
            "JS object wrapper for GObject %p (%s) is being released while "
            "toggle references are still pending.",
            m_ptr.get(), type_name());
    }

    if (!m_gobj_disposed)
        g_object_weak_unref(m_ptr.get(), wrapped_gobj_dispose_notify, this);

    if (!m_gobj_finalized) {
        /* Fist, remove the wrapper pointer from the wrapped GObject */
        unset_object_qdata();
    }

    /* Now release all the resources the current wrapper has */
    invalidate_closures();
    release_native_object();

    /* Mark that a JS object once existed, but it doesn't any more */
    m_wrapper_finalized = true;
}

bool ObjectInstance::init_impl(JSContext* context, const JS::CallArgs& args,
                               JS::HandleObject object) {
    g_assert(gtype() != G_TYPE_NONE);

    if (args.length() > 1 &&
        !JS::WarnUTF8(context,
                      "Too many arguments to the constructor of %s: expected "
                      "1, got %u",
                      name(), args.length()))
        return false;

    Gjs::AutoTypeClass<GObjectClass> object_class{gtype()};
    std::vector<const char *> names;
    AutoGValueVector values;

    if (args.length() > 0 && !args[0].isUndefined()) {
        if (!args[0].isObject()) {
            gjs_throw(context,
                      "Argument to the constructor of %s should be a plain JS "
                      "object with properties to set",
                      name());
            return false;
        }

        JS::RootedObject props(context, &args[0].toObject());
        if (ObjectBase::for_js(context, props)) {
            gjs_throw(context,
                      "Argument to the constructor of %s should be a plain JS "
                      "object with properties to set",
                      name());
            return false;
        }
        if (!m_proto->props_to_g_parameters(context, object_class, props,
                                            &names, &values))
            return false;
    }

    if (G_TYPE_IS_ABSTRACT(gtype())) {
        gjs_throw(context,
                  "Cannot instantiate abstract type %s", g_type_name(gtype()));
        return false;
    }

    // Mark this object in the construction stack, it will be popped in
    // gjs_object_custom_init() in gi/gobject.cpp.
    if (is_custom_js_class()) {
        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
        if (!gjs->object_init_list().append(object)) {
            JS_ReportOutOfMemory(context);
            return false;
        }
    }

    g_assert(names.size() == values.size());
    GObject* gobj = g_object_new_with_properties(gtype(), values.size(),
                                                 names.data(), values.data());

    ObjectInstance *other_priv = ObjectInstance::for_gobject(gobj);
    if (other_priv && other_priv->m_wrapper != object.get()) {
        /* g_object_new_with_properties() returned an object that's already
         * tracked by a JS object.
         *
         * This typically occurs in one of two cases:
         * - This object is a singleton like IBus.IBus
         * - This object passed itself to JS before g_object_new_* returned
         *
         * In these cases, return the existing JS wrapper object instead
         * of creating a new one.
         *
         * 'object' has a value that was originally created by
         * JS_NewObjectForConstructor in GJS_NATIVE_CONSTRUCTOR_PRELUDE, but
         * we're not actually using it, so just let it get collected. Avoiding
         * this would require a non-trivial amount of work.
         * */
        bool toggle_ref_added = false;
        if (!m_uses_toggle_ref) {
            other_priv->ensure_uses_toggle_ref(context);
            toggle_ref_added = m_uses_toggle_ref;
        }

        args.rval().setObject(*other_priv->m_wrapper.get());

        if (toggle_ref_added)
            g_clear_object(&gobj); /* We already own a reference */
        return true;
    }

    if (G_IS_INITIALLY_UNOWNED(gobj) &&
        !g_object_is_floating(gobj)) {
        /* GtkWindow does not return a ref to caller of g_object_new.
         * Need a flag in gobject-introspection to tell us this.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Newly-created object is initially unowned but we did not get the "
                  "floating ref, probably GtkWindow, using hacky workaround");
        g_object_ref(gobj);
    } else if (g_object_is_floating(gobj)) {
        g_object_ref_sink(gobj);
    } else {
        /* we should already have a ref */
    }

    if (!m_ptr)
        associate_js_gobject(context, object, gobj);

    TRACE(GJS_OBJECT_WRAPPER_NEW(this, m_ptr, ns(), name()));

    args.rval().setObject(*object);
    return true;
}

// See GIWrapperBase::constructor()
bool ObjectInstance::constructor_impl(JSContext* context,
                                      JS::HandleObject object,
                                      const JS::CallArgs& argv) {
    JS::RootedValue initer(context);
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    const auto& new_target = argv.newTarget();
    bool has_gtype;

    g_assert(new_target.isObject() && "new.target needs to be an object");
    JS::RootedObject rooted_target(context, &new_target.toObject());
    if (!JS_HasOwnPropertyById(context, rooted_target, gjs->atoms().gtype(),
                               &has_gtype))
        return false;

    if (!has_gtype) {
        gjs_throw(context,
                  "Tried to construct an object without a GType; are "
                  "you using GObject.registerClass() when inheriting "
                  "from a GObject type?");
        return false;
    }

    return gjs_object_require_property(context, object, "GObject instance",
                                       gjs->atoms().init(), &initer) &&
           gjs->call_function(object, initer, argv, argv.rval());
}

void ObjectInstance::trace_impl(JSTracer* tracer) {
    for (GClosure *closure : m_closures)
        Gjs::Closure::for_gclosure(closure)->trace(tracer);
}

void ObjectPrototype::trace_impl(JSTracer* tracer) {
    m_unresolvable_cache.trace(tracer);
    for (GClosure* closure : m_vfuncs)
        Gjs::Closure::for_gclosure(closure)->trace(tracer);
}

void ObjectInstance::finalize_impl(JS::GCContext* gcx, JSObject* obj) {
    GTypeQuery query;
    g_type_query(gtype(), &query);
    if (G_LIKELY(query.type))
        JS::RemoveAssociatedMemory(obj, query.instance_size,
                                   MemoryUse::GObjectInstanceStruct);

    GIWrapperInstance::finalize_impl(gcx, obj);
}

ObjectInstance::~ObjectInstance() {
    TRACE(GJS_OBJECT_WRAPPER_FINALIZE(this, m_ptr, ns(), name()));

    invalidate_closures();

    // Do not keep the queue locked here, as we may want to leave the other
    // threads to queue toggle events till we're owning the GObject so that
    // eventually (once the toggle reference is finally removed) we can be
    // sure that no other toggle event will target this (soon dead) wrapper.
    bool had_toggle_up;
    bool had_toggle_down;
    std::tie(had_toggle_down, had_toggle_up) =
        ToggleQueue::get_default()->cancel(this);

    /* GObject is not already freed */
    if (m_ptr) {
        if (!had_toggle_up && had_toggle_down) {
            g_error(
                "Finalizing wrapper for an object that's scheduled to be "
                "unrooted: %s",
                format_name().c_str());
        }

        if (!m_gobj_disposed)
            g_object_weak_unref(m_ptr, wrapped_gobj_dispose_notify, this);

        if (!m_gobj_finalized)
            unset_object_qdata();

        bool was_using_toggle_refs = m_uses_toggle_ref;
        release_native_object();

        if (was_using_toggle_refs) {
            // We need to cancel again, to be sure that no other thread added
            // another toggle reference before we were removing the last one.
            ToggleQueue::get_default()->cancel(this);
        }
    }

    if (wrapper_is_rooted()) {
        /* This happens when the refcount on the object is still >1,
         * for example with global objects GDK never frees like GdkDisplay,
         * when we close down the JS runtime.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Wrapper was finalized despite being kept alive, has refcount >1");

        debug_lifecycle("Unrooting object");

        discard_wrapper();
    }
    unlink();

    GJS_DEC_COUNTER(object_instance);
}

ObjectPrototype::~ObjectPrototype() {
    invalidate_closure_collection(&m_vfuncs, this, &vfunc_invalidated_notify);

    g_type_class_unref(g_type_class_peek(m_gtype));

    GJS_DEC_COUNTER(object_prototype);
}

static JSObject* gjs_lookup_object_constructor_from_info(
    JSContext* context, Maybe<const GI::BaseInfo> info, GType gtype) {
    g_return_val_if_fail(!info || info->is_object() || info->is_interface(),
                         nullptr);

    JS::RootedObject in_object(context);
    const char *constructor_name;

    if (info) {
        in_object = gjs_lookup_namespace_object(context, info.value());
        constructor_name = info->name();
    } else {
        in_object = gjs_lookup_private_namespace(context);
        constructor_name = g_type_name(gtype);
    }

    if (G_UNLIKELY (!in_object))
        return NULL;

    bool found;
    if (!JS_HasProperty(context, in_object, constructor_name, &found))
        return NULL;

    JS::RootedValue value(context);
    if (found && !JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    JS::RootedObject constructor(context);
    if (value.isUndefined()) {
        /* In case we're looking for a private type, and we don't find it,
           we need to define it first.
        */
        JS::RootedObject ignored(context);
        if (!ObjectPrototype::define_class(context, in_object, Nothing{}, gtype,
                                           nullptr, 0, &constructor, &ignored))
            return nullptr;
    } else {
        if (G_UNLIKELY (!value.isObject()))
            return NULL;

        constructor = &value.toObject();
    }

    g_assert(constructor);

    return constructor;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_lookup_object_prototype_from_info(
    JSContext* context, Maybe<const GI::BaseInfo> info, GType gtype) {
    g_return_val_if_fail(!info || info->is_object() || info->is_interface(),
                         nullptr);

    JS::RootedObject constructor(context,
        gjs_lookup_object_constructor_from_info(context, info, gtype));

    if (G_UNLIKELY(!constructor))
        return NULL;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject prototype(context);
    if (!gjs_object_require_property(context, constructor, "constructor object",
                                     atoms.prototype(), &prototype))
        return NULL;

    return prototype;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
gjs_lookup_object_prototype(JSContext *context,
                            GType      gtype)
{
    GI::Repository repo;
    Maybe<const GI::ObjectInfo> info = repo.find_by_gtype(gtype).andThen(
        std::mem_fn(&GI::AutoRegisteredTypeInfo::as<GI::InfoTag::OBJECT>));
    return gjs_lookup_object_prototype_from_info(context, info, gtype);
}

bool ObjectInstance::associate_closure(JSContext* cx, GClosure* closure) {
    if (!is_prototype())
        to_instance()->ensure_uses_toggle_ref(cx);

    g_assert(std::find(m_closures.begin(), m_closures.end(), closure) ==
                 m_closures.end() &&
             "This closure was already associated with this object");

    /* This is a weak reference, and will be cleared when the closure is
     * invalidated */
    m_closures.push_back(closure);
    g_closure_add_invalidate_notifier(
        closure, this, &ObjectInstance::closure_invalidated_notify);

    return true;
}

void ObjectInstance::closure_invalidated_notify(void* data, GClosure* closure) {
    // This callback should *only* touch m_closures
    auto* priv = static_cast<ObjectInstance*>(data);
    Gjs::remove_one_from_unsorted_vector(&priv->m_closures, closure);
}

void ObjectInstance::invalidate_closures() {
    invalidate_closure_collection(&m_closures, this,
                                  &closure_invalidated_notify);
    m_closures.shrink_to_fit();
}

bool ObjectBase::connect(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    if (!priv->check_is_instance(cx, "connect to signals"))
        return false;

    return priv->to_instance()->connect_impl(cx, args, false);
}

bool ObjectBase::connect_after(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    if (!priv->check_is_instance(cx, "connect to signals"))
        return false;

    return priv->to_instance()->connect_impl(cx, args, true);
}

bool ObjectBase::connect_object(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    if (!priv->check_is_instance(cx, "connect to signals"))
        return false;

    return priv->to_instance()->connect_impl(cx, args, false, true);
}

bool ObjectInstance::connect_impl(JSContext* context, const JS::CallArgs& args,
                                  bool after, bool object) {
    gulong id;
    guint signal_id;
    GQuark signal_detail;
    const char* func_name = object  ? "connect_object"
                            : after ? "connect_after"
                                    : "connect";

    gjs_debug_gsignal("connect obj %p priv %p", m_wrapper.get(), this);

    if (!check_gobject_disposed_or_finalized("connect to any signal on")) {
        args.rval().setInt32(0);
        return true;
    }

    JS::UniqueChars signal_name;
    JS::RootedObject callback(context);
    JS::RootedObject associate_obj(context);
    GConnectFlags flags;
    if (object) {
        if (!gjs_parse_call_args(context, func_name, args, "sooi",
                                 "signal name", &signal_name, "callback",
                                 &callback, "gobject", &associate_obj,
                                 "connect_flags", &flags))
            return false;

        if (flags & G_CONNECT_SWAPPED) {
            gjs_throw(context, "Unsupported connect flag G_CONNECT_SWAPPED");
            return false;
        }

        after = flags & G_CONNECT_AFTER;
    } else {
        if (!gjs_parse_call_args(context, func_name, args, "so", "signal name",
                                 &signal_name, "callback", &callback))
            return false;
    }

    std::string dynamic_string{GJS_PROFILER_DYNAMIC_STRING(
        context,
        format_name() + '.' + func_name + "('" + signal_name.get() + "')")};
    AutoProfilerLabel label{context, "", dynamic_string};

    if (!JS::IsCallable(callback)) {
        gjs_throw(context, "second arg must be a callback");
        return false;
    }

    if (!g_signal_parse_name(signal_name.get(), gtype(), &signal_id,
                             &signal_detail, true)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(), type_name());
        return false;
    }

    GClosure* closure = Gjs::Closure::create_for_signal(
        context, callback, "signal callback", signal_id);
    if (closure == NULL)
        return false;

    if (associate_obj.get() != nullptr) {
        ObjectInstance* obj = ObjectInstance::for_js(context, associate_obj);
        if (!obj)
            return false;

        if (!obj->associate_closure(context, closure))
            return false;
    } else if (!associate_closure(context, closure)) {
        return false;
    }

    id = g_signal_connect_closure_by_id(m_ptr, signal_id, signal_detail,
                                        closure, after);

    args.rval().setDouble(id);

    return true;
}

bool ObjectBase::emit(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    if (!priv->check_is_instance(cx, "emit signal"))
        return false;

    return priv->to_instance()->emit_impl(cx, args);
}

bool
ObjectInstance::emit_impl(JSContext          *context,
                          const JS::CallArgs& argv)
{
    guint signal_id;
    GQuark signal_detail;
    GSignalQuery signal_query;
    unsigned int i;

    gjs_debug_gsignal("emit obj %p priv %p argc %d", m_wrapper.get(), this,
                      argv.length());

    if (!check_gobject_finalized("emit any signal on")) {
        argv.rval().setUndefined();
        return true;
    }

    JS::UniqueChars signal_name;
    if (!gjs_parse_call_args(context, "emit", argv, "!s",
                             "signal name", &signal_name))
        return false;

    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        context, format_name() + " emit('" + signal_name.get() + "')")};
    AutoProfilerLabel label{context, "", full_name};

    if (!g_signal_parse_name(signal_name.get(), gtype(), &signal_id,
                             &signal_detail, false)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(), type_name());
        return false;
    }

    g_signal_query(signal_id, &signal_query);

    if ((argv.length() - 1) != signal_query.n_params) {
        gjs_throw(context, "Signal '%s' on %s requires %d args got %d",
                  signal_name.get(), type_name(), signal_query.n_params,
                  argv.length() - 1);
        return false;
    }

    AutoGValueVector instance_and_args;
    instance_and_args.reserve(signal_query.n_params + 1);
    std::vector<Gjs::AutoGValue*> args_to_steal;
    Gjs::AutoGValue& instance = instance_and_args.emplace_back(gtype());
    g_value_set_instance(&instance, m_ptr);

    for (i = 0; i < signal_query.n_params; ++i) {
        GType gtype = signal_query.param_types[i] & ~G_SIGNAL_TYPE_STATIC_SCOPE;
        Gjs::AutoGValue& value = instance_and_args.emplace_back(gtype);
        if ((signal_query.param_types[i] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0) {
            if (!gjs_value_to_g_value_no_copy(context, argv[i + 1], &value))
                return false;
        } else {
            if (!gjs_value_to_g_value(context, argv[i + 1], &value))
                return false;
        }

        if (!ObjectBase::info())
            continue;

        Maybe<GI::AutoSignalInfo> signal_info =
            ObjectBase::info()->signal(signal_query.signal_name);
        if (!signal_info)
            continue;

        GI::AutoArgInfo arg_info{signal_info->arg(i)};
        if (arg_info.ownership_transfer() != GI_TRANSFER_NOTHING) {
            // FIXME(3v1n0): As it happens in many places in gjs, we can't track
            // (yet) containers content, so in case of transfer container we
            // can only leak.
            args_to_steal.push_back(&value);
        }
    }

    if (signal_query.return_type == G_TYPE_NONE) {
        g_signal_emitv(instance_and_args.data(), signal_id, signal_detail,
                       nullptr);
        argv.rval().setUndefined();
        std::for_each(args_to_steal.begin(), args_to_steal.end(),
                      [](Gjs::AutoGValue* value) { value->steal(); });
        return true;
    }

    GType gtype = signal_query.return_type & ~G_SIGNAL_TYPE_STATIC_SCOPE;
    Gjs::AutoGValue rvalue(gtype);
    g_signal_emitv(instance_and_args.data(), signal_id, signal_detail, &rvalue);

    std::for_each(args_to_steal.begin(), args_to_steal.end(),
                  [](Gjs::AutoGValue* value) { value->steal(); });

    return gjs_value_from_g_value(context, argv.rval(), &rvalue);
}

bool ObjectInstance::signal_match_arguments_from_object(
    JSContext* cx, JS::HandleObject match_obj, GSignalMatchType* mask_out,
    unsigned* signal_id_out, GQuark* detail_out,
    JS::MutableHandleObject callable_out) {
    g_assert(mask_out && signal_id_out && detail_out && "forgot out parameter");

    int mask = 0;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

    bool has_id;
    unsigned signal_id = 0;
    if (!JS_HasOwnPropertyById(cx, match_obj, atoms.signal_id(), &has_id))
        return false;
    if (has_id) {
        mask |= G_SIGNAL_MATCH_ID;

        JS::RootedValue value(cx);
        if (!JS_GetPropertyById(cx, match_obj, atoms.signal_id(), &value))
            return false;

        JS::UniqueChars signal_name = gjs_string_to_utf8(cx, value);
        if (!signal_name)
            return false;

        signal_id = g_signal_lookup(signal_name.get(), gtype());
    }

    bool has_detail;
    GQuark detail = 0;
    if (!JS_HasOwnPropertyById(cx, match_obj, atoms.detail(), &has_detail))
        return false;
    if (has_detail) {
        mask |= G_SIGNAL_MATCH_DETAIL;

        JS::RootedValue value(cx);
        if (!JS_GetPropertyById(cx, match_obj, atoms.detail(), &value))
            return false;

        JS::UniqueChars detail_string = gjs_string_to_utf8(cx, value);
        if (!detail_string)
            return false;

        detail = g_quark_from_string(detail_string.get());
    }

    bool has_func;
    JS::RootedObject callable(cx);
    if (!JS_HasOwnPropertyById(cx, match_obj, atoms.func(), &has_func))
        return false;
    if (has_func) {
        mask |= G_SIGNAL_MATCH_CLOSURE;

        JS::RootedValue value(cx);
        if (!JS_GetPropertyById(cx, match_obj, atoms.func(), &value))
            return false;

        if (!value.isObject() || !JS::IsCallable(&value.toObject())) {
            gjs_throw(cx, "'func' property must be a function");
            return false;
        }

        callable = &value.toObject();
    }

    if (!has_id && !has_detail && !has_func) {
        gjs_throw(cx, "Must specify at least one of signalId, detail, or func");
        return false;
    }

    *mask_out = GSignalMatchType(mask);
    if (has_id)
        *signal_id_out = signal_id;
    if (has_detail)
        *detail_out = detail;
    if (has_func)
        callable_out.set(callable);
    return true;
}

bool ObjectBase::signal_find(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    if (!priv->check_is_instance(cx, "find signal"))
        return false;

    return priv->to_instance()->signal_find_impl(cx, args);
}

bool ObjectInstance::signal_find_impl(JSContext* cx, const JS::CallArgs& args) {
    gjs_debug_gsignal("[Gi.signal_find_symbol]() obj %p priv %p argc %d",
                      m_wrapper.get(), this, args.length());

    if (!check_gobject_finalized("find any signal on")) {
        args.rval().setInt32(0);
        return true;
    }

    JS::RootedObject match(cx);
    if (!gjs_parse_call_args(cx, "[Gi.signal_find_symbol]", args, "o", "match",
                             &match))
        return false;

    GSignalMatchType mask;
    unsigned signal_id;
    GQuark detail;
    JS::RootedObject callable(cx);
    if (!signal_match_arguments_from_object(cx, match, &mask, &signal_id,
                                            &detail, &callable))
        return false;

    uint64_t handler = 0;
    if (!callable) {
        handler = g_signal_handler_find(m_ptr, mask, signal_id, detail, nullptr,
                                        nullptr, nullptr);
    } else {
        for (GClosure* candidate : m_closures) {
            if (Gjs::Closure::for_gclosure(candidate)->callable() == callable) {
                handler = g_signal_handler_find(m_ptr, mask, signal_id, detail,
                                                candidate, nullptr, nullptr);
                if (handler != 0)
                    break;
            }
        }
    }

    args.rval().setNumber(static_cast<double>(handler));
    return true;
}

template <ObjectBase::SignalMatchFunc(*MatchFunc)>
static inline const char* signal_match_to_action_name();

template <>
inline const char*
signal_match_to_action_name<&g_signal_handlers_block_matched>() {
    return "block";
}

template <>
inline const char*
signal_match_to_action_name<&g_signal_handlers_unblock_matched>() {
    return "unblock";
}

template <>
inline const char*
signal_match_to_action_name<&g_signal_handlers_disconnect_matched>() {
    return "disconnect";
}

template <ObjectBase::SignalMatchFunc(*MatchFunc)>
bool ObjectBase::signals_action(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    const std::string action_name = signal_match_to_action_name<MatchFunc>();
    if (!priv->check_is_instance(cx, (action_name + " signal").c_str()))
        return false;

    return priv->to_instance()->signals_action_impl<MatchFunc>(cx, args);
}

template <ObjectBase::SignalMatchFunc(*MatchFunc)>
bool ObjectInstance::signals_action_impl(JSContext* cx,
                                         const JS::CallArgs& args) {
    const std::string action_name = signal_match_to_action_name<MatchFunc>();
    const std::string action_tag = "[Gi.signals_" + action_name + "_symbol]";
    gjs_debug_gsignal("[%s]() obj %p priv %p argc %d", action_tag.c_str(),
                      m_wrapper.get(), this, args.length());

    if (!check_gobject_finalized((action_name + " any signal on").c_str())) {
        args.rval().setInt32(0);
        return true;
    }
    JS::RootedObject match(cx);
    if (!gjs_parse_call_args(cx, action_tag.c_str(), args, "o", "match",
                             &match)) {
        return false;
    }
    GSignalMatchType mask;
    unsigned signal_id;
    GQuark detail;
    JS::RootedObject callable(cx);
    if (!signal_match_arguments_from_object(cx, match, &mask, &signal_id,
                                            &detail, &callable)) {
        return false;
    }
    unsigned n_matched = 0;
    if (!callable) {
        n_matched = MatchFunc(m_ptr, mask, signal_id, detail, nullptr, nullptr,
                              nullptr);
    } else {
        std::vector<GClosure*> candidates;
        for (GClosure* candidate : m_closures) {
            if (Gjs::Closure::for_gclosure(candidate)->callable() == callable)
                candidates.push_back(candidate);
        }
        for (GClosure* candidate : candidates) {
            n_matched += MatchFunc(m_ptr, mask, signal_id, detail, candidate,
                                   nullptr, nullptr);
        }
    }

    args.rval().setNumber(n_matched);
    return true;
}

bool ObjectBase::to_string(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);
    const char* kind = ObjectBase::DEBUG_TAG;
    if (!priv->is_prototype())
        kind = priv->to_instance()->to_string_kind();
    return gjs_wrapper_to_string_func(
        cx, obj, kind, priv->info(), priv->gtype(),
        priv->is_prototype() ? nullptr : priv->to_instance()->ptr(),
        args.rval());
}

/*
 * ObjectInstance::to_string_kind:
 *
 * ObjectInstance shows a "disposed" marker in its toString() method if the
 * wrapped GObject has already been disposed.
 */
const char* ObjectInstance::to_string_kind(void) const {
    if (m_gobj_finalized)
        return "object (FINALIZED)";
    return m_gobj_disposed ? "object (DISPOSED)" : "object";
}

/*
 * ObjectBase::init_gobject:
 *
 * This is named "init_gobject()" but corresponds to "_init()" in JS. The reason
 * for the name is that an "init()" method is used within SpiderMonkey to
 * indicate fallible initialization that must be done before an object can be
 * used, which is not the case here.
 */
bool ObjectBase::init_gobject(JSContext* context, unsigned argc,
                              JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(context, argc, vp, argv, obj, ObjectBase, priv);
    if (!priv->check_is_instance(context, "initialize"))
        return false;

    std::string full_name{
        GJS_PROFILER_DYNAMIC_STRING(context, priv->format_name() + "._init")};
    AutoProfilerLabel label{context, "", full_name};

    return priv->to_instance()->init_impl(context, argv, obj);
}

// clang-format off
const struct JSClassOps ObjectBase::class_ops = {
    &ObjectBase::add_property,
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    &ObjectBase::new_enumerate,
    &ObjectBase::resolve,
    nullptr,  // mayResolve
    &ObjectBase::finalize,
    NULL,
    NULL,
    &ObjectBase::trace,
};

const struct JSClass ObjectBase::klass = {
    "GObject_Object",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
    &ObjectBase::class_ops
};

JSFunctionSpec ObjectBase::proto_methods[] = {
    JS_FN("_init", &ObjectBase::init_gobject, 0, 0),
    JS_FN("connect", &ObjectBase::connect, 0, 0),
    JS_FN("connect_after", &ObjectBase::connect_after, 0, 0),
    JS_FN("connect_object", &ObjectBase::connect_object, 0, 0),
    JS_FN("emit", &ObjectBase::emit, 0, 0),
    JS_FS_END
};

JSPropertySpec ObjectBase::proto_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "GObject_Object", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

// Override of GIWrapperPrototype::get_parent_proto()
bool ObjectPrototype::get_parent_proto(JSContext* cx,
                                       JS::MutableHandleObject proto) const {
    GType parent_type = g_type_parent(gtype());
    if (parent_type == G_TYPE_INVALID) {
        proto.set(nullptr);
        return true;
    }

    JSObject* prototype = gjs_lookup_object_prototype(cx, parent_type);
    if (!prototype)
        return false;

    proto.set(prototype);
    return true;
}

bool ObjectPrototype::get_parent_constructor(
    JSContext* cx, JS::MutableHandleObject constructor) const {
    GType parent_type = g_type_parent(gtype());

    if (parent_type == G_TYPE_INVALID) {
        constructor.set(nullptr);
        return true;
    }

    JS::RootedValue v_constructor(cx);
    if (!gjs_lookup_object_constructor(cx, parent_type, &v_constructor))
        return false;

    g_assert(v_constructor.isObject() &&
             "gjs_lookup_object_constructor() should always produce an object");
    constructor.set(&v_constructor.toObject());
    return true;
}

void ObjectPrototype::set_interfaces(GType* interface_gtypes,
                                     uint32_t n_interface_gtypes) {
    if (interface_gtypes) {
        for (uint32_t n = 0; n < n_interface_gtypes; n++) {
            m_interface_gtypes.push_back(interface_gtypes[n]);
        }
    }
}

/*
 * ObjectPrototype::define_class:
 * @in_object: Object where the constructor is stored, typically a repo object.
 * @info: Introspection info for the GObject class.
 * @gtype: #GType for the GObject class.
 * @constructor: Return location for the constructor object.
 * @prototype: Return location for the prototype object.
 *
 * Define a GObject class constructor and prototype, including all the
 * necessary methods and properties that are not introspected. Provides the
 * constructor and prototype objects as out parameters, for convenience
 * elsewhere.
 */
bool ObjectPrototype::define_class(JSContext* context,
                                   JS::HandleObject in_object,
                                   Maybe<const GI::ObjectInfo> info,
                                   GType gtype, GType* interface_gtypes,
                                   uint32_t n_interface_gtypes,
                                   JS::MutableHandleObject constructor,
                                   JS::MutableHandleObject prototype) {
    ObjectPrototype* priv = ObjectPrototype::create_class(
        context, in_object, info, gtype, constructor, prototype);
    if (!priv)
        return false;

    priv->set_interfaces(interface_gtypes, n_interface_gtypes);

    JS::RootedObject parent_constructor(context);
    if (!priv->get_parent_constructor(context, &parent_constructor))
        return false;
    // If this is a fundamental constructor (e.g. GObject.Object) the
    // parent constructor may be null.
    if (parent_constructor) {
        if (!JS_SetPrototype(context, constructor, parent_constructor))
            return false;
    }

    // hook_up_vfunc and the signal handler matcher functions can't be included
    // in gjs_object_instance_proto_funcs because they are custom symbols.
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunctionById(context, prototype, atoms.hook_up_vfunc(),
                                 &ObjectBase::hook_up_vfunc, 3,
                                 GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunctionById(context, prototype, atoms.signal_find(),
                                 &ObjectBase::signal_find, 1,
                                 GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunctionById(
               context, prototype, atoms.signals_block(),
               &ObjectBase::signals_action<&g_signal_handlers_block_matched>, 1,
               GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunctionById(
               context, prototype, atoms.signals_unblock(),
               &ObjectBase::signals_action<&g_signal_handlers_unblock_matched>,
               1, GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunctionById(context, prototype, atoms.signals_disconnect(),
                                 &ObjectBase::signals_action<
                                     &g_signal_handlers_disconnect_matched>,
                                 1, GJS_MODULE_PROP_FLAGS);
}

/*
 * ObjectInstance::init_custom_class_from_gobject:
 *
 * Does all the necessary initialization for an ObjectInstance and JSObject
 * wrapper, given a newly-created GObject pointer, of a GObject class that was
 * created in JS with GObject.registerClass(). This is called from the GObject's
 * instance init function in gobject.cpp, and that's the only reason it's a
 * public method.
 */
bool ObjectInstance::init_custom_class_from_gobject(JSContext* cx,
                                                    JS::HandleObject wrapper,
                                                    GObject* gobj) {
    associate_js_gobject(cx, wrapper, gobj);

    // Custom JS objects will most likely have visible state, so just do this
    // from the start.
    ensure_uses_toggle_ref(cx);
    if (!m_uses_toggle_ref) {
        gjs_throw(cx, "Impossible to set toggle references on %sobject %p",
                  m_gobj_disposed ? "disposed " : "", gobj);
        return false;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue v(cx);
    if (!JS_GetPropertyById(cx, wrapper, atoms.instance_init(), &v))
        return false;

    if (v.isUndefined())
        return true;
    if (!v.isObject() || !JS::IsCallable(&v.toObject())) {
        gjs_throw(cx, "_instance_init property was not a function");
        return false;
    }

    JS::RootedValue ignored_rval(cx);
    return JS_CallFunctionValue(cx, wrapper, v, JS::HandleValueArray::empty(),
                                &ignored_rval);
}

/*
 * ObjectInstance::new_for_gobject:
 *
 * Creates a new JSObject wrapper for the GObject pointer @gobj, and an
 * ObjectInstance private structure to go along with it.
 */
ObjectInstance* ObjectInstance::new_for_gobject(JSContext* cx, GObject* gobj) {
    g_assert(gobj && "Cannot create JSObject for null GObject pointer");

    GType gtype = G_TYPE_FROM_INSTANCE(gobj);

    gjs_debug_marshal(GJS_DEBUG_GOBJECT, "Wrapping %s %p with JSObject",
                      g_type_name(gtype), gobj);

    JS::RootedObject proto(cx, gjs_lookup_object_prototype(cx, gtype));
    if (!proto)
        return nullptr;

    JS::RootedObject obj(
        cx, JS_NewObjectWithGivenProto(cx, &ObjectBase::klass, proto));
    if (!obj)
        return nullptr;

    ObjectPrototype* prototype = resolve_prototype(cx, proto);
    if (!prototype)
        return nullptr;

    ObjectInstance* priv = new ObjectInstance(prototype, obj);

    ObjectBase::init_private(obj, priv);

    g_object_ref_sink(gobj);
    priv->associate_js_gobject(cx, obj, gobj);

    g_assert(priv->wrapper() == obj.get());

    return priv;
}

/*
 * ObjectInstance::wrapper_from_gobject:
 *
 * Gets a JSObject wrapper for the GObject pointer @gobj. If one already exists,
 * then it is returned. Otherwise a new one is created with
 * ObjectInstance::new_for_gobject().
 */
JSObject* ObjectInstance::wrapper_from_gobject(JSContext* cx, GObject* gobj) {
    g_assert(gobj && "Cannot get JSObject for null GObject pointer");

    ObjectInstance* priv = ObjectInstance::for_gobject(gobj);

    if (!priv) {
        /* We have to create a wrapper */
        priv = new_for_gobject(cx, gobj);
        if (!priv)
            return nullptr;
    }

    return priv->wrapper();
}

bool ObjectInstance::set_value_from_gobject(JSContext* cx, GObject* gobj,
                                            JS::MutableHandleValue value_p) {
    if (!gobj) {
        value_p.setNull();
        return true;
    }

    auto* wrapper = ObjectInstance::wrapper_from_gobject(cx, gobj);
    if (wrapper) {
        value_p.setObject(*wrapper);
        return true;
    }

    gjs_throw(cx, "Failed to find JS object for GObject %p of type %s", gobj,
              g_type_name(G_TYPE_FROM_INSTANCE(gobj)));
    return false;
}

// Replaces GIWrapperBase::to_c_ptr(). The GIWrapperBase version is deleted.
bool ObjectBase::to_c_ptr(JSContext* cx, JS::HandleObject obj, GObject** ptr) {
    g_assert(ptr);

    auto* priv = ObjectBase::for_js(cx, obj);
    if (!priv || priv->is_prototype())
        return false;

    ObjectInstance* instance = priv->to_instance();
    if (!instance->check_gobject_finalized("access")) {
        *ptr = nullptr;
        return true;
    }

    *ptr = instance->ptr();
    return true;
}

// Overrides GIWrapperBase::transfer_to_gi_argument().
bool ObjectBase::transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                         GIArgument* arg,
                                         GIDirection transfer_direction,
                                         GITransfer transfer_ownership,
                                         GType expected_gtype) {
    g_assert(transfer_direction != GI_DIRECTION_INOUT &&
             "transfer_to_gi_argument() must choose between in or out");

    if (!ObjectBase::typecheck(cx, obj, expected_gtype)) {
        gjs_arg_unset(arg);
        return false;
    }

    GObject* ptr;
    if (!ObjectBase::to_c_ptr(cx, obj, &ptr))
        return false;

    gjs_arg_set(arg, ptr);

    // Pointer can be null if object was already disposed by C code
    if (!ptr)
        return true;

    if ((transfer_direction == GI_DIRECTION_IN &&
         transfer_ownership != GI_TRANSFER_NOTHING) ||
        (transfer_direction == GI_DIRECTION_OUT &&
         transfer_ownership == GI_TRANSFER_EVERYTHING)) {
        gjs_arg_set(arg, ObjectInstance::copy_ptr(cx, expected_gtype,
                                                  gjs_arg_get<void*>(arg)));
        if (!gjs_arg_get<void*>(arg))
            return false;
    }

    return true;
}

// Returns pair of implementor_vtable pointer, maybe field info
GJS_JSAPI_RETURN_CONVENTION
static Maybe<std::pair<void*, Maybe<GI::AutoFieldInfo>>> find_vfunc_info(
    JSContext* context, GType implementor_gtype, const GI::VFuncInfo vfunc_info,
    const char* vfunc_name) {
    Maybe<GI::AutoStructInfo> struct_info;
    void* implementor_vtable_ret = nullptr;

    const GI::RegisteredTypeInfo ancestor_info =
        vfunc_info.container<GI::InfoTag::REGISTERED_TYPE>().value();
    GType ancestor_gtype = ancestor_info.gtype();

    Gjs::AutoTypeClass implementor_class{implementor_gtype};
    if (auto iface_info = ancestor_info.as<GI::InfoTag::INTERFACE>()) {
        GTypeInstance *implementor_iface_class;
        implementor_iface_class = (GTypeInstance*) g_type_interface_peek(implementor_class,
                                                        ancestor_gtype);
        if (implementor_iface_class == NULL) {
            gjs_throw (context, "Couldn't find GType of implementor of interface %s.",
                       g_type_name(ancestor_gtype));
            return Nothing{};
        }

        implementor_vtable_ret = implementor_iface_class;

        struct_info = iface_info->iface_struct();
    } else {
        struct_info = ancestor_info.as<GI::InfoTag::OBJECT>()->class_struct();
        implementor_vtable_ret = implementor_class;
    }

    for (GI::AutoFieldInfo field_info : struct_info->fields()) {
        if (strcmp(field_info.name(), vfunc_name) != 0)
            continue;

        GI::AutoTypeInfo type_info{field_info.type_info()};
        if (type_info.tag() != GI_TYPE_TAG_INTERFACE) {
            /* We have a field with the same name, but it's not a callback.
             * There's no hope of being another field with a correct name,
             * so just abort early. */
            return Some(std::make_pair(implementor_vtable_ret, Nothing{}));
        }
        return Some(std::make_pair(implementor_vtable_ret,
                                   Some(std::move(field_info))));
    }
    return Some(std::make_pair(implementor_vtable_ret, Nothing{}));
}

bool ObjectBase::hook_up_vfunc(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, prototype, ObjectBase, priv);
    /* Normally we wouldn't assert is_prototype(), but this method can only be
     * called internally so it's OK to crash if done wrongly */
    return priv->to_prototype()->hook_up_vfunc_impl(cx, args);
}

bool ObjectPrototype::hook_up_vfunc_impl(JSContext* cx,
                                         const JS::CallArgs& args) {
    JS::UniqueChars name;
    JS::RootedObject callable(cx);
    bool is_static = false;
    if (!gjs_parse_call_args(cx, "hook_up_vfunc", args, "so|b", "name", &name,
                             "function", &callable, "is_static", &is_static))
        return false;

    args.rval().setUndefined();

    /* find the first class that actually has repository information */
    GI::Repository repo;
    Maybe<GI::AutoObjectInfo> info = m_info;
    GType info_gtype = m_gtype;
    while (!info && info_gtype != G_TYPE_OBJECT) {
        info_gtype = g_type_parent(info_gtype);

        info = repo.find_by_gtype<GI::InfoTag::OBJECT>(info_gtype);
    }

    /* If we don't have 'info', we don't have the base class (GObject).
     * This is awful, so abort now. */
    g_assert(info);

    Maybe<GI::AutoVFuncInfo> vfunc{info->vfunc(name.get())};
    // Search the parent type chain
    while (!vfunc && info_gtype != G_TYPE_OBJECT) {
        info_gtype = g_type_parent(info_gtype);

        info = repo.find_by_gtype<GI::InfoTag::OBJECT>(info_gtype);
        if (info)
            vfunc = info->vfunc(name.get());
    }

    // If the vfunc doesn't exist in the parent
    // type chain, loop through the explicitly
    // defined interfaces...
    if (!vfunc) {
        for (GType interface_gtype : m_interface_gtypes) {
            Maybe<GI::AutoInterfaceInfo> interface{
                repo.find_by_gtype<GI::InfoTag::INTERFACE>(interface_gtype)};

            // Private and dynamic interfaces (defined in JS) do not have type
            // info.
            if (interface) {
                vfunc = interface->vfunc(name.get());
                if (vfunc)
                    break;
            }
        }
    }

    // If the vfunc is still not found, it could exist on an interface
    // implemented by a parent. This is an error, as hooking up the vfunc
    // would create an implementation on the interface itself. In this
    // case, print a more helpful error than...
    // "Could not find definition of virtual function"
    //
    // See https://gitlab.gnome.org/GNOME/gjs/-/issues/89
    if (!vfunc) {
        unsigned n_interfaces;
        Gjs::AutoPointer<GType> interface_list{
            g_type_interfaces(m_gtype, &n_interfaces)};

        for (unsigned i = 0; i < n_interfaces; i++) {
            Maybe<GI::AutoInterfaceInfo> interface{
                repo.find_by_gtype<GI::InfoTag::INTERFACE>(interface_list[i])};

            if (!interface)
                continue;

            Maybe<GI::AutoVFuncInfo> parent_vfunc{interface->vfunc(name.get())};

            if (parent_vfunc) {
                Gjs::AutoChar identifier{g_strdup_printf(
                    "%s.%s", interface->ns(), interface->name())};
                gjs_throw(cx,
                          "%s does not implement %s, add %s to your "
                          "implements array",
                          g_type_name(m_gtype), identifier.get(),
                          identifier.get());
                return false;
            }
        }

        // Fall back to less helpful error message
        gjs_throw(cx, "Could not find definition of virtual function %s",
                  name.get());
        return false;
    }

    if (vfunc->is_method() != !is_static) {
        gjs_throw(cx, "Invalid %s definition of %s virtual function %s",
                  is_static ? "static" : "non-static",
                  is_static ? "non-static" : "static", name.get());
        return false;
    }

    auto result = find_vfunc_info(cx, m_gtype, vfunc.ref(), name.get());
    if (!result)
        return false;

    void* implementor_vtable = result->first;
    Maybe<GI::AutoFieldInfo> field_info = result->second;
    if (field_info) {
        void* method_ptr;
        GjsCallbackTrampoline *trampoline;

        int offset = field_info->offset();
        method_ptr = G_STRUCT_MEMBER_P(implementor_vtable, offset);

        if (!JS::IsCallable(callable)) {
            gjs_throw(cx, "Tried to deal with a vfunc that wasn't callable");
            return false;
        }
        trampoline = GjsCallbackTrampoline::create(
            cx, callable, vfunc.ref(), GI_SCOPE_TYPE_NOTIFIED, true, !is_static);
        if (!trampoline)
            return false;

        // This is traced, and will be cleared from the list when the closure is
        // invalidated
        g_assert(std::find(m_vfuncs.begin(), m_vfuncs.end(), trampoline) ==
                     m_vfuncs.end() &&
                 "This vfunc was already associated with this class");
        m_vfuncs.insert(trampoline);
        g_closure_add_invalidate_notifier(
            trampoline, this, &ObjectPrototype::vfunc_invalidated_notify);
        g_closure_add_invalidate_notifier(
            trampoline, nullptr,
            [](void*, GClosure* closure) { g_closure_unref(closure); });

        *reinterpret_cast<void**>(method_ptr) = trampoline->closure();
    }

    return true;
}

void ObjectPrototype::vfunc_invalidated_notify(void* data, GClosure* closure) {
    // This callback should *only* touch m_vfuncs
    auto* priv = static_cast<ObjectPrototype*>(data);
    priv->m_vfuncs.erase(closure);
}

bool
gjs_lookup_object_constructor(JSContext             *context,
                              GType                  gtype,
                              JS::MutableHandleValue value_p)
{
    JSObject *constructor;

    GI::Repository repo;
    Maybe<const GI::ObjectInfo> object_info = repo.find_by_gtype(gtype).andThen(
        std::mem_fn(&GI::AutoRegisteredTypeInfo::as<GI::InfoTag::OBJECT>));

    constructor = gjs_lookup_object_constructor_from_info(context, object_info, gtype);

    if (G_UNLIKELY (constructor == NULL))
        return false;

    value_p.setObject(*constructor);
    return true;
}

void ObjectInstance::associate_string(GObject* obj, char* str) {
    auto* instance_strings = static_cast<GPtrArray*>(
        g_object_get_qdata(obj, ObjectBase::instance_strings_quark()));

    if (!instance_strings) {
        instance_strings = g_ptr_array_new_with_free_func(g_free);
        g_object_set_qdata_full(
            obj, ObjectBase::instance_strings_quark(), instance_strings,
            reinterpret_cast<GDestroyNotify>(g_ptr_array_unref));
    }
    g_ptr_array_add(instance_strings, str);
}
