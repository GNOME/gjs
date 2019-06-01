/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef GI_BOXED_H_
#define GI_BOXED_H_

#include <girepository.h>
#include <glib.h>

#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/macros.h"
#include "util/log.h"

#include "js/GCHashTable.h"

class BoxedPrototype;
class BoxedInstance;

/* To conserve memory, we have two different kinds of private data for GBoxed
 * JS wrappers: BoxedInstance, and BoxedPrototype. Both inherit from BoxedBase
 * for their common functionality. For more information, see the notes in
 * wrapperutils.h.
 */

class BoxedBase
    : public GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance> {
    friend class GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance>;

 protected:
    explicit BoxedBase(BoxedPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}
    ~BoxedBase(void) {}

    static const GjsDebugTopic debug_topic = GJS_DEBUG_GBOXED;
    static constexpr const char* debug_tag = "GBoxed";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;

    // JS property accessors

    GJS_JSAPI_RETURN_CONVENTION
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    // Helper methods that work on either instances or prototypes

    GJS_USE const char* to_string_kind(void) const { return "boxed"; }

    GJS_JSAPI_RETURN_CONVENTION
    GIFieldInfo* get_field_info(JSContext* cx, uint32_t id) const;

 public:
    GJS_USE
    BoxedBase* get_copy_source(JSContext* cx, JS::Value value) const;
};

class BoxedPrototype : public GIWrapperPrototype<BoxedBase, BoxedPrototype,
                                                 BoxedInstance, GIStructInfo> {
    friend class GIWrapperPrototype<BoxedBase, BoxedPrototype, BoxedInstance,
                                    GIStructInfo>;
    friend class GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance>;

    using FieldMap =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoFieldInfo,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;

    int m_zero_args_constructor;  // -1 if none
    int m_default_constructor;  // -1 if none
    JS::Heap<jsid> m_default_constructor_name;
    FieldMap* m_field_map;
    bool m_can_allocate_directly : 1;

    explicit BoxedPrototype(GIStructInfo* info, GType gtype);
    ~BoxedPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx);

    static constexpr InfoType::Tag info_type_tag = InfoType::Struct;

    // Accessors

 public:
    GJS_USE
    bool can_allocate_directly(void) const { return m_can_allocate_directly; }
    GJS_USE
    bool has_zero_args_constructor(void) const {
        return m_zero_args_constructor >= 0;
    }
    GJS_USE
    bool has_default_constructor(void) const {
        return m_default_constructor >= 0;
    }
    GJS_USE
    GIFunctionInfo* zero_args_constructor_info(void) const {
        return g_struct_info_get_method(info(), m_zero_args_constructor);
    }
    // The ID is traced from the object, so it's OK to create a handle from it.
    GJS_USE
    JS::HandleId default_constructor_name(void) const {
        return JS::HandleId::fromMarkedLocation(
            m_default_constructor_name.address());
    }

    // JSClass operations

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      const char* prop_name, bool* resolved);
    void trace_impl(JSTracer* trc);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    static FieldMap* create_field_map(JSContext* cx, GIStructInfo* struct_info);
    GJS_JSAPI_RETURN_CONVENTION
    bool ensure_field_map(JSContext* cx);
    GJS_JSAPI_RETURN_CONVENTION
    bool define_boxed_class_fields(JSContext* cx, JS::HandleObject proto);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             GIStructInfo* info);
    GJS_JSAPI_RETURN_CONVENTION
    GIFieldInfo* lookup_field(JSContext* cx, JSString* prop_name);
};

class BoxedInstance
    : public GIWrapperInstance<BoxedBase, BoxedPrototype, BoxedInstance> {
    friend class GIWrapperInstance<BoxedBase, BoxedPrototype, BoxedInstance>;
    friend class GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance>;
    friend class BoxedBase;  // for field_getter, etc.

    bool m_allocated_directly : 1;
    bool m_owning_ptr : 1;  // if set, the JS wrapper owns the C memory referred
                            // to by m_ptr.

    explicit BoxedInstance(JSContext* cx, JS::HandleObject obj);
    ~BoxedInstance(void);

    // Don't set GIWrapperBase::m_ptr directly. Instead, use one of these
    // setters to express your intention to own the pointer or not.
    void own_ptr(void* boxed_ptr) {
        g_assert(!m_ptr);
        m_ptr = boxed_ptr;
        m_owning_ptr = true;
    }
    void share_ptr(void* unowned_boxed_ptr) {
        g_assert(!m_ptr);
        m_ptr = unowned_boxed_ptr;
        m_owning_ptr = false;
    }

    // Methods for different ways to allocate the GBoxed pointer

    void allocate_directly(void);
    void copy_boxed(void* boxed_ptr);
    void copy_boxed(BoxedInstance* source);
    void copy_memory(void* boxed_ptr);
    void copy_memory(BoxedInstance* source);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_props(JSContext* cx, JS::Value props_value);

    GJS_JSAPI_RETURN_CONVENTION
    bool get_nested_interface_object(JSContext* cx, JSObject* parent_obj,
                                     GIFieldInfo* field_info,
                                     GIBaseInfo* interface_info,
                                     JS::MutableHandleValue value) const;
    GJS_JSAPI_RETURN_CONVENTION
    bool set_nested_interface_object(JSContext* cx, GIFieldInfo* field_info,
                                     GIBaseInfo* interface_info,
                                     JS::HandleValue value);

    GJS_JSAPI_RETURN_CONVENTION
    static void* copy_ptr(JSContext* cx, GType gtype, void* ptr);

    // JS property accessors

    GJS_JSAPI_RETURN_CONVENTION
    bool field_getter_impl(JSContext* cx, JSObject* obj, GIFieldInfo* info,
                           JS::MutableHandleValue rval) const;
    GJS_JSAPI_RETURN_CONVENTION
    bool field_setter_impl(JSContext* cx, GIFieldInfo* info,
                           JS::HandleValue value);

    // JS constructor

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

    // Public API for initializing BoxedInstance JS object from C struct

 public:
    struct NoCopy {};

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_c_struct(JSContext* cx, void* gboxed);
    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_c_struct(JSContext* cx, void* gboxed, NoCopy);
    template <typename... Args>
    GJS_JSAPI_RETURN_CONVENTION static JSObject* new_for_c_struct_impl(
        JSContext* cx, GIStructInfo* info, void* gboxed, Args&&... args);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_struct(JSContext* cx, GIStructInfo* info,
                                      void* gboxed);
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* new_for_c_struct(JSContext* cx, GIStructInfo* info,
                                      void* gboxed, NoCopy);
};

#endif  // GI_BOXED_H_
