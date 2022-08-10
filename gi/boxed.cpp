/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2022 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <stdint.h>
#include <string.h>  // for memcpy, size_t, strcmp

#include <algorithm>  // for one_of, any_of
#include <string>
#include <type_traits>
#include <utility>  // for move, forward

#include <girepository/girepository.h>
#include <glib-object.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/Class.h>        // for ESClass
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/Exception.h>
#include <js/GCHashTable.h>  // for GCHashMap
#include <js/GCVector.h>     // for MutableWrappedPtrOperations
#include <js/Object.h>       // for SetReservedSlot
#include <js/PropertyAndElement.h>  // for JS_DefineFunction, JS_Enumerate
#include <js/String.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>  // for IdVector
#include <mozilla/HashTable.h>
#include <mozilla/Span.h>

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/cwrapper.h"
#include "gi/function.h"
#include "gi/info.h"
#include "gi/repo.h"
#include "gi/struct.h"
#include "gi/union.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"

using mozilla::Maybe, mozilla::Some;

template <GI::InfoTag TAG>
[[nodiscard]] static bool struct_is_simple(const GI::UnownedInfo<TAG>& info);

template <class Base, class Prototype, class Instance>
BoxedInstance<Base, Prototype, Instance>::BoxedInstance(Prototype* prototype,
                                                        JS::HandleObject obj)
    : BaseClass(prototype, obj),
      m_allocated_directly(false),
      m_owning_ptr(false) {}

// See GIWrapperBase::resolve().
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::resolve_impl(
    JSContext* cx, JS::HandleObject obj, JS::HandleId id, bool* resolved) {
    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(cx, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    // Look for methods and other class properties
    Maybe<GI::AutoFunctionInfo> method_info{info().method(prop_name.get())};
    if (!method_info) {
        *resolved = false;
        return true;
    }
    method_info->log_usage();

    if (method_info->is_method()) {
        gjs_debug(GJS_DEBUG_GBOXED, "Defining method %s in prototype for %s",
                  method_info->name(), format_name().c_str());

        /* obj is the Boxed prototype */
        if (!gjs_define_function(cx, obj, gtype(), *method_info))
            return false;

        *resolved = true;
    } else {
        *resolved = false;
    }

    return true;
}

// See GIWrapperBase::new_enumerate().
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::new_enumerate_impl(
    JSContext* cx, JS::HandleObject, JS::MutableHandleIdVector properties,
    bool only_enumerable [[maybe_unused]]) {
    for (GI::AutoFunctionInfo meth_info : info().methods()) {
        if (meth_info.is_method()) {
            jsid id = gjs_intern_string_to_id(cx, meth_info.name());
            if (id.isVoid())
                return false;
            if (!properties.append(id)) {
                JS_ReportOutOfMemory(cx);
                return false;
            }
        }
    }

    return true;
}

/*
 * BoxedBase::get_copy_source():
 *
 * Check to see if JS::Value passed in is another Boxed instance object of the
 * same type, and if so, retrieve the BoxedInstance private structure for it.
 * This function does not throw any JS exceptions.
 */
template <class Base, class Prototype, class Instance>
Base* BoxedBase<Base, Prototype, Instance>::get_copy_source(
    JSContext* context, JS::Value value) const {
    if (!value.isObject())
        return nullptr;

    JS::RootedObject object(context, &value.toObject());
    Base* source_priv = Base::for_js(context, object);
    if (!source_priv || info() != source_priv->info())
        return nullptr;

    return source_priv;
}

/*
 * BoxedInstance::allocate_directly:
 *
 * Allocate a boxed object of the correct size, set all the bytes to 0, and set
 * m_ptr to point to it. This is used when constructing a boxed object that can
 * be allocated directly (i.e., does not need to be created by a constructor
 * function.)
 */
template <class Base, class Prototype, class Instance>
void BoxedInstance<Base, Prototype, Instance>::allocate_directly() {
    g_assert(get_prototype()->can_allocate_directly());

    own_ptr(g_malloc0(info().size()));
    m_allocated_directly = true;

    debug_lifecycle("Boxed pointer directly allocated");
}

// When initializing a boxed object from a hash of properties, we don't want to
// do n O(n) lookups, so put put the fields into a hash table and store it on
// proto->priv for fast lookup.
template <class Base, class Prototype, class Instance>
std::unique_ptr<Boxed::FieldMap>
BoxedPrototype<Base, Prototype, Instance>::create_field_map(
    JSContext* cx, const BoxedInfo info) {
    auto result = std::make_unique<Boxed::FieldMap>();
    typename BoxedInfo::FieldsIterator fields = info.fields();
    if (!result->reserve(fields.size())) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }

    for (GI::AutoFieldInfo field_info : fields) {
        // We get the string as a jsid later, which is interned. We intern the
        // string here as well, so it will be the same string pointer
        const std::string& field_name =
            find_unique_js_field_name(info, field_info.name());
        JSString* atom = JS_AtomizeAndPinStringN(cx, field_name.c_str(),
                                                 field_name.length());

        result->putNewInfallible(atom, std::move(field_info));
    }

    return result;
}

/*
 * BoxedPrototype::ensure_field_map:
 *
 * BoxedPrototype keeps a cache of field names to introspection info.
 * We only create the field cache the first time it is needed. An alternative
 * would be to create it when the prototype is created, in BoxedPrototype::init.
 */
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::ensure_field_map(
    JSContext* cx) {
    if (!m_field_map)
        m_field_map = create_field_map(cx, info());
    return !!m_field_map;
}

/*
 * BoxedPrototype::lookup_field:
 *
 * Look up the introspection info corresponding to the field name @prop_name,
 * creating the field cache if necessary.
 */
template <class Base, class Prototype, class Instance>
Maybe<const GI::FieldInfo>
BoxedPrototype<Base, Prototype, Instance>::lookup_field(JSContext* cx,
                                                        JSString* prop_name) {
    if (!ensure_field_map(cx))
        return {};

    JS::RootedString rooted_name(cx, prop_name);
    JS::UniqueChars encoded_prop_name = JS_EncodeStringToUTF8(cx, rooted_name);
    const std::string field_name{
        find_unique_js_field_name(info(), encoded_prop_name.get())};

    if (field_name != encoded_prop_name.get()) {
        prop_name = JS_AtomizeAndPinStringN(cx, field_name.c_str(),
                                            field_name.length());
    }

    auto entry = m_field_map->lookup(prop_name);
    if (!entry) {
        gjs_throw(cx, "No field %s on boxed type %s",
                  gjs_debug_string(prop_name).c_str(), name());
        return {};
    }

    return Some(entry->value());
}

/* Initialize a newly created Boxed from an object that is a "hash" of
 * properties to set as fields of the object. We don't require that every field
 * of the object be set.
 */
template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::init_from_props(
    JSContext* context, JS::Value props_value) {
    size_t ix, length;

    if (!props_value.isObject()) {
        gjs_throw(context, "argument should be a hash with fields to set");
        return false;
    }

    JS::RootedObject props(context, &props_value.toObject());
    JS::Rooted<JS::IdVector> ids(context, context);
    if (!JS_Enumerate(context, props, &ids)) {
        gjs_throw(context, "Failed to enumerate fields hash");
        return false;
    }

    JS::RootedValue value(context);
    for (ix = 0, length = ids.length(); ix < length; ix++) {
        if (!ids[ix].isString()) {
            gjs_throw(context, "Fields hash contained a non-string field");
            return false;
        }

        Maybe<const GI::FieldInfo> field_info =
            get_prototype()->lookup_field(context, ids[ix].toString());
        if (!field_info)
            return false;

        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        if (!gjs_object_require_property(context, props, "property list",
                                         JS::HandleId::fromMarkedLocation(ids[ix].address()),
                                         &value))
            return false;

        if (!field_setter_impl(context, *field_info, value))
            return false;
    }

    return true;
}

template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::invoke_static_method(
    JSContext* context, JS::HandleObject obj, JS::HandleId method_name,
    const JS::CallArgs& args) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    JS::RootedObject js_constructor(context);

    if (!gjs_object_require_property(
            context, obj, nullptr, gjs->atoms().constructor(), &js_constructor))
        return false;

    JS::RootedValue method{context};
    if (!gjs_object_require_property(context, js_constructor, NULL, method_name,
                                     &method))
        return false;

    return gjs->call_function(nullptr, method, args, args.rval());
}

/*
 * BoxedInstance::copy_boxed:
 *
 * Allocate a new boxed pointer using g_boxed_copy(), either from a raw boxed
 * pointer or another BoxedInstance.
 */
template <class Base, class Prototype, class Instance>
void BoxedInstance<Base, Prototype, Instance>::copy_boxed(void* boxed_ptr) {
    own_ptr(g_boxed_copy(gtype(), boxed_ptr));
    debug_lifecycle("Boxed pointer created with g_boxed_copy()");
}

template <class Base, class Prototype, class Instance>
void BoxedInstance<Base, Prototype, Instance>::copy_boxed(Instance* source) {
    copy_boxed(source->ptr());
}

/*
 * BoxedInstance::copy_memory:
 *
 * Allocate a new boxed pointer by copying the contents of another boxed pointer
 * or another BoxedInstance.
 */
template <class Base, class Prototype, class Instance>
void BoxedInstance<Base, Prototype, Instance>::copy_memory(void* boxed_ptr) {
    allocate_directly();
    memcpy(m_ptr, boxed_ptr, info().size());
}

template <class Base, class Prototype, class Instance>
void BoxedInstance<Base, Prototype, Instance>::copy_memory(Instance* source) {
    copy_memory(source->ptr());
}

// See GIWrapperBase::constructor().
template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::constructor_impl(
    JSContext* context, JS::HandleObject obj, const JS::CallArgs& args) {
    // Short-circuit copy-construction in the case where we can use copy_boxed()
    // or copy_memory()
    Base* source_priv;
    if (args.length() == 1 &&
        (source_priv = get_copy_source(context, args[0]))) {
        if (!source_priv->check_is_instance(context, "construct boxed object"))
            return false;

        if (g_type_is_a(gtype(), G_TYPE_BOXED)) {
            copy_boxed(source_priv->to_instance());
            return true;
        } else if (get_prototype()->can_allocate_directly()) {
            copy_memory(source_priv->to_instance());
            return true;
        }
    }

    Prototype* proto = get_prototype();

    // If the structure is registered as a boxed, we can create a new instance
    // by looking for a zero-args constructor and calling it.
    // Constructors don't really make sense for non-boxed types, since there is
    // no memory management for the return value, and zero_args_constructor and
    // default_constructor are always -1 for them.
    //
    // For backward compatibility, we choose the zero args constructor if one
    // exists, otherwise we malloc the correct amount of space if possible;
    // finally, we fallback on the default constructor.
    if (proto->has_zero_args_constructor()) {
        GI::AutoFunctionInfo func_info{proto->zero_args_constructor_info()};

        GIArgument rval_arg;
        Gjs::GErrorResult<> result = func_info.invoke({}, {}, &rval_arg);
        if (result.isErr()) {
            gjs_throw(context, "Failed to invoke boxed constructor: %s",
                      result.inspectErr()->message);
            return false;
        }

        own_ptr(gjs_arg_steal<void*>(&rval_arg));

        debug_lifecycle("Boxed pointer created from zero-args constructor");

    } else if (proto->can_allocate_directly_without_pointers()) {
        allocate_directly();
    } else if (proto->has_default_constructor()) {
        js::ESClass es_class = js::ESClass::Other;
        if (proto->can_allocate_directly() && args.length() == 1 && args[0].isObject()) {
            JS::RootedObject arg0{context, &args[0].toObject()};
            if (!JS::GetBuiltinClass(context, arg0, &es_class))
                return false;
        }

        if (es_class == js::ESClass::Object) {
            // If one argument is passed and it's a plain object, assume we are
            // constructing from a property bag. Introspected constructors
            // should not take a property bag as argument.
            allocate_directly();
        } else {
            // for simplicity, we simply delegate all the work to the actual JS
            // constructor function (which we retrieve from the JS constructor,
            // that is, Namespace.BoxedType, or object.constructor, given that
            // object was created with the right prototype.
            if (!invoke_static_method(
                    context, obj, proto->default_constructor_name(), args))
                return false;

            // The return value of the JS constructor gets its own
            // BoxedInstance, and this one is discarded.
            debug_lifecycle(
                "Boxed construction delegated to JS constructor, boxed object "
                "discarded");

            return true;
        }
    } else if (proto->can_allocate_directly()) {
        allocate_directly();
    } else {
        gjs_throw(context,
                  "Unable to construct struct type %s since it has no default "
                  "constructor and cannot be allocated directly",
                  name());
        return false;
    }

    /* If we reach this code, we need to init from a map of fields */

    if (args.length() == 0)
        return true;

    if (args.length() > 1) {
        gjs_throw(context,
                  "Constructor with multiple arguments not supported for %s",
                  name());
        return false;
    }

    return init_from_props(context, args[0]);
}

template <class Base, class Prototype, class Instance>
BoxedInstance<Base, Prototype, Instance>::~BoxedInstance() {
    if (!m_owning_ptr)
        return;

    if (m_allocated_directly) {
        g_free(m_ptr.release());
        return;
    }

    if (g_type_is_a(gtype(), G_TYPE_BOXED)) {
        g_boxed_free(gtype(), m_ptr.release());
        return;
    }

    // This check is not easily moveable to ~StructInstance() because of the
    // g_assert_not_reached() below. So we do it inline here with if constexpr.
    if constexpr (Base::TAG == GI::InfoTag::STRUCT) {
        if (g_type_is_a(gtype(), G_TYPE_VARIANT)) {
            g_variant_unref(static_cast<GVariant*>(m_ptr.release()));
            return;
        }
    }

    g_assert_not_reached();
}

/*
 * BoxedBase::get_field_info:
 *
 * Does the same thing as g_struct_info_get_field(), but throws a JS exception
 * if there is no such field.
 */
template <class Base, class Prototype, class Instance>
Maybe<GI::AutoFieldInfo> BoxedBase<Base, Prototype, Instance>::get_field_info(
    JSContext* cx, uint32_t id) const {
    Maybe<GI::AutoFieldInfo> field_info = info().fields()[id];
    if (!field_info)
        gjs_throw(cx, "No field %d on boxed type %s", id, name());

    return field_info;
}

// Helper function to work around -Wunsupported-friend, where it is not possible
// for BoxedInstance<STRUCT> to be a friend of a BoxedInstance<UNION> method and
// vice versa. This could be static for g++, but not for clang++.
template <class OtherInstance>
void adopt_nested_ptr(OtherInstance* priv, void* data) {
    priv->share_ptr(data);
    priv->debug_lifecycle(
        "Boxed pointer created, pointing inside memory owned by parent");
}

/*
 * BoxedInstance::get_nested_interface_object:
 * @parent_obj: the BoxedInstance JS object that owns `this`
 * @field_info: introspection info for the field of the parent boxed type that
 *   is another boxed type
 * @interface_info: introspection info for the nested boxed type
 * @value: return location for a new BoxedInstance JS object
 *
 * Some boxed types have a field that consists of another boxed type. We want to
 * be able to expose these nested boxed types without copying them, because
 * changing fields of the nested boxed struct should affect the enclosing boxed
 * struct.
 *
 * This method creates a new BoxedInstance and JS object for a nested boxed
 * struct. Since both the nested JS object and the parent boxed's JS object
 * refer to the same memory, the parent JS object will be prevented from being
 * garbage collected while the nested JS object is active.
 */
template <class Base, class Prototype, class Instance>
template <class FieldInstance>
bool BoxedInstance<Base, Prototype, Instance>::get_nested_interface_object(
    JSContext* context, JSObject* parent_obj, const GI::FieldInfo field_info,
    const GI::UnownedInfo<FieldInstance::TAG> struct_info,
    JS::MutableHandleValue value) const {
    if (!struct_is_simple(struct_info)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  format_name().c_str(), field_info.name());

        return false;
    }

    JS::RootedObject obj{
        context, gjs_new_object_with_generic_prototype(context, struct_info)};
    if (!obj)
        return false;

    FieldInstance* priv = FieldInstance::new_for_js_object(context, obj);

    /* A structure nested inside a parent object; doesn't have an independent allocation */
    adopt_nested_ptr(priv, raw_ptr() + field_info.offset());

    /* We never actually read the reserved slot, but we put the parent object
     * into it to hold onto the parent object.
     */
    JS::SetReservedSlot(obj, BoxedInstance::PARENT_OBJECT,
                        JS::ObjectValue(*parent_obj));

    value.setObject(*obj);
    return true;
}

/*
 * BoxedBase::field_getter:
 *
 * JSNative property getter that is called when accessing a field defined on a
 * boxed type. Delegates to BoxedInstance::field_getter_impl() if the minimal
 * conditions have been met.
 */
template <class Base, class Prototype, class Instance>
bool BoxedBase<Base, Prototype, Instance>::field_getter(JSContext* context,
                                                        unsigned argc,
                                                        JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(context, argc, vp, args, obj, Base, priv);
    if (!priv->check_is_instance(context, "get a field"))
        return false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    Maybe<GI::AutoFieldInfo> field_info{
        priv->get_field_info(context, field_ix)};
    if (!field_info)
        return false;

    return priv->to_instance()->field_getter_impl(
        context, obj, field_info.ref(), args.rval());
}

// See BoxedBase::field_getter().
template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::field_getter_impl(
    JSContext* cx, JSObject* obj, const GI::FieldInfo field_info,
    JS::MutableHandleValue rval) const {
    GI::AutoTypeInfo type_info{field_info.type_info()};

    if (!type_info.is_pointer() && type_info.tag() == GI_TYPE_TAG_INTERFACE) {
        GI::AutoBaseInfo interface{type_info.interface()};
        if (auto union_info = interface.as<GI::InfoTag::UNION>()) {
            return get_nested_interface_object<UnionInstance>(
                cx, obj, field_info, union_info.value(), rval);
        } else if (auto struct_info = interface.as<GI::InfoTag::STRUCT>()) {
            return get_nested_interface_object<StructInstance>(
                cx, obj, field_info, struct_info.value(), rval);
        }
    }

    GIArgument arg;
    if (field_info.read(m_ptr, &arg).isErr()) {
        gjs_throw(cx, "Reading field %s.%s is not supported",
                  format_name().c_str(), field_info.name());
        return false;
    }

    if (type_info.tag() == GI_TYPE_TAG_ARRAY &&
        type_info.array_length_index()) {
        unsigned length_field_ix = type_info.array_length_index().value();
        Maybe<GI::AutoFieldInfo> length_field_info{
            get_field_info(cx, length_field_ix)};
        if (!length_field_info) {
            gjs_throw(cx, "Reading field %s.%s is not supported",
                      format_name().c_str(), field_info.name());
            return false;
        }

        GIArgument length_arg;
        if (length_field_info->read(m_ptr, &length_arg).isErr()) {
            gjs_throw(cx, "Reading field %s.%s is not supported",
                      format_name().c_str(), length_field_info->name());
            return false;
        }

        size_t length = gjs_gi_argument_get_array_length(
            length_field_info->type_info().tag(), &length_arg);
        return gjs_value_from_explicit_array(cx, rval, type_info, &arg, length);
    }

    return gjs_value_from_gi_argument(cx, rval, type_info, GJS_ARGUMENT_FIELD,
                                      GI_TRANSFER_EVERYTHING, &arg);
}

/*
 * BoxedInstance::set_nested_interface_object:
 * @field_info: introspection info for the field of the parent boxed type that
 *   is another boxed type
 * @interface_info: introspection info for the nested boxed type
 * @value: holds a BoxedInstance JS object of type @interface_info
 *
 * Some boxed types have a field that consists of another boxed type. This
 * method is called from BoxedInstance::field_setter_impl() when any such field
 * is being set. The contents of the BoxedInstance JS object in @value are
 * copied into the correct place in this BoxedInstance's memory.
 */
template <class Base, class Prototype, class Instance>
template <class FieldBase>
bool BoxedInstance<Base, Prototype, Instance>::set_nested_interface_object(
    JSContext* context, const GI::FieldInfo field_info,
    const GI::UnownedInfo<FieldBase::TAG> boxed_info, JS::HandleValue value) {
    if (!struct_is_simple(boxed_info)) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  format_name().c_str(), field_info.name());

        return false;
    }

    /* If we can't directly copy from the source object we need
     * to construct a new temporary object.
     */
    FieldBase* source_priv = nullptr;
    bool field_has_same_info = false;

    if constexpr (std::is_same_v<FieldBase, Base>) {
        field_has_same_info = info() == boxed_info;
        if (field_has_same_info) {
            JS::RootedObject object{context, &value.toObject()};
            source_priv = FieldBase::for_js(context, object);
        }
    }

    if (!source_priv && !field_has_same_info) {
        JS::RootedObject object{context, &value.toObject()};
        source_priv = FieldBase::for_js(context, object);

        if (source_priv && source_priv->info() != boxed_info) {
            std::string source_name{source_priv->format_name()};
            gjs_throw(context, "Impossible to associate a %s to a %s.%s field",
                      source_name.c_str(), name(), field_info.name());
            return false;
        }
    }

    if (!source_priv) {
        JS::RootedObject proto{
            context, gjs_lookup_generic_prototype(context, boxed_info)};
        if (!proto)
            return false;

        JS::RootedValueArray<1> args(context);
        args[0].set(value);
        JS::RootedObject tmp_object(context,
            gjs_construct_object_dynamic(context, proto, args));
        if (!tmp_object ||
            !FieldBase::for_js_typecheck(context, tmp_object, &source_priv))
            return false;
    }

    if (!source_priv->check_is_instance(context, "copy"))
        return false;

    memcpy(raw_ptr() + field_info.offset(), source_priv->to_instance()->ptr(),
           source_priv->info().size());

    return true;
}

// See BoxedBase::field_setter().
template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::field_setter_impl(
    JSContext* context, const GI::FieldInfo field_info, JS::HandleValue value) {
    GI::AutoTypeInfo type_info{field_info.type_info()};

    if (!type_info.is_pointer() && type_info.tag() == GI_TYPE_TAG_INTERFACE) {
        GI::AutoBaseInfo interface_info{type_info.interface()};
        if (auto union_info = interface_info.as<GI::InfoTag::UNION>()) {
            return set_nested_interface_object<UnionBase>(
                context, field_info, union_info.value(), value);
        } else if (auto struct_info =
                       interface_info.as<GI::InfoTag::STRUCT>()) {
            return set_nested_interface_object<StructBase>(
                context, field_info, struct_info.value(), value);
        }
    }

    GIArgument arg;
    if (!gjs_value_to_gi_argument(context, value, type_info, field_info.name(),
                                  GJS_ARGUMENT_FIELD, GI_TRANSFER_NOTHING,
                                  GjsArgumentFlags::MAY_BE_NULL, &arg))
        return false;

    bool success = true;
    if (field_info.write(m_ptr, &arg).isErr()) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  format_name().c_str(), field_info.name());
        success = false;
    }

    JS::AutoSaveExceptionState saved_exc(context);
    if (!gjs_gi_argument_release(context, GI_TRANSFER_NOTHING, type_info,
                                 GjsArgumentFlags::ARG_IN, &arg))
        gjs_log_exception(context);
    saved_exc.restore();

    return success;
}

/*
 * BoxedBase::field_setter:
 *
 * JSNative property setter that is called when writing to a field defined on a
 * boxed type. Delegates to BoxedInstance::field_setter_impl() if the minimal
 * conditions have been met.
 */
template <class Base, class Prototype, class Instance>
bool BoxedBase<Base, Prototype, Instance>::field_setter(JSContext* cx,
                                                        unsigned argc,
                                                        JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, Base, priv);
    if (!priv->check_is_instance(cx, "set a field"))
        return false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    Maybe<GI::AutoFieldInfo> field_info{priv->get_field_info(cx, field_ix)};
    if (!field_info)
        return false;

    if (!priv->to_instance()->field_setter_impl(cx, *field_info, args[0]))
        return false;

    args.rval().setUndefined();  /* No stored value */
    return true;
}

template <class Base, class Prototype, class Instance>
std::string
BoxedPrototype<Base, Prototype, Instance>::find_unique_js_field_name(
    const BoxedInfo info, std::string const& c_field_name) {
    // Give priority to methods that have names equal to fields
    std::string property_name{c_field_name};

    Maybe<GI::AutoFunctionInfo> method_info;
    while ((method_info = info.method(property_name.c_str())))
        property_name.insert(0, "_");

    return property_name;
}

/*
 * BoxedPrototype::define_boxed_class_fields:
 *
 * Defines properties on the JS prototype object, with JSNative getters and
 * setters, for all the fields exposed by GObject introspection.
 */
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::define_boxed_class_fields(
    JSContext* cx, JS::HandleObject proto) {
    uint32_t count = 0;

    // We define all fields as read/write so that the user gets an error
    // message. If we omitted fields or defined them read-only we'd:
    //
    //  - Store a new property for a non-accessible field
    //  - Silently do nothing when writing a read-only field
    //
    // Which is pretty confusing if the only reason a field isn't writable is
    // language binding or memory-management restrictions.
    //
    // We just go ahead and define the fields immediately for the class; doing
    // it lazily in boxed_resolve() would be possible as well if doing it ahead
    // of time caused too much start-up memory overhead.
    //
    // At this point methods have already been defined on the prototype, so we
    // may get name conflicts which we need to check for.
    for (GI::AutoFieldInfo field : info().fields()) {
        const std::string property_name =
            find_unique_js_field_name(info(), field.name());
        JS::RootedValue private_id{cx, JS::PrivateUint32Value(count++)};
        JS::RootedId id{cx, gjs_intern_string_to_id(cx, property_name.c_str())};

        gjs_debug_marshal(GJS_DEBUG_GBOXED,
                          "Defining field %s%s in prototype for %s",
                          field.name(),
                          property_name != field.name()
                              ? (" (as " + property_name + ")").c_str()
                              : "",
                          format_name().c_str());

        if (!gjs_define_property_dynamic(cx, proto, property_name.c_str(), id,
                                         "boxed_field", &Base::field_getter,
                                         &Base::field_setter, private_id,
                                         GJS_MODULE_PROP_FLAGS))
            return false;
    }

    return true;
}

// Overrides GIWrapperPrototype::trace_impl().
template <class Base, class Prototype, class Instance>
void BoxedPrototype<Base, Prototype, Instance>::trace_impl(JSTracer* trc) {
    JS::TraceEdge<jsid>(trc, &m_default_constructor_name,
                        "Boxed::default_constructor_name");
    if (m_field_map)
        m_field_map->trace(trc);
}

[[nodiscard]]
static bool type_can_be_allocated_directly(const GI::TypeInfo& type_info) {
    if (type_info.is_pointer()) {
        if (type_info.tag() == GI_TYPE_TAG_ARRAY &&
            type_info.array_type() == GI_ARRAY_TYPE_C)
            return type_can_be_allocated_directly(type_info.element_type());

        return true;
    }

    if (type_info.tag() != GI_TYPE_TAG_INTERFACE)
        return true;

    GI::AutoBaseInfo interface_info{type_info.interface()};
    if (auto struct_info = interface_info.as<GI::InfoTag::STRUCT>())
        return struct_is_simple(struct_info.value());
    if (auto union_info = interface_info.as<GI::InfoTag::UNION>())
        return struct_is_simple(union_info.value());
    if (interface_info.is_enum_or_flags())
        return true;
    return false;
}

template <GI::InfoTag TAG>
[[nodiscard]]
static bool simple_struct_has_pointers(const GI::UnownedInfo<TAG>);

[[nodiscard]]
static bool direct_allocation_has_pointers(const GI::TypeInfo type_info) {
    if (type_info.is_pointer()) {
        if (type_info.tag() == GI_TYPE_TAG_ARRAY &&
            type_info.array_type() == GI_ARRAY_TYPE_C) {
            return direct_allocation_has_pointers(type_info.element_type());
        }

        return type_info.tag() != GI_TYPE_TAG_VOID;
    }

    if (type_info.tag() != GI_TYPE_TAG_INTERFACE)
        return false;

    GI::AutoBaseInfo interface{type_info.interface()};
    if (auto struct_info = interface.as<GI::InfoTag::STRUCT>())
        return simple_struct_has_pointers(struct_info.value());
    else if (auto union_info = interface.as<GI::InfoTag::UNION>())
        return simple_struct_has_pointers(union_info.value());

    return false;
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
template <GI::InfoTag TAG>
[[nodiscard]]
bool struct_is_simple(const GI::UnownedInfo<TAG>& info) {
    typename GI::UnownedInfo<TAG>::FieldsIterator iter = info.fields();

    // If it's opaque, it's not simple
    if (iter.size() == 0)
        return false;

    return std::all_of(
        iter.begin(), iter.end(), [](GI::AutoFieldInfo field_info) {
            return type_can_be_allocated_directly(field_info.type_info());
        });
}

template <GI::InfoTag TAG>
[[nodiscard]]
static bool simple_struct_has_pointers(const GI::UnownedInfo<TAG> info) {
    g_assert(struct_is_simple(info) &&
             "Don't call simple_struct_has_pointers() on a non-simple struct");

    typename GI::UnownedInfo<TAG>::FieldsIterator fields = info.fields();
    return std::any_of(
        fields.begin(), fields.end(), [](GI::AutoFieldInfo field) {
            return direct_allocation_has_pointers(field.type_info());
        });
}

template <class Base, class Prototype, class Instance>
BoxedPrototype<Base, Prototype, Instance>::BoxedPrototype(const BoxedInfo info,
                                                          GType gtype)
    : BaseClass(info, gtype),
      m_zero_args_constructor(-1),
      m_default_constructor(-1),
      m_default_constructor_name(JS::PropertyKey::Void()),
      m_can_allocate_directly(struct_is_simple(info)) {
    if (!m_can_allocate_directly) {
        m_can_allocate_directly_without_pointers = false;
    } else {
        m_can_allocate_directly_without_pointers =
            !simple_struct_has_pointers(info);
    }
}

// Overrides GIWrapperPrototype::init().
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::init(JSContext* context) {
    int i = 0;
    int first_constructor = -1;
    jsid first_constructor_name = JS::PropertyKey::Void();
    jsid zero_args_constructor_name = JS::PropertyKey::Void();

    if (gtype() != G_TYPE_NONE) {
        /* If the structure is registered as a boxed, we can create a new instance by
         * looking for a zero-args constructor and calling it; constructors don't
         * really make sense for non-boxed types, since there is no memory management
         * for the return value.
         */
        for (GI::AutoFunctionInfo func_info : info().methods()) {
            if (func_info.is_constructor()) {
                if (first_constructor < 0) {
                    first_constructor = i;
                    first_constructor_name =
                        gjs_intern_string_to_id(context, func_info.name());
                    if (first_constructor_name.isVoid())
                        return false;
                }

                if (m_zero_args_constructor < 0 && func_info.n_args() == 0) {
                    m_zero_args_constructor = i;
                    zero_args_constructor_name =
                        gjs_intern_string_to_id(context, func_info.name());
                    if (zero_args_constructor_name.isVoid())
                        return false;
                }

                if (m_default_constructor < 0 &&
                    strcmp(func_info.name(), "new") == 0) {
                    m_default_constructor = i;
                    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
                    m_default_constructor_name = atoms.new_();
                }
            }
            i++;
        }

        if (m_default_constructor < 0) {
            m_default_constructor = m_zero_args_constructor;
            m_default_constructor_name = zero_args_constructor_name;
        }
        if (m_default_constructor < 0) {
            m_default_constructor = first_constructor;
            m_default_constructor_name = first_constructor_name;
        }
    }

    return true;
}

/*
 * BoxedPrototype::define_class_impl:
 * @in_object: Object where the constructor is stored, typically a repo object.
 * @info: Introspection info for the boxed class.
 *
 * Define a boxed class constructor and prototype, including all the necessary
 * methods and properties.
 */
template <class Base, class Prototype, class Instance>
bool BoxedPrototype<Base, Prototype, Instance>::define_class_impl(
    JSContext* cx, JS::HandleObject in_object, const BoxedInfo info,
    JS::MutableHandleObject prototype) {
    JS::RootedObject unused_constructor{cx};
    BoxedPrototype* priv = BoxedPrototype::create_class(
        cx, in_object, info, info.gtype(), &unused_constructor, prototype);
    return priv && priv->define_boxed_class_fields(cx, prototype);
}

/* Helper function to make the public API more readable. The overloads are
 * specified explicitly in the public API, but the implementation uses
 * std::forward in order to avoid duplicating code. */
template <class Base, class Prototype, class Instance>
template <typename... Args>
JSObject* BoxedInstance<Base, Prototype, Instance>::new_for_c_struct_impl(
    JSContext* cx, const BoxedInfo info, void* gboxed, Args&&... args) {
    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED, "Wrapping struct %s %p with JSObject",
                      info.name(), gboxed);

    JS::RootedObject obj(cx, gjs_new_object_with_generic_prototype(cx, info));
    if (!obj)
        return nullptr;

    BoxedInstance* priv = BoxedInstance::new_for_js_object(cx, obj);
    if (!priv)
        return nullptr;

    if (!priv->init_from_c_struct(cx, gboxed, std::forward<Args>(args)...))
        return nullptr;

    return obj;
}

/*
 * BoxedInstance::init_from_c_struct:
 *
 * Do the necessary initialization when creating a BoxedInstance JS object from
 * a C boxed struct pointer.
 *
 * There are two overloads of this method; the NoCopy overload will simply take
 * the passed-in pointer, while the normal method will take a reference, or if
 * the boxed type can be directly allocated, copy the memory.
 */
template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::init_from_c_struct(
    JSContext*, void* gboxed, Boxed::NoCopy) {
    // We need to create a JS Boxed which references the original C struct, not
    // a copy of it. Used for G_SIGNAL_TYPE_STATIC_SCOPE.
    share_ptr(gboxed);
    debug_lifecycle("Boxed pointer acquired, memory not owned");
    return true;
}

template <class Base, class Prototype, class Instance>
bool BoxedInstance<Base, Prototype, Instance>::init_from_c_struct(
    JSContext* cx, void* gboxed) {
    if (gtype() != G_TYPE_NONE && g_type_is_a(gtype(), G_TYPE_BOXED)) {
        copy_boxed(gboxed);
        return true;
    } else if (gtype() == G_TYPE_VARIANT) {
        own_ptr(g_variant_ref_sink(static_cast<GVariant*>(gboxed)));
        debug_lifecycle("Boxed pointer created by sinking GVariant ref");
        return true;
    } else if (get_prototype()->can_allocate_directly()) {
        copy_memory(gboxed);
        return true;
    }

    gjs_throw(cx, "Can't create a Javascript object for %s; no way to copy",
              name());
    return false;
}

template class BoxedBase<StructBase, StructPrototype, StructInstance>;
template class BoxedPrototype<StructBase, StructPrototype, StructInstance>;
template class BoxedInstance<StructBase, StructPrototype, StructInstance>;
template JSObject* StructInstance::new_for_c_struct_impl<>(JSContext*,
                                                           const GI::StructInfo,
                                                           void*);
template JSObject* StructInstance::new_for_c_struct_impl<Boxed::NoCopy>(
    JSContext*, const GI::StructInfo, void*, Boxed::NoCopy&&);
template class BoxedBase<UnionBase, UnionPrototype, UnionInstance>;
template class BoxedPrototype<UnionBase, UnionPrototype, UnionInstance>;
template class BoxedInstance<UnionBase, UnionPrototype, UnionInstance>;
template JSObject* UnionInstance::new_for_c_struct_impl<>(JSContext*,
                                                          const GI::UnionInfo,
                                                          void*);
