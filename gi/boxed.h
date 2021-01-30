/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_BOXED_H_
#define GI_BOXED_H_

#include <config.h>

#include <stdint.h>

#include <memory>  // for unique_ptr

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCHashTable.h>  // for GCHashMap
#include <js/HashTable.h>    // for DefaultHasher
#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/cwrapper.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

class BoxedPrototype;
class BoxedInstance;
class JSTracer;
namespace JS {
class CallArgs;
}
namespace js {
class SystemAllocPolicy;
}

/* To conserve memory, we have two different kinds of private data for GBoxed
 * JS wrappers: BoxedInstance, and BoxedPrototype. Both inherit from BoxedBase
 * for their common functionality. For more information, see the notes in
 * wrapperutils.h.
 */

class BoxedBase
    : public GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance> {
    friend class CWrapperPointerOps<BoxedBase>;
    friend class GIWrapperBase<BoxedBase, BoxedPrototype, BoxedInstance>;

 protected:
    explicit BoxedBase(BoxedPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GBOXED;
    static constexpr const char* DEBUG_TAG = "boxed";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;

    // JS property accessors

    GJS_JSAPI_RETURN_CONVENTION
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    // Helper methods that work on either instances or prototypes

    GJS_JSAPI_RETURN_CONVENTION
    GIFieldInfo* get_field_info(JSContext* cx, uint32_t id) const;

 public:
    [[nodiscard]] BoxedBase* get_copy_source(JSContext* cx,
                                             JS::Value value) const;
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
    std::unique_ptr<FieldMap> m_field_map;
    bool m_can_allocate_directly : 1;

    explicit BoxedPrototype(GIStructInfo* info, GType gtype);
    ~BoxedPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx);

    static constexpr InfoType::Tag info_type_tag = InfoType::Struct;

    // Accessors

 public:
    [[nodiscard]] bool can_allocate_directly() const {
        return m_can_allocate_directly;
    }
    [[nodiscard]] bool has_zero_args_constructor() const {
        return m_zero_args_constructor >= 0;
    }
    [[nodiscard]] bool has_default_constructor() const {
        return m_default_constructor >= 0;
    }
    [[nodiscard]] GIFunctionInfo* zero_args_constructor_info() const {
        return g_struct_info_get_method(info(), m_zero_args_constructor);
    }
    // The ID is traced from the object, so it's OK to create a handle from it.
    [[nodiscard]] JS::HandleId default_constructor_name() const {
        return JS::HandleId::fromMarkedLocation(
            m_default_constructor_name.address());
    }

    // JSClass operations

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);

    GJS_JSAPI_RETURN_CONVENTION
    bool new_enumerate_impl(JSContext* cx, JS::HandleObject obj,
                            JS::MutableHandleIdVector properties,
                            bool only_enumerable);
    void trace_impl(JSTracer* trc);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    static std::unique_ptr<FieldMap> create_field_map(
        JSContext* cx, GIStructInfo* struct_info);
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
