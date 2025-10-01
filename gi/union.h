/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_UNION_H_
#define GI_UNION_H_

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gi/boxed.h"
#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/macros.h"

struct JSClass;
struct JSClassOps;
class UnionPrototype;
class UnionInstance;

class UnionBase : public BoxedBase<UnionBase, UnionPrototype, UnionInstance> {
    friend class CWrapperPointerOps<UnionBase>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;
    friend class BoxedBase<UnionBase, UnionPrototype, UnionInstance>;
    friend class BoxedPrototype<UnionBase, UnionPrototype, UnionInstance>;
    friend class BoxedInstance<UnionBase, UnionPrototype, UnionInstance>;

 protected:
    using BoxedBase::BoxedBase;

    static constexpr const char* DEBUG_TAG = "union";

    static const JSClassOps class_ops;
    static const JSClass klass;

 public:
    static constexpr const GI::InfoTag TAG = GI::InfoTag::UNION;
};

class UnionPrototype
    : public BoxedPrototype<UnionBase, UnionPrototype, UnionInstance> {
    friend class GIWrapperPrototype<UnionBase, UnionPrototype, UnionInstance,
                                    GI::AutoUnionInfo, GI::UnionInfo>;

    explicit UnionPrototype(const GI::UnionInfo info, GType gtype);
    ~UnionPrototype(void);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             const GI::UnionInfo);
};

class UnionInstance
    : public BoxedInstance<UnionBase, UnionPrototype, UnionInstance> {
    friend class GIWrapperInstance<UnionBase, UnionPrototype, UnionInstance>;

    explicit UnionInstance(UnionPrototype* prototype, JS::HandleObject obj);
    ~UnionInstance(void);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_union(JSContext*, const GI::UnionInfo,
                                     void* gboxed);
};

#endif  // GI_UNION_H_
