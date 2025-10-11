/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>

#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/GCHashTable.h>         // for WeakCache
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewObjectWithGivenProto
#include <mozilla/HashTable.h>

#include "gi/cwrapper.h"
#include "gi/gtype.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-root.h"  // for WeakPtr methods
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

/*
 * GTypeObj:
 *
 * Wrapper object used to represent a GType in JavaScript.
 * In C, GTypes are just a pointer-sized integer, but in JS they have a 'name'
 * property and a toString() method.
 * The integer is stuffed into CWrapper's pointer slot.
 */
class GTypeObj : public CWrapper<GTypeObj, void> {
    friend CWrapperPointerOps<GTypeObj, void>;
    friend CWrapper<GTypeObj, void>;

    static constexpr auto PROTOTYPE_SLOT = GjsGlobalSlot::PROTOTYPE_gtype;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GTYPE;

    // JSClass operations

    // No private data is allocated, it's stuffed directly in the private field
    // of JSObject, so nothing to free
    static void finalize_impl(JS::GCContext*, void*) {}

    // Properties

    GJS_JSAPI_RETURN_CONVENTION
    static bool get_name(JSContext* cx, unsigned argc, JS::Value* vp) {
        GJS_GET_THIS(cx, argc, vp, args, obj);
        GType gtype = value(cx, obj, &args);
        if (gtype == 0)
            return false;

        return gjs_string_from_utf8(cx, g_type_name(gtype), args.rval());
    }

    // Methods

    GJS_JSAPI_RETURN_CONVENTION
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp) {
        GJS_GET_THIS(cx, argc, vp, rec, obj);
        GType gtype = value(cx, obj, &rec);
        if (gtype == 0)
            return false;

        Gjs::AutoChar strval{
            g_strdup_printf("[object GType for '%s']", g_type_name(gtype))};
        return gjs_string_from_utf8(cx, strval, rec.rval());
    }

    // clang-format off
    static constexpr JSPropertySpec proto_props[] = {
        JS_PSG("name", &GTypeObj::get_name, JSPROP_PERMANENT),
        JS_STRING_SYM_PS(toStringTag, "GIRepositoryGType", JSPROP_READONLY),
        JS_PS_END};

    static constexpr JSFunctionSpec proto_funcs[] = {
        JS_FN("toString", &GTypeObj::to_string, 0, 0),
        JS_FS_END};
    // clang-format on

    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        nullptr,  // createPrototype
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        GTypeObj::proto_funcs,
        GTypeObj::proto_props,
        nullptr,  // finishInit
        js::ClassSpec::DontDefineConstructor};

    static constexpr JSClass klass = {
        "GIRepositoryGType",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_FOREGROUND_FINALIZE,
        &GTypeObj::class_ops, &GTypeObj::class_spec};

    GJS_JSAPI_RETURN_CONVENTION
    static GType value(JSContext* cx, JS::HandleObject obj,
                       JS::CallArgs* args) {
        void* data;
        if (!for_js_typecheck(cx, obj, &data, args))
            return G_TYPE_NONE;
        return GPOINTER_TO_SIZE(data);
    }

    GJS_JSAPI_RETURN_CONVENTION
    static GType value(JSContext* cx, JS::HandleObject obj) {
        return GPOINTER_TO_SIZE(for_js(cx, obj));
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool actual_gtype_recurse(JSContext* cx, const GjsAtoms& atoms,
                                     JS::HandleObject object, GType* gtype_out,
                                     int recurse) {
        GType gtype = value(cx, object);
        if (gtype > 0) {
            *gtype_out = gtype;
            return true;
        }

        JS::RootedValue v_gtype(cx);

        // OK, we don't have a GType wrapper object -- grab the "$gtype"
        // property on that and hope it's a GType wrapper object
        if (!JS_GetPropertyById(cx, object, atoms.gtype(), &v_gtype))
            return false;
        if (!v_gtype.isObject()) {
            // OK, so we're not a class. But maybe we're an instance. Check for
            // "constructor" and recurse on that.
            if (!JS_GetPropertyById(cx, object, atoms.constructor(), &v_gtype))
                return false;
        }

        if (recurse > 0 && v_gtype.isObject()) {
            JS::RootedObject gtype_obj(cx, &v_gtype.toObject());
            return actual_gtype_recurse(cx, atoms, gtype_obj, gtype_out,
                                        recurse - 1);
        }

        *gtype_out = G_TYPE_INVALID;
        return true;
    }

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext* cx, GType gtype) {
        g_assert(gtype != 0 &&
                 "Attempted to create wrapper object for invalid GType");

        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
        // We cannot use gtype_table().lookupForAdd() here, because in between
        // the lookup and the add, GCs may take place and mutate the hash table.
        // A GC may only remove an element, not add one, so it's still safe to
        // do this without locking.
        auto p = gjs->gtype_table().lookup(gtype);
        if (p.found())
            return p->value();

        JS::RootedObject proto(cx, GTypeObj::create_prototype(cx));
        if (!proto)
            return nullptr;

        JS::RootedObject gtype_wrapper(
            cx, JS_NewObjectWithGivenProto(cx, &GTypeObj::klass, proto));
        if (!gtype_wrapper)
            return nullptr;

        GTypeObj::init_private(gtype_wrapper, GSIZE_TO_POINTER(gtype));

        gjs->gtype_table().put(gtype, gtype_wrapper);

        return gtype_wrapper;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool actual_gtype(JSContext* cx, JS::HandleObject object,
                             GType* gtype_out) {
        g_assert(gtype_out && "Missing return location");

        // 2 means: recurse at most three times (including this call).
        // The levels are calculated considering that, in the worst case we need
        // to go from instance to class, from class to GType object and from
        // GType object to GType value.

        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        return actual_gtype_recurse(cx, atoms, object, gtype_out, 2);
    }
};

JSObject* gjs_gtype_create_gtype_wrapper(JSContext* context, GType gtype) {
    return GTypeObj::create(context, gtype);
}

bool gjs_gtype_get_actual_gtype(JSContext* context, JS::HandleObject object,
                                GType* gtype_out) {
    return GTypeObj::actual_gtype(context, object, gtype_out);
}
