/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <string.h>  // for strlen

#if GJS_VERBOSE_ENABLE_GI_USAGE
#    include <string>
#endif

#include <girepository.h>
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

#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/enumeration.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/interface.h"
#include "gi/ns.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/repo.h"
#include "gi/union.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/module.h"
#include "util/log.h"

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

static void strlist_free(GList* l) { g_list_free_full(l, g_free); }

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

    GjsAutoPointer<GList, GList, strlist_free> versions =
        g_irepository_enumerate_versions(nullptr, ns_name.get());
    unsigned nversions = g_list_length(versions);
    if (nversions > 1 && !version &&
        !g_irepository_is_registered(nullptr, ns_name.get(), nullptr) &&
        !JS::WarnUTF8(context,
                      "Requiring %s but it has %u versions available; use "
                      "imports.gi.versions to pick one",
                      ns_name.get(), nversions))
        return false;

    GjsAutoError error;
    // If resolving Gio, load the platform-specific typelib first, so that
    // GioUnix/GioWin32 GTypes get looked up in there with higher priority,
    // instead of in Gio.
#if GLIB_CHECK_VERSION(2, 79, 2) && (defined(G_OS_UNIX) || defined(G_OS_WIN32))
    if (strcmp(ns_name.get(), "Gio") == 0) {
#    ifdef G_OS_UNIX
        const char* platform = "Unix";
#    else   // G_OS_WIN32
        const char* platform = "Win32";
#    endif  // G_OS_UNIX/G_OS_WIN32
        GjsAutoChar platform_specific =
            g_strconcat(ns_name.get(), platform, nullptr);
        if (!g_irepository_require(nullptr, platform_specific, version.get(),
                                   GIRepositoryLoadFlags(0), &error)) {
            gjs_throw(context, "Failed to require %s %s: %s",
                      platform_specific.get(), version.get(), error->message);
            return false;
        }
    }
#endif  // GLib >= 2.79.2

    g_irepository_require(nullptr, ns_name.get(), version.get(),
                          GIRepositoryLoadFlags(0), &error);
    if (error) {
        gjs_throw(context, "Requiring %s, version %s: %s", ns_name.get(),
                  version ? version.get() : "none", error->message);
        return false;
    }

    /* Defines a property on "obj" (the javascript repo object)
     * with the given namespace name, pointing to that namespace
     * in the repo.
     */
    JS::RootedObject gi_namespace(context,
                                  gjs_create_ns(context, ns_name.get()));

    JS::RootedValue override(context);
    if (!lookup_override_function(context, ns_id, &override))
        return false;

    /* Define the property early, to avoid reentrancy issues if
       the override module looks for namespaces that import this */
    if (!JS_DefinePropertyById(context, repo_obj, ns_id, gi_namespace,
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
                               JSPROP_PERMANENT))
        return nullptr;
    if (!JS_DefinePropertyById(context, versions, atoms.gobject(), two_point_oh,
                               JSPROP_PERMANENT))
        return nullptr;
    if (!JS_DefinePropertyById(context, versions, atoms.gio(), two_point_oh,
                               JSPROP_PERMANENT))
        return nullptr;

#if GLIB_CHECK_VERSION(2, 79, 2)
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
#endif      // GLib >= 2.79.2

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
static bool gjs_value_from_constant_info(JSContext* cx, GIConstantInfo* info,
                                         JS::MutableHandleValue value) {
    GIArgument garg;
    g_constant_info_get_value(info, &garg);

    GjsAutoTypeInfo type_info = g_constant_info_get_type(info);

    bool ok = gjs_value_from_gi_argument(cx, value, type_info, &garg, true);

    g_constant_info_free_value(info, &garg);
    return ok;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_define_constant(JSContext       *context,
                    JS::HandleObject in_object,
                    GIConstantInfo  *info)
{
    JS::RootedValue value(context);
    const char *name;

    if (!gjs_value_from_constant_info(context, info, &value))
        return false;

    name = g_base_info_get_name((GIBaseInfo*) info);

    return JS_DefineProperty(context, in_object, name, value,
                             GJS_MODULE_PROP_FLAGS);
}

#if GJS_VERBOSE_ENABLE_GI_USAGE
void
_gjs_log_info_usage(GIBaseInfo *info)
{
#define DIRECTION_STRING(d) ( ((d) == GI_DIRECTION_IN) ? "IN" : ((d) == GI_DIRECTION_OUT) ? "OUT" : "INOUT" )
#define TRANSFER_STRING(t) ( ((t) == GI_TRANSFER_NOTHING) ? "NOTHING" : ((t) == GI_TRANSFER_CONTAINER) ? "CONTAINER" : "EVERYTHING" )

    {
        char *details;
        GIInfoType info_type;
        GIBaseInfo *container;

        info_type = g_base_info_get_type(info);

        if (info_type == GI_INFO_TYPE_FUNCTION) {
            std::string args("{ ");
            int n_args;
            int i;
            GITransfer retval_transfer;

            n_args = g_callable_info_get_n_args((GICallableInfo*) info);
            for (i = 0; i < n_args; ++i) {
                GIArgInfo *arg;
                GIDirection direction;
                GITransfer transfer;

                arg = g_callable_info_get_arg((GICallableInfo*)info, i);
                direction = g_arg_info_get_direction(arg);
                transfer = g_arg_info_get_ownership_transfer(arg);

                if (i > 0)
                    args += ", ";

                args += std::string("{ GI_DIRECTION_") +
                        DIRECTION_STRING(direction) + ", GI_TRANSFER_" +
                        TRANSFER_STRING(transfer) + " }";

                g_base_info_unref((GIBaseInfo*) arg);
            }

            args += " }";

            retval_transfer = g_callable_info_get_caller_owns((GICallableInfo*) info);

            details = g_strdup_printf(
                ".details = { .func = { .retval_transfer = GI_TRANSFER_%s, "
                ".n_args = %d, .args = %s } }",
                TRANSFER_STRING(retval_transfer), n_args, args.c_str());
        } else {
            details = g_strdup_printf(".details = { .nothing = {} }");
        }

        container = g_base_info_get_container(info);

        gjs_debug_gi_usage("{ GI_INFO_TYPE_%s, \"%s\", \"%s\", \"%s\", %s },",
                           gjs_info_type_name(info_type),
                           g_base_info_get_namespace(info),
                           container ? g_base_info_get_name(container) : "",
                           g_base_info_get_name(info),
                           details);
        g_free(details);
    }
}
#endif /* GJS_VERBOSE_ENABLE_GI_USAGE */

bool
gjs_define_info(JSContext       *context,
                JS::HandleObject in_object,
                GIBaseInfo      *info,
                bool            *defined)
{
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(info);
#endif

    *defined = true;

    switch (g_base_info_get_type(info)) {
    case GI_INFO_TYPE_FUNCTION:
        {
            JSObject *f;
            f = gjs_define_function(context, in_object, 0, (GICallableInfo*) info);
            if (f == NULL)
                return false;
        }
        break;
    case GI_INFO_TYPE_OBJECT:
        {
            GType gtype;
            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);

            if (g_type_is_a (gtype, G_TYPE_PARAM)) {
                if (!gjs_define_param_class(context, in_object))
                    return false;
            } else if (g_type_is_a (gtype, G_TYPE_OBJECT)) {
                JS::RootedObject ignored1(context), ignored2(context);
                if (!ObjectPrototype::define_class(context, in_object, info,
                                                   gtype, nullptr, 0, &ignored1,
                                                   &ignored2))
                    return false;
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                JS::RootedObject ignored(context);
                if (!FundamentalPrototype::define_class(context, in_object,
                                                        info, &ignored))
                    return false;
            } else {
                gjs_throw (context,
                           "Unsupported type %s, deriving from fundamental %s",
                           g_type_name(gtype), g_type_name(g_type_fundamental(gtype)));
                return false;
            }
        }
        break;
    case GI_INFO_TYPE_STRUCT:
        /* We don't want GType structures in the namespace,
           we expose their fields as vfuncs and their methods
           as static methods
        */
        if (g_struct_info_is_gtype_struct((GIStructInfo*) info)) {
            *defined = false;
            break;
        }
        /* Fall through */

    case GI_INFO_TYPE_BOXED:
        if (!BoxedPrototype::define_class(context, in_object, info))
            return false;
        break;
    case GI_INFO_TYPE_UNION:
        if (!UnionPrototype::define_class(context, in_object,
                                          (GIUnionInfo*)info))
            return false;
        break;
    case GI_INFO_TYPE_ENUM:
        if (g_enum_info_get_error_domain((GIEnumInfo*) info)) {
            /* define as GError subclass */
            if (!ErrorPrototype::define_class(context, in_object, info))
                return false;
            break;
        }
        [[fallthrough]];

    case GI_INFO_TYPE_FLAGS:
        if (!gjs_define_enumeration(context, in_object, (GIEnumInfo*) info))
            return false;
        break;
    case GI_INFO_TYPE_CONSTANT:
        if (!gjs_define_constant(context, in_object, (GIConstantInfo*) info))
            return false;
        break;
    case GI_INFO_TYPE_INTERFACE:
        {
            JS::RootedObject ignored1(context), ignored2(context);
            if (!InterfacePrototype::create_class(
                    context, in_object, info,
                    g_registered_type_info_get_g_type(info), &ignored1,
                    &ignored2))
                return false;
        }
        break;
    case GI_INFO_TYPE_INVALID:
    case GI_INFO_TYPE_INVALID_0:
    case GI_INFO_TYPE_CALLBACK:
    case GI_INFO_TYPE_VALUE:
    case GI_INFO_TYPE_SIGNAL:
    case GI_INFO_TYPE_VFUNC:
    case GI_INFO_TYPE_PROPERTY:
    case GI_INFO_TYPE_FIELD:
    case GI_INFO_TYPE_ARG:
    case GI_INFO_TYPE_TYPE:
    case GI_INFO_TYPE_UNRESOLVED:
    default:
        gjs_throw(context, "API of type %s not implemented, cannot define %s.%s",
                  gjs_info_type_name(g_base_info_get_type(info)),
                  g_base_info_get_namespace(info),
                  g_base_info_get_name(info));
        return false;
    }

    return true;
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
JSObject*
gjs_lookup_namespace_object(JSContext  *context,
                            GIBaseInfo *info)
{
    const char *ns;

    ns = g_base_info_get_namespace(info);
    if (ns == NULL) {
        gjs_throw(context, "%s '%s' does not have a namespace",
                     gjs_info_type_name(g_base_info_get_type(info)),
                     g_base_info_get_name(info));

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

const char*
gjs_info_type_name(GIInfoType type)
{
    switch (type) {
    case GI_INFO_TYPE_INVALID:
        return "INVALID";
    case GI_INFO_TYPE_FUNCTION:
        return "FUNCTION";
    case GI_INFO_TYPE_CALLBACK:
        return "CALLBACK";
    case GI_INFO_TYPE_STRUCT:
        return "STRUCT";
    case GI_INFO_TYPE_BOXED:
        return "BOXED";
    case GI_INFO_TYPE_ENUM:
        return "ENUM";
    case GI_INFO_TYPE_FLAGS:
        return "FLAGS";
    case GI_INFO_TYPE_OBJECT:
        return "OBJECT";
    case GI_INFO_TYPE_INTERFACE:
        return "INTERFACE";
    case GI_INFO_TYPE_CONSTANT:
        return "CONSTANT";
    case GI_INFO_TYPE_UNION:
        return "UNION";
    case GI_INFO_TYPE_VALUE:
        return "VALUE";
    case GI_INFO_TYPE_SIGNAL:
        return "SIGNAL";
    case GI_INFO_TYPE_VFUNC:
        return "VFUNC";
    case GI_INFO_TYPE_PROPERTY:
        return "PROPERTY";
    case GI_INFO_TYPE_FIELD:
        return "FIELD";
    case GI_INFO_TYPE_ARG:
        return "ARG";
    case GI_INFO_TYPE_TYPE:
        return "TYPE";
    case GI_INFO_TYPE_UNRESOLVED:
        return "UNRESOLVED";
    case GI_INFO_TYPE_INVALID_0:
        g_assert_not_reached();
        return "----INVALID0----";
    default:
      return "???";
    }
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

JSObject *
gjs_lookup_generic_constructor(JSContext  *context,
                               GIBaseInfo *info)
{
    JS::RootedObject in_object{context,
        gjs_lookup_namespace_object(context, info)};
    const char* constructor_name = g_base_info_get_name(info);

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    if (G_UNLIKELY(!value.isObject())) {
        gjs_throw(context,
                  "Constructor of %s.%s was the wrong type, expected an object",
                  g_base_info_get_namespace(info), constructor_name);
        return NULL;
    }

    return &value.toObject();
}

JSObject *
gjs_lookup_generic_prototype(JSContext  *context,
                             GIBaseInfo *info)
{
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
                  g_base_info_get_namespace(info), g_base_info_get_name(info));
        return NULL;
    }

    return &value.toObject();
}

JSObject* gjs_new_object_with_generic_prototype(JSContext* cx,
                                                GIBaseInfo* info) {
    JS::RootedObject proto(cx, gjs_lookup_generic_prototype(cx, info));
    if (!proto)
        return nullptr;

    return JS_NewObjectWithGivenProto(cx, JS::GetClass(proto), proto);
}
