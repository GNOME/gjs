/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#ifndef GI_ARG_CACHE_H_
#define GI_ARG_CACHE_H_

#include <config.h>

#include <stdint.h>

#include <limits>

#include <girepository/girepository.h>
#include <glib-object.h>

#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gi/arg.h"
#include "gi/info.h"
#include "gjs/auto.h"
#include "gjs/enum-utils.h"
#include "gjs/macros.h"

class GjsFunctionCallState;

enum NotIntrospectableReason : uint8_t {
    CALLBACK_OUT,
    DESTROY_NOTIFY_NO_CALLBACK,
    DESTROY_NOTIFY_NO_USER_DATA,
    INTERFACE_TRANSFER_CONTAINER,
    OUT_CALLER_ALLOCATES_NON_STRUCT,
    UNREGISTERED_BOXED_WITH_TRANSFER,
    UNREGISTERED_UNION,
    UNSUPPORTED_TYPE,
    LAST_REASON
};

namespace Gjs {
namespace Arg {

struct Instance;

enum class Kind {
    NORMAL,
    INSTANCE,
    RETURN_VALUE,
};

class ReturnTag {
    GITypeTag m_tag : 5;
    bool m_is_enum_or_flags_interface : 1;
    bool m_is_pointer : 1;

 public:
    constexpr explicit ReturnTag(GITypeTag tag)
        : m_tag(tag),
          m_is_enum_or_flags_interface(false),
          m_is_pointer(false) {}
    constexpr explicit ReturnTag(GITypeTag tag, bool is_enum_or_flags_interface,
                                 bool is_pointer)
        : m_tag(tag),
          m_is_enum_or_flags_interface(is_enum_or_flags_interface),
          m_is_pointer(is_pointer) {}
    explicit ReturnTag(const GI::TypeInfo type_info)
        : m_tag(type_info.tag()),
          m_is_pointer(type_info.is_pointer()) {
        if (m_tag == GI_TYPE_TAG_INTERFACE) {
            GI::AutoBaseInfo interface_info{type_info.interface()};
            m_is_enum_or_flags_interface = interface_info.is_enum_or_flags();
        }
    }

    constexpr GITypeTag tag() const { return m_tag; }
    [[nodiscard]]
    constexpr bool is_enum_or_flags_interface() const {
        return m_tag == GI_TYPE_TAG_INTERFACE && m_is_enum_or_flags_interface;
    }
    [[nodiscard]]
    constexpr GType interface_gtype() const {
        return is_enum_or_flags_interface() ? GI_TYPE_ENUM_INFO : G_TYPE_NONE;
    }
    constexpr bool is_pointer() const { return m_is_pointer; }
};

}  // namespace Arg

// When creating an Argument, pass it directly to ArgsCache::set_argument() or
// one of the similar methods, which will call init_common() on it and store it
// in the appropriate place in the arguments cache.
struct Argument {
    // Convenience struct to prevent long argument lists to make() and the
    // functions that call it
    struct Init {
        const char* name;
        uint8_t index;
        GITransfer transfer : 2;
        GjsArgumentFlags flags : 6;
    };

    virtual ~Argument() = default;

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool in(JSContext* cx, GjsFunctionCallState*,
                    GIArgument* in_argument, JS::HandleValue value);

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool out(JSContext* cx, GjsFunctionCallState*,
                     GIArgument* out_argument, JS::MutableHandleValue value);

    GJS_JSAPI_RETURN_CONVENTION
    virtual bool release(JSContext* cx, GjsFunctionCallState*,
                         GIArgument* in_argument, GIArgument* out_argument);

    virtual GjsArgumentFlags flags() const {
        GjsArgumentFlags flags = GjsArgumentFlags::NONE;
        if (m_skip_in)
            flags |= GjsArgumentFlags::SKIP_IN;
        else
            flags |= GjsArgumentFlags::ARG_IN;
        if (m_skip_out)
            flags |= GjsArgumentFlags::SKIP_OUT;
        else
            flags |= GjsArgumentFlags::ARG_OUT;

        return flags;
    }

    // Introspected functions can have up to 253 arguments. The callback
    // closure or destroy notify parameter may have a value of 255 to indicate
    // that it is absent.
    static constexpr uint8_t MAX_ARGS = std::numeric_limits<uint8_t>::max() - 2;
    static constexpr uint8_t ABSENT = std::numeric_limits<uint8_t>::max();

    constexpr const char* arg_name() const { return m_arg_name; }

    constexpr bool skip_in() const { return m_skip_in; }

    constexpr bool skip_out() const { return m_skip_out; }

 protected:
    constexpr Argument() : m_skip_in(false), m_skip_out(false) {}

    virtual mozilla::Maybe<Arg::ReturnTag> return_tag() const { return {}; }
    virtual mozilla::Maybe<const Arg::Instance*> as_instance() const {
        return {};
    }

    constexpr void set_instance_parameter() {
        m_arg_name = "instance parameter";
        m_skip_out = true;
    }

    constexpr void set_return_value() { m_arg_name = "return value"; }

    bool invalid(JSContext*, const char* func = nullptr) const;

    const char* m_arg_name = nullptr;
    bool m_skip_in : 1;
    bool m_skip_out : 1;

 private:
    friend struct ArgsCache;

    template <typename T, Arg::Kind ArgKind>
    static void init_common(const Init&, T* arg);
};

using ArgumentPtr = AutoCppPointer<Argument>;

// This is a trick to print out the sizes of the structs at compile time, in
// an error message:
// template <int s> struct Measure;
// Measure<sizeof(Argument)> arg_cache_size;

#if defined(__x86_64__) && defined(__clang__) && !defined(_MSC_VER)
#    define GJS_DO_ARGUMENTS_SIZE_CHECK 1
// This isn't meant to be comprehensive, but should trip on at least one CI job
// if sizeof(Gjs::Argument) is increased.
// Note that this check is not applicable for clang-cl builds, as Windows is
// an LLP64 system
static_assert(sizeof(Argument) <= 24,
              "Think very hard before increasing the size of Gjs::Argument. "
              "One is allocated for every argument to every introspected "
              "function.");
#endif  // x86-64 clang

struct ArgsCache {
    GJS_JSAPI_RETURN_CONVENTION
    bool initialize(JSContext*, const GI::CallableInfo);

    // COMPAT: in C++20, use default initializers for these bitfields
    ArgsCache() : m_is_method(false), m_has_return(false) {}

    constexpr bool initialized() { return m_args != nullptr; }
    constexpr void clear() { m_args.reset(); }

    void build_arg(uint8_t gi_index, GIDirection, const GI::ArgInfo,
                   const GI::CallableInfo, bool* inc_counter_out);

    void build_return(const GI::CallableInfo, bool* inc_counter_out);

    void build_instance(const GI::CallableInfo);

    mozilla::Maybe<GType> instance_type() const;
    mozilla::Maybe<Arg::ReturnTag> return_tag() const;

 private:
    void build_normal_in_arg(uint8_t gi_index, const GI::TypeInfo,
                             const GI::ArgInfo, GjsArgumentFlags);
    void build_normal_out_arg(uint8_t gi_index, const GI::TypeInfo,
                              const GI::ArgInfo, GjsArgumentFlags);
    void build_normal_inout_arg(uint8_t gi_index, const GI::TypeInfo,
                                const GI::ArgInfo, GjsArgumentFlags);

    // GITypeInfo is not available for instance parameters (see
    // https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/334) but
    // for other parameters, this function additionally takes a GITypeInfo.
    template <Arg::Kind ArgKind = Arg::Kind::NORMAL>
    void build_interface_in_arg(const Argument::Init&,
                                const GI::BaseInfo interface_info);

    template <Arg::Kind ArgKind = Arg::Kind::NORMAL, typename T>
    constexpr void set_argument(T* arg, const Argument::Init&);

    void set_array_argument(const GI::CallableInfo, uint8_t gi_index,
                            const GI::TypeInfo, GIDirection, const GI::ArgInfo,
                            GjsArgumentFlags, unsigned length_pos);

    void set_array_return(const GI::CallableInfo, const GI::TypeInfo,
                          GjsArgumentFlags, unsigned length_pos);

    void init_out_array_length_argument(const GI::ArgInfo, GjsArgumentFlags,
                                        unsigned length_pos);

    template <typename T>
    constexpr void set_return(T* arg, GITransfer, GjsArgumentFlags);

    template <typename T>
    constexpr void set_instance(
        T* arg, GITransfer, GjsArgumentFlags flags = GjsArgumentFlags::NONE);

    void set_skip_all(uint8_t index, const char* name = nullptr);

    template <Arg::Kind ArgKind = Arg::Kind::NORMAL>
    constexpr uint8_t arg_index(uint8_t index
                                [[maybe_unused]] = Argument::MAX_ARGS) const {
        if constexpr (ArgKind == Arg::Kind::RETURN_VALUE)
            return 0;
        else if constexpr (ArgKind == Arg::Kind::INSTANCE)
            return (m_has_return ? 1 : 0);
        else if constexpr (ArgKind == Arg::Kind::NORMAL)
            return (m_has_return ? 1 : 0) + (m_is_method ? 1 : 0) + index;
    }

    template <Arg::Kind ArgKind = Arg::Kind::NORMAL>
    constexpr ArgumentPtr& arg_get(uint8_t index = Argument::MAX_ARGS) const {
        return m_args[arg_index<ArgKind>(index)];
    }

 public:
    constexpr Argument* argument(uint8_t index) const {
        return arg_get(index).get();
    }

    constexpr mozilla::Maybe<Argument*> instance() const {
        if (!m_is_method)
            return {};

        return mozilla::Some(arg_get<Arg::Kind::INSTANCE>().get());
    }

    constexpr mozilla::Maybe<Argument*> return_value() const {
        if (!m_has_return)
            return {};

        return mozilla::Some(arg_get<Arg::Kind::RETURN_VALUE>().get());
    }

 private:
    AutoCppPointer<ArgumentPtr[]> m_args;

    bool m_is_method : 1;
    bool m_has_return : 1;
};

}  // namespace Gjs

#endif  // GI_ARG_CACHE_H_
