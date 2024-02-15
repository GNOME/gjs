/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_UNION_H_
#define GI_UNION_H_

#include <config.h>

#include <girepository.h>
#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/macros.h"
#include "util/log.h"

namespace JS {
class CallArgs;
}
struct JSClass;
struct JSClassOps;
class UnionPrototype;
class UnionInstance;

class UnionBase
    : public GIWrapperBase<UnionBase, UnionPrototype, UnionInstance> {
    friend class CWrapperPointerOps<UnionBase>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

 protected:
    explicit UnionBase(UnionPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GBOXED;
    static constexpr const char* DEBUG_TAG = "union";

    static const JSClassOps class_ops;
    static const JSClass klass;
};

class UnionPrototype
    : public GIWrapperPrototype<UnionBase, UnionPrototype, UnionInstance,
                                GI::AutoUnionInfo, GI::UnionInfo> {
    friend class GIWrapperPrototype<UnionBase, UnionPrototype, UnionInstance,
                                    GI::AutoUnionInfo, GI::UnionInfo>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

    explicit UnionPrototype(const GI::UnionInfo info, GType gtype);
    ~UnionPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);

    // Overrides GIWrapperPrototype::constructor_nargs().
    [[nodiscard]] unsigned constructor_nargs(void) const { return 0; }

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             const GI::UnionInfo);
};

class UnionInstance
    : public GIWrapperInstance<UnionBase, UnionPrototype, UnionInstance> {
    friend class GIWrapperInstance<UnionBase, UnionPrototype, UnionInstance>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

    explicit UnionInstance(UnionPrototype* prototype, JS::HandleObject obj);
    ~UnionInstance(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_union(JSContext*, const GI::UnionInfo,
                                     void* gboxed);

    /*
     * UnionInstance::copy_union:
     *
     * Allocate a new union pointer using g_boxed_copy(), from a raw union
     * pointer.
     */
    void copy_union(void* ptr) { m_ptr = g_boxed_copy(gtype(), ptr); }

    GJS_JSAPI_RETURN_CONVENTION
    static void* copy_ptr(JSContext* cx, GType gtype, void* ptr);
};

#endif  // GI_UNION_H_
