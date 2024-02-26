/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#ifndef GI_ARG_CACHE_H_
#define GI_ARG_CACHE_H_

#include <config.h>

#include <stdint.h>

#include <limits>

#include <girepository.h>
#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gi/arg.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-util.h"
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

using ReturnValue = struct GenericOut;
struct Instance;

enum class Kind {
    NORMAL,
    INSTANCE,
    RETURN_VALUE,
};

}  // namespace Arg

struct Argument {
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

    virtual GITypeTag return_tag() const { return GI_TYPE_TAG_VOID; }
    virtual const GITypeInfo* return_type() const { return nullptr; }
    virtual const Arg::Instance* as_instance() const { return nullptr; }

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

    template <typename T, Arg::Kind ArgKind, typename... Args>
    static GjsAutoCppPointer<T> make(uint8_t index, const char* name,
                                     GITypeInfo* type_info, GITransfer transfer,
                                     GjsArgumentFlags flags, Args&&... args);
};

using ArgumentPtr = GjsAutoCppPointer<Argument>;

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
    bool initialize(JSContext* cx, GICallableInfo* callable);

    // COMPAT: in C++20, use default initializers for these bitfields
    ArgsCache() : m_is_method(false), m_has_return(false) {}

    constexpr bool initialized() { return m_args != nullptr; }
    constexpr void clear() { m_args.reset(); }

    void build_arg(uint8_t gi_index, GIDirection, GIArgInfo*, GICallableInfo*,
                   bool* inc_counter_out);

    void build_return(GICallableInfo* callable, bool* inc_counter_out);

    void build_instance(GICallableInfo* callable);

    GType instance_type() const;
    GITypeTag return_tag() const;
    GITypeInfo* return_type() const;

 private:
    void build_normal_in_arg(uint8_t gi_index, GITypeInfo*, GIArgInfo*,
                             GjsArgumentFlags);
    void build_normal_out_arg(uint8_t gi_index, GITypeInfo*, GIArgInfo*,
                              GjsArgumentFlags);
    void build_normal_inout_arg(uint8_t gi_index, GITypeInfo*, GIArgInfo*,
                                GjsArgumentFlags);

    template <Arg::Kind ArgKind = Arg::Kind::NORMAL>
    void build_interface_in_arg(uint8_t gi_index, GITypeInfo*, GIBaseInfo*,
                                GITransfer, const char* name, GjsArgumentFlags);

    template <typename T, Arg::Kind ArgKind = Arg::Kind::NORMAL,
              typename... Args>
    constexpr T* set_argument(uint8_t index, const char* name, GITypeInfo*,
                              GITransfer, GjsArgumentFlags flags,
                              Args&&... args);

    template <typename T, Arg::Kind ArgKind = Arg::Kind::NORMAL,
              typename... Args>
    constexpr T* set_argument(uint8_t index, const char* name, GITransfer,
                              GjsArgumentFlags flags, Args&&... args);

    template <typename T, Arg::Kind ArgKind = Arg::Kind::NORMAL,
              typename... Args>
    constexpr T* set_argument_auto(Args&&... args);

    template <typename T, Arg::Kind ArgKind = Arg::Kind::NORMAL, typename Tuple,
              typename... Args>
    constexpr T* set_argument_auto(Tuple&& tuple, Args&&... args);

    template <Arg::Kind ArgKind = Arg::Kind::NORMAL>
    void set_array_argument(GICallableInfo* callable, uint8_t gi_index,
                            GITypeInfo*, GIDirection, GIArgInfo*,
                            GjsArgumentFlags flags, int length_pos);

    template <typename T>
    constexpr T* set_return(GITypeInfo*, GITransfer, GjsArgumentFlags);

    template <typename T>
    constexpr T* set_instance(GITransfer,
                              GjsArgumentFlags flags = GjsArgumentFlags::NONE);

    constexpr void set_skip_all(uint8_t index, const char* name = nullptr);

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

    constexpr Argument* instance() const {
        if (!m_is_method)
            return nullptr;

        return arg_get<Arg::Kind::INSTANCE>().get();
    }

    constexpr Argument* return_value() const {
        if (!m_has_return)
            return nullptr;

        return arg_get<Arg::Kind::RETURN_VALUE>().get();
    }

 private:
    GjsAutoCppPointer<ArgumentPtr[]> m_args;

    bool m_is_method : 1;
    bool m_has_return : 1;
};

}  // namespace Gjs

#endif  // GI_ARG_CACHE_H_
