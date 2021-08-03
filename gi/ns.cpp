/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <girepository.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/Id.h>  // for JSID_IS_STRING
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>       // for JS_GetPrivate, JS_NewObjectWithGivenProto

#include "gi/cwrapper.h"
#include "gi/ns.h"
#include "gi/repo.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

[[nodiscard]] static bool type_is_enumerable(GIInfoType info_type) {
    switch (info_type) {
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_STRUCT:
        case GI_INFO_TYPE_UNION:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_ENUM:
        case GI_INFO_TYPE_FLAGS:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_FUNCTION:
        case GI_INFO_TYPE_CONSTANT:
            return true;
        // Don't enumerate types which GJS doesn't define on namespaces.
        // See gjs_define_info
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
            return false;
    }
}

class Ns : private GjsAutoChar, public CWrapper<Ns> {
    friend CWrapperPointerOps<Ns>;
    friend CWrapper<Ns>;

    static constexpr auto PROTOTYPE_SLOT = GjsGlobalSlot::PROTOTYPE_ns;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GNAMESPACE;

    explicit Ns(const char* ns_name)
        : GjsAutoChar(const_cast<char*>(ns_name), GjsAutoTakeOwnership()) {
        GJS_INC_COUNTER(ns);
    }

    ~Ns() { GJS_DEC_COUNTER(ns); }

    // JSClass operations

    // The *resolved out parameter, on success, should be false to indicate that
    // id was not resolved; and true if id was resolved.
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved) {
        if (!JSID_IS_STRING(id)) {
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

        GjsAutoBaseInfo info =
            g_irepository_find_by_name(nullptr, get(), name.get());
        if (!info) {
            *resolved = false;  // No property defined, but no error either
            return true;
        }

        gjs_debug(GJS_DEBUG_GNAMESPACE,
                  "Found info type %s for '%s' in namespace '%s'",
                  gjs_info_type_name(info.type()), info.name(), info.ns());

        bool defined;
        if (!gjs_define_info(cx, obj, info, &defined)) {
            gjs_debug(GJS_DEBUG_GNAMESPACE, "Failed to define info '%s'",
                      info.name());
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
        int n = g_irepository_get_n_infos(nullptr, get());
        if (!properties.reserve(properties.length() + n)) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

        for (int k = 0; k < n; k++) {
            GjsAutoBaseInfo info = g_irepository_get_info(nullptr, get(), k);
            GIInfoType info_type = g_base_info_get_type(info);
            if (!type_is_enumerable(info_type))
                continue;

            const char* name = info.name();

            jsid id = gjs_intern_string_to_id(cx, name);
            if (id == JSID_VOID)
                return false;
            properties.infallibleAppend(id);
        }

        return true;
    }

    static void finalize_impl(JSFreeOp* fop [[maybe_unused]], Ns* priv) {
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
        const char *version = g_irepository_get_version(nullptr, priv->get());
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
        JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE, &Ns::class_ops,
        &Ns::class_spec};

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
        g_assert(!JS_GetPrivate(ns));
        JS_SetPrivate(ns, priv);

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
