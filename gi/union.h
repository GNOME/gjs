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

class UnionPrototype : public GIWrapperPrototype<UnionBase, UnionPrototype,
                                                 UnionInstance, GIUnionInfo> {
    friend class GIWrapperPrototype<UnionBase, UnionPrototype, UnionInstance,
                                    GIUnionInfo>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

    static constexpr InfoType::Tag info_type_tag = InfoType::Union;

    explicit UnionPrototype(GIUnionInfo* info, GType gtype);
    ~UnionPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);

    // Overrides GIWrapperPrototype::constructor_nargs().
    [[nodiscard]] unsigned constructor_nargs(void) const { return 0; }
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

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_union_class(JSContext       *context,
                            JS::HandleObject in_object,
                            GIUnionInfo     *info);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_union_from_c_union       (JSContext    *context,
                                        GIUnionInfo  *info,
                                        void         *gboxed);

#endif  // GI_UNION_H_
