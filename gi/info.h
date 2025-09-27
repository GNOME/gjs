/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <stdint.h>
#include <string.h>

#include <cstddef>  // for nullptr_t
#include <iterator>
#include <utility>  // for pair, make_pair, move

#if GJS_VERBOSE_ENABLE_GI_USAGE
#    include <sstream>
#    include <string>
#endif

#include <ffi.h>
#include <girepository/girepository.h>
#include <girepository/girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCPolicyAPI.h>  // for IgnoreGCPolicy
#include <mozilla/Maybe.h>
#include <mozilla/Result.h>
#include <mozilla/ResultVariant.h>
#include <mozilla/Span.h>

#include "gjs/auto.h"
#include "gjs/gerror-result.h"
#include "util/log.h"

// This file is a C++ wrapper for libgirepository that attempts to be more
// null-safe and type-safe.
// Each introspection info type has the methods of the C API's GIFooInfo, but
// indicates whether the return value is owned by the caller (GI::AutoFooInfo)
// or unowned (GI::FooInfo), and uses Maybe to indicate when it is nullable.
// There are also GI::StackArgInfo and GI::StackTypeInfo for use with the
// CallableInfo.load_arg(), CallableInfo.load_return_type(), and
// ArgInfo.load_type() methods, for performance.

// COMPAT: We use Mozilla's Maybe, Result, and Span types because they are more
// complete than the C++ standard library types.
// std::optional does not have transform(), and_then(), etc., until C++23.
// std::expected does not appear until C++23.
// std::span does not appear until C++20.

// Note, only the methods actually needed in GJS are wrapped here. So if one is
// missing, that's not for any particular reason unless noted otherwise; it just
// was never needed yet.

using BoolResult = mozilla::Result<mozilla::Ok, mozilla::Nothing>;

namespace GI {

enum class InfoTag : unsigned {
    ARG,
    BASE,
    CALLABLE,
    CALLBACK,
    CONSTANT,
    ENUM,
    FIELD,
    FLAGS,
    FUNCTION,
    INTERFACE,
    OBJECT,
    PROPERTY,
    REGISTERED_TYPE,
    SIGNAL,
    STRUCT,
    TYPE,
    UNION,
    VALUE,
    VFUNC,
};

namespace detail {
template <InfoTag TAG>
struct InfoTraits {};
template <>
struct InfoTraits<InfoTag::ARG> {
    using CStruct = GIArgInfo;
};
template <>
struct InfoTraits<InfoTag::BASE> {
    using CStruct = GIBaseInfo;
};
template <>
struct InfoTraits<InfoTag::CALLABLE> {
    using CStruct = GICallableInfo;
};
template <>
struct InfoTraits<InfoTag::CALLBACK> {
    using CStruct = GICallbackInfo;
};
template <>
struct InfoTraits<InfoTag::CONSTANT> {
    using CStruct = GIConstantInfo;
};
template <>
struct InfoTraits<InfoTag::ENUM> {
    using CStruct = GIEnumInfo;
};
template <>
struct InfoTraits<InfoTag::FIELD> {
    using CStruct = GIFieldInfo;
};
template <>
struct InfoTraits<InfoTag::FLAGS> {
    using CStruct = GIFlagsInfo;
};
template <>
struct InfoTraits<InfoTag::FUNCTION> {
    using CStruct = GIFunctionInfo;
};
template <>
struct InfoTraits<InfoTag::INTERFACE> {
    using CStruct = GIInterfaceInfo;
};
template <>
struct InfoTraits<InfoTag::OBJECT> {
    using CStruct = GIObjectInfo;
};
template <>
struct InfoTraits<InfoTag::PROPERTY> {
    using CStruct = GIPropertyInfo;
};
template <>
struct InfoTraits<InfoTag::REGISTERED_TYPE> {
    using CStruct = GIRegisteredTypeInfo;
};
template <>
struct InfoTraits<InfoTag::SIGNAL> {
    using CStruct = GISignalInfo;
};
template <>
struct InfoTraits<InfoTag::STRUCT> {
    using CStruct = GIStructInfo;
};
template <>
struct InfoTraits<InfoTag::TYPE> {
    using CStruct = GITypeInfo;
};
template <>
struct InfoTraits<InfoTag::UNION> {
    using CStruct = GIUnionInfo;
};
template <>
struct InfoTraits<InfoTag::VALUE> {
    using CStruct = GIValueInfo;
};
template <>
struct InfoTraits<InfoTag::VFUNC> {
    using CStruct = GIVFuncInfo;
};

using GTypeFunc = GType (*)();
static constexpr const GTypeFunc gtype_funcs[] = {
    gi_arg_info_get_type,
    gi_base_info_get_type,
    gi_callable_info_get_type,
    gi_callback_info_get_type,
    gi_constant_info_get_type,
    gi_enum_info_get_type,
    gi_field_info_get_type,
    gi_flags_info_get_type,
    gi_function_info_get_type,
    gi_interface_info_get_type,
    gi_object_info_get_type,
    gi_property_info_get_type,
    gi_registered_type_info_get_type,
    gi_signal_info_get_type,
    gi_struct_info_get_type,
    gi_type_info_get_type,
    gi_union_info_get_type,
    gi_value_info_get_type,
    gi_vfunc_info_get_type,
};

constexpr GTypeFunc gtype_func(InfoTag tag) { return gtype_funcs[size_t(tag)]; }

}  // namespace detail

template <typename Wrapper, InfoTag TAG>
class InfoOperations {};

class StackArgInfo;
class StackTypeInfo;

template <InfoTag TAG>
class OwnedInfo;

template <InfoTag TAG>
class UnownedInfo;

namespace detail {
// We want the underlying pointer to be inaccessible. However, the three storage
// classes sometimes have to interact with each others' pointers. It's easier to
// put all of those operations into detail::Pointer and have the classes be
// friends of it, than it is to expose all the pointer operations via friend
// declarations individually.
struct Pointer {
    template <InfoTag TAG>
    using CStruct = typename InfoTraits<TAG>::CStruct;

    template <InfoTag TAG>
    [[nodiscard]]
    static constexpr
        typename detail::InfoTraits<TAG>::CStruct* cast(GIBaseInfo* ptr) {
        // (the following is a GI_TAG_INFO() cast but written out)
        return reinterpret_cast<typename detail::InfoTraits<TAG>::CStruct*>(
            g_type_check_instance_cast(reinterpret_cast<GTypeInstance*>(ptr),
                                       gtype_func(TAG)()));
    }

    template <InfoTag TAG>
    static constexpr CStruct<TAG>* get_from(const OwnedInfo<TAG>& owned) {
        return const_cast<CStruct<TAG>*>(owned.m_info);
    }

    template <InfoTag TAG>
    static constexpr CStruct<TAG>* get_from(const UnownedInfo<TAG>& unowned) {
        return const_cast<CStruct<TAG>*>(unowned.m_info);
    }

    // Defined out-of-line because they are not templates and so StackArgInfo
    // and StackTypeInfo need to be complete types.
    static constexpr GIArgInfo* get_from(const StackArgInfo& stack);
    static constexpr GITypeInfo* get_from(const StackTypeInfo& stack);

    template <InfoTag TAG>
    static constexpr OwnedInfo<TAG> to_owned(CStruct<TAG>* ptr) {
        return OwnedInfo<TAG>{ptr};
    }

    template <InfoTag TAG>
    static constexpr UnownedInfo<TAG> to_unowned(CStruct<TAG>* ptr) {
        return UnownedInfo<TAG>{ptr};
    }

    // Same, defined out of line so StackTypeInfo is not incomplete.
    static void to_stack(GITypeInfo* ptr, StackTypeInfo* stack);

    template <InfoTag TAG>
    static constexpr mozilla::Maybe<OwnedInfo<TAG>> nullable(
        CStruct<TAG>* ptr) {
        return ptr ? mozilla::Some(OwnedInfo<TAG>{ptr}) : mozilla::Nothing{};
    }

    template <InfoTag TAG>
    static constexpr mozilla::Maybe<UnownedInfo<TAG>> nullable_unowned(
        CStruct<TAG>* ptr) {
        return ptr ? mozilla::Some(UnownedInfo<TAG>{ptr}) : mozilla::Nothing{};
    }

    template <InfoTag TAG>
    [[nodiscard]]
    static constexpr bool typecheck(GIBaseInfo* ptr) {
        return G_TYPE_CHECK_INSTANCE_TYPE(ptr, gtype_func(TAG)());
    }
};
}  // namespace detail

///// UNOWNED INTROSPECTION INFO ///////////////////////////////////////////////

template <InfoTag TAG>
class UnownedInfo : public InfoOperations<UnownedInfo<TAG>, TAG> {
    friend struct detail::Pointer;

    using CStruct = typename detail::InfoTraits<TAG>::CStruct;
    CStruct* m_info;
    UnownedInfo() = delete;
    explicit UnownedInfo(std::nullptr_t) = delete;
    // No need to delete move constructor; declaring a copy constructor prevents
    // it from being generated.

    explicit UnownedInfo(CStruct* info) : m_info(info) { validate(); }
    [[nodiscard]] CStruct* ptr() const { return m_info; }

    void validate() const {
        static_assert(sizeof(CStruct*) == sizeof(UnownedInfo<TAG>),
                      "UnownedInfo<T> should be byte-compatible with T*");

#ifndef G_DISABLE_CAST_CHECKS
        g_assert(m_info && "Info pointer cannot be null");
        g_assert(detail::Pointer::typecheck<TAG>(GI_BASE_INFO(m_info)) &&
                 "Info type must match");
#endif  // G_DISABLE_CAST_CHECKS
    }

 public:
    // Copying is cheap, UnownedInfo just consists of a pointer.
    constexpr UnownedInfo(const UnownedInfo& other) : m_info(other.m_info) {}
    UnownedInfo& operator=(const UnownedInfo& other) {
        m_info = other.m_info;
        return *this;
    }

    // Caller must take care that the lifetime of UnownedInfo does not exceed
    // the lifetime of the StackInfo. Do not store the UnownedInfo, or try to
    // take ownership.
    UnownedInfo(const StackArgInfo& other)  // NOLINT(runtime/explicit)
        : UnownedInfo(detail::Pointer::get_from(other)) {
        static_assert(TAG == InfoTag::ARG);
    }
    UnownedInfo(const StackTypeInfo& other)  // NOLINT(runtime/explicit)
        : UnownedInfo(detail::Pointer::get_from(other)) {
        static_assert(TAG == InfoTag::TYPE);
    }

    // Caller must take care that the lifetime of UnownedInfo does not exceed
    // the lifetime of the originating OwnedInfo. That means, if you store it,
    // only store it as an OwnedInfo, adding another reference.
    UnownedInfo(const OwnedInfo<TAG>& other)  // NOLINT(runtime/explicit)
        : UnownedInfo(detail::Pointer::get_from(other)) {}
};

using ArgInfo = UnownedInfo<InfoTag::ARG>;
using BaseInfo = UnownedInfo<InfoTag::BASE>;
using CallableInfo = UnownedInfo<InfoTag::CALLABLE>;
using CallbackInfo = UnownedInfo<InfoTag::CALLBACK>;
using ConstantInfo = UnownedInfo<InfoTag::CONSTANT>;
using EnumInfo = UnownedInfo<InfoTag::ENUM>;
using FieldInfo = UnownedInfo<InfoTag::FIELD>;
using FlagsInfo = UnownedInfo<InfoTag::FLAGS>;
using FunctionInfo = UnownedInfo<InfoTag::FUNCTION>;
using InterfaceInfo = UnownedInfo<InfoTag::INTERFACE>;
using ObjectInfo = UnownedInfo<InfoTag::OBJECT>;
using RegisteredTypeInfo = UnownedInfo<InfoTag::REGISTERED_TYPE>;
using StructInfo = UnownedInfo<InfoTag::STRUCT>;
using TypeInfo = UnownedInfo<InfoTag::TYPE>;
using UnionInfo = UnownedInfo<InfoTag::UNION>;
using ValueInfo = UnownedInfo<InfoTag::VALUE>;
using VFuncInfo = UnownedInfo<InfoTag::VFUNC>;

///// OWNED INTROSPECTION INFO /////////////////////////////////////////////////

template <InfoTag TAG>
class OwnedInfo : public InfoOperations<OwnedInfo<TAG>, TAG> {
    friend struct detail::Pointer;

    using CStruct = typename detail::InfoTraits<TAG>::CStruct;
    CStruct* m_info;

    OwnedInfo() = delete;
    explicit OwnedInfo(std::nullptr_t) = delete;
    explicit OwnedInfo(CStruct* info) : m_info(info) {
        static_assert(sizeof(CStruct*) == sizeof(OwnedInfo<TAG>),
                      "OwnedInfo<T> should be byte-compatible with T*");
#ifndef G_DISABLE_CAST_CHECKS
        g_assert(m_info && "Info pointer cannot be null");
        g_assert(detail::Pointer::typecheck<TAG>(GI_BASE_INFO(m_info)) &&
                 "Info type must match");
#endif  // G_DISABLE_CAST_CHECKS
    }

    [[nodiscard]] CStruct* ptr() const { return m_info; }

 public:
    // Copy OwnedInfo from another OwnedInfo. Explicit because it takes a
    // reference.
    explicit OwnedInfo(const OwnedInfo& other) : OwnedInfo(other.m_info) {
        gi_base_info_ref(m_info);
    }
    // Move another OwnedInfo into this one
    OwnedInfo(OwnedInfo&& other) : OwnedInfo(other.m_info) {
        other.m_info = nullptr;
    }
    OwnedInfo& operator=(const OwnedInfo& other) {
        m_info = other.m_info;
        gi_base_info_ref(m_info);
        return *this;
    }
    OwnedInfo& operator=(OwnedInfo&& other) {
        std::swap(m_info, other.m_info);
        return *this;
    }
    ~OwnedInfo() { g_clear_pointer(&m_info, gi_base_info_unref); }

    // Copy OwnedInfo from UnownedInfo, which also comes down to just taking a
    // reference. Explicit because it takes a reference. However, make sure the
    // UnownedInfo is not borrowed from a StackInfo!
    explicit OwnedInfo(const UnownedInfo<TAG>& other)
        : OwnedInfo(detail::Pointer::get_from(other)) {
        gi_base_info_ref(m_info);
    }

    // Do not try to take ownership of a StackInfo.
    // (cpplint false positive: https://github.com/cpplint/cpplint/issues/386)
    OwnedInfo(const StackArgInfo& other) = delete;   // NOLINT(runtime/explicit)
    OwnedInfo(const StackTypeInfo& other) = delete;  // NOLINT(runtime/explicit)
};

using AutoArgInfo = OwnedInfo<InfoTag::ARG>;
using AutoBaseInfo = OwnedInfo<InfoTag::BASE>;
using AutoCallableInfo = OwnedInfo<InfoTag::CALLABLE>;
using AutoCallbackInfo = OwnedInfo<InfoTag::CALLBACK>;
using AutoEnumInfo = OwnedInfo<InfoTag::ENUM>;
using AutoFieldInfo = OwnedInfo<InfoTag::FIELD>;
using AutoFunctionInfo = OwnedInfo<InfoTag::FUNCTION>;
using AutoInterfaceInfo = OwnedInfo<InfoTag::INTERFACE>;
using AutoObjectInfo = OwnedInfo<InfoTag::OBJECT>;
using AutoPropertyInfo = OwnedInfo<InfoTag::PROPERTY>;
using AutoRegisteredTypeInfo = OwnedInfo<InfoTag::REGISTERED_TYPE>;
using AutoSignalInfo = OwnedInfo<InfoTag::SIGNAL>;
using AutoStructInfo = OwnedInfo<InfoTag::STRUCT>;
using AutoTypeInfo = OwnedInfo<InfoTag::TYPE>;
using AutoUnionInfo = OwnedInfo<InfoTag::UNION>;
using AutoValueInfo = OwnedInfo<InfoTag::VALUE>;
using AutoVFuncInfo = OwnedInfo<InfoTag::VFUNC>;

// The various specializations of InfoOperations are used to ensure that the
// OwnedInfo and UnownedInfo specializations for a particular GIFooInfo type
// (and the stack-allocated class, if applicable) have the same methods. So, for
// example, AutoTypeInfo, TypeInfo, and StackTypeInfo all inherit from
// InfoOperations<T, InfoTag::TYPE>.

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::BASE> {
 protected:
    [[nodiscard]]
    GIBaseInfo* ptr() const {
        return GI_BASE_INFO(
            detail::Pointer::get_from(*static_cast<const Wrapper*>(this)));
    }

    // Helper for adapting GLib-style error reporting into GErrorResult
    [[nodiscard]]
    static Gjs::GErrorResult<> bool_gerror(bool ok, GError* error) {
        if (!ok)
            return mozilla::Err(error);
        return mozilla::Ok{};
    }

    // Helper for adapting C-style success/failure result into mozilla::Result.
    // Used when there is no GError out parameter.
    [[nodiscard]]
    static BoolResult bool_to_result(bool ok) {
        if (!ok)
            return Err(mozilla::Nothing{});
        return mozilla::Ok{};
    }

 public:
    template <InfoTag TAG>
    bool operator==(const OwnedInfo<TAG>& other) const {
        return gi_base_info_equal(
            ptr(), GI_BASE_INFO(detail::Pointer::get_from(other)));
    }
    template <InfoTag TAG>
    bool operator==(const UnownedInfo<TAG>& other) const {
        return gi_base_info_equal(
            ptr(), GI_BASE_INFO(detail::Pointer::get_from(other)));
    }
    template <InfoTag TAG>
    bool operator!=(const OwnedInfo<TAG>& other) const {
        return !(*this == other);
    }
    template <InfoTag TAG>
    bool operator!=(const UnownedInfo<TAG>& other) const {
        return !(*this == other);
    }

    template <InfoTag TAG = InfoTag::BASE>
    [[nodiscard]]
    mozilla::Maybe<const UnownedInfo<TAG>> container() const {
        return detail::Pointer::nullable_unowned<TAG>(
            detail::Pointer::cast<TAG>(gi_base_info_get_container(ptr())));
    }
    [[nodiscard]]
    bool is_deprecated() const {
        return gi_base_info_is_deprecated(ptr());
    }
    [[nodiscard]]
    const char* name() const {
        return gi_base_info_get_name(ptr());
    }
    [[nodiscard]]
    const char* ns() const {
        return gi_base_info_get_namespace(ptr());
    }
    [[nodiscard]]
    const char* type_string() const {
        return g_type_name_from_instance(
            reinterpret_cast<GTypeInstance*>(ptr()));
    }

    // Type-checking methods

    [[nodiscard]]
    bool is_callback() const {
        return GI_IS_CALLBACK_INFO(ptr());
    }
    [[nodiscard]]
    bool is_enum_or_flags() const {
        return GI_IS_ENUM_INFO(ptr());
    }
    [[nodiscard]] bool is_flags() const { return GI_IS_FLAGS_INFO(ptr()); }
    [[nodiscard]]
    bool is_function() const {
        return GI_IS_FUNCTION_INFO(ptr());
    }
    [[nodiscard]]
    bool is_interface() const {
        return GI_IS_INTERFACE_INFO(ptr());
    }
    [[nodiscard]] bool is_object() const { return GI_IS_OBJECT_INFO(ptr()); }
    [[nodiscard]]
    bool is_registered_type() const {
        return GI_IS_REGISTERED_TYPE_INFO(ptr());
    }
    [[nodiscard]] bool is_struct() const { return GI_IS_STRUCT_INFO(ptr()); }
    [[nodiscard]] bool is_union() const { return GI_IS_UNION_INFO(ptr()); }
    [[nodiscard]]
    bool is_unresolved() const {
        // We don't have a wrapper for GIUnresolvedInfo because it has no
        // methods, but you can check whether a BaseInfo is one.
        return GI_IS_UNRESOLVED_INFO(ptr());
    }
    [[nodiscard]] bool is_vfunc() const { return GI_IS_VFUNC_INFO(ptr()); }
    // Don't enumerate types which GJS doesn't define on namespaces.
    // See gjs_define_info().
    [[nodiscard]]
    bool is_enumerable() const {
        return GI_IS_REGISTERED_TYPE_INFO(ptr()) ||
               GI_IS_FUNCTION_INFO(ptr()) || GI_IS_CONSTANT_INFO(ptr());
    }

    // Having this casting function be a template is slightly inconsistent with
    // all the is_X() type-checking methods above. But if we were to make
    // separate as_X() methods, C++ can't easily deal with all the forward decls
    // of UnownedInfo<T> instantiating the template.
    template <InfoTag TAG2>
    [[nodiscard]]
    mozilla::Maybe<const UnownedInfo<TAG2>> as() const {
        if (!detail::Pointer::typecheck<TAG2>(ptr()))
            return {};
        auto* checked_ptr = detail::Pointer::cast<TAG2>(ptr());
        return mozilla::Some(detail::Pointer::to_unowned<TAG2>(checked_ptr));
    }

    void log_usage() const {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        mozilla::Maybe<GI::BaseInfo> parent = container();
        gjs_debug_gi_usage(
            "{ GIInfoType %s, \"%s\", \"%s\", \"%s\" }", type_string(), ns(),
            parent.map(std::mem_fn(&GI::BaseInfo::name)).valueOr(""), name());
#endif  // GJS_VERBOSE_ENABLE_GI_USAGE
    }
};

template <typename Wrapper>
using BaseInfoOperations = InfoOperations<Wrapper, InfoTag::BASE>;

// The following InfoIterator class is a C++ iterator implementation that's used
// to implement the C iteration pattern:
//
// unsigned n_bars = gi_foo_info_get_n_bars(info);
// for (unsigned ix = 0; ix < n_bars; ix++) {
//   GIBarInfo* bar = gi_foo_info_get_bar(info, ix);
//   do_stuff(bar);
//   gi_base_info_unref(bar);
// }
//
// as a more idiomatic C++ pattern:
//
// for (AutoBarInfo bar : info.bars())
//   do_stuff(bar);

template <typename T>
using NInfosFunc = unsigned (*)(T);

template <typename T, InfoTag TAG>
using GetInfoFunc = typename detail::InfoTraits<TAG>::CStruct* (*)(T, unsigned);

template <typename T, InfoTag TAG, NInfosFunc<T> get_n_infos,
          GetInfoFunc<T, TAG> get_info>
class InfoIterator {
    T m_obj;
    int m_ix;

    InfoIterator(T obj, int ix) : m_obj(obj), m_ix(ix) {}

 public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = int;
    using value_type = OwnedInfo<TAG>;
    using pointer = value_type*;
    using reference = value_type&;

    explicit InfoIterator(T info) : InfoIterator(info, 0) {}

    OwnedInfo<TAG> operator*() const {
        return detail::Pointer::to_owned<TAG>(get_info(m_obj, m_ix));
    }
    InfoIterator& operator++() {
        m_ix++;
        return *this;
    }
    InfoIterator operator++(int) {
        InfoIterator tmp = *this;
        m_ix++;
        return tmp;
    }
    bool operator==(const InfoIterator& other) const {
        return m_obj == other.m_obj && m_ix == other.m_ix;
    }
    bool operator!=(const InfoIterator& other) const {
        return m_obj != other.m_obj || m_ix != other.m_ix;
    }

    [[nodiscard]]
    mozilla::Maybe<OwnedInfo<TAG>> operator[](size_t ix) const {
        return detail::Pointer::nullable<TAG>(get_info(m_obj, ix));
    }

    [[nodiscard]] InfoIterator begin() const { return InfoIterator{m_obj, 0}; }
    [[nodiscard]]
    InfoIterator end() const {
        int n_fields = get_n_infos(m_obj);
        return InfoIterator{m_obj, n_fields};
    }
    [[nodiscard]] size_t size() const { return get_n_infos(m_obj); }
};

// These are used to delete the type-checking and casting methods from
// InfoOperations specializations for subtypes of GIBaseInfo, as appropriate.
// So, for example, if you have AutoCallableInfo, you still want to be able to
// check is_callback, is_function, and is_vfunc, but not is_boxed etc.

#define DELETE_CALLABLE_TYPECHECK_METHODS \
    bool is_callback() const = delete;    \
    bool is_function() const = delete;    \
    bool is_vfunc() const = delete;

#define DELETE_REGISTERED_TYPE_TYPECHECK_METHODS \
    bool is_boxed() const = delete;              \
    bool is_enum_or_flags() const = delete;      \
    bool is_flags() const = delete;              \
    bool is_interface() const = delete;          \
    bool is_object() const = delete;             \
    bool is_struct() const = delete;             \
    bool is_union() const = delete;

#define DELETE_SUPERCLASS_TYPECHECK_METHODS   \
    bool is_registered_type() const = delete; \
    bool is_unresolved() const = delete;

#define DELETE_CAST_METHOD  \
    template <InfoTag TAG2> \
    mozilla::Maybe<const UnownedInfo<TAG2>> as() const = delete;

#define DELETE_ALL_TYPECHECK_METHODS         \
    DELETE_SUPERCLASS_TYPECHECK_METHODS      \
    DELETE_CALLABLE_TYPECHECK_METHODS        \
    DELETE_REGISTERED_TYPE_TYPECHECK_METHODS \
    DELETE_CAST_METHOD

// Needs to come first, because InfoOperations<ARG> and InfoOperations<CALLABLE>
// instantiate the template by having methods with GI::StackTypeInfo* parameters
template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::TYPE>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GITypeInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }
    // Private, because we don't use this directly. Use the more semantic
    // versions below (element_type() for GSLIST, GLIST, and ARRAY type tags;
    // key_type() and value_type() for GHASH.)
    [[nodiscard]]
    AutoTypeInfo param_type(int n) const {
        return detail::Pointer::to_owned<InfoTag::TYPE>(
            gi_type_info_get_param_type(ptr(), n));
    }

 public:
    [[nodiscard]]
    mozilla::Maybe<unsigned> array_length_index() const {
        unsigned out;
        if (!gi_type_info_get_array_length_index(ptr(), &out))
            return {};
        return mozilla::Some(out);
    }
    [[nodiscard]]
    mozilla::Maybe<size_t> array_fixed_size() const {
        size_t out;
        if (!gi_type_info_get_array_fixed_size(ptr(), &out))
            return {};
        return mozilla::Some(out);
    }
    [[nodiscard]]
    GIArrayType array_type() const {
        return gi_type_info_get_array_type(ptr());
    }
    void argument_from_hash_pointer(void* hash_pointer, GIArgument* arg) const {
        gi_type_info_argument_from_hash_pointer(ptr(), hash_pointer, arg);
    }
    [[nodiscard]]
    void* hash_pointer_from_argument(GIArgument* arg) const {
        return gi_type_info_hash_pointer_from_argument(ptr(), arg);
    }
    // Unlike the libgirepository API, this doesn't return null. Only call it on
    // TypeInfo with GI_TYPE_TAG_INTERFACE tag.
    [[nodiscard]]
    AutoBaseInfo interface() const {
        g_assert(tag() == GI_TYPE_TAG_INTERFACE);
        return detail::Pointer::to_owned<InfoTag::BASE>(
            gi_type_info_get_interface(ptr()));
    }
    [[nodiscard]]
    bool is_pointer() const {
        return gi_type_info_is_pointer(ptr());
    }
    [[nodiscard]]
    bool is_zero_terminated() const {
        return gi_type_info_is_zero_terminated(ptr());
    }
    [[nodiscard]]
    GITypeTag storage_type() const {
        return gi_type_info_get_storage_type(ptr());
    }
    [[nodiscard]] GITypeTag tag() const { return gi_type_info_get_tag(ptr()); }
    void extract_ffi_return_value(GIFFIReturnValue* ffi_value,
                                  GIArgument* arg) const {
        gi_type_info_extract_ffi_return_value(ptr(), ffi_value, arg);
    }

    // Methods not present in GIRepository

    [[nodiscard]] bool can_be_allocated_directly() const;
    [[nodiscard]] bool direct_allocation_has_pointers() const;
    [[nodiscard]]
    const char* display_string() const {
        GITypeTag type_tag = tag();
        if (type_tag == GI_TYPE_TAG_INTERFACE)
            return interface().type_string();
        return gi_type_tag_to_string(type_tag);
    }

    [[nodiscard]]
    bool is_string_type() const {
        GITypeTag t = tag();
        return t == GI_TYPE_TAG_FILENAME || t == GI_TYPE_TAG_UTF8;
    }

    [[nodiscard]]
    bool is_basic() const {
        GITypeTag t = tag();
        if (t == GI_TYPE_TAG_VOID && is_pointer())
            return false;  // void* is not a basic type
        return GI_TYPE_TAG_IS_BASIC(t);
    }

    // More semantic versions of param_type(), that are only intended to be
    // called on TypeInfos where the result is known not to be null

    [[nodiscard]]
    AutoTypeInfo element_type() const {
        g_assert(tag() == GI_TYPE_TAG_ARRAY || tag() == GI_TYPE_TAG_GLIST ||
                 tag() == GI_TYPE_TAG_GSLIST);
        return param_type(0);
    }

    [[nodiscard]]
    AutoTypeInfo key_type() const {
        g_assert(tag() == GI_TYPE_TAG_GHASH);
        return param_type(0);
    }

    [[nodiscard]]
    AutoTypeInfo value_type() const {
        g_assert(tag() == GI_TYPE_TAG_GHASH);
        return param_type(1);
    }
};

// Needs to come after InfoOperations<TYPE> but before InfoOperations<CALLABLE>
// since this class instantiates the GI::StackTypeInfo template, but
// InfoOperations<CALLABLE> instantiates this one.
template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::ARG>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIArgInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    [[nodiscard]]
    bool caller_allocates() const {
        return gi_arg_info_is_caller_allocates(ptr());
    }
    [[nodiscard]]
    mozilla::Maybe<unsigned> closure_index() const {
        unsigned out;
        if (!gi_arg_info_get_closure_index(ptr(), &out))
            return {};
        return mozilla::Some(out);
    }
    [[nodiscard]]
    mozilla::Maybe<unsigned> destroy_index() const {
        unsigned out;
        if (!gi_arg_info_get_destroy_index(ptr(), &out))
            return {};
        return mozilla::Some(out);
    }
    [[nodiscard]]
    GIDirection direction() const {
        return gi_arg_info_get_direction(ptr());
    }
    void load_type(StackTypeInfo* type) const {
        gi_arg_info_load_type_info(ptr(), detail::Pointer::get_from(*type));
    }
    [[nodiscard]]
    bool is_optional() const {
        return gi_arg_info_is_optional(ptr());
    }
    [[nodiscard]]
    bool is_return_value() const {
        return gi_arg_info_is_return_value(ptr());
    }
    [[nodiscard]]
    bool may_be_null() const {
        return gi_arg_info_may_be_null(ptr());
    }
    [[nodiscard]]
    GITransfer ownership_transfer() const {
        return gi_arg_info_get_ownership_transfer(ptr());
    }
    [[nodiscard]]
    GIScopeType scope() const {
        return gi_arg_info_get_scope(ptr());
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::CALLABLE>
    : public BaseInfoOperations<Wrapper> {
    DELETE_SUPERCLASS_TYPECHECK_METHODS;
    DELETE_REGISTERED_TYPE_TYPECHECK_METHODS;

    [[nodiscard]]
    GICallableInfo* ptr() const {
        return GI_CALLABLE_INFO(
            detail::Pointer::get_from(*static_cast<const Wrapper*>(this)));
    }

 public:
    using ArgsIterator =
        InfoIterator<GICallableInfo*, InfoTag::ARG, gi_callable_info_get_n_args,
                     gi_callable_info_get_arg>;
    [[nodiscard]]
    ArgsIterator args() const {
        return ArgsIterator{ptr()};
    }
    [[nodiscard]]
    AutoArgInfo arg(unsigned n) const {
        g_assert(n < n_args());
        return detail::Pointer::to_owned<InfoTag::ARG>(
            gi_callable_info_get_arg(ptr(), n));
    }
    [[nodiscard]]
    unsigned n_args() const {
        return gi_callable_info_get_n_args(ptr());
    }

    [[nodiscard]]
    GITransfer caller_owns() const {
        return gi_callable_info_get_caller_owns(ptr());
    }
    [[nodiscard]]
    bool can_throw_gerror() const {
        return gi_callable_info_can_throw_gerror(ptr());
    }
    [[nodiscard]]
    void** closure_native_address(ffi_closure* closure) const {
        return gi_callable_info_get_closure_native_address(ptr(), closure);
    }
    [[nodiscard]]
    ffi_closure* create_closure(ffi_cif* cif, GIFFIClosureCallback callback,
                                void* user_data) const {
        return gi_callable_info_create_closure(ptr(), cif, callback, user_data);
    }
    void destroy_closure(ffi_closure* closure) const {
        gi_callable_info_destroy_closure(ptr(), closure);
    }
    [[nodiscard]]
    Gjs::GErrorResult<> init_function_invoker(
        void* address, GIFunctionInvoker* invoker) const {
        GError* error = nullptr;
        return this->bool_gerror(gi_function_invoker_new_for_address(
                                     address, ptr(), invoker, &error),
                                 error);
    }
    [[nodiscard]]
    GITransfer instance_ownership_transfer() const {
        return gi_callable_info_get_instance_ownership_transfer(ptr());
    }
    [[nodiscard]]
    bool is_method() const {
        return gi_callable_info_is_method(ptr());
    }
    void load_arg(unsigned n, StackArgInfo* arg) const {
        g_assert(n < n_args());
        gi_callable_info_load_arg(ptr(), n, detail::Pointer::get_from(*arg));
    }
    void load_return_type(StackTypeInfo* type) const {
        gi_callable_info_load_return_type(ptr(),
                                          detail::Pointer::get_from(*type));
    }
    [[nodiscard]]
    bool may_return_null() const {
        return gi_callable_info_may_return_null(ptr());
    }
    [[nodiscard]]
    bool skip_return() const {
        return gi_callable_info_skip_return(ptr());
    }

    // Methods not in GIRepository

    void log_usage() {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        std::ostringstream out;

#    define DIRECTION_STRING(d)              \
        (((d) == GI_DIRECTION_IN)    ? "IN"  \
         : ((d) == GI_DIRECTION_OUT) ? "OUT" \
                                     : "INOUT")
#    define TRANSFER_STRING(t)                          \
        (((t) == GI_TRANSFER_NOTHING)     ? "NOTHING"   \
         : ((t) == GI_TRANSFER_CONTAINER) ? "CONTAINER" \
                                          : "EVERYTHING")

        out << ".details = { .func = { .retval_transfer = GI_TRANSFER_"
            << TRANSFER_STRING(caller_owns()) << ", .n_args = " << n_args()
            << ", .args = { ";

        ArgsIterator iter = args();
        std::for_each(iter.begin(), iter.end(), [&out](AutoArgInfo arg_info) {
            out << "{ GI_DIRECTION_" << DIRECTION_STRING(arg_info.direction())
                << ", GI_TRANSFER_"
                << TRANSFER_STRING(arg_info.ownership_transfer()) << " }, ";
        });
        out.seekp(-2, std::ios_base::end);  // Erase trailing comma

#    undef DIRECTION_STRING
#    undef TRANSFER_STRING

        out << " } } }";
        std::string details{out.str()};

        using Base = BaseInfoOperations<Wrapper>;
        mozilla::Maybe<GI::BaseInfo> parent = Base::container();
        gjs_debug_gi_usage(
            "{ GIInfoType %s, \"%s\", \"%s\", \"%s\", %s }",
            Base::type_string(), Base::ns(),
            parent.map(std::mem_fn(&GI::BaseInfo::name)).valueOr(""),
            Base::name(), details.c_str());
#endif  // GJS_VERBOSE_ENABLE_GI_USAGE
    }
};

template <class Wrapper>
using CallableInfoOperations = InfoOperations<Wrapper, InfoTag::CALLABLE>;

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::REGISTERED_TYPE>
    : public BaseInfoOperations<Wrapper> {
    DELETE_SUPERCLASS_TYPECHECK_METHODS;
    DELETE_CALLABLE_TYPECHECK_METHODS;

    [[nodiscard]]
    GIRegisteredTypeInfo* ptr() const {
        return GI_REGISTERED_TYPE_INFO(
            detail::Pointer::get_from(*static_cast<const Wrapper*>(this)));
    }

 public:
    [[nodiscard]]
    GType gtype() const {
        return gi_registered_type_info_get_g_type(ptr());
    }

    // Methods not in GIRepository

    [[nodiscard]]
    bool is_gdk_atom() const {
        return strcmp("Atom", this->name()) == 0 &&
               strcmp("Gdk", this->ns()) == 0;
    }
    [[nodiscard]]
    bool is_g_value() const {
        return g_type_is_a(gtype(), G_TYPE_VALUE);
    }

    operator const BaseInfo() const {
        return detail::Pointer::to_unowned<InfoTag::BASE>(GI_BASE_INFO(ptr()));
    }
};

template <class Wrapper>
using RegisteredTypeInfoOperations =
    InfoOperations<Wrapper, InfoTag::REGISTERED_TYPE>;

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::CALLBACK>
    : public CallableInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GICallbackInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    operator const BaseInfo() const {
        return detail::Pointer::to_unowned<InfoTag::BASE>(GI_BASE_INFO(ptr()));
    }

    operator const CallableInfo() const {
        return detail::Pointer::to_unowned<InfoTag::CALLABLE>(
            GI_CALLABLE_INFO(ptr()));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::CONSTANT>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIConstantInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    void free_value(GIArgument* arg) const {
        gi_constant_info_free_value(ptr(), arg);
    }
    int load_value(GIArgument* arg) const {
        return gi_constant_info_get_value(ptr(), arg);
    }
    [[nodiscard]]
    AutoTypeInfo type_info() const {
        return detail::Pointer::to_owned<InfoTag::TYPE>(
            gi_constant_info_get_type_info(ptr()));
    }
};

// Must come before any use of MethodsIterator
template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::FUNCTION>
    : public CallableInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIFunctionInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

    [[nodiscard]]
    GIFunctionInfoFlags flags() const {
        return gi_function_info_get_flags(ptr());
    }

 public:
    [[nodiscard]]
    Gjs::GErrorResult<> invoke(const mozilla::Span<const GIArgument>& in_args,
                               const mozilla::Span<GIArgument>& out_args,
                               GIArgument* return_value) const {
        g_assert(in_args.size() <= G_MAXINT);
        g_assert(out_args.size() <= G_MAXINT);
        GError* error = nullptr;
        return this->bool_gerror(
            gi_function_info_invoke(ptr(), in_args.data(), in_args.size(),
                                    out_args.data(), out_args.size(),
                                    return_value, &error),
            error);
    }
    [[nodiscard]]
    Gjs::GErrorResult<> prep_invoker(GIFunctionInvoker* invoker) const {
        GError* error = nullptr;
        return this->bool_gerror(
            gi_function_info_prep_invoker(ptr(), invoker, &error), error);
    }
    [[nodiscard]]
    const char* symbol() const {
        return gi_function_info_get_symbol(ptr());
    }

    // Has to be defined later because there's a chicken-and-egg loop between
    // AutoPropertyInfo and AutoFunctionInfo
    [[nodiscard]]
    mozilla::Maybe<GI::AutoPropertyInfo> property() const;

    // Methods not in GIRepository

    [[nodiscard]]
    bool is_method() const {
        return flags() & GI_FUNCTION_IS_METHOD;
    }
    [[nodiscard]]
    bool is_constructor() const {
        return flags() & GI_FUNCTION_IS_CONSTRUCTOR;
    }

    operator const CallableInfo() const {
        return detail::Pointer::to_unowned<InfoTag::CALLABLE>(
            GI_CALLABLE_INFO(ptr()));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::ENUM>
    : public RegisteredTypeInfoOperations<Wrapper> {
    DELETE_REGISTERED_TYPE_TYPECHECK_METHODS;

    [[nodiscard]]
    GIEnumInfo* ptr() const {
        return GI_ENUM_INFO(
            detail::Pointer::get_from(*static_cast<const Wrapper*>(this)));
    }

 public:
    using ValuesIterator =
        InfoIterator<GIEnumInfo*, InfoTag::VALUE, gi_enum_info_get_n_values,
                     gi_enum_info_get_value>;
    [[nodiscard]]
    ValuesIterator values() const {
        return ValuesIterator{ptr()};
    }

    using MethodsIterator =
        InfoIterator<GIEnumInfo*, InfoTag::FUNCTION, gi_enum_info_get_n_methods,
                     gi_enum_info_get_method>;
    [[nodiscard]]
    MethodsIterator methods() const {
        return MethodsIterator{ptr()};
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> method(const char* name) const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_enum_info_find_method(ptr(), name));
    }

    [[nodiscard]]
    const char* error_domain() const {
        return gi_enum_info_get_error_domain(ptr());
    }
    [[nodiscard]]
    GITypeTag storage_type() const {
        return gi_enum_info_get_storage_type(ptr());
    }

    // Methods not in GIRepository

    [[nodiscard]]
    bool uses_signed_type() const {
        switch (storage_type()) {
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_INT64:
                return true;
            default:
                return false;
        }
    }

    // This is hacky - gi_function_info_invoke() and
    // gi_field_info_get/set_field() expect the enum value in
    // gjs_arg_member<int>(arg) and depend on all flags and enumerations being
    // passed on the stack in a 32-bit field. See FIXME comment in
    // gi_field_info_get_field(). The same assumption of enums cast to 32-bit
    // signed integers is found in g_value_set_enum() / g_value_set_flags().
    [[nodiscard]]
    int64_t enum_from_int(int int_value) const {
        if (uses_signed_type())
            return int64_t{int_value};
        else
            return int64_t{static_cast<uint32_t>(int_value)};
    }

    // Here for symmetry, but result is the same for the two cases
    [[nodiscard]]
    int enum_to_int(int64_t value) const {
        return static_cast<int>(value);
    }
};

template <class Wrapper>
using EnumInfoOperations = InfoOperations<Wrapper, InfoTag::ENUM>;

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::FLAGS>
    : public EnumInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::FIELD>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIFieldInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

    // Use the various is_FLAG() methods instead.
    [[nodiscard]]
    GIFieldInfoFlags flags() const {
        return gi_field_info_get_flags(ptr());
    }

 public:
    [[nodiscard]] size_t offset() const {
        return gi_field_info_get_offset(ptr());
    }
    [[nodiscard]]
    BoolResult read(void* blob, GIArgument* value_out) const {
        return this->bool_to_result(
            gi_field_info_get_field(ptr(), blob, value_out));
    }
    [[nodiscard]]
    AutoTypeInfo type_info() const {
        return detail::Pointer::to_owned<InfoTag::TYPE>(
            gi_field_info_get_type_info(ptr()));
    }
    [[nodiscard]]
    BoolResult write(void* blob, const GIArgument* value) const {
        return this->bool_to_result(
            gi_field_info_set_field(ptr(), blob, value));
    }

    // Methods not in GIRepository

    [[nodiscard]]
    bool is_readable() const {
        return flags() & GI_FIELD_IS_READABLE;
    }
    [[nodiscard]]
    bool is_writable() const {
        return flags() & GI_FIELD_IS_WRITABLE;
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::SIGNAL>
    : public CallableInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GISignalInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::STRUCT>
    : public RegisteredTypeInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIStructInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    using FieldsIterator =
        InfoIterator<GIStructInfo*, InfoTag::FIELD, gi_struct_info_get_n_fields,
                     gi_struct_info_get_field>;
    [[nodiscard]]
    FieldsIterator fields() const {
        return FieldsIterator{ptr()};
    }

    using MethodsIterator =
        InfoIterator<GIStructInfo*, InfoTag::FUNCTION,
                     gi_struct_info_get_n_methods, gi_struct_info_get_method>;
    [[nodiscard]]
    MethodsIterator methods() const {
        return MethodsIterator{ptr()};
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> method(const char* name) const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_struct_info_find_method(ptr(), name));
    }

    [[nodiscard]]
    bool is_foreign() const {
        return gi_struct_info_is_foreign(ptr());
    }
    [[nodiscard]]
    bool is_gtype_struct() const {
        return gi_struct_info_is_gtype_struct(ptr());
    }
    [[nodiscard]] size_t size() const { return gi_struct_info_get_size(ptr()); }

    operator const BaseInfo() const {
        return detail::Pointer::to_unowned<InfoTag::BASE>(GI_BASE_INFO(ptr()));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::UNION>
    : public RegisteredTypeInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIUnionInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    using FieldsIterator =
        InfoIterator<GIUnionInfo*, InfoTag::FIELD, gi_union_info_get_n_fields,
                     gi_union_info_get_field>;
    [[nodiscard]]
    FieldsIterator fields() const {
        return FieldsIterator{ptr()};
    }

    using MethodsIterator =
        InfoIterator<GIUnionInfo*, InfoTag::FUNCTION,
                     gi_union_info_get_n_methods, gi_union_info_get_method>;
    [[nodiscard]]
    MethodsIterator methods() const {
        return MethodsIterator{ptr()};
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> method(const char* name) const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_union_info_find_method(ptr(), name));
    }

    [[nodiscard]] size_t size() const { return gi_union_info_get_size(ptr()); }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::VFUNC>
    : public CallableInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIVFuncInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    [[nodiscard]]
    Gjs::GErrorResult<void*> address(GType implementor_gtype) const {
        Gjs::AutoError error;  // Cannot use GError*, distinguish from void*
        void* address =
            gi_vfunc_info_get_address(ptr(), implementor_gtype, error.out());
        if (!address)
            return mozilla::Err(std::move(error));
        return address;
    }

    [[nodiscard]] operator const CallableInfo() const {
        return detail::Pointer::to_unowned<InfoTag::CALLABLE>(
            GI_CALLABLE_INFO(ptr()));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::INTERFACE>
    : public RegisteredTypeInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIInterfaceInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    using MethodsIterator = InfoIterator<GIInterfaceInfo*, InfoTag::FUNCTION,
                                         gi_interface_info_get_n_methods,
                                         gi_interface_info_get_method>;
    [[nodiscard]]
    MethodsIterator methods() const {
        return MethodsIterator{ptr()};
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> method(const char* name) const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_interface_info_find_method(ptr(), name));
    }

    using PropertiesIterator = InfoIterator<GIInterfaceInfo*, InfoTag::PROPERTY,
                                            gi_interface_info_get_n_properties,
                                            gi_interface_info_get_property>;
    [[nodiscard]]
    PropertiesIterator properties() const {
        return PropertiesIterator{ptr()};
    }

    [[nodiscard]]
    mozilla::Maybe<AutoStructInfo> iface_struct() const {
        return detail::Pointer::nullable<InfoTag::STRUCT>(
            gi_interface_info_get_iface_struct(ptr()));
    }
    [[nodiscard]]
    mozilla::Maybe<AutoSignalInfo> signal(const char* name) const {
        return detail::Pointer::nullable<InfoTag::SIGNAL>(
            gi_interface_info_find_signal(ptr(), name));
    }
    [[nodiscard]]
    mozilla::Maybe<AutoVFuncInfo> vfunc(const char* name) const {
        return detail::Pointer::nullable<InfoTag::VFUNC>(
            gi_interface_info_find_vfunc(ptr(), name));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::OBJECT>
    : public RegisteredTypeInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIObjectInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    using FieldsIterator =
        InfoIterator<GIObjectInfo*, InfoTag::FIELD, gi_object_info_get_n_fields,
                     gi_object_info_get_field>;
    [[nodiscard]]
    FieldsIterator fields() const {
        return FieldsIterator{ptr()};
    }

    using InterfacesIterator = InfoIterator<GIObjectInfo*, InfoTag::INTERFACE,
                                            gi_object_info_get_n_interfaces,
                                            gi_object_info_get_interface>;
    [[nodiscard]]
    InterfacesIterator interfaces() const {
        return InterfacesIterator{ptr()};
    }

    using MethodsIterator =
        InfoIterator<GIObjectInfo*, InfoTag::FUNCTION,
                     gi_object_info_get_n_methods, gi_object_info_get_method>;
    [[nodiscard]]
    MethodsIterator methods() const {
        return MethodsIterator{ptr()};
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> method(const char* name) const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_object_info_find_method(ptr(), name));
    }

    using PropertiesIterator = InfoIterator<GIObjectInfo*, InfoTag::PROPERTY,
                                            gi_object_info_get_n_properties,
                                            gi_object_info_get_property>;
    [[nodiscard]]
    PropertiesIterator properties() const {
        return PropertiesIterator{ptr()};
    }

    [[nodiscard]]
    mozilla::Maybe<AutoStructInfo> class_struct() const {
        return detail::Pointer::nullable<InfoTag::STRUCT>(
            gi_object_info_get_class_struct(ptr()));
    }
    [[nodiscard]]
    mozilla::Maybe<std::pair<AutoFunctionInfo, AutoRegisteredTypeInfo>>
    find_method_using_interfaces(const char* name) const {
        GIBaseInfo* declarer_ptr = nullptr;
        GIFunctionInfo* method_ptr =
            gi_object_info_find_method_using_interfaces(ptr(), name,
                                                        &declarer_ptr);

        if (!method_ptr) {
            g_assert(!declarer_ptr);
            return {};
        }

        AutoFunctionInfo method{
            detail::Pointer::to_owned<InfoTag::FUNCTION>(method_ptr)};
        AutoRegisteredTypeInfo declarer{
            detail::Pointer::to_owned<InfoTag::REGISTERED_TYPE>(
                GI_REGISTERED_TYPE_INFO(declarer_ptr))};
        g_assert(declarer.is_object() || declarer.is_interface());
        return mozilla::Some(std::make_pair(method, declarer));
    }
    [[nodiscard]]
    mozilla::Maybe<std::pair<AutoVFuncInfo, AutoRegisteredTypeInfo>>
    find_vfunc_using_interfaces(const char* name) const {
        GIBaseInfo* declarer_ptr = nullptr;
        GIVFuncInfo* vfunc_ptr = gi_object_info_find_vfunc_using_interfaces(
            ptr(), name, &declarer_ptr);

        if (!vfunc_ptr) {
            g_assert(!declarer_ptr);
            return {};
        }

        AutoVFuncInfo vfunc{
            detail::Pointer::to_owned<InfoTag::VFUNC>(vfunc_ptr)};
        AutoRegisteredTypeInfo declarer{
            detail::Pointer::to_owned<InfoTag::REGISTERED_TYPE>(
                GI_REGISTERED_TYPE_INFO(declarer_ptr))};
        g_assert(declarer.is_object() || declarer.is_interface());
        return mozilla::Some(std::make_pair(vfunc, declarer));
    }
    [[nodiscard]]
    GIObjectInfoGetValueFunction get_value_function_pointer() const {
        return gi_object_info_get_get_value_function_pointer(ptr());
    }
    [[nodiscard]]
    mozilla::Maybe<AutoObjectInfo> parent() const {
        return detail::Pointer::nullable<InfoTag::OBJECT>(
            gi_object_info_get_parent(ptr()));
    }
    [[nodiscard]]
    GIObjectInfoRefFunction ref_function_pointer() const {
        return gi_object_info_get_ref_function_pointer(ptr());
    }
    [[nodiscard]]
    GIObjectInfoSetValueFunction set_value_function_pointer() const {
        return gi_object_info_get_set_value_function_pointer(ptr());
    }
    [[nodiscard]]
    mozilla::Maybe<AutoSignalInfo> signal(const char* name) const {
        return detail::Pointer::nullable<InfoTag::SIGNAL>(
            gi_object_info_find_signal(ptr(), name));
    }
    [[nodiscard]]
    GIObjectInfoUnrefFunction unref_function_pointer() const {
        return gi_object_info_get_unref_function_pointer(ptr());
    }
    [[nodiscard]]
    mozilla::Maybe<AutoVFuncInfo> vfunc(const char* name) const {
        return detail::Pointer::nullable<InfoTag::VFUNC>(
            gi_object_info_find_vfunc(ptr(), name));
    }

    [[nodiscard]] operator const BaseInfo() const {
        return detail::Pointer::to_unowned<InfoTag::BASE>(GI_BASE_INFO(ptr()));
    }
};

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::PROPERTY>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIPropertyInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

    [[nodiscard]]
    GParamFlags flags() const {
        return gi_property_info_get_flags(ptr());
    }

 public:
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> getter() const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_property_info_get_getter(ptr()));
    }
    [[nodiscard]]
    mozilla::Maybe<AutoFunctionInfo> setter() const {
        return detail::Pointer::nullable<InfoTag::FUNCTION>(
            gi_property_info_get_setter(ptr()));
    }
    [[nodiscard]]
    AutoTypeInfo type_info() const {
        return detail::Pointer::to_owned<InfoTag::TYPE>(
            gi_property_info_get_type_info(ptr()));
    }

    // Methods not in GIRepository

    [[nodiscard]]
    bool has_deprecated_param_flag() const {
        // Note, different from is_deprecated(). It's possible that the property
        // has the deprecated GParamSpec flag, but is not marked deprecated in
        // the GIR doc comment.
        return flags() & G_PARAM_DEPRECATED;
    }
};

// Out-of-line definition to avoid chicken-and-egg loop between AutoFunctionInfo
// and AutoPropertyInfo
template <class Wrapper>
inline mozilla::Maybe<AutoPropertyInfo>
InfoOperations<Wrapper, InfoTag::FUNCTION>::property() const {
    return detail::Pointer::nullable<InfoTag::PROPERTY>(
        gi_function_info_get_property(ptr()));
}

template <class Wrapper>
class InfoOperations<Wrapper, InfoTag::VALUE>
    : public BaseInfoOperations<Wrapper> {
    DELETE_ALL_TYPECHECK_METHODS;

    [[nodiscard]]
    GIValueInfo* ptr() const {
        return detail::Pointer::get_from(*static_cast<const Wrapper*>(this));
    }

 public:
    [[nodiscard]]
    int64_t value() const {
        return gi_value_info_get_value(ptr());
    }
};

// In order to avoid having to create an OwnedInfo or UnownedInfo from a pointer
// anywhere except in these wrappers, we also wrap GIRepository.
// (ArgCache::HasTypeInfo is the one exception.)
class Repository {
    Gjs::AutoUnref<GIRepository> m_ptr = gi_repository_dup_default();

    // Helper object for iterating the introspection info objects of a
    // namespace. Unlike the other introspection info iterators, this requires
    // two parameters, the GIRepository* and the namespace string, so we need
    // this helper object to adapt InfoIterator.
    struct IterableNamespace {
        GIRepository* repo;
        const char* ns;

        static unsigned get_n_infos(const IterableNamespace obj) {
            return gi_repository_get_n_infos(obj.repo, obj.ns);
        }

        static GIBaseInfo* get_info(const IterableNamespace obj, unsigned ix) {
            return gi_repository_get_info(obj.repo, obj.ns, ix);
        }

        bool operator==(const IterableNamespace& other) const {
            return repo == other.repo && strcmp(ns, other.ns) == 0;
        }

        bool operator!=(const IterableNamespace& other) const {
            return !(*this == other);
        }
    };

 public:
    using Iterator = InfoIterator<IterableNamespace, InfoTag::BASE,
                                  &IterableNamespace::get_n_infos,
                                  &IterableNamespace::get_info>;
    [[nodiscard]]
    Iterator infos(const char* ns) const {
        return Iterator{{m_ptr, ns}};
    }

    [[nodiscard]]
    Gjs::AutoStrv enumerate_versions(const char* ns, size_t* n_versions) const {
        return gi_repository_enumerate_versions(m_ptr, ns, n_versions);
    }
    [[nodiscard]]
    mozilla::Maybe<AutoEnumInfo> find_by_error_domain(GQuark domain) const {
        return detail::Pointer::nullable<InfoTag::ENUM>(
            gi_repository_find_by_error_domain(m_ptr, domain));
    }
    template <InfoTag TAG = InfoTag::REGISTERED_TYPE>
    [[nodiscard]]
    mozilla::Maybe<OwnedInfo<TAG>> find_by_gtype(GType gtype) const {
        return detail::Pointer::nullable<TAG>(detail::Pointer::cast<TAG>(
            gi_repository_find_by_gtype(m_ptr, gtype)));
    }
    template <InfoTag TAG = InfoTag::BASE>
    [[nodiscard]]
    mozilla::Maybe<OwnedInfo<TAG>> find_by_name(const char* ns,
                                                const char* name) const {
        return detail::Pointer::nullable<TAG>(detail::Pointer::cast<TAG>(
            gi_repository_find_by_name(m_ptr, ns, name)));
    }
    [[nodiscard]]
    const char* get_version(const char* ns) const {
        return gi_repository_get_version(m_ptr, ns);
    }
    [[nodiscard]]
    bool is_registered(const char* ns, const char* version) const {
        return gi_repository_is_registered(m_ptr, ns, version);
    }
    [[nodiscard]]
    mozilla::Span<const InterfaceInfo> object_get_gtype_interfaces(
        GType gtype) const {
        InterfaceInfo* interfaces;
        size_t n_interfaces;
        gi_repository_get_object_gtype_interfaces(
            m_ptr, gtype, &n_interfaces,
            reinterpret_cast<GIInterfaceInfo***>(&interfaces));
        return {interfaces, n_interfaces};
    }
    void prepend_search_path(const char* path) {
        gi_repository_prepend_search_path(m_ptr, path);
    }
    [[nodiscard]]
    Gjs::GErrorResult<GITypelib*> require(
        const char* ns, const char* version,
        GIRepositoryLoadFlags flags = {}) const {
        GError* error = nullptr;
        GITypelib* typelib =
            gi_repository_require(m_ptr, ns, version, flags, &error);
        if (!typelib)
            return mozilla::Err(error);
        return typelib;
    }
};

///// STACK-ALLOCATED INTROSPECTION INFO ///////////////////////////////////////

// Introspection info allocated directly on the stack. This is used only in a
// few cases, for performance reasons. In C, the stack-allocated struct is
// filled in by a function such as gi_arg_info_load_type_info().
// Needs to appear at the end, due to FIXME.

class StackArgInfo : public InfoOperations<StackArgInfo, InfoTag::ARG> {
    friend struct detail::Pointer;

    GIArgInfo m_info = {};

    [[nodiscard]]
    constexpr GIArgInfo* ptr() const {
        return detail::Pointer::get_from(*this);
    }

 public:
    constexpr StackArgInfo() {}
    ~StackArgInfo() { gi_base_info_clear(&m_info); }
    // Moving is okay, we copy the contents of the GIArgInfo struct and reset
    // the existing one
    StackArgInfo(StackArgInfo&& other) : m_info(other.m_info) {
        gi_base_info_clear(&other.m_info);
    }
    StackArgInfo& operator=(StackArgInfo&& other) {
        m_info = other.m_info;
        gi_base_info_clear(&other.m_info);
        return *this;
    }
    // Prefer moving to copying
    StackArgInfo(const StackArgInfo&) = delete;
    StackArgInfo& operator=(const StackArgInfo&) = delete;
};

class StackTypeInfo : public InfoOperations<StackTypeInfo, InfoTag::TYPE> {
    friend struct detail::Pointer;

    GITypeInfo m_info = {};

    [[nodiscard]]
    constexpr GITypeInfo* ptr() const {
        return detail::Pointer::get_from(*this);
    }

 public:
    constexpr StackTypeInfo() {}
    ~StackTypeInfo() { gi_base_info_clear(&m_info); }
    // Moving is okay, we copy the contents of the GITypeInfo struct and reset
    // the existing one
    StackTypeInfo(StackTypeInfo&& other) : m_info(other.m_info) {
        gi_base_info_clear(&other.m_info);
    }
    StackTypeInfo& operator=(StackTypeInfo&& other) {
        m_info = other.m_info;
        gi_base_info_clear(&other.m_info);
        return *this;
    }
    // Prefer moving to copying
    StackTypeInfo(const StackTypeInfo&) = delete;
    StackTypeInfo& operator=(const StackTypeInfo&) = delete;
};

namespace detail {
constexpr inline GIArgInfo* Pointer::get_from(const StackArgInfo& stack) {
    return const_cast<GIArgInfo*>(&stack.m_info);
}
constexpr inline GITypeInfo* Pointer::get_from(const StackTypeInfo& stack) {
    return const_cast<GITypeInfo*>(&stack.m_info);
}
inline void Pointer::to_stack(GITypeInfo* ptr, StackTypeInfo* stack) {
    stack->m_info = std::move(*ptr);
    // Hacky: Reproduce gi_info_init() and mark the copied GITypeInfo as
    // stack-allocated. Unfortunately, GI_TYPE_TYPE_INFO makes this function
    // unable to be constexpr.
    GIBaseInfoStack* stack_ptr = &stack->m_info.parent;
    stack_ptr->parent_instance.g_class =
        static_cast<GTypeClass*>(g_type_class_ref(GI_TYPE_TYPE_INFO));
    stack_ptr->dummy0 = 0x7fff'ffff;
}
}  // namespace detail

static_assert(sizeof(StackArgInfo) == sizeof(GIArgInfo),
              "StackArgInfo should be byte-compatible with GIArgInfo");
static_assert(sizeof(StackTypeInfo) == sizeof(GITypeInfo),
              "StackTypeInfo should be byte-compatible with GITypeInfo");

}  // namespace GI

/* For use of GI::OwnedInfo<TAG> in GC hash maps */
namespace JS {
template <GI::InfoTag TAG>
struct GCPolicy<GI::OwnedInfo<TAG>>
    : public IgnoreGCPolicy<GI::OwnedInfo<TAG>> {};
}  // namespace JS
