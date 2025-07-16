/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Intel Corporation
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#ifndef GI_FUNDAMENTAL_H_
#define GI_FUNDAMENTAL_H_

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
    static bool to_gvalue(JSContext* cx, JS::HandleObject obj, GValue* gvalue);
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
    ~FundamentalPrototype(void);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static FundamentalPrototype* for_gtype(JSContext* cx, GType gtype);

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
    bool get_parent_proto(JSContext* cx, JS::MutableHandleObject proto) const;

    [[nodiscard]] unsigned constructor_nargs() const;

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_interface(JSContext* cx, JS::HandleObject obj, bool* resolved,
                           const char* name);

    // JSClass operations

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);

    // Public API
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
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

    explicit FundamentalInstance(FundamentalPrototype* prototype,
                                 JS::HandleObject obj);
    ~FundamentalInstance(void);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke_constructor(JSContext* cx, JS::HandleObject obj,
                            const JS::CallArgs& args, GIArgument* rvalue);

    void ref(void) { get_prototype()->call_ref_function(m_ptr); }
    void unref(void) { get_prototype()->call_unref_function(m_ptr); }
    [[nodiscard]] bool set_value(GValue* gvalue) const {
        return get_prototype()->call_set_value_function(gvalue, m_ptr);
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool associate_js_instance(JSContext* cx, JSObject* object,
                               void* gfundamental);

    // JS constructor

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* object_for_c_ptr(JSContext* cx, void* gfundamental);
    GJS_JSAPI_RETURN_CONVENTION
    static bool object_for_gvalue(JSContext* cx, const GValue* gvalue,
                                  GType gtype,
                                  JS::MutableHandleObject object_out);

    static void* copy_ptr(JSContext* cx, GType gtype, void* gfundamental);
};

#endif  // GI_FUNDAMENTAL_H_
