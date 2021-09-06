/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <stdint.h>
#include <string.h>  // for memset, strcmp

#include <algorithm>  // for find
#include <functional>  // for mem_fn
#include <limits>
#include <string>
#include <tuple>        // for tie
#include <type_traits>
#include <unordered_set>
#include <utility>      // for move
#include <vector>

#include <ffi.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/GCAPI.h>               // for JS_AddWeakPointerCompartmentCallback
#include <js/GCVector.h>            // for MutableWrappedPtrOperations
#include <js/HeapAPI.h>
#include <js/MemoryFunctions.h>     // for AddAssociatedMemory, RemoveAssoci...
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_READONLY
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>        // for JS_ReportOutOfMemory, IsCallable
#include <jsfriendapi.h>  // for JS_GetObjectFunction, IsFunctionO...
#include <mozilla/HashTable.h>

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/closure.h"
#include "gi/cwrapper.h"
#include "gi/function.h"
#include "gi/gjs_gi_trace.h"
#include "gi/object.h"
#include "gi/repo.h"
#include "gi/toggle.h"
#include "gi/utils-inl.h"  // for gjs_int_to_pointer
#include "gi/value.h"
#include "gi/wrapperutils.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/mem-private.h"
#include "gjs/profiler-private.h"
#include "util/log.h"

class JSTracer;

/* This is a trick to print out the sizes of the structs at compile time, in
 * an error message. */
// template <int s> struct Measure;
// Measure<sizeof(ObjectInstance)> instance_size;
// Measure<sizeof(ObjectPrototype)> prototype_size;

#if defined(__x86_64__) && defined(__clang__)
/* This isn't meant to be comprehensive, but should trip on at least one CI job
 * if sizeof(ObjectInstance) is increased. */
static_assert(sizeof(ObjectInstance) <= 48,
              "Think very hard before increasing the size of ObjectInstance. "
              "There can be tens of thousands of them alive in a typical "
              "gnome-shell run.");
#endif  // x86-64 clang

bool ObjectInstance::s_weak_pointer_callback = false;
decltype(ObjectInstance::s_wrapped_gobject_list)
    ObjectInstance::s_wrapped_gobject_list;

static const auto DISPOSED_OBJECT = std::numeric_limits<uintptr_t>::max();

// clang-format off
G_DEFINE_QUARK(gjs::custom-type, ObjectBase::custom_type)
G_DEFINE_QUARK(gjs::custom-property, ObjectBase::custom_property)
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

// Plain g_type_query fails and leaves @query uninitialized for dynamic types.
// See https://gitlab.gnome.org/GNOME/glib/issues/623
void ObjectBase::type_query_dynamic_safe(GTypeQuery* query) {
    GType type = gtype();
    while (g_type_get_qdata(type, ObjectBase::custom_type_quark()))
        type = g_type_parent(type);

    g_type_query(type, query);
}

void ObjectInstance::link() {
    g_assert(std::find(s_wrapped_gobject_list.begin(),
                       s_wrapped_gobject_list.end(),
                       this) == s_wrapped_gobject_list.end());
    s_wrapped_gobject_list.push_back(this);
}

void ObjectInstance::unlink() {
    Gjs::remove_one_from_unsorted_vector(&s_wrapped_gobject_list, this);
}

const void* ObjectBase::jsobj_addr(void) const {
    if (is_prototype())
        return nullptr;
    return to_instance()->m_wrapper.debug_addr();
}

// Overrides GIWrapperBase::typecheck(). We only override the overload that
// throws, so that we can throw our own more informative error.
bool ObjectBase::typecheck(JSContext* cx, JS::HandleObject obj,
                           GIObjectInfo* expected_info, GType expected_gtype) {
    if (GIWrapperBase::typecheck(cx, obj, expected_info, expected_gtype))
        return true;

    gjs_throw(cx,
              "This JS object wrapper isn't wrapping a GObject."
              " If this is a custom subclass, are you sure you chained"
              " up to the parent _init properly?");
    return false;
}

bool ObjectInstance::check_gobject_disposed_or_finalized(
    const char* for_what) const {
    if (!m_gobj_disposed)
        return true;

    g_critical(
        "Object %s.%s (%p), has been already %s — impossible to %s "
        "it. This might be caused by the object having been destroyed from C "
        "code using something such as destroy(), dispose(), or remove() "
        "vfuncs.",
        ns(), name(), m_ptr.get(), m_gobj_finalized ? "finalized" : "disposed",
        for_what);
    gjs_dumpstack();
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

GParamSpec* ObjectPrototype::find_param_spec_from_id(JSContext* cx,
                                                     JS::HandleString key) {
    /* First check for the ID in the cache */
    auto entry = m_property_cache.lookupForAdd(key);
    if (entry)
        return entry->value();

    JS::UniqueChars js_prop_name(JS_EncodeStringToUTF8(cx, key));
    if (!js_prop_name)
        return nullptr;

    GjsAutoChar gname = gjs_hyphen_from_camel(js_prop_name.get());
    GjsAutoTypeClass<GObjectClass> gobj_class(m_gtype);
    GParamSpec* pspec = g_object_class_find_property(gobj_class, gname);
    GjsAutoParam param_spec(pspec, GjsAutoTakeOwnership());

    if (!param_spec) {
        gjs_wrapper_throw_nonexistent_field(cx, m_gtype, js_prop_name.get());
        return nullptr;
    }

    if (!m_property_cache.add(entry, key, std::move(param_spec))) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }
    return pspec; /* owned by property cache */
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

    if (!ensure_uses_toggle_ref(cx)) {
        gjs_throw(cx, "Impossible to set toggle references on %sobject %p",
                  m_gobj_disposed ? "disposed " : "", m_ptr.get());
        return false;
    }

    return true;
}

bool ObjectBase::prop_getter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    std::string fullName = priv->format_name() + "." + gjs_debug_string(name);
    AutoProfilerLabel label(cx, "property getter", fullName.c_str());

    priv->debug_jsprop("Property getter", name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->to_instance()->prop_getter_impl(cx, name, args.rval());
}

bool ObjectInstance::prop_getter_impl(JSContext* cx, JS::HandleString name,
                                      JS::MutableHandleValue rval) {
    if (!check_gobject_finalized("get any property from")) {
        rval.setUndefined();
        return true;
    }

    ObjectPrototype* proto_priv = get_prototype();
    GParamSpec *param = proto_priv->find_param_spec_from_id(cx, name);

    /* This is guaranteed because we resolved the property before */
    g_assert(param);

    /* Do not fetch JS overridden properties from GObject, to avoid
     * infinite recursion. */
    if (g_param_spec_get_qdata(param, ObjectBase::custom_property_quark()))
        return true;

    if ((param->flags & G_PARAM_READABLE) == 0) {
        rval.setUndefined();
        return true;
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Accessing GObject property %s",
                     param->name);

    Gjs::AutoGValue gvalue(G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(m_ptr, param->name, &gvalue);

    return gjs_value_from_g_value(cx, rval, &gvalue);
}

[[nodiscard]] static GjsAutoFieldInfo lookup_field_info(GIObjectInfo* info,
                                                        const char* name) {
    int n_fields = g_object_info_get_n_fields(info);
    int ix;
    GjsAutoFieldInfo retval;

    for (ix = 0; ix < n_fields; ix++) {
        retval = g_object_info_get_field(info, ix);
        if (strcmp(name, retval.name()) == 0)
            break;
        retval.reset();
    }

    if (!retval || !(g_field_info_get_flags(retval) & GI_FIELD_IS_READABLE))
        return nullptr;

    return retval;
}

bool ObjectBase::field_getter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    std::string fullName = priv->format_name() + "." + gjs_debug_string(name);
    AutoProfilerLabel label(cx, "field getter", fullName.c_str());

    priv->debug_jsprop("Field getter", name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->to_instance()->field_getter_impl(cx, name, args.rval());
}

bool ObjectInstance::field_getter_impl(JSContext* cx, JS::HandleString name,
                                       JS::MutableHandleValue rval) {
    if (!check_gobject_finalized("get any property from"))
        return true;

    ObjectPrototype* proto_priv = get_prototype();
    GIFieldInfo* field = proto_priv->lookup_cached_field_info(cx, name);
    GITypeTag tag;
    GIArgument arg = { 0 };

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Overriding %s with GObject field",
                     gjs_debug_string(name).c_str());

    GjsAutoTypeInfo type = g_field_info_get_type(field);
    tag = g_type_info_get_tag(type);

    switch (tag) {
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_INTERFACE:
            gjs_throw(cx,
                      "Can't get field %s; GObject introspection supports only "
                      "fields with simple types, not %s",
                      gjs_debug_string(name).c_str(),
                      g_type_tag_to_string(tag));
            return false;

        default:
            break;
    }

    if (!g_field_info_get_field(field, m_ptr, &arg)) {
        gjs_throw(cx, "Error getting field %s from object",
                  gjs_debug_string(name).c_str());
        return false;
    }

    return gjs_value_from_g_argument(cx, rval, type, &arg, true);
    /* copy_structs is irrelevant because g_field_info_get_field() doesn't
     * handle boxed types */
}

/* Dynamic setter for GObject properties. Returns false on OOM/exception.
 * args.rval() becomes the "stored value" for the property. */
bool ObjectBase::prop_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    std::string fullName = priv->format_name() + "." + gjs_debug_string(name);
    AutoProfilerLabel label(cx, "property setter", fullName.c_str());

    priv->debug_jsprop("Property setter", name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* Clear the JS stored value, to avoid keeping additional references */
    args.rval().setUndefined();

    return priv->to_instance()->prop_setter_impl(cx, name, args[0]);
}

bool ObjectInstance::prop_setter_impl(JSContext* cx, JS::HandleString name,
                                      JS::HandleValue value) {
    if (!check_gobject_finalized("set any property on"))
        return true;

    ObjectPrototype* proto_priv = get_prototype();
    GParamSpec *param_spec = proto_priv->find_param_spec_from_id(cx, name);
    if (!param_spec)
        return false;

    /* Do not set JS overridden properties through GObject, to avoid
     * infinite recursion (unless constructing) */
    if (g_param_spec_get_qdata(param_spec, ObjectBase::custom_property_quark()))
        return true;

    if (!(param_spec->flags & G_PARAM_WRITABLE))
        /* prevent setting the prop even in JS */
        return gjs_wrapper_throw_readonly_field(cx, gtype(), param_spec->name);

    if (param_spec->flags & G_PARAM_DEPRECATED)
        _gjs_warn_deprecated_once_per_callsite(cx, DeprecatedGObjectProperty);

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Setting GObject prop %s",
                     param_spec->name);

    Gjs::AutoGValue gvalue(G_PARAM_SPEC_VALUE_TYPE(param_spec));
    if (!gjs_value_to_g_value(cx, value, &gvalue))
        return false;

    g_object_set_property(m_ptr, param_spec->name, &gvalue);

    return true;
}

bool ObjectBase::field_setter(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ObjectBase, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    std::string fullName = priv->format_name() + "." + gjs_debug_string(name);
    AutoProfilerLabel label(cx, "field setter", fullName.c_str());

    priv->debug_jsprop("Field setter", name, obj);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* We have to update args.rval(), because JS caches it as the property's "stored
     * value" (https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/Stored_value)
     * and so subsequent gets would get the stored value instead of accessing
     * the field */
    args.rval().setUndefined();

    return priv->to_instance()->field_setter_not_impl(cx, name);
}

bool ObjectInstance::field_setter_not_impl(JSContext* cx,
                                           JS::HandleString name) {
    if (!check_gobject_finalized("set GObject field on"))
        return true;

    ObjectPrototype* proto_priv = get_prototype();
    GIFieldInfo* field = proto_priv->lookup_cached_field_info(cx, name);

    /* As far as I know, GI never exposes GObject instance struct fields as
     * writable, so no need to implement this for the time being */
    if (g_field_info_get_flags(field) & GI_FIELD_IS_WRITABLE) {
        g_message("Field %s of a GObject is writable, but setting it is not "
                  "implemented", gjs_debug_string(name).c_str());
        return true;
    }

    return gjs_wrapper_throw_readonly_field(cx, gtype(),
                                            g_base_info_get_name(field));
}

bool ObjectPrototype::is_vfunc_unchanged(GIVFuncInfo* info) {
    GType ptype = g_type_parent(m_gtype);
    GError *error = NULL;
    gpointer addr1, addr2;

    addr1 = g_vfunc_info_get_address(info, m_gtype, &error);
    if (error) {
        g_clear_error(&error);
        return false;
    }

    addr2 = g_vfunc_info_get_address(info, ptype, &error);
    if (error) {
        g_clear_error(&error);
        return false;
    }

    return addr1 == addr2;
}

[[nodiscard]] static GjsAutoVFuncInfo find_vfunc_on_parents(
    GIObjectInfo* info, const char* name, bool* out_defined_by_parent) {
    bool defined_by_parent = false;

    /* ref the first info so that we don't destroy
     * it when unrefing parents later */
    GjsAutoObjectInfo parent(info, GjsAutoTakeOwnership());

    /* Since it isn't possible to override a vfunc on
     * an interface without reimplementing it, we don't need
     * to search the parent types when looking for a vfunc. */
    GjsAutoVFuncInfo vfunc =
        g_object_info_find_vfunc_using_interfaces(parent, name, nullptr);
    while (!vfunc && parent) {
        parent = g_object_info_get_parent(parent);
        if (parent)
            vfunc = g_object_info_find_vfunc(parent, name);

        defined_by_parent = true;
    }

    if (out_defined_by_parent)
        *out_defined_by_parent = defined_by_parent;

    return vfunc;
}

/* Taken from GLib */
static void canonicalize_key(const GjsAutoChar& key) {
    for (char* p = key; *p != 0; p++) {
        char c = *p;

        if (c != '-' && (c < '0' || c > '9') && (c < 'A' || c > 'Z') &&
            (c < 'a' || c > 'z'))
            *p = '-';
    }
}

/* @name must already be canonicalized */
[[nodiscard]] static bool is_ginterface_property_name(GIInterfaceInfo* info,
                                                      const char* name) {
    int n_props = g_interface_info_get_n_properties(info);
    GjsAutoPropertyInfo prop_info;

    for (int ix = 0; ix < n_props; ix++) {
        prop_info = g_interface_info_get_property(info, ix);
        if (strcmp(name, prop_info.name()) == 0)
            break;
        prop_info.reset();
    }

    return !!prop_info;
}

bool ObjectPrototype::lazy_define_gobject_property(JSContext* cx,
                                                   JS::HandleObject obj,
                                                   JS::HandleId id,
                                                   bool* resolved,
                                                   const char* name) {
    bool found = false;
    if (!JS_AlreadyHasOwnPropertyById(cx, obj, id, &found))
        return false;
    if (found) {
        /* Already defined, so *resolved = false because we didn't just
         * define it */
        *resolved = false;
        return true;
    }

    debug_jsprop("Defining lazy GObject property", id, obj);

    JS::RootedValue private_id(cx, JS::StringValue(JSID_TO_STRING(id)));
    if (!gjs_define_property_dynamic(
            cx, obj, name, "gobject_prop", &ObjectBase::prop_getter,
            &ObjectBase::prop_setter, private_id,
            // Make property configurable so that interface properties can be
            // overridden by GObject.ParamSpec.override in the class that
            // implements them
            GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT))
        return false;

    *resolved = true;
    return true;
}

bool ObjectPrototype::resolve_no_info(JSContext* cx, JS::HandleObject obj,
                                      JS::HandleId id, bool* resolved,
                                      const char* name,
                                      ResolveWhat resolve_props) {
    guint n_interfaces;
    guint i;

    GjsAutoChar canonical_name;
    if (resolve_props == ConsiderMethodsAndProperties) {
        // Optimization: GObject property names must start with a letter
        if (g_ascii_isalpha(name[0])) {
            canonical_name = gjs_hyphen_from_camel(name);
            canonicalize_key(canonical_name);
        }
    }

    GIInterfaceInfo** interfaces;
    g_irepository_get_object_gtype_interfaces(nullptr, m_gtype, &n_interfaces,
        &interfaces);

    /* Fallback to GType system for non custom GObjects with no GI information
     */
    if (canonical_name && G_TYPE_IS_CLASSED(m_gtype) && !is_custom_js_class()) {
        GjsAutoTypeClass<GObjectClass> oclass(m_gtype);

        if (g_object_class_find_property(oclass, canonical_name))
            return lazy_define_gobject_property(cx, obj, id, resolved, name);

        for (i = 0; i < n_interfaces; i++) {
            GType iface_gtype =
                g_registered_type_info_get_g_type(interfaces[i]);
            if (!G_TYPE_IS_CLASSED(iface_gtype))
                continue;

            GjsAutoTypeClass<GObjectClass> iclass(iface_gtype);

            if (g_object_class_find_property(iclass, canonical_name))
                return lazy_define_gobject_property(cx, obj, id, resolved, name);
        }
    }

    for (i = 0; i < n_interfaces; i++) {
        GIInterfaceInfo* iface_info = interfaces[i];
        GjsAutoFunctionInfo method_info =
            g_interface_info_find_method(iface_info, name);
        if (method_info) {
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                if (!gjs_define_function(cx, obj, m_gtype, method_info))
                    return false;

                *resolved = true;
                return true;
            }
        }

        if (!canonical_name)
            continue;

        /* If the name refers to a GObject property, lazily define the property
         * in JS as we do below in the real resolve hook. We ignore fields here
         * because I don't think interfaces can have fields */
        if (is_ginterface_property_name(iface_info, canonical_name)) {
            GjsAutoTypeClass<GObjectClass> oclass(m_gtype);
            // unowned
            GParamSpec* pspec = g_object_class_find_property(
                oclass, canonical_name);  // unowned
            if (pspec && pspec->owner_type == m_gtype) {
                return lazy_define_gobject_property(cx, obj, id, resolved,
                                                    name);
            }
        }
    }

    *resolved = false;
    return true;
}

[[nodiscard]] static bool is_gobject_property_name(GIObjectInfo* info,
                                                   const char* name) {
    // Optimization: GObject property names must start with a letter
    if (!g_ascii_isalpha(name[0]))
        return false;

    int n_props = g_object_info_get_n_properties(info);
    int n_ifaces = g_object_info_get_n_interfaces(info);
    int ix;

    GjsAutoChar canonical_name = gjs_hyphen_from_camel(name);
    canonicalize_key(canonical_name);

    for (ix = 0; ix < n_props; ix++) {
        GjsAutoPropertyInfo prop_info = g_object_info_get_property(info, ix);
        if (strcmp(canonical_name, prop_info.name()) == 0)
            return true;
    }

    for (ix = 0; ix < n_ifaces; ix++) {
        GjsAutoInterfaceInfo iface_info = g_object_info_get_interface(info, ix);
        if (is_ginterface_property_name(iface_info, canonical_name))
            return true;
    }
    return false;
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
        GjsAutoVFuncInfo vfunc = find_vfunc_on_parents(
            m_info, name_without_vfunc_, &defined_by_parent);
        if (vfunc) {
            /* In the event that the vfunc is unchanged, let regular
             * prototypal inheritance take over. */
            if (defined_by_parent && is_vfunc_unchanged(vfunc)) {
                *resolved = false;
                return true;
            }

            if (!gjs_define_function(context, obj, m_gtype, vfunc))
                return false;

            *resolved = true;
            return true;
        }

        /* If the vfunc wasn't found, fall through, back to normal
         * method resolution. */
    }

    if (is_gobject_property_name(m_info, name))
        return lazy_define_gobject_property(context, obj, id, resolved, name);

    GjsAutoFieldInfo field_info = lookup_field_info(m_info, name);
    if (field_info) {
        bool found = false;
        if (!JS_AlreadyHasOwnPropertyById(context, obj, id, &found))
            return false;
        if (found) {
            *resolved = false;
            return true;
        }

        debug_jsprop("Defining lazy GObject field", id, obj);

        unsigned flags = GJS_MODULE_PROP_FLAGS;
        if (!(g_field_info_get_flags(field_info) & GI_FIELD_IS_WRITABLE))
            flags |= JSPROP_READONLY;

        JS::RootedString key(context, JSID_TO_STRING(id));
        if (!m_field_cache.putNew(key, field_info.release())) {
            JS_ReportOutOfMemory(context);
            return false;
        }

        JS::RootedValue private_id(context, JS::StringValue(key));
        if (!gjs_define_property_dynamic(
                context, obj, name, "gobject_field", &ObjectBase::field_getter,
                &ObjectBase::field_setter, private_id, flags))
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

    GjsAutoFunctionInfo method_info =
        g_object_info_find_method_using_interfaces(m_info, name, nullptr);

    /**
     * Search through any interfaces implemented by the GType;
     * See https://bugzilla.gnome.org/show_bug.cgi?id=632922
     * for background on why we need to do this.
     */
    if (!method_info)
        return resolve_no_info(context, obj, id, resolved, name,
                               ConsiderOnlyMethods);

#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(method_info);
#endif

    if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for %s (%s.%s)",
                  method_info.name(), type_name(), ns(), this->name());

        if (!gjs_define_function(context, obj, m_gtype, method_info))
            return false;

        *resolved = true; /* we defined the prop in obj */
    } else {
        *resolved = false;
    }

    return true;
}

bool ObjectPrototype::new_enumerate_impl(JSContext* cx, JS::HandleObject,
                                         JS::MutableHandleIdVector properties,
                                         bool only_enumerable
                                         [[maybe_unused]]) {
    unsigned n_interfaces;
    GType* interfaces = g_type_interfaces(gtype(), &n_interfaces);

    for (unsigned k = 0; k < n_interfaces; k++) {
        GjsAutoInterfaceInfo iface_info =
            g_irepository_find_by_gtype(nullptr, interfaces[k]);

        if (!iface_info) {
            continue;
        }

        int n_methods = g_interface_info_get_n_methods(iface_info);
        int n_properties = g_interface_info_get_n_properties(iface_info);
        if (!properties.reserve(properties.length() + n_methods +
                                n_properties)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        // Methods
        for (int i = 0; i < n_methods; i++) {
            GjsAutoFunctionInfo meth_info =
                g_interface_info_get_method(iface_info, i);
            GIFunctionInfoFlags flags = g_function_info_get_flags(meth_info);

            if (flags & GI_FUNCTION_IS_METHOD) {
                const char* name = meth_info.name();
                jsid id = gjs_intern_string_to_id(cx, name);
                if (id == JSID_VOID)
                    return false;
                properties.infallibleAppend(id);
            }
        }

        // Properties
        for (int i = 0; i < n_properties; i++) {
            GjsAutoPropertyInfo prop_info =
                g_interface_info_get_property(iface_info, i);

            GjsAutoChar js_name = gjs_hyphen_to_underscore(prop_info.name());

            jsid id = gjs_intern_string_to_id(cx, js_name);
            if (id == JSID_VOID)
                return false;
            properties.infallibleAppend(id);
        }
    }

    g_free(interfaces);

    if (info()) {
        int n_methods = g_object_info_get_n_methods(info());
        int n_properties = g_object_info_get_n_properties(info());
        if (!properties.reserve(properties.length() + n_methods +
                                n_properties)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        // Methods
        for (int i = 0; i < n_methods; i++) {
            GjsAutoFunctionInfo meth_info = g_object_info_get_method(info(), i);
            GIFunctionInfoFlags flags = g_function_info_get_flags(meth_info);

            if (flags & GI_FUNCTION_IS_METHOD) {
                const char* name = meth_info.name();
                jsid id = gjs_intern_string_to_id(cx, name);
                if (id == JSID_VOID)
                    return false;
                properties.infallibleAppend(id);
            }
        }

        // Properties
        for (int i = 0; i < n_properties; i++) {
            GjsAutoPropertyInfo prop_info =
                g_object_info_get_property(info(), i);

            GjsAutoChar js_name = gjs_hyphen_to_underscore(prop_info.name());
            jsid id = gjs_intern_string_to_id(cx, js_name);
            if (id == JSID_VOID)
                return false;
            properties.infallibleAppend(id);
        }
    }

    return true;
}

/* Set properties from args to constructor (args[0] is supposed to be
 * a hash) */
bool ObjectPrototype::props_to_g_parameters(JSContext* context,
                                            JS::HandleObject props,
                                            std::vector<const char*>* names,
                                            AutoGValueVector* values) {
    size_t ix, length;
    JS::RootedId prop_id(context);
    JS::RootedValue value(context);
    JS::Rooted<JS::IdVector> ids(context);
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

        if (!JSID_IS_STRING(prop_id))
            return gjs_wrapper_throw_nonexistent_field(
                context, m_gtype, gjs_debug_id(prop_id).c_str());

        JS::RootedString js_prop_name(context, JSID_TO_STRING(prop_id));
        GParamSpec *param_spec = find_param_spec_from_id(context, js_prop_name);
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

        names->push_back(param_spec->name);  /* owned by GParamSpec in cache */
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
    // Note: remove_if() does not actually remove elements, just reorders them
    // and returns a start iterator of elements to remove
    s_wrapped_gobject_list.erase(
        std::remove_if(s_wrapped_gobject_list.begin(),
                       s_wrapped_gobject_list.end(),
                       ([predicate, action](ObjectInstance* link) {
                           if (predicate(link)) {
                               action(link);
                               return true;
                           }
                           return false;
                       })),
        s_wrapped_gobject_list.end());
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

    if (m_uses_toggle_ref && !m_gobj_disposed)
        g_object_remove_toggle_ref(m_ptr.release(), wrapped_gobj_toggle_notify,
                                   this);
    else
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

ObjectInstance::ObjectInstance(JSContext* cx, JS::HandleObject object)
    : GIWrapperInstance(cx, object),
      m_wrapper_finalized(false),
      m_gobj_disposed(false),
      m_gobj_finalized(false),
      m_uses_toggle_ref(false) {
    GTypeQuery query;
    type_query_dynamic_safe(&query);
    if (G_LIKELY(query.type))
        JS::AddAssociatedMemory(object, query.instance_size,
                                MemoryUse::GObjectInstanceStruct);

    GJS_INC_COUNTER(object_instance);
}

ObjectPrototype::ObjectPrototype(GIObjectInfo* info, GType gtype)
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
void ObjectInstance::update_heap_wrapper_weak_pointers(JSContext*,
                                                       JS::Compartment*,
                                                       void*) {
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Weak pointer update callback, "
                        "%zu wrapped GObject(s) to examine",
                        ObjectInstance::num_wrapped_gobjects());

    // Take a lock on the queue till we're done with it, so that we don't
    // risk that another thread will queue something else while sweeping
    auto locked_queue = ToggleQueue::get_default();

    ObjectInstance::remove_wrapped_gobjects_if(
        std::mem_fn(&ObjectInstance::weak_pointer_was_finalized),
        std::mem_fn(&ObjectInstance::disassociate_js_gobject));

    s_wrapped_gobject_list.shrink_to_fit();
}

bool
ObjectInstance::weak_pointer_was_finalized(void)
{
    if (has_wrapper() && !wrapper_is_rooted()) {
        bool toggle_down_queued, toggle_up_queued;

        auto toggle_queue = ToggleQueue::get_default();
        std::tie(toggle_down_queued, toggle_up_queued) =
            toggle_queue->is_queued(this);

        if (!toggle_down_queued && toggle_up_queued)
            return false;

        if (!update_after_gc())
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

bool ObjectInstance::ensure_uses_toggle_ref(JSContext* cx) {
    if (m_uses_toggle_ref)
        return true;

    if (!check_gobject_disposed_or_finalized("add toggle reference on"))
        return true;

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

    return true;
}

static void invalidate_closure_list(std::forward_list<GClosure*>* closures,
                                    void* data, GClosureNotify notify_func) {
    g_assert(closures);
    g_assert(notify_func);

    auto before = closures->before_begin();
    for (auto it = closures->begin(); it != closures->end();) {
        // This will also free the closure data, through the closure
        // invalidation mechanism, but adding a temporary reference to
        // ensure that the closure is still valid when calling invalidation
        // notify callbacks
        GjsAutoGClosure closure(closures->front(), GjsAutoTakeOwnership());
        it = closures->erase_after(before);

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

    auto locked_queue = ToggleQueue::get_default();
    std::tie(had_toggle_down, had_toggle_up) = locked_queue->cancel(this);
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

    std::vector<const char *> names;
    AutoGValueVector values;

    if (args.length() > 0 && !args[0].isUndefined()) {
        if (!args[0].isObject()) {
            gjs_throw(context,
                      "Argument to the constructor of %s should be an object "
                      "with properties to set",
                      name());
            return false;
        }

        JS::RootedObject props(context, &args[0].toObject());
        if (!m_proto->props_to_g_parameters(context, props, &names, &values))
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
            if (!other_priv->ensure_uses_toggle_ref(context)) {
                gjs_throw(context,
                          "Impossible to set toggle references on %sobject %p",
                          other_priv->m_gobj_disposed ? "disposed " : "", gobj);
                return false;
            }

            toggle_ref_added = m_uses_toggle_ref;
        }

        args.rval().setObject(*other_priv->m_wrapper);

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
    m_property_cache.trace(tracer);
    m_field_cache.trace(tracer);
    m_unresolvable_cache.trace(tracer);
    for (GClosure* closure : m_vfuncs)
        Gjs::Closure::for_gclosure(closure)->trace(tracer);
}

void ObjectInstance::finalize_impl(JSFreeOp* fop, JSObject* obj) {
    GTypeQuery query;
    type_query_dynamic_safe(&query);
    if (G_LIKELY(query.type))
        JS::RemoveAssociatedMemory(obj, query.instance_size,
                                   MemoryUse::GObjectInstanceStruct);

    GIWrapperInstance::finalize_impl(fop, obj);
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
                "unrooted: %s.%s\n",
                ns(), name());
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
    invalidate_closure_list(&m_vfuncs, this, &vfunc_invalidated_notify);

    g_type_class_unref(g_type_class_peek(m_gtype));

    GJS_DEC_COUNTER(object_prototype);
}

JSObject* gjs_lookup_object_constructor_from_info(JSContext* context,
                                                  GIObjectInfo* info,
                                                  GType gtype) {
    JS::RootedObject in_object(context);
    const char *constructor_name;

    if (info) {
        in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);
        constructor_name = g_base_info_get_name((GIBaseInfo*) info);
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
        if (!ObjectPrototype::define_class(context, in_object, nullptr, gtype,
                                           &constructor, &ignored))
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
static JSObject *
gjs_lookup_object_prototype_from_info(JSContext    *context,
                                      GIObjectInfo *info,
                                      GType         gtype)
{
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
    GjsAutoObjectInfo info = g_irepository_find_by_gtype(nullptr, gtype);
    return gjs_lookup_object_prototype_from_info(context, info, gtype);
}

// Retrieves a GIFieldInfo for a field named @key. This is for use in
// field_getter_impl() and field_setter_not_impl(), where the field info *must*
// have been cached previously in resolve_impl() on this ObjectPrototype or one
// of its parent ObjectPrototypes. This will fail an assertion if there is no
// cached field info.
//
// The caller does not own the return value, and it can never be null.
GIFieldInfo* ObjectPrototype::lookup_cached_field_info(JSContext* cx,
                                                       JS::HandleString key) {
    if (!info()) {
        // Custom JS classes can't have fields, and fields on internal classes
        // are not available. We must be looking up a field on a
        // GObject-introspected parent.
        GType parent_gtype = g_type_parent(m_gtype);
        g_assert(parent_gtype != G_TYPE_INVALID &&
                 "Custom JS class must have parent");
        ObjectPrototype* parent_proto =
            ObjectPrototype::for_gtype(parent_gtype);
        g_assert(parent_proto &&
                 "Custom JS class's parent must have been accessed in JS");
        return parent_proto->lookup_cached_field_info(cx, key);
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Looking up cached field info for '%s' in '%s' prototype",
                     gjs_debug_string(key).c_str(), g_type_name(m_gtype));
    auto entry = m_field_cache.lookupForAdd(key);
    if (entry)
        return entry->value().get();

    // We must be looking up a field defined on a parent. Look up the prototype
    // object via its GIObjectInfo.
    GjsAutoObjectInfo parent_info = g_object_info_get_parent(m_info);
    JS::RootedObject parent_proto(cx, gjs_lookup_object_prototype_from_info(
                                          cx, parent_info, G_TYPE_INVALID));
    ObjectPrototype* parent = ObjectPrototype::for_js(cx, parent_proto);
    return parent->lookup_cached_field_info(cx, key);
}

bool ObjectInstance::associate_closure(JSContext* cx, GClosure* closure) {
    if (!is_prototype()) {
        if (!to_instance()->ensure_uses_toggle_ref(cx)) {
            gjs_throw(cx, "Impossible to set toggle references on %sobject %p",
                      m_gobj_disposed ? "disposed " : "", to_instance()->ptr());
            return false;
        }
    }

    g_assert(std::find(m_closures.begin(), m_closures.end(), closure) ==
                 m_closures.end() &&
             "This closure was already associated with this object");

    /* This is a weak reference, and will be cleared when the closure is
     * invalidated */
    m_closures.push_front(closure);
    g_closure_add_invalidate_notifier(
        closure, this, &ObjectInstance::closure_invalidated_notify);

    return true;
}

void ObjectInstance::closure_invalidated_notify(void* data, GClosure* closure) {
    // This callback should *only* touch m_closures
    auto* priv = static_cast<ObjectInstance*>(data);
    priv->m_closures.remove(closure);
}

void ObjectInstance::invalidate_closures() {
    invalidate_closure_list(&m_closures, this, &closure_invalidated_notify);
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

bool
ObjectInstance::connect_impl(JSContext          *context,
                             const JS::CallArgs& args,
                             bool                after)
{
    gulong id;
    guint signal_id;
    GQuark signal_detail;

    gjs_debug_gsignal("connect obj %p priv %p", m_wrapper.get(), this);

    if (!check_gobject_disposed_or_finalized("connect to any signal on")) {
        args.rval().setInt32(0);
        return true;
    }

    JS::UniqueChars signal_name;
    JS::RootedObject callback(context);
    if (!gjs_parse_call_args(context, after ? "connect_after" : "connect", args, "so",
                             "signal name", &signal_name,
                             "callback", &callback))
        return false;

    std::string dynamicString = format_name() + '.' +
                                (after ? "connect_after" : "connect") + "('" +
                                signal_name.get() + "')";
    AutoProfilerLabel label(context, "", dynamicString.c_str());

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
        context, JS_GetObjectFunction(callback), "signal callback", signal_id);
    if (closure == NULL)
        return false;
    if (!associate_closure(context, closure))
        return false;

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

    std::string dynamicString =
        format_name() + "emit('" + signal_name.get() + "')";
    AutoProfilerLabel label(context, "", dynamicString.c_str());

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
    }

    if (signal_query.return_type == G_TYPE_NONE) {
        g_signal_emitv(instance_and_args.data(), signal_id, signal_detail,
                       nullptr);
        argv.rval().setUndefined();
        return true;
    }

    GType gtype = signal_query.return_type & ~G_SIGNAL_TYPE_STATIC_SCOPE;
    Gjs::AutoGValue rvalue(gtype);
    g_signal_emitv(instance_and_args.data(), signal_id, signal_detail, &rvalue);

    return gjs_value_from_g_value(context, argv.rval(), &rvalue);
}

bool ObjectInstance::signal_match_arguments_from_object(
    JSContext* cx, JS::HandleObject match_obj, GSignalMatchType* mask_out,
    unsigned* signal_id_out, GQuark* detail_out,
    JS::MutableHandleFunction func_out) {
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
    JS::RootedFunction func(cx);
    if (!JS_HasOwnPropertyById(cx, match_obj, atoms.func(), &has_func))
        return false;
    if (has_func) {
        mask |= G_SIGNAL_MATCH_CLOSURE;

        JS::RootedValue value(cx);
        if (!JS_GetPropertyById(cx, match_obj, atoms.func(), &value))
            return false;

        if (!value.isObject() || !JS_ObjectIsFunction(&value.toObject())) {
            gjs_throw(cx, "'func' property must be a function");
            return false;
        }

        func = JS_GetObjectFunction(&value.toObject());
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
        func_out.set(func);
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
    JS::RootedFunction func(cx);
    if (!signal_match_arguments_from_object(cx, match, &mask, &signal_id,
                                            &detail, &func))
        return false;

    uint64_t handler = 0;
    if (!func) {
        handler = g_signal_handler_find(m_ptr, mask, signal_id, detail, nullptr,
                                        nullptr, nullptr);
    } else {
        for (GClosure* candidate : m_closures) {
            if (Gjs::Closure::for_gclosure(candidate)->callable() == func) {
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
    JS::RootedFunction func(cx);
    if (!signal_match_arguments_from_object(cx, match, &mask, &signal_id,
                                            &detail, &func)) {
        return false;
    }
    unsigned n_matched = 0;
    if (!func) {
        n_matched = MatchFunc(m_ptr, mask, signal_id, detail, nullptr, nullptr,
                              nullptr);
    } else {
        std::vector<GClosure*> candidates;
        for (GClosure* candidate : m_closures) {
            if (Gjs::Closure::for_gclosure(candidate)->callable() == func)
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

    std::string dynamicString = priv->format_name() + "._init";
    AutoProfilerLabel label(context, "", dynamicString.c_str());

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
    NULL,
    &ObjectBase::trace,
};

const struct JSClass ObjectBase::klass = {
    "GObject_Object",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &ObjectBase::class_ops
};

JSFunctionSpec ObjectBase::proto_methods[] = {
    JS_FN("_init", &ObjectBase::init_gobject, 0, 0),
    JS_FN("connect", &ObjectBase::connect, 0, 0),
    JS_FN("connect_after", &ObjectBase::connect_after, 0, 0),
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
    if (parent_type != G_TYPE_INVALID) {
        proto.set(gjs_lookup_object_prototype(cx, parent_type));
        if (!proto)
            return false;
    }
    return true;
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
                                   GIObjectInfo* info, GType gtype,
                                   JS::MutableHandleObject constructor,
                                   JS::MutableHandleObject prototype) {
    if (!ObjectPrototype::create_class(context, in_object, info, gtype,
                                       constructor, prototype))
        return false;

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
    if (!ensure_uses_toggle_ref(cx) || !m_uses_toggle_ref) {
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
        cx, JS_NewObjectWithGivenProto(cx, JS_GetClass(proto), proto));
    if (!obj)
        return nullptr;

    ObjectInstance* priv = ObjectInstance::new_for_js_object(cx, obj);

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
                                         GType expected_gtype,
                                         GIBaseInfo* expected_info) {
    g_assert(transfer_direction != GI_DIRECTION_INOUT &&
             "transfer_to_gi_argument() must choose between in or out");

    if (!ObjectBase::typecheck(cx, obj, expected_info, expected_gtype)) {
        gjs_arg_unset<void*>(arg);
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

// Overrides GIWrapperInstance::typecheck_impl()
bool ObjectInstance::typecheck_impl(JSContext* cx, GIBaseInfo* expected_info,
                                    GType expected_type) const {
    g_assert(m_gobj_disposed || gtype() == G_OBJECT_TYPE(m_ptr.as<GObject*>()));
    return GIWrapperInstance::typecheck_impl(cx, expected_info, expected_type);
}

GJS_JSAPI_RETURN_CONVENTION
static bool find_vfunc_info(JSContext* context, GType implementor_gtype,
                            GIBaseInfo* vfunc_info, const char* vfunc_name,
                            void** implementor_vtable_ret,
                            GjsAutoFieldInfo* field_info_ret) {
    GType ancestor_gtype;
    int length, i;
    GIBaseInfo *ancestor_info;
    GjsAutoStructInfo struct_info;
    bool is_interface;

    field_info_ret->reset();
    *implementor_vtable_ret = NULL;

    ancestor_info = g_base_info_get_container(vfunc_info);
    ancestor_gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)ancestor_info);

    is_interface = g_base_info_get_type(ancestor_info) == GI_INFO_TYPE_INTERFACE;

    GjsAutoTypeClass<GTypeClass> implementor_class(implementor_gtype);
    if (is_interface) {
        GTypeInstance *implementor_iface_class;
        implementor_iface_class = (GTypeInstance*) g_type_interface_peek(implementor_class,
                                                        ancestor_gtype);
        if (implementor_iface_class == NULL) {
            gjs_throw (context, "Couldn't find GType of implementor of interface %s.",
                       g_type_name(ancestor_gtype));
            return false;
        }

        *implementor_vtable_ret = implementor_iface_class;

        struct_info = g_interface_info_get_iface_struct((GIInterfaceInfo*)ancestor_info);
    } else {
        struct_info = g_object_info_get_class_struct((GIObjectInfo*)ancestor_info);
        *implementor_vtable_ret = implementor_class;
    }

    length = g_struct_info_get_n_fields(struct_info);
    for (i = 0; i < length; i++) {
        GjsAutoFieldInfo field_info = g_struct_info_get_field(struct_info, i);
        if (strcmp(field_info.name(), vfunc_name) != 0)
            continue;

        GjsAutoTypeInfo type_info = g_field_info_get_type(field_info);
        if (g_type_info_get_tag(type_info) != GI_TYPE_TAG_INTERFACE) {
            /* We have a field with the same name, but it's not a callback.
             * There's no hope of being another field with a correct name,
             * so just abort early. */
            return true;
        } else {
            *field_info_ret = std::move(field_info);
            return true;
        }
    }
    return true;
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
    JS::RootedObject function(cx);
    if (!gjs_parse_call_args(cx, "hook_up_vfunc", args, "so",
                             "name", &name,
                             "function", &function))
        return false;

    args.rval().setUndefined();

    /* find the first class that actually has repository information */
    GIObjectInfo *info = m_info;
    GType info_gtype = m_gtype;
    while (!info && info_gtype != G_TYPE_OBJECT) {
        info_gtype = g_type_parent(info_gtype);

        info = g_irepository_find_by_gtype(nullptr, info_gtype);
    }

    /* If we don't have 'info', we don't have the base class (GObject).
     * This is awful, so abort now. */
    g_assert(info != NULL);

    GjsAutoVFuncInfo vfunc = find_vfunc_on_parents(info, name.get(), nullptr);

    if (!vfunc) {
        guint i, n_interfaces;
        GType *interface_list;

        interface_list = g_type_interfaces(m_gtype, &n_interfaces);

        for (i = 0; i < n_interfaces; i++) {
            GjsAutoInterfaceInfo interface =
                g_irepository_find_by_gtype(nullptr, interface_list[i]);

            /* The interface doesn't have to exist -- it could be private
             * or dynamic. */
            if (interface) {
                vfunc = g_interface_info_find_vfunc(interface, name.get());

                if (vfunc)
                    break;
            }
        }

        g_free(interface_list);
    }

    if (!vfunc) {
        gjs_throw(cx, "Could not find definition of virtual function %s",
                  name.get());
        return false;
    }

    void *implementor_vtable;
    GjsAutoFieldInfo field_info;
    if (!find_vfunc_info(cx, m_gtype, vfunc, name.get(), &implementor_vtable,
                         &field_info))
        return false;

    if (field_info) {
        gint offset;
        gpointer method_ptr;
        GjsCallbackTrampoline *trampoline;

        offset = g_field_info_get_offset(field_info);
        method_ptr = G_STRUCT_MEMBER_P(implementor_vtable, offset);

        if (!js::IsFunctionObject(function)) {
            gjs_throw(cx, "Tried to deal with a vfunc that wasn't a function");
            return false;
        }
        JS::RootedFunction func(cx, JS_GetObjectFunction(function));
        trampoline = GjsCallbackTrampoline::create(
            cx, func, vfunc, GI_SCOPE_TYPE_NOTIFIED, true, true);
        if (!trampoline)
            return false;

        // This is traced, and will be cleared from the list when the closure is
        // invalidated
        g_assert(std::find(m_vfuncs.begin(), m_vfuncs.end(), trampoline) ==
                     m_vfuncs.end() &&
                 "This vfunc was already associated with this class");
        m_vfuncs.push_front(trampoline);
        g_closure_add_invalidate_notifier(
            trampoline, this, &ObjectPrototype::vfunc_invalidated_notify);
        g_closure_add_invalidate_notifier(
            trampoline, nullptr,
            [](void*, GClosure* closure) { g_closure_unref(closure); });

        *reinterpret_cast<ffi_closure**>(method_ptr) = trampoline->closure();
    }

    return true;
}

void ObjectPrototype::vfunc_invalidated_notify(void* data, GClosure* closure) {
    // This callback should *only* touch m_vfuncs
    auto* priv = static_cast<ObjectPrototype*>(data);
    priv->m_vfuncs.remove(closure);
}

bool
gjs_lookup_object_constructor(JSContext             *context,
                              GType                  gtype,
                              JS::MutableHandleValue value_p)
{
    JSObject *constructor;

    GjsAutoObjectInfo object_info = g_irepository_find_by_gtype(nullptr, gtype);

    constructor = gjs_lookup_object_constructor_from_info(context, object_info, gtype);

    if (G_UNLIKELY (constructor == NULL))
        return false;

    value_p.setObject(*constructor);
    return true;
}
