/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013       Intel Corporation
 * Copyright (c) 2008-2010  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef GI_FUNDAMENTAL_H_
#define GI_FUNDAMENTAL_H_

#include <stdbool.h>
#include <glib.h>
#include <girepository.h>

#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

class FundamentalPrototype;
class FundamentalInstance;

/* To conserve memory, we have two different kinds of private data for JS
 * wrappers for fundamental types: FundamentalInstance, and
 * FundamentalPrototype. Both inherit from FundamentalBase for their common
 * functionality. For more information, see the notes in wrapperutils.h.
 */

class FundamentalBase
    : public GIWrapperBase<FundamentalBase, FundamentalPrototype,
                           FundamentalInstance> {
    friend class GIWrapperBase<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance>;

 protected:
    explicit FundamentalBase(FundamentalPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}
    ~FundamentalBase(void) {}

    static const GjsDebugTopic debug_topic = GJS_DEBUG_GFUNDAMENTAL;
    static constexpr const char* debug_tag = "fundamental";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;

    // Helper methods

    GJS_USE const char* to_string_kind(void) const { return "fundamental"; }

    // Public API

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_gvalue(JSContext* cx, JS::HandleObject obj, GValue* gvalue);
};

class FundamentalPrototype
    : public GIWrapperPrototype<FundamentalBase, FundamentalPrototype,
                                FundamentalInstance> {
    friend class GIWrapperPrototype<FundamentalBase, FundamentalPrototype,
                                    FundamentalInstance>;
    friend class GIWrapperBase<FundamentalBase, FundamentalPrototype,
                               FundamentalInstance>;

    GIObjectInfoRefFunction m_ref_function;
    GIObjectInfoUnrefFunction m_unref_function;
    GIObjectInfoGetValueFunction m_get_value_function;
    GIObjectInfoSetValueFunction m_set_value_function;

    JS::Heap<jsid> m_constructor_name;
    GICallableInfo* m_constructor_info;

    explicit FundamentalPrototype(GIObjectInfo* info, GType gtype);
    ~FundamentalPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx);

    static constexpr InfoType::Tag info_type_tag = InfoType::Object;

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static FundamentalPrototype* for_gtype(JSContext* cx, GType gtype);

    // Accessors

    GJS_USE
    jsid constructor_name(void) const { return m_constructor_name.get(); }
    GJS_USE
    GICallableInfo* constructor_info(void) const { return m_constructor_info; }

    void* call_ref_function(void* ptr) const { return m_ref_function(ptr); }
    void call_unref_function(void* ptr) const { m_unref_function(ptr); }
    GJS_USE void* call_get_value_function(const GValue* value) const {
        return m_get_value_function(value);
    }
    void call_set_value_function(GValue* value, void* object) const {
        m_set_value_function(value, object);
    }

    // Helper methods

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool get_parent_proto(JSContext* cx, JS::MutableHandleObject proto) const;

    GJS_USE unsigned constructor_nargs(void) const;

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_interface(JSContext* cx, JS::HandleObject obj, bool* resolved,
                           const char* name);

    // JSClass operations

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      const char* prop_name, bool* resolved);
    void trace_impl(JSTracer* trc);

    // Public API
 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             GIObjectInfo* info,
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

    explicit FundamentalInstance(JSContext* cx, JS::HandleObject obj);
    ~FundamentalInstance(void);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke_constructor(JSContext* cx, JS::HandleObject obj,
                            const JS::HandleValueArray& args,
                            GIArgument* rvalue);

    void ref(void) { get_prototype()->call_ref_function(m_ptr); }
    void unref(void) { get_prototype()->call_unref_function(m_ptr); }
    void set_value(GValue* gvalue) const {
        get_prototype()->call_set_value_function(gvalue, m_ptr);
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
    static JSObject* object_for_gvalue(JSContext* cx, const GValue* gvalue,
                                       GType gtype);

    static void* copy_ptr(JSContext* cx, GType gtype, void* gfundamental);
};

#endif  // GI_FUNDAMENTAL_H_
