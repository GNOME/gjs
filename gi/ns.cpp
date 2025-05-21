/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <string.h>

#include <string>
#include <vector>

#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/GCVector.h>     // for MutableHandleIdVector
#include <js/Id.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>       // for JS_NewObjectWithGivenProto
#include <mozilla/Maybe.h>

#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/ns.h"
#include "gi/repo.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

#if GLIB_CHECK_VERSION(2, 79, 2)
#    include "gjs/deprecation.h"
#endif  // GLib >= 2.79.2

using mozilla::Maybe;

class Ns : private Gjs::AutoChar, public CWrapper<Ns> {
    friend CWrapperPointerOps<Ns>;
    friend CWrapper<Ns>;

#if GLIB_CHECK_VERSION(2, 79, 2)
    bool m_is_gio_or_glib : 1;
#endif  // GLib >= 2.79.2

    static constexpr auto PROTOTYPE_SLOT = GjsGlobalSlot::PROTOTYPE_ns;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GNAMESPACE;

    explicit Ns(const char* ns_name)
        : Gjs::AutoChar(const_cast<char*>(ns_name), Gjs::TakeOwnership{}) {
        GJS_INC_COUNTER(ns);
#if GLIB_CHECK_VERSION(2, 79, 2)
        m_is_gio_or_glib =
            strcmp(ns_name, "Gio") == 0 || strcmp(ns_name, "GLib") == 0;
#endif  // GLib >= 2.79.2
    }

    ~Ns() { GJS_DEC_COUNTER(ns); }

#if GLIB_CHECK_VERSION(2, 79, 2)
    // helper function
    void platform_specific_warning(JSContext* cx, const char* prefix,
                                   const char* platform,
                                   const char* resolved_name,
                                   const char** exceptions = nullptr) {
        if (!g_str_has_prefix(resolved_name, prefix))
            return;

        const char* base_name = resolved_name + strlen(prefix);
        Gjs::AutoChar old_name{
            g_strdup_printf("%s.%s", this->get(), resolved_name)};
        if (exceptions) {
            for (const char** exception = exceptions; *exception; exception++) {
                if (strcmp(old_name, *exception) == 0)
                    return;
            }
        }

        Gjs::AutoChar new_name{
            g_strdup_printf("%s%s.%s", this->get(), platform, base_name)};
        _gjs_warn_deprecated_once_per_callsite(
            cx, GjsDeprecationMessageId::PlatformSpecificTypelib,
            {old_name.get(), new_name.get()});
    }
#endif  // GLib >= 2.79.2

    // JSClass operations

    // The *resolved out parameter, on success, should be false to indicate that
    // id was not resolved; and true if id was resolved.
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved) {
        if (!id.isString()) {
            *resolved = false;
            return true;  // not resolved, but no error
        }

        // let Object.prototype resolve these
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (id == atoms.to_string() || id == atoms.value_of()) {
            *resolved = false;
            return true;
        }

        JS::UniqueChars name;
        if (!gjs_get_string_id(cx, id, &name))
            return false;
        if (!name) {
            *resolved = false;
            return true;  // not resolved, but no error
        }

        Maybe<GI::AutoBaseInfo> info{
            GI::Repository{}.find_by_name(get(), name.get())};
        if (!info) {
            *resolved = false;  // No property defined, but no error either
            return true;
        }

        gjs_debug(GJS_DEBUG_GNAMESPACE,
                  "Found info type %s for '%s' in namespace '%s'",
                  info->type_string(), info->name(), info->ns());

#if GLIB_CHECK_VERSION(2, 79, 2)
        static const char* unix_types_exceptions[] = {
            "Gio.UnixConnection",
            "Gio.UnixCredentialsMessage",
            "Gio.UnixFDList",
            "Gio.UnixSocketAddress",
            "Gio.UnixSocketAddressType",
            nullptr};

        if (m_is_gio_or_glib) {
            platform_specific_warning(cx, "Unix", "Unix", name.get(),
                                      unix_types_exceptions);
            platform_specific_warning(cx, "unix_", "Unix", name.get());
            platform_specific_warning(cx, "Win32", "Win32", name.get());
            platform_specific_warning(cx, "win32_", "Win32", name.get());
        }
#endif  // GLib >= 2.79.2

        bool defined;
        if (!gjs_define_info(cx, obj, info.ref(), &defined)) {
            gjs_debug(GJS_DEBUG_GNAMESPACE, "Failed to define info '%s'",
                      info->name());
            return false;
        }

        // we defined the property in this object?
        *resolved = defined;
        return true;
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool new_enumerate_impl(JSContext* cx,
                            JS::HandleObject obj [[maybe_unused]],
                            JS::MutableHandleIdVector properties,
                            bool only_enumerable [[maybe_unused]]) {
        GI::Repository::Iterator infos{GI::Repository{}.infos(get())};
        if (!properties.reserve(properties.length() + infos.size())) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        for (GI::AutoBaseInfo info : infos) {
            if (!info.is_enumerable())
                continue;

            jsid id = gjs_intern_string_to_id(cx, info.name());
            if (id.isVoid())
                return false;
            properties.infallibleAppend(id);
        }

        return true;
    }

    static void finalize_impl(JS::GCContext*, Ns* priv) {
        g_assert(priv && "Finalize called on wrong object");
        delete priv;
    }

    // Properties and methods

    GJS_JSAPI_RETURN_CONVENTION
    static bool get_name(JSContext* cx, unsigned argc, JS::Value* vp) {
        GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, this_obj, Ns, priv);
        return gjs_string_from_utf8(cx, priv->get(), args.rval());
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool get_version(JSContext* cx, unsigned argc, JS::Value* vp) {
        GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, this_obj, Ns, priv);
        const char* version = GI::Repository{}.get_version(priv->get());
        return gjs_string_from_utf8(cx, version, args.rval());
    }

    static constexpr JSClassOps class_ops = {
        nullptr,  // addProperty
        nullptr,  // deleteProperty
        nullptr,  // enumerate
        &Ns::new_enumerate,
        &Ns::resolve,
        nullptr,  // mayResolve
        &Ns::finalize,
    };

    // clang-format off
    static constexpr JSPropertySpec proto_props[] = {
        JS_STRING_SYM_PS(toStringTag, "GIRepositoryNamespace", JSPROP_READONLY),
        JS_PSG("__name__", &Ns::get_name, GJS_MODULE_PROP_FLAGS),
        JS_PSG("__version__", &Ns::get_version, GJS_MODULE_PROP_FLAGS & ~JSPROP_ENUMERATE),
        JS_PS_END};
    // clang-format on

    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        nullptr,  // prototypeFunctions
        Ns::proto_props,
        nullptr,  // finishInit
        js::ClassSpec::DontDefineConstructor};

    static constexpr JSClass klass = {
        "GIRepositoryNamespace",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
        &Ns::class_ops, &Ns::class_spec};

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext* cx, const char* ns_name) {
        JS::RootedObject proto(cx, Ns::create_prototype(cx));
        if (!proto)
            return nullptr;

        JS::RootedObject ns(cx,
                            JS_NewObjectWithGivenProto(cx, &Ns::klass, proto));
        if (!ns)
            return nullptr;

        auto* priv = new Ns(ns_name);
        Ns::init_private(ns, priv);

        gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE,
                            "ns constructor, obj %p priv %p", ns.get(), priv);

        return ns;
    }
};

JSObject*
gjs_create_ns(JSContext    *context,
              const char   *ns_name)
{
    return Ns::create(context, ns_name);
}
