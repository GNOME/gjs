/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2022 Marco Trevisan <marco.trevisan@canonical.com>
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

#ifndef GI_BOXED_H_
#define GI_BOXED_H_

#include <config.h>

#include <stddef.h>  // for size_t
#include <stdint.h>

#include <memory>  // for unique_ptr
#include <string>

#include <glib-object.h>
#include <glib.h>

#include <js/AllocPolicy.h>
#include <js/GCHashTable.h>  // for GCHashMap
#include <js/HashTable.h>    // for DefaultHasher
#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

class JSTracer;
namespace JS {
class CallArgs;
}

namespace Boxed {
struct NoCopy {};

using FieldMap =
    JS::GCHashMap<JS::Heap<JSString*>, GI::AutoFieldInfo,
                  js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
}  // namespace Boxed

/* To conserve memory, we have two different kinds of private data for GBoxed
 * JS wrappers: BoxedInstance, and BoxedPrototype. Both inherit from BoxedBase
 * for their common functionality. For more information, see the notes in
 * wrapperutils.h.
 */

template <class Base, class Prototype, class Instance>
class BoxedBase : public GIWrapperBase<Base, Prototype, Instance> {
    using BaseClass = GIWrapperBase<Base, Prototype, Instance>;

 protected:
    using BaseClass::BaseClass;

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GBOXED;

    // JS property accessors

    GJS_JSAPI_RETURN_CONVENTION
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    // Helper methods that work on either instances or prototypes

    GJS_JSAPI_RETURN_CONVENTION
    mozilla::Maybe<GI::AutoFieldInfo> get_field_info(JSContext*,
                                                     uint32_t id) const;

 public:
    [[nodiscard]] Base* get_copy_source(JSContext*, JS::Value) const;
    using BaseClass::info;
    using BaseClass::name;
};

template <class Base, class Prototype, class Instance>
class BoxedPrototype : public GIWrapperPrototype<Base, Prototype, Instance,
                                                 GI::OwnedInfo<Base::TAG>,
                                                 GI::UnownedInfo<Base::TAG>> {
    using BoxedInfo = GI::UnownedInfo<Base::TAG>;
    using BaseClass = GIWrapperPrototype<Base, Prototype, Instance,
                                         GI::OwnedInfo<Base::TAG>, BoxedInfo>;
    friend class GIWrapperBase<Base, Prototype, Instance>;

    int m_zero_args_constructor;  // -1 if none
    int m_default_constructor;  // -1 if none
    JS::Heap<jsid> m_default_constructor_name;
    std::unique_ptr<Boxed::FieldMap> m_field_map;
    bool m_can_allocate_directly_without_pointers : 1;
    bool m_can_allocate_directly : 1;

 protected:
    explicit BoxedPrototype(const BoxedInfo, GType);

    GJS_JSAPI_RETURN_CONVENTION bool init(JSContext* cx);

    // Accessors

 public:
    GJS_JSAPI_RETURN_CONVENTION
    mozilla::Maybe<const GI::FieldInfo> lookup_field(JSContext*,
                                                     JSString* prop_name);

    [[nodiscard]] bool can_allocate_directly_without_pointers() const {
        return m_can_allocate_directly_without_pointers;
    }
    [[nodiscard]] bool can_allocate_directly() const {
        return m_can_allocate_directly;
    }
    [[nodiscard]] bool has_zero_args_constructor() const {
        return m_zero_args_constructor >= 0;
    }
    [[nodiscard]] bool has_default_constructor() const {
        return m_default_constructor >= 0;
    }
    [[nodiscard]]
    GI::AutoFunctionInfo zero_args_constructor_info() const {
        g_assert(has_zero_args_constructor());
        return *info().methods()[m_zero_args_constructor];
    }
    // The ID is traced from the object, so it's OK to create a handle from it.
    [[nodiscard]] JS::HandleId default_constructor_name() const {
        return JS::HandleId::fromMarkedLocation(
            m_default_constructor_name.unsafeAddress());
    }

    using BaseClass::format_name;
    using BaseClass::gtype;
    using BaseClass::info;
    using BaseClass::name;

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
    static std::unique_ptr<Boxed::FieldMap> create_field_map(JSContext*,
                                                             const BoxedInfo);
    GJS_JSAPI_RETURN_CONVENTION
    bool ensure_field_map(JSContext* cx);
    GJS_JSAPI_RETURN_CONVENTION
    bool define_boxed_class_fields(JSContext* cx, JS::HandleObject proto);

 protected:
    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class_impl(JSContext*, JS::HandleObject in_object,
                                  const BoxedInfo,
                                  JS::MutableHandleObject prototype);
    static std::string find_unique_js_field_name(const BoxedInfo,
                                                 const std::string& field_name);
};

template <class Base, class Prototype, class Instance>
class BoxedInstance : public GIWrapperInstance<Base, Prototype, Instance> {
    using BaseClass = GIWrapperInstance<Base, Prototype, Instance>;
    friend class GIWrapperBase<Base, Prototype, Instance>;
    friend class BoxedBase<Base, Prototype, Instance>;  // for field_getter, etc
    template <class OtherInstance>
    friend void adopt_nested_ptr(OtherInstance*, void*);

    using BoxedInfo = GI::UnownedInfo<Base::TAG>;

    // Reserved slots
    static const size_t PARENT_OBJECT = 1;

 protected:
    bool m_allocated_directly : 1;
    bool m_owning_ptr : 1;  // if set, the JS wrapper owns the C memory referred
                            // to by m_ptr.

    explicit BoxedInstance(Prototype* prototype, JS::HandleObject obj);
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
    void copy_boxed(Instance* source);
    void copy_memory(void* boxed_ptr);
    void copy_memory(Instance* source);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke_static_method(JSContext*, JS::HandleObject,
                              JS::HandleId method_name, const JS::CallArgs&);
    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_props(JSContext* cx, JS::Value props_value);

    template <class FieldInstance>
    GJS_JSAPI_RETURN_CONVENTION bool get_nested_interface_object(
        JSContext*, JSObject* parent_obj, const GI::FieldInfo,
        const GI::UnownedInfo<FieldInstance::TAG>,
        JS::MutableHandleValue) const;
    template <class FieldBase>
    GJS_JSAPI_RETURN_CONVENTION bool set_nested_interface_object(
        JSContext*, const GI::FieldInfo, const GI::UnownedInfo<FieldBase::TAG>,
        JS::HandleValue);

    GJS_JSAPI_RETURN_CONVENTION
    static void* copy_ptr(JSContext* cx, GType gtype, void* ptr) {
        if (g_type_is_a(gtype, G_TYPE_BOXED))
            return g_boxed_copy(gtype, ptr);

        gjs_throw(cx,
                  "Can't transfer ownership of a %s type not registered as "
                  "boxed",
                  Base::DEBUG_TAG);
        return nullptr;
    }

    // JS property accessors

    GJS_JSAPI_RETURN_CONVENTION
    bool field_getter_impl(JSContext*, JSObject*, const GI::FieldInfo,
                           JS::MutableHandleValue rval) const;
    GJS_JSAPI_RETURN_CONVENTION
    bool field_setter_impl(JSContext*, const GI::FieldInfo, JS::HandleValue);

    // JS constructor

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

    // Public API for initializing BoxedInstance JS object from C struct

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_c_struct(JSContext* cx, void* gboxed);
    GJS_JSAPI_RETURN_CONVENTION
    bool init_from_c_struct(JSContext* cx, void* gboxed, Boxed::NoCopy);

 protected:
    template <typename... Args>
    GJS_JSAPI_RETURN_CONVENTION static JSObject* new_for_c_struct_impl(
        JSContext*, const BoxedInfo, void* gboxed, Args&&...);

 protected:
    using BaseClass::debug_lifecycle;
    using BaseClass::get_copy_source;
    using BaseClass::get_field_info;
    using BaseClass::get_prototype;
    using BaseClass::m_ptr;
    using BaseClass::raw_ptr;

 public:
    using BaseClass::format_name;
    using BaseClass::gtype;
    using BaseClass::info;
    using BaseClass::name;
};

#endif  // GI_BOXED_H_
