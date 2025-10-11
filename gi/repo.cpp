/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <string.h>  // for strlen

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallAndConstruct.h>  // for JS_CallFunctionValue
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/Exception.h>
#include <js/GlobalObject.h>        // for CurrentGlobalOrNull
#include <js/Id.h>                  // for PropertyKey
#include <js/Object.h>              // for GetClass
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT, JSPROP_RESOLVING
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>  // for JS_NewPlainObject, JS_NewObject
#include <mozilla/Maybe.h>
#include <mozilla/ScopeExit.h>

#include "gi/arg.h"
#include "gi/enumeration.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/info.h"
#include "gi/interface.h"
#include "gi/ns.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/repo.h"
#include "gi/struct.h"
#include "gi/union.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/gerror-result.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/module.h"
#include "util/log.h"

using mozilla::Maybe;

GJS_JSAPI_RETURN_CONVENTION
static bool lookup_override_function(JSContext *, JS::HandleId,
                                     JS::MutableHandleValue);

GJS_JSAPI_RETURN_CONVENTION
static bool get_version_for_ns(JSContext* context, JS::HandleObject repo_obj,
                               JS::HandleId ns_id, JS::UniqueChars* version) {
    JS::RootedObject versions(context);
    bool found;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    if (!gjs_object_require_property(context, repo_obj, "GI repository object",
                                     atoms.versions(), &versions))
        return false;

    if (!JS_AlreadyHasOwnPropertyById(context, versions, ns_id, &found))
        return false;

    if (!found)
        return true;

    return gjs_object_require_property(context, versions, NULL, ns_id, version);
}

GJS_JSAPI_RETURN_CONVENTION
static bool resolve_namespace_object(JSContext* context,
                                     JS::HandleObject repo_obj,
                                     JS::HandleId ns_id) {
    JS::UniqueChars version;
    if (!get_version_for_ns(context, repo_obj, ns_id, &version))
        return false;

    JS::UniqueChars ns_name;
    if (!gjs_get_string_id(context, ns_id, &ns_name))
        return false;
    if (!ns_name) {
        gjs_throw(context, "Requiring invalid namespace on imports.gi");
        return false;
    }

    GI::Repository repo;
    size_t nversions;
    Gjs::AutoStrv versions{repo.enumerate_versions(ns_name.get(), &nversions)};
    if (nversions > 1 && !version &&
        !repo.is_registered(ns_name.get(), nullptr) &&
        !JS::WarnUTF8(context,
                      "Requiring %s but it has %zu versions available; use "
                      "imports.gi.versions to pick one",
                      ns_name.get(), nversions))
        return false;

    // If resolving Gio, load the platform-specific typelib first, so that
    // GioUnix/GioWin32 GTypes get looked up in there with higher priority,
    // instead of in Gio.
#if (defined(G_OS_UNIX) || defined(G_OS_WIN32))
    if (strcmp(ns_name.get(), "Gio") == 0) {
#    ifdef G_OS_UNIX
        const char* platform = "Unix";
#    else   // G_OS_WIN32
        const char* platform = "Win32";
#    endif  // G_OS_UNIX/G_OS_WIN32
        Gjs::AutoChar platform_specific{
            g_strconcat(ns_name.get(), platform, nullptr)};
        auto required = repo.require(platform_specific, version.get());
        if (!required.isOk()) {
            gjs_throw(context, "Failed to require %s %s: %s",
                      platform_specific.get(), version.get(),
                      required.inspectErr()->message);
            return false;
        }
    }
#endif  // (defined(G_OS_UNIX) || defined(G_OS_WIN32))

    auto required = repo.require(ns_name.get(), version.get());
    if (!required.isOk()) {
        gjs_throw(context, "Requiring %s, version %s: %s", ns_name.get(),
                  version ? version.get() : "none",
                  required.inspectErr()->message);
        return false;
    }

    /* Defines a property on "obj" (the javascript repo object)
     * with the given namespace name, pointing to that namespace
     * in the repo.
     */
    JS::RootedObject gi_namespace(context,
                                  gjs_create_ns(context, ns_name.get()));

    JS::RootedValue override(context);
    if (!lookup_override_function(context, ns_id, &override) ||
        // Define the property early, to avoid reentrancy issues if the override
        // module looks for namespaces that import this
        !JS_DefinePropertyById(context, repo_obj, ns_id, gi_namespace,
                               GJS_MODULE_PROP_FLAGS))
        return false;

    JS::RootedValue result(context);
    if (!override.isUndefined() &&
        !JS_CallFunctionValue (context, gi_namespace, /* thisp */
                               override, /* callee */
                               JS::HandleValueArray::empty(), &result))
        return false;

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Defined namespace '%s' %p in GIRepository %p", ns_name.get(),
              gi_namespace.get(), repo_obj.get());

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    gjs->schedule_gc_if_needed();
    return true;
}

/*
 * The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
repo_resolve(JSContext       *context,
             JS::HandleObject obj,
             JS::HandleId     id,
             bool            *resolved)
{
    if (!id.isString()) {
        *resolved = false;
        return true; /* not resolved, but no error */
    }

    /* let Object.prototype resolve these */
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (id == atoms.to_string() || id == atoms.value_of()) {
        *resolved = false;
        return true;
    }

    gjs_debug_jsprop(GJS_DEBUG_GREPO, "Resolve prop '%s' hook, obj %s",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str());

    if (!resolve_namespace_object(context, obj, id))
        return false;

    *resolved = true;
    return true;
}

static const struct JSClassOps gjs_repo_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    repo_resolve,
};

struct JSClass gjs_repo_class = {
    "GIRepository",
    0,
    &gjs_repo_class_ops,
};

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
repo_new(JSContext *context)
{
    JS::RootedObject repo(context, JS_NewObject(context, &gjs_repo_class));
    if (repo == nullptr)
        return nullptr;

    gjs_debug_lifecycle(GJS_DEBUG_GREPO, "repo constructor, obj %p",
                        repo.get());

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject versions(context, JS_NewPlainObject(context));
    if (!JS_DefinePropertyById(context, repo, atoms.versions(), versions,
                               JSPROP_PERMANENT | JSPROP_RESOLVING))
        return nullptr;

    /* GLib/GObject/Gio are fixed at 2.0, since we depend on them
     * internally.
     */
    JS::RootedString two_point_oh(context, JS_NewStringCopyZ(context, "2.0"));
    if (!JS_DefinePropertyById(context, versions, atoms.glib(), two_point_oh,
                               JSPROP_PERMANENT) ||
        !JS_DefinePropertyById(context, versions, atoms.gobject(), two_point_oh,
                               JSPROP_PERMANENT) ||
        !JS_DefinePropertyById(context, versions, atoms.gio(), two_point_oh,
                               JSPROP_PERMANENT))
        return nullptr;

#    if defined(G_OS_UNIX)
    if (!JS_DefineProperty(context, versions, "GLibUnix", two_point_oh,
                           JSPROP_PERMANENT) ||
        !JS_DefineProperty(context, versions, "GioUnix", two_point_oh,
                           JSPROP_PERMANENT))
        return nullptr;
#    elif defined(G_OS_WIN32)
    if (!JS_DefineProperty(context, versions, "GLibWin32", two_point_oh,
                           JSPROP_PERMANENT) ||
        !JS_DefineProperty(context, versions, "GioWin32", two_point_oh,
                           JSPROP_PERMANENT))
        return nullptr;
#    endif  // G_OS_UNIX/G_OS_WIN32

    JS::RootedObject private_ns(context, JS_NewPlainObject(context));
    if (!JS_DefinePropertyById(context, repo, atoms.private_ns_marker(),
                               private_ns, JSPROP_PERMANENT | JSPROP_RESOLVING))
        return nullptr;

    return repo;
}

bool
gjs_define_repo(JSContext              *cx,
                JS::MutableHandleObject repo)
{
    repo.set(repo_new(cx));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_value_from_constant_info(JSContext* cx,
                                         const GI::ConstantInfo info,
                                         JS::MutableHandleValue value) {
    GIArgument garg;
    info.load_value(&garg);
    auto guard =
        mozilla::MakeScopeExit([&info, &garg]() { info.free_value(&garg); });

    return gjs_value_from_gi_argument(cx, value, info.type_info(), &garg, true);
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_define_constant(JSContext* context, JS::HandleObject in_object,
                                const GI::ConstantInfo info) {
    JS::RootedValue value(context);

    if (!gjs_value_from_constant_info(context, info, &value))
        return false;

    return JS_DefineProperty(context, in_object, info.name(), value,
                             GJS_MODULE_PROP_FLAGS);
}

bool gjs_define_info(JSContext* cx, JS::HandleObject in_object,
                     const GI::BaseInfo info, bool* defined) {
    info.log_usage();

    *defined = true;

    if (auto func_info = info.as<GI::InfoTag::FUNCTION>())
        return gjs_define_function(cx, in_object, 0, func_info.value());

    if (auto object_info = info.as<GI::InfoTag::OBJECT>()) {
        GType gtype = object_info->gtype();

        if (g_type_is_a(gtype, G_TYPE_PARAM))
            return gjs_define_param_class(cx, in_object);

        if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
            JS::RootedObject ignored1{cx}, ignored2{cx};
            return ObjectPrototype::define_class(cx, in_object, object_info,
                                                 gtype, nullptr, 0, &ignored1,
                                                 &ignored2);
        }

        if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
            JS::RootedObject ignored{cx};
            return FundamentalPrototype::define_class(
                cx, in_object, object_info.value(), &ignored);
        }

        gjs_throw(cx, "Unsupported type %s, deriving from fundamental %s",
                  g_type_name(gtype), g_type_name(g_type_fundamental(gtype)));
        return false;
    }

    auto struct_info = info.as<GI::InfoTag::STRUCT>();
    // We don't want GType structures in the namespace, we expose their fields
    // as vfuncs and their methods as static methods
    if (struct_info && struct_info->is_gtype_struct()) {
        *defined = false;
        return true;
    }

    if (struct_info)
        return StructPrototype::define_class(cx, in_object,
                                             struct_info.value());

    if (auto union_info = info.as<GI::InfoTag::UNION>())
        return UnionPrototype::define_class(cx, in_object, union_info.value());

    if (auto enum_info = info.as<GI::InfoTag::ENUM>()) {
        if (!info.is_flags() && enum_info->error_domain()) {
            /* define as GError subclass */
            return ErrorPrototype::define_class(cx, in_object,
                                                enum_info.value());
        }

        return gjs_define_enumeration(cx, in_object, enum_info.value());
    }

    if (auto constant_info = info.as<GI::InfoTag::CONSTANT>())
        return gjs_define_constant(cx, in_object, constant_info.value());

    if (auto interface_info = info.as<GI::InfoTag::INTERFACE>()) {
        JS::RootedObject ignored1{cx}, ignored2{cx};
        return InterfacePrototype::create_class(cx, in_object, interface_info,
                                                interface_info->gtype(),
                                                &ignored1, &ignored2);
    }

    gjs_throw(cx, "API of type %s not implemented, cannot define %s.%s",
              info.type_string(), info.ns(), info.name());
    return false;
}

/* Get the "unknown namespace", which should be used for unnamespaced types */
JSObject*
gjs_lookup_private_namespace(JSContext *context)
{
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return gjs_lookup_namespace_object_by_name(context,
                                               atoms.private_ns_marker());
}

/* Get the namespace object that the GIBaseInfo should be inside */
JSObject* gjs_lookup_namespace_object(JSContext* context,
                                      const GI::BaseInfo info) {
    const char* ns = info.ns();
    if (ns == NULL) {
        gjs_throw(context, "%s '%s' does not have a namespace",
                  info.type_string(), info.name());

        return NULL;
    }

    JS::RootedId ns_name(context, gjs_intern_string_to_id(context, ns));
    if (ns_name.isVoid())
        return nullptr;
    return gjs_lookup_namespace_object_by_name(context, ns_name);
}

/* Check if an exception's 'name' property is equal to ImportError. Ignores
 * all errors that might arise. */
[[nodiscard]] static bool is_import_error(JSContext* cx,
                                          JS::HandleValue thrown_value) {
    if (!thrown_value.isObject())
        return false;

    JS::AutoSaveExceptionState saved_exc(cx);
    JS::RootedObject exc(cx, &thrown_value.toObject());
    JS::RootedValue exc_name(cx);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    bool eq;
    bool retval =
        JS_GetPropertyById(cx, exc, atoms.name(), &exc_name) &&
        JS_StringEqualsLiteral(cx, exc_name.toString(), "ImportError", &eq) &&
        eq;

    saved_exc.restore();
    return retval;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
lookup_override_function(JSContext             *cx,
                         JS::HandleId           ns_name,
                         JS::MutableHandleValue function)
{
    JS::AutoSaveExceptionState saved_exc(cx);

    JS::RootedObject global{cx, JS::CurrentGlobalOrNull(cx)};
    JS::RootedValue importer(
        cx, gjs_get_global_slot(global, GjsGlobalSlot::IMPORTS));
    g_assert(importer.isObject());

    JS::RootedObject overridespkg(cx), module(cx);
    JS::RootedObject importer_obj(cx, &importer.toObject());
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!gjs_object_require_property(cx, importer_obj, "importer",
                                     atoms.overrides(), &overridespkg))
        return false;

    if (!gjs_object_require_property(cx, overridespkg,
                                     "GI repository object", ns_name,
                                     &module)) {
        JS::RootedValue exc(cx);
        JS_GetPendingException(cx, &exc);

        /* If the exception was an ImportError (i.e., module not found) then
         * we simply didn't have an override, don't throw an exception */
        if (is_import_error(cx, exc)) {
            saved_exc.restore();
            return true;
        }

        return false;
    }

    // If the override module is present, it must have a callable _init(). An
    // override module without _init() is probably unintentional. (function
    // being undefined means there was no override module.)
    if (!gjs_object_require_property(cx, module, "override module",
                                     atoms.init(), function) ||
        !function.isObject() || !JS::IsCallable(&function.toObject())) {
        gjs_throw(cx, "Unexpected value for _init in overrides module");
        return false;
    }
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* lookup_namespace(JSContext* cx, JSObject* global,
                                  JS::HandleId ns_name) {
    JS::RootedObject native_registry(cx, gjs_get_native_registry(global));
    auto priv = GjsContextPrivate::from_cx(cx);
    const GjsAtoms& atoms = priv->atoms();
    JS::RootedObject gi(cx);

    if (!gjs_global_registry_get(cx, native_registry, atoms.gi(), &gi))
        return nullptr;

    if (!gi) {
        gjs_throw(cx, "No gi property in native registry");
        return nullptr;
    }

    JS::RootedObject retval(cx);
    if (!gjs_object_require_property(cx, gi, "GI repository object", ns_name,
                                     &retval))
        return NULL;

    return retval;
}

JSObject* gjs_lookup_namespace_object_by_name(JSContext* cx,
                                              JS::HandleId ns_name) {
    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));

    g_assert(gjs_global_get_type(global) == GjsGlobalType::DEFAULT);
    return lookup_namespace(cx, global, ns_name);
}

char*
gjs_hyphen_from_camel(const char *camel_name)
{
    GString *s;
    const char *p;

    /* four hyphens should be reasonable guess */
    s = g_string_sized_new(strlen(camel_name) + 4 + 1);

    for (p = camel_name; *p; ++p) {
        if (g_ascii_isupper(*p)) {
            g_string_append_c(s, '-');
            g_string_append_c(s, g_ascii_tolower(*p));
        } else {
            g_string_append_c(s, *p);
        }
    }

    return g_string_free(s, false);
}

JSObject* gjs_lookup_generic_constructor(JSContext* context,
                                         const GI::BaseInfo info) {
    JS::RootedObject in_object{context,
        gjs_lookup_namespace_object(context, info)};
    const char* constructor_name = info.name();

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    if (G_UNLIKELY(!value.isObject())) {
        gjs_throw(context,
                  "Constructor of %s.%s was the wrong type, expected an object",
                  info.ns(), constructor_name);
        return NULL;
    }

    return &value.toObject();
}

JSObject* gjs_lookup_generic_prototype(JSContext* context,
                                       const GI::BaseInfo info) {
    JS::RootedObject constructor(context,
                                 gjs_lookup_generic_constructor(context, info));
    if (G_UNLIKELY(!constructor))
        return NULL;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedValue value(context);
    if (!JS_GetPropertyById(context, constructor, atoms.prototype(), &value))
        return NULL;

    if (G_UNLIKELY(!value.isObject())) {
        gjs_throw(context,
                  "Prototype of %s.%s was the wrong type, expected an object",
                  info.ns(), info.name());
        return NULL;
    }

    return &value.toObject();
}

JSObject* gjs_new_object_with_generic_prototype(JSContext* cx,
                                                const GI::BaseInfo info) {
    JS::RootedObject proto(cx, gjs_lookup_generic_prototype(cx, info));
    if (!proto)
        return nullptr;

    return JS_NewObjectWithGivenProto(cx, JS::GetClass(proto), proto);
}
