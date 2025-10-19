/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Intel Corporation
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#pragma once

#include <config.h>

#include <girepository/girepository.h>
#include <glib-object.h>

#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/macros.h"
#include "util/log.h"

class FundamentalPrototype;
class FundamentalInstance;
namespace JS { class CallArgs; }

/* To conserve memory, we have two different kinds of private data for JS
 * wrappers for fundamental types: FundamentalInstance, and
 * FundamentalPrototype. Both inherit from FundamentalBase for their common
 * functionality. For more information, see the notes in wrapperutils.h.
 */

class FundamentalBase
    : public GIWrapperBase<FundamentalBase, FundamentalPrototype,
                           FundamentalInstance> {
    friend class CWrapperPointerOps<FundamentalBase>;
    friend class GIWrapperBase<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance>;

 protected:
    explicit FundamentalBase(FundamentalPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GFUNDAMENTAL;
    static constexpr const char* DEBUG_TAG = "fundamental";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;

    // Public API

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_gvalue(JSContext*, JS::HandleObject, GValue*);
};

class FundamentalPrototype
    : public GIWrapperPrototype<FundamentalBase, FundamentalPrototype,
                                FundamentalInstance, GI::AutoObjectInfo,
                                GI::ObjectInfo> {
    friend class GIWrapperPrototype<FundamentalBase, FundamentalPrototype,
                                    FundamentalInstance, GI::AutoObjectInfo,
                                    GI::ObjectInfo>;
    friend class GIWrapperBase<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance>;

    GIObjectInfoRefFunction m_ref_function;
    GIObjectInfoUnrefFunction m_unref_function;
    GIObjectInfoGetValueFunction m_get_value_function;
    GIObjectInfoSetValueFunction m_set_value_function;
    mozilla::Maybe<GI::AutoFunctionInfo> m_constructor_info;

    explicit FundamentalPrototype(const GI::ObjectInfo, GType);
    ~FundamentalPrototype();

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static FundamentalPrototype* for_gtype(JSContext*, GType);

    // Accessors

    [[nodiscard]]
    mozilla::Maybe<const GI::FunctionInfo> constructor_info() const {
        return m_constructor_info;
    }

    void* call_ref_function(void* ptr) const {
        if (!m_ref_function)
            return ptr;

        return m_ref_function(ptr);
    }
    void call_unref_function(void* ptr) const {
        if (m_unref_function)
            m_unref_function(ptr);
    }
    [[nodiscard]] bool call_get_value_function(const GValue* value,
                                               void** ptr_out) const {
        if (!m_get_value_function)
            return false;

        *ptr_out = m_get_value_function(value);
        return true;
    }
    bool call_set_value_function(GValue* value, void* object) const {
        if (m_set_value_function) {
            m_set_value_function(value, object);
            return true;
        }

        return false;
    }

    // Helper methods

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool get_parent_proto(JSContext*, JS::MutableHandleObject proto) const;

    [[nodiscard]] unsigned constructor_nargs() const;

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_interface(JSContext*, JS::HandleObject, bool* resolved,
                           const char* name);

    // JSClass operations

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext*, JS::HandleObject, JS::HandleId,
                      bool* resolved);

    // Public API
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext*, JS::HandleObject in_object,
                             const GI::ObjectInfo,
                             JS::MutableHandleObject constructor);
};

class FundamentalInstance
    : public GIWrapperInstance<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance> {
    friend class FundamentalBase;  // for set_value()
    friend class GIWrapperInstance<FundamentalBase, FundamentalPrototype,
                                   FundamentalInstance>;
    friend class GIWrapperBase<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance>;

    explicit FundamentalInstance(FundamentalPrototype*, JS::HandleObject);
    ~FundamentalInstance();

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke_constructor(JSContext*, JS::HandleObject, const JS::CallArgs&,
                            GIArgument* rvalue);

    void ref() { get_prototype()->call_ref_function(m_ptr); }
    void unref() { get_prototype()->call_unref_function(m_ptr); }
    [[nodiscard]] bool set_value(GValue* gvalue) const {
        return get_prototype()->call_set_value_function(gvalue, m_ptr);
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool associate_js_instance(JSContext*, JSObject*, void* gfundamental);

    // JS constructor

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext*, JS::HandleObject, const JS::CallArgs&);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* object_for_c_ptr(JSContext*, void* gfundamental);
    GJS_JSAPI_RETURN_CONVENTION
    static bool object_for_gvalue(JSContext*, const GValue*, GType,
                                  JS::MutableHandleObject);

    static void* copy_ptr(JSContext*, GType, void* gfundamental);
};
