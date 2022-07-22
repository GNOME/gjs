/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>
#include <string.h>  // for memcpy, size_t, strcmp

#include <string>
#include <utility>  // for move, forward

#include <girepository.h>
#include <glib-object.h>

#include <js/CallArgs.h>
#include <js/Class.h>
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

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/function.h"
#include "gi/gerror.h"
#include "gi/repo.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

BoxedInstance::BoxedInstance(BoxedPrototype* prototype, JS::HandleObject obj)
    : GIWrapperInstance(prototype, obj),
      m_allocated_directly(false),
      m_owning_ptr(false) {
    GJS_INC_COUNTER(boxed_instance);
}

[[nodiscard]] static bool struct_is_simple(GIStructInfo* info);

// See GIWrapperBase::resolve().
bool BoxedPrototype::resolve_impl(JSContext* cx, JS::HandleObject obj,
                                  JS::HandleId id, bool* resolved) {
    JS::UniqueChars prop_name;
    if (!gjs_get_string_id(cx, id, &prop_name))
        return false;
    if (!prop_name) {
        *resolved = false;
        return true;  // not resolved, but no error
    }

    // Look for methods and other class properties
    GjsAutoFunctionInfo method_info =
        g_struct_info_find_method(info(), prop_name.get());
    if (!method_info) {
        *resolved = false;
        return true;
    }
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(method_info);
#endif

    if (g_function_info_get_flags(method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GBOXED, "Defining method %s in prototype for %s.%s",
                  method_info.name(), ns(), name());

        /* obj is the Boxed prototype */
        if (!gjs_define_function(cx, obj, gtype(), method_info))
            return false;

        *resolved = true;
    } else {
        *resolved = false;
    }

    return true;
}

// See GIWrapperBase::new_enumerate().
bool BoxedPrototype::new_enumerate_impl(JSContext* cx, JS::HandleObject,
                                        JS::MutableHandleIdVector properties,
                                        bool only_enumerable [[maybe_unused]]) {
    int n_methods = g_struct_info_get_n_methods(info());
    for (int i = 0; i < n_methods; i++) {
        GjsAutoFunctionInfo meth_info = g_struct_info_get_method(info(), i);
        GIFunctionInfoFlags flags = g_function_info_get_flags(meth_info);

        if (flags & GI_FUNCTION_IS_METHOD) {
            const char* name = meth_info.name();
            jsid id = gjs_intern_string_to_id(cx, name);
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
BoxedBase* BoxedBase::get_copy_source(JSContext* context,
                                      JS::Value value) const {
    if (!value.isObject())
        return nullptr;

    JS::RootedObject object(context, &value.toObject());
    BoxedBase* source_priv = BoxedBase::for_js(context, object);
    if (!source_priv || !g_base_info_equal(info(), source_priv->info()))
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
void BoxedInstance::allocate_directly(void) {
    g_assert(get_prototype()->can_allocate_directly());

    own_ptr(g_malloc0(g_struct_info_get_size(info())));
    m_allocated_directly = true;

    debug_lifecycle("Boxed pointer directly allocated");
}

// When initializing a boxed object from a hash of properties, we don't want to
// do n O(n) lookups, so put put the fields into a hash table and store it on
// proto->priv for fast lookup.
std::unique_ptr<BoxedPrototype::FieldMap> BoxedPrototype::create_field_map(
    JSContext* cx, GIStructInfo* struct_info) {
    int n_fields;
    int i;

    auto result = std::make_unique<BoxedPrototype::FieldMap>();
    n_fields = g_struct_info_get_n_fields(struct_info);
    if (!result->reserve(n_fields)) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }

    for (i = 0; i < n_fields; i++) {
        GjsAutoFieldInfo field_info = g_struct_info_get_field(struct_info, i);

        // We get the string as a jsid later, which is interned. We intern the
        // string here as well, so it will be the same string pointer
        JSString* atom = JS_AtomizeAndPinString(cx, field_info.name());

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
bool BoxedPrototype::ensure_field_map(JSContext* cx) {
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
GIFieldInfo* BoxedPrototype::lookup_field(JSContext* cx, JSString* prop_name) {
    if (!ensure_field_map(cx))
        return nullptr;

    auto entry = m_field_map->lookup(prop_name);
    if (!entry) {
        gjs_throw(cx, "No field %s on boxed type %s",
                  gjs_debug_string(prop_name).c_str(), name());
        return nullptr;
    }

    return entry->value().get();
}

/* Initialize a newly created Boxed from an object that is a "hash" of
 * properties to set as fieds of the object. We don't require that every field
 * of the object be set.
 */
bool BoxedInstance::init_from_props(JSContext* context, JS::Value props_value) {
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

        GIFieldInfo* field_info =
            get_prototype()->lookup_field(context, ids[ix].toString());
        if (!field_info)
            return false;

        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        if (!gjs_object_require_property(context, props, "property list",
                                         JS::HandleId::fromMarkedLocation(ids[ix].address()),
                                         &value))
            return false;

        if (!field_setter_impl(context, field_info, value))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool boxed_invoke_constructor(JSContext* context, JS::HandleObject obj,
                                     JS::HandleId constructor_name,
                                     const JS::CallArgs& args) {
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    JS::RootedObject js_constructor(context);

    if (!gjs_object_require_property(
            context, obj, nullptr, gjs->atoms().constructor(), &js_constructor))
        return false;

    JS::RootedValue js_constructor_func(context);
    if (!gjs_object_require_property(context, js_constructor, NULL,
                                     constructor_name, &js_constructor_func))
        return false;

    return gjs->call_function(nullptr, js_constructor_func, args, args.rval());
}

/*
 * BoxedInstance::copy_boxed:
 *
 * Allocate a new boxed pointer using g_boxed_copy(), either from a raw boxed
 * pointer or another BoxedInstance.
 */
void BoxedInstance::copy_boxed(void* boxed_ptr) {
    own_ptr(g_boxed_copy(gtype(), boxed_ptr));
    debug_lifecycle("Boxed pointer created with g_boxed_copy()");
}

void BoxedInstance::copy_boxed(BoxedInstance* source) {
    copy_boxed(source->ptr());
}

/*
 * BoxedInstance::copy_memory:
 *
 * Allocate a new boxed pointer by copying the contents of another boxed pointer
 * or another BoxedInstance.
 */
void BoxedInstance::copy_memory(void* boxed_ptr) {
    allocate_directly();
    memcpy(m_ptr, boxed_ptr, g_struct_info_get_size(info()));
}

void BoxedInstance::copy_memory(BoxedInstance* source) {
    copy_memory(source->ptr());
}

// See GIWrapperBase::constructor().
bool BoxedInstance::constructor_impl(JSContext* context, JS::HandleObject obj,
                                     const JS::CallArgs& args) {
    // Short-circuit copy-construction in the case where we can use copy_boxed()
    // or copy_memory()
    BoxedBase* source_priv;
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

    if (gtype() == G_TYPE_VARIANT) {
        /* Short-circuit construction for GVariants by calling into the JS packing
           function */
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        if (!boxed_invoke_constructor(context, obj, atoms.new_internal(), args))
            return false;

        // The return value of GLib.Variant.new_internal() gets its own
        // BoxedInstance, and the one we're setting up in this constructor is
        // discarded.
        debug_lifecycle(
            "Boxed construction delegated to GVariant constructor, "
            "boxed object discarded");

        return true;
    }

    BoxedPrototype* proto = get_prototype();

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
        GjsAutoFunctionInfo func_info = proto->zero_args_constructor_info();

        GIArgument rval_arg;
        GError *error = NULL;

        if (!g_function_info_invoke(func_info, NULL, 0, NULL, 0, &rval_arg, &error)) {
            gjs_throw(context, "Failed to invoke boxed constructor: %s", error->message);
            g_clear_error(&error);
            return false;
        }

        own_ptr(gjs_arg_steal<void*>(&rval_arg));

        debug_lifecycle("Boxed pointer created from zero-args constructor");

    } else if (proto->can_allocate_directly_without_pointers()) {
        allocate_directly();
    } else if (proto->has_default_constructor()) {
        /* for simplicity, we simply delegate all the work to the actual JS
         * constructor function (which we retrieve from the JS constructor,
         * that is, Namespace.BoxedType, or object.constructor, given that
         * object was created with the right prototype. */
        if (!boxed_invoke_constructor(context, obj,
                                      proto->default_constructor_name(), args))
            return false;

        // Define the expected Error properties
        if (gtype() == G_TYPE_ERROR) {
            JS::RootedObject gerror(context, &args.rval().toObject());
            if (!gjs_define_error_properties(context, gerror))
                return false;
        }

        // The return value of the JS constructor gets its own BoxedInstance,
        // and this one is discarded.
        debug_lifecycle(
            "Boxed construction delegated to JS constructor, "
            "boxed object discarded");

        return true;
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

BoxedInstance::~BoxedInstance() {
    if (m_owning_ptr) {
        if (m_allocated_directly) {
            if (gtype() == G_TYPE_VALUE)
                g_value_unset(m_ptr.as<GValue>());
            g_free(m_ptr.release());
        } else {
            if (g_type_is_a(gtype(), G_TYPE_BOXED))
                g_boxed_free(gtype(), m_ptr.release());
            else if (g_type_is_a(gtype(), G_TYPE_VARIANT))
                g_variant_unref(static_cast<GVariant*>(m_ptr.release()));
            else
                g_assert_not_reached ();
        }
    }

    GJS_DEC_COUNTER(boxed_instance);
}

BoxedPrototype::~BoxedPrototype(void) {
    GJS_DEC_COUNTER(boxed_prototype);
}

/*
 * BoxedBase::get_field_info:
 *
 * Does the same thing as g_struct_info_get_field(), but throws a JS exception
 * if there is no such field.
 */
GIFieldInfo* BoxedBase::get_field_info(JSContext* cx, uint32_t id) const {
    GIFieldInfo* field_info = g_struct_info_get_field(info(), id);
    if (field_info == NULL) {
        gjs_throw(cx, "No field %d on boxed type %s", id, name());
        return NULL;
    }

    return field_info;
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
bool BoxedInstance::get_nested_interface_object(
    JSContext* context, JSObject* parent_obj, GIFieldInfo* field_info,
    GIBaseInfo* interface_info, JS::MutableHandleValue value) const {
    int offset;

    if (!struct_is_simple(reinterpret_cast<GIStructInfo*>(interface_info))) {
        gjs_throw(context, "Reading field %s.%s is not supported", name(),
                  g_base_info_get_name(field_info));

        return false;
    }

    offset = g_field_info_get_offset (field_info);

    JS::RootedObject obj(context, gjs_new_object_with_generic_prototype(
                                      context, interface_info));
    if (!obj)
        return false;

    BoxedInstance* priv = BoxedInstance::new_for_js_object(context, obj);

    /* A structure nested inside a parent object; doesn't have an independent allocation */
    priv->share_ptr(raw_ptr() + offset);
    priv->debug_lifecycle(
        "Boxed pointer created, pointing inside memory owned by parent");

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
bool BoxedBase::field_getter(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(context, argc, vp, args, obj, BoxedBase, priv);
    if (!priv->check_is_instance(context, "get a field"))
        return false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    GjsAutoFieldInfo field_info = priv->get_field_info(context, field_ix);
    if (!field_info)
        return false;

    return priv->to_instance()->field_getter_impl(context, obj, field_info,
                                                  args.rval());
}

// See BoxedBase::field_getter().
bool BoxedInstance::field_getter_impl(JSContext* cx, JSObject* obj,
                                      GIFieldInfo* field_info,
                                      JS::MutableHandleValue rval) const {
    GjsAutoTypeInfo type_info = g_field_info_get_type(field_info);

    if (!g_type_info_is_pointer(type_info) &&
        g_type_info_get_tag(type_info) == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info = g_type_info_get_interface(type_info);

        if (interface_info.type() == GI_INFO_TYPE_STRUCT ||
            interface_info.type() == GI_INFO_TYPE_BOXED) {
            return get_nested_interface_object(cx, obj, field_info,
                                               interface_info, rval);
        }
    }

    GIArgument arg;
    if (!g_field_info_get_field(field_info, m_ptr, &arg)) {
        gjs_throw(cx, "Reading field %s.%s is not supported", name(),
                  g_base_info_get_name(field_info));
        return false;
    }

    if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_length(type_info) != -1) {
        auto length_field_ix = g_type_info_get_array_length(type_info);
        GjsAutoFieldInfo length_field_info =
            get_field_info(cx, length_field_ix);
        if (!length_field_info) {
            gjs_throw(cx, "Reading field %s.%s is not supported", name(),
                      g_base_info_get_name(length_field_info));
            return false;
        }

        GIArgument length_arg;
        if (!g_field_info_get_field(length_field_info, m_ptr, &length_arg)) {
            gjs_throw(cx, "Reading field %s.%s is not supported", name(),
                      g_base_info_get_name(length_field_info));
            return false;
        }

        GjsAutoTypeInfo length_type_info =
            g_field_info_get_type(length_field_info);
        size_t length = gjs_g_argument_get_array_length(
            g_type_info_get_tag(length_type_info), &length_arg);
        return gjs_value_from_explicit_array(cx, rval, type_info, &arg, length);
    }

    return gjs_value_from_g_argument(cx, rval, type_info, &arg, true);
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
bool BoxedInstance::set_nested_interface_object(JSContext* context,
                                                GIFieldInfo* field_info,
                                                GIBaseInfo* interface_info,
                                                JS::HandleValue value) {
    int offset;

    if (!struct_is_simple(reinterpret_cast<GIStructInfo*>(interface_info))) {
        gjs_throw(context, "Writing field %s.%s is not supported", name(),
                  g_base_info_get_name(field_info));

        return false;
    }

    JS::RootedObject proto(
        context, gjs_lookup_generic_prototype(context, interface_info));

    if (!proto)
        return false;

    /* If we can't directly copy from the source object we need
     * to construct a new temporary object.
     */
    BoxedBase* source_priv = get_copy_source(context, value);
    if (!source_priv) {
        JS::RootedValueArray<1> args(context);
        args[0].set(value);
        JS::RootedObject tmp_object(context,
            gjs_construct_object_dynamic(context, proto, args));
        if (!tmp_object || !for_js_typecheck(context, tmp_object, &source_priv))
            return false;
    }

    if (!source_priv->check_is_instance(context, "copy"))
        return false;

    offset = g_field_info_get_offset (field_info);
    memcpy(raw_ptr() + offset, source_priv->to_instance()->ptr(),
           g_struct_info_get_size(source_priv->info()));

    return true;
}

// See BoxedBase::field_setter().
bool BoxedInstance::field_setter_impl(JSContext* context,
                                      GIFieldInfo* field_info,
                                      JS::HandleValue value) {
    GArgument arg;
    GjsAutoTypeInfo type_info = g_field_info_get_type(field_info);

    if (!g_type_info_is_pointer (type_info) &&
        g_type_info_get_tag (type_info) == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info = g_type_info_get_interface(type_info);

        if (interface_info.type() == GI_INFO_TYPE_STRUCT ||
            interface_info.type() == GI_INFO_TYPE_BOXED) {
            return set_nested_interface_object(context, field_info,
                                               interface_info, value);
        }
    }

    if (!gjs_value_to_g_argument(context, value, type_info,
                                 g_base_info_get_name(field_info),
                                 GJS_ARGUMENT_FIELD, GI_TRANSFER_NOTHING,
                                 GjsArgumentFlags::MAY_BE_NULL, &arg))
        return false;

    bool success = true;
    if (!g_field_info_set_field(field_info, m_ptr, &arg)) {
        gjs_throw(context, "Writing field %s.%s is not supported", name(),
                  g_base_info_get_name(field_info));
        success = false;
    }

    JS::AutoSaveExceptionState saved_exc(context);
    if (!gjs_g_argument_release(context, GI_TRANSFER_NOTHING, type_info, &arg))
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
bool BoxedBase::field_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, BoxedBase, priv);
    if (!priv->check_is_instance(cx, "set a field"))
        return false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    GjsAutoFieldInfo field_info = priv->get_field_info(cx, field_ix);
    if (!field_info)
        return false;

    if (!priv->to_instance()->field_setter_impl(cx, field_info, args[0]))
        return false;

    args.rval().setUndefined();  /* No stored value */
    return true;
}

/*
 * BoxedPrototype::define_boxed_class_fields:
 *
 * Defines properties on the JS prototype object, with JSNative getters and
 * setters, for all the fields exposed by GObject introspection.
 */
bool BoxedPrototype::define_boxed_class_fields(JSContext* cx,
                                               JS::HandleObject proto) {
    int n_fields = g_struct_info_get_n_fields(info());
    int i;

    /* We define all fields as read/write so that the user gets an
     * error message. If we omitted fields or defined them read-only
     * we'd:
     *
     *  - Store a new property for a non-accessible field
     *  - Silently do nothing when writing a read-only field
     *
     * Which is pretty confusing if the only reason a field isn't
     * writable is language binding or memory-management restrictions.
     *
     * We just go ahead and define the fields immediately for the
     * class; doing it lazily in boxed_resolve() would be possible
     * as well if doing it ahead of time caused to much start-up
     * memory overhead.
     */
    for (i = 0; i < n_fields; i++) {
        GjsAutoFieldInfo field = g_struct_info_get_field(info(), i);
        JS::RootedValue private_id(cx, JS::PrivateUint32Value(i));
        if (!gjs_define_property_dynamic(cx, proto, field.name(), "boxed_field",
                                         &BoxedBase::field_getter,
                                         &BoxedBase::field_setter, private_id,
                                         GJS_MODULE_PROP_FLAGS))
            return false;
    }

    return true;
}

// Overrides GIWrapperPrototype::trace_impl().
void BoxedPrototype::trace_impl(JSTracer* trc) {
    JS::TraceEdge<jsid>(trc, &m_default_constructor_name,
                        "Boxed::default_constructor_name");
    if (m_field_map)
        m_field_map->trace(trc);
}

// clang-format off
const struct JSClassOps BoxedBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    &BoxedBase::new_enumerate,
    &BoxedBase::resolve,
    nullptr,  // mayResolve
    &BoxedBase::finalize,
    nullptr,  // call
    nullptr,  // construct
    &BoxedBase::trace
};

/* We allocate 1 reserved slot; this is typically unused, but if the
 * boxed is for a nested structure inside a parent structure, the
 * reserved slot is used to hold onto the parent Javascript object and
 * make sure it doesn't get freed.
 */
const struct JSClass BoxedBase::klass = {
    "GObject_Boxed",
    JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
    &BoxedBase::class_ops
};
// clang-format on

[[nodiscard]] static bool type_can_be_allocated_directly(
    GITypeInfo* type_info) {
    bool is_simple = true;

    if (g_type_info_is_pointer(type_info)) {
        if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_ARRAY &&
            g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            GjsAutoBaseInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            return type_can_be_allocated_directly(param_info);
        }

        return true;
    } else {
        switch (g_type_info_get_tag(type_info)) {
        case GI_TYPE_TAG_INTERFACE:
            {
            GjsAutoBaseInfo interface = g_type_info_get_interface(type_info);
            switch (g_base_info_get_type(interface)) {
                case GI_INFO_TYPE_BOXED:
                case GI_INFO_TYPE_STRUCT:
                    return struct_is_simple(interface.as<GIStructInfo>());
                case GI_INFO_TYPE_UNION:
                    /* FIXME: Need to implement */
                    is_simple = false;
                    break;
                case GI_INFO_TYPE_OBJECT:
                case GI_INFO_TYPE_VFUNC:
                case GI_INFO_TYPE_CALLBACK:
                case GI_INFO_TYPE_INVALID:
                case GI_INFO_TYPE_INTERFACE:
                case GI_INFO_TYPE_FUNCTION:
                case GI_INFO_TYPE_CONSTANT:
                case GI_INFO_TYPE_VALUE:
                case GI_INFO_TYPE_SIGNAL:
                case GI_INFO_TYPE_PROPERTY:
                case GI_INFO_TYPE_FIELD:
                case GI_INFO_TYPE_ARG:
                case GI_INFO_TYPE_TYPE:
                case GI_INFO_TYPE_UNRESOLVED:
                    is_simple = false;
                    break;
                case GI_INFO_TYPE_INVALID_0:
                    g_assert_not_reached();
                    break;
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                default:
                    break;
            }
                break;
            }
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_UNICHAR:
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        default:
            break;
        }
    }
    return is_simple;
}

[[nodiscard]] static bool simple_struct_has_pointers(GIStructInfo*);

[[nodiscard]] static bool direct_allocation_has_pointers(
    GITypeInfo* type_info) {
    if (g_type_info_is_pointer(type_info)) {
        if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_ARRAY &&
            g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            GjsAutoBaseInfo param_info =
                g_type_info_get_param_type(type_info, 0);
            return direct_allocation_has_pointers(param_info);
        }

        return g_type_info_get_tag(type_info) != GI_TYPE_TAG_VOID;
    }

    if (g_type_info_get_tag(type_info) != GI_TYPE_TAG_INTERFACE)
        return false;

    GjsAutoBaseInfo interface = g_type_info_get_interface(type_info);
    if (interface.type() == GI_INFO_TYPE_BOXED ||
        interface.type() == GI_INFO_TYPE_STRUCT)
        return simple_struct_has_pointers(interface.as<GIStructInfo>());

    return false;
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
[[nodiscard]] static bool struct_is_simple(GIStructInfo* info) {
    int n_fields = g_struct_info_get_n_fields(info);
    bool is_simple = true;
    int i;

    /* If it's opaque, it's not simple */
    if (n_fields == 0)
        return false;

    for (i = 0; i < n_fields && is_simple; i++) {
        GjsAutoBaseInfo field_info = g_struct_info_get_field(info, i);
        GjsAutoBaseInfo type_info = g_field_info_get_type(field_info);

        is_simple = type_can_be_allocated_directly(type_info);
    }

    return is_simple;
}

[[nodiscard]] static bool simple_struct_has_pointers(GIStructInfo* info) {
    g_assert(struct_is_simple(info) &&
             "Don't call simple_struct_has_pointers() on a non-simple struct");

    int n_fields = g_struct_info_get_n_fields(info);
    g_assert(n_fields > 0);

    for (int i = 0; i < n_fields; i++) {
        GjsAutoBaseInfo field_info = g_struct_info_get_field(info, i);
        GjsAutoBaseInfo type_info = g_field_info_get_type(field_info);
        if (direct_allocation_has_pointers(type_info))
            return true;
    }
    return false;
}

BoxedPrototype::BoxedPrototype(GIStructInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype),
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
    GJS_INC_COUNTER(boxed_prototype);
}

// Overrides GIWrapperPrototype::init().
bool BoxedPrototype::init(JSContext* context) {
    int i, n_methods;
    int first_constructor = -1;
    jsid first_constructor_name = JS::PropertyKey::Void();
    jsid zero_args_constructor_name = JS::PropertyKey::Void();

    if (m_gtype != G_TYPE_NONE) {
        /* If the structure is registered as a boxed, we can create a new instance by
         * looking for a zero-args constructor and calling it; constructors don't
         * really make sense for non-boxed types, since there is no memory management
         * for the return value.
         */
        n_methods = g_struct_info_get_n_methods(m_info);

        for (i = 0; i < n_methods; ++i) {
            GIFunctionInfoFlags flags;

            GjsAutoFunctionInfo func_info = g_struct_info_get_method(m_info, i);

            flags = g_function_info_get_flags(func_info);
            if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0) {
                if (first_constructor < 0) {
                    first_constructor = i;
                    first_constructor_name =
                        gjs_intern_string_to_id(context, func_info.name());
                    if (first_constructor_name.isVoid())
                        return false;
                }

                if (m_zero_args_constructor < 0 &&
                    g_callable_info_get_n_args(func_info) == 0) {
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
 * BoxedPrototype::define_class:
 * @in_object: Object where the constructor is stored, typically a repo object.
 * @info: Introspection info for the boxed class.
 *
 * Define a boxed class constructor and prototype, including all the necessary
 * methods and properties.
 */
bool BoxedPrototype::define_class(JSContext* context,
                                  JS::HandleObject in_object,
                                  GIStructInfo* info) {
    JS::RootedObject prototype(context), unused_constructor(context);
    GType gtype = g_registered_type_info_get_g_type(info);
    BoxedPrototype* priv = BoxedPrototype::create_class(
        context, in_object, info, gtype, &unused_constructor, &prototype);
    if (!priv || !priv->define_boxed_class_fields(context, prototype))
        return false;

    if (gtype == G_TYPE_ERROR &&
        !JS_DefineFunction(context, prototype, "toString",
                           &ErrorBase::to_string, 0, GJS_MODULE_PROP_FLAGS))
        return false;

    return true;
}

/* Helper function to make the public API more readable. The overloads are
 * specified explicitly in the public API, but the implementation uses
 * std::forward in order to avoid duplicating code. */
template <typename... Args>
JSObject* BoxedInstance::new_for_c_struct_impl(JSContext* cx,
                                               GIStructInfo* info, void* gboxed,
                                               Args&&... args) {
    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    JS::RootedObject obj(cx, gjs_new_object_with_generic_prototype(cx, info));
    if (!obj)
        return nullptr;

    BoxedInstance* priv = BoxedInstance::new_for_js_object(cx, obj);
    if (!priv)
        return nullptr;

    if (!priv->init_from_c_struct(cx, gboxed, std::forward<Args>(args)...))
        return nullptr;

    if (priv->gtype() == G_TYPE_ERROR && !gjs_define_error_properties(cx, obj))
        return nullptr;

    return obj;
}

/*
 * BoxedInstance::new_for_c_struct:
 *
 * Creates a new BoxedInstance JS object from a C boxed struct pointer.
 *
 * There are two overloads of this method; the NoCopy overload will simply take
 * the passed-in pointer but not own it, while the normal method will take a
 * reference, or if the boxed type can be directly allocated, copy the memory.
 */
JSObject* BoxedInstance::new_for_c_struct(JSContext* cx, GIStructInfo* info,
                                          void* gboxed) {
    return new_for_c_struct_impl(cx, info, gboxed);
}

JSObject* BoxedInstance::new_for_c_struct(JSContext* cx, GIStructInfo* info,
                                          void* gboxed, NoCopy no_copy) {
    return new_for_c_struct_impl(cx, info, gboxed, no_copy);
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
bool BoxedInstance::init_from_c_struct(JSContext*, void* gboxed, NoCopy) {
    // We need to create a JS Boxed which references the original C struct, not
    // a copy of it. Used for G_SIGNAL_TYPE_STATIC_SCOPE.
    share_ptr(gboxed);
    debug_lifecycle("Boxed pointer acquired, memory not owned");
    return true;
}

bool BoxedInstance::init_from_c_struct(JSContext* cx, void* gboxed) {
    if (gtype() != G_TYPE_NONE && g_type_is_a(gtype(), G_TYPE_BOXED)) {
        copy_boxed(gboxed);
        return true;
    } else if (gtype() == G_TYPE_VARIANT) {
        // Sink the reference if it is floating
        GVariant* temp = g_variant_take_ref(static_cast<GVariant*>(gboxed));
        // Add an additional reference which will be unref-ed
        // in the marshaller
        own_ptr(g_variant_ref(temp));
        debug_lifecycle("Boxed pointer created by taking GVariant ref");
        return true;
    } else if (get_prototype()->can_allocate_directly()) {
        copy_memory(gboxed);
        return true;
    }

    gjs_throw(cx, "Can't create a Javascript object for %s; no way to copy",
              name());
    return false;
}

void* BoxedInstance::copy_ptr(JSContext* cx, GType gtype, void* ptr) {
    if (g_type_is_a(gtype, G_TYPE_BOXED))
        return g_boxed_copy(gtype, ptr);
    if (g_type_is_a(gtype, G_TYPE_VARIANT))
        return g_variant_ref(static_cast<GVariant*>(ptr));

    gjs_throw(cx,
              "Can't transfer ownership of a structure type not registered as "
              "boxed");
    return nullptr;
}
