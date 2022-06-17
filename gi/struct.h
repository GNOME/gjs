/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gi/boxed.h"
#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/macros.h"

struct JSClassOps;
class StructPrototype;
class StructInstance;

class StructBase
    : public BoxedBase<StructBase, StructPrototype, StructInstance> {
    friend class CWrapperPointerOps<StructBase>;
    friend class GIWrapperBase<StructBase, StructPrototype, StructInstance>;
    friend class BoxedBase<StructBase, StructPrototype, StructInstance>;
    friend class BoxedPrototype<StructBase, StructPrototype, StructInstance>;
    friend class BoxedInstance<StructBase, StructPrototype, StructInstance>;

 protected:
    using BoxedBase::BoxedBase;

    static constexpr const char* DEBUG_TAG = "boxed";  // for historical reasons

    static const JSClassOps class_ops;
    static const JSClass klass;

 public:
    static constexpr const GI::InfoTag TAG = GI::InfoTag::STRUCT;
};

class StructPrototype
    : public BoxedPrototype<StructBase, StructPrototype, StructInstance> {
    friend class GIWrapperPrototype<StructBase, StructPrototype, StructInstance,
                                    GI::AutoStructInfo, GI::StructInfo>;

    StructPrototype(const GI::StructInfo, GType);
    ~StructPrototype();

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             const GI::StructInfo);
};

class StructInstance
    : public BoxedInstance<StructBase, StructPrototype, StructInstance> {
    friend class GIWrapperInstance<StructBase, StructPrototype, StructInstance>;

    StructInstance(StructPrototype*, JS::HandleObject);
    ~StructInstance();

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_struct(JSContext*, const GI::StructInfo,
                                      void* gboxed);
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_struct(JSContext*, const GI::StructInfo,
                                      void* gboxed, Boxed::NoCopy);
};
