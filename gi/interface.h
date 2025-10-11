/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#ifndef GI_INTERFACE_H_
#define GI_INTERFACE_H_

#include <config.h>

#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

class InterfacePrototype;
class InterfaceInstance;
struct JSFunctionSpec;

/* For more information on this Base/Prototype/Interface scheme, see the notes
 * in wrapperutils.h.
 *
 * What's unusual about this subclass is that InterfaceInstance should never
 * actually be instantiated. Interfaces can't be constructed, and
 * GIWrapperBase::constructor() is overridden to just throw an exception and not
 * create any JS wrapper object.
 *
 * We use the template classes from wrapperutils.h anyway, because there is
 * still a lot of common code.
 */

class InterfaceBase : public GIWrapperBase<InterfaceBase, InterfacePrototype,
                                           InterfaceInstance> {
    friend class CWrapperPointerOps<InterfaceBase>;
    friend class GIWrapperBase<InterfaceBase, InterfacePrototype,
                               InterfaceInstance>;

 protected:
    explicit InterfaceBase(InterfacePrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GINTERFACE;
    static constexpr const char* DEBUG_TAG = "interface";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;
    static JSFunctionSpec static_methods[];

    // JSNative methods

    // Overrides GIWrapperBase::constructor().
    GJS_JSAPI_RETURN_CONVENTION
    static bool constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        gjs_throw_abstract_constructor_error(cx, args);
        return false;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool has_instance(JSContext* cx, unsigned argc, JS::Value* vp);
};

class InterfacePrototype
    : public GIWrapperPrototype<InterfaceBase, InterfacePrototype,
                                InterfaceInstance,
                                mozilla::Maybe<GI::AutoInterfaceInfo>,
                                mozilla::Maybe<GI::InterfaceInfo>> {
    friend class GIWrapperPrototype<InterfaceBase, InterfacePrototype,
                                    InterfaceInstance,
                                    mozilla::Maybe<GI::AutoInterfaceInfo>,
                                    mozilla::Maybe<GI::InterfaceInfo>>;
    friend class GIWrapperBase<InterfaceBase, InterfacePrototype,
                               InterfaceInstance>;
    friend class InterfaceBase;  // for has_instance_impl

    // the GTypeInterface vtable wrapped by this JS object
    GTypeInterface* m_vtable;

    explicit InterfacePrototype(mozilla::Maybe<const GI::InterfaceInfo>, GType);
    ~InterfacePrototype(void);

    // JSClass operations

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);

    GJS_JSAPI_RETURN_CONVENTION
    bool new_enumerate_impl(JSContext* cx, JS::HandleObject obj,
                            JS::MutableHandleIdVector properties,
                            bool only_enumerable);

    // JS methods

    GJS_JSAPI_RETURN_CONVENTION
    bool has_instance_impl(JSContext* cx, const JS::CallArgs& args);
};

class InterfaceInstance
    : public GIWrapperInstance<InterfaceBase, InterfacePrototype,
                               InterfaceInstance> {
    friend class GIWrapperInstance<InterfaceBase, InterfacePrototype,
                                   InterfaceInstance>;
    friend class GIWrapperBase<InterfaceBase, InterfacePrototype,
                               InterfaceInstance>;

    [[noreturn]] InterfaceInstance(InterfacePrototype* prototype,
                                   JS::HandleObject obj)
        : GIWrapperInstance(prototype, obj) {
        g_assert_not_reached();
    }
    [[noreturn]] ~InterfaceInstance(void) { g_assert_not_reached(); }
};

GJS_JSAPI_RETURN_CONVENTION
bool gjs_lookup_interface_constructor(JSContext             *context,
                                      GType                  gtype,
                                      JS::MutableHandleValue value_p);

#endif  // GI_INTERFACE_H_
