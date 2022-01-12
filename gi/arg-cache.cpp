/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <limits>
#include <tuple>
#include <type_traits>
#include <unordered_set>  // for unordered_set::erase(), insert()
#include <utility>

#include <ffi.h>
#include <girepository.h>
#include <glib.h>

#include <js/Conversions.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>        // for JS_TypeOfValue
#include <jsfriendapi.h>  // for JS_GetObjectFunction
#include <jspubtd.h>      // for JSTYPE_FUNCTION

#include "gi/arg-cache.h"
#include "gi/arg-inl.h"
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/boxed.h"
#include "gi/closure.h"
#include "gi/foreign.h"
#include "gi/function.h"
#include "gi/fundamental.h"
#include "gi/gerror.h"
#include "gi/gtype.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"  // for GjsTypecheckNoThrow
#include "gjs/byteArray.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"

enum ExpectedType {
    OBJECT,
    FUNCTION,
    STRING,
    LAST,
};

static const char* expected_type_names[] = {"object", "function", "string"};
static_assert(G_N_ELEMENTS(expected_type_names) == ExpectedType::LAST,
              "Names must match the values in ExpectedType");

// A helper function to retrieve array lengths from a GIArgument (letting the
// compiler generate good instructions in case of big endian machines)
[[nodiscard]] size_t gjs_g_argument_get_array_length(GITypeTag tag,
                                                     GIArgument* arg) {
    switch (tag) {
        case GI_TYPE_TAG_INT8:
            return gjs_arg_get<int8_t>(arg);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_get<uint8_t>(arg);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_get<int16_t>(arg);
        case GI_TYPE_TAG_UINT16:
            return gjs_arg_get<uint16_t>(arg);
        case GI_TYPE_TAG_INT32:
            return gjs_arg_get<int32_t>(arg);
        case GI_TYPE_TAG_UINT32:
            return gjs_arg_get<uint32_t>(arg);
        case GI_TYPE_TAG_INT64:
            return gjs_arg_get<int64_t>(arg);
        case GI_TYPE_TAG_UINT64:
            return gjs_arg_get<uint64_t>(arg);
        default:
            g_assert_not_reached();
    }
}

static void gjs_g_argument_set_array_length(GITypeTag tag, GIArgument* arg,
                                            size_t value) {
    switch (tag) {
        case GI_TYPE_TAG_INT8:
            gjs_arg_set<int8_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT8:
            gjs_arg_set<uint8_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT16:
            gjs_arg_set<int16_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT16:
            gjs_arg_set<uint16_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT32:
            gjs_arg_set<int32_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT32:
            gjs_arg_set<uint32_t>(arg, value);
            break;
        case GI_TYPE_TAG_INT64:
            gjs_arg_set<int64_t>(arg, value);
            break;
        case GI_TYPE_TAG_UINT64:
            gjs_arg_set<uint64_t>(arg, value);
            break;
        default:
            g_assert_not_reached();
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_typeof_mismatch(JSContext* cx, const char* arg_name,
                                   JS::HandleValue value,
                                   ExpectedType expected) {
    gjs_throw(cx, "Expected type %s for argument '%s' but got type %s",
              expected_type_names[expected], arg_name,
              JS::InformalValueTypeName(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_gtype_mismatch(JSContext* cx, const char* arg_name,
                                  JS::Value value, GType expected) {
    gjs_throw(
        cx, "Expected an object of type %s for argument '%s' but got type %s",
        g_type_name(expected), arg_name, JS::InformalValueTypeName(value));
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool report_invalid_null(JSContext* cx, const char* arg_name) {
    gjs_throw(cx, "Argument %s may not be null", arg_name);
    return false;
}

namespace Gjs {
namespace Arg {

// Arguments Interfaces:
//
// Each of these types are meant to be used to extend each Gjs::Argument
// implementation, taking advantage of the C++ multiple inheritance.

struct BasicType {
    constexpr BasicType() : m_tag(GI_TYPE_TAG_VOID) {}
    constexpr explicit BasicType(GITypeTag tag) : m_tag(tag) {}
    constexpr operator GITypeTag() const { return m_tag; }

    GITypeTag m_tag : 5;
};

struct String {
    constexpr String() : m_filename(false) {}
    bool m_filename : 1;
};

struct TypeInfo {
    constexpr const GITypeInfo* type_info() const { return &m_type_info; }

    GITypeInfo m_type_info;
};

struct Transferable {
    constexpr Transferable() : m_transfer(GI_TRANSFER_NOTHING) {}
    constexpr explicit Transferable(GITransfer transfer)
        : m_transfer(transfer) {}
    GITransfer m_transfer : 2;
};

struct Nullable {
    constexpr Nullable() : m_nullable(false) {}
    bool m_nullable : 1;

    bool handle_nullable(JSContext* cx, GIArgument* arg, const char* arg_name);

    constexpr GjsArgumentFlags flags() const {
        return m_nullable ? GjsArgumentFlags::MAY_BE_NULL
                          : GjsArgumentFlags::NONE;
    }
};

// Overload operator| so that Visual Studio won't complain
// when converting unsigned char to GjsArgumentFlags
GjsArgumentFlags operator|(
    GjsArgumentFlags const& v1, GjsArgumentFlags const& v2) {
    return static_cast<GjsArgumentFlags>(std::underlying_type<GjsArgumentFlags>::type(v1) |
                                         std::underlying_type<GjsArgumentFlags>::type(v2));
}

struct Positioned {
    void set_arg_pos(int pos) {
        g_assert(pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        m_arg_pos = pos;
    }

    bool set_out_parameter(GjsFunctionCallState* state, GIArgument* arg) {
        gjs_arg_unset<void*>(&state->out_cvalue(m_arg_pos));
        gjs_arg_set(arg, &gjs_arg_member<void*>(&state->out_cvalue(m_arg_pos)));
        return true;
    }

    uint8_t m_arg_pos = 0;
};

struct Array : BasicType {
    uint8_t m_length_pos = 0;

    void set_array_length(int pos, GITypeTag tag) {
        g_assert(pos >= 0 && pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        m_length_pos = pos;
        m_tag = tag;
    }
};

struct BaseInfo {
    explicit BaseInfo(GIBaseInfo* info)
        : m_info(info ? g_base_info_ref(info) : nullptr) {}

    GjsAutoBaseInfo m_info;
};

// boxed / union / GObject
struct RegisteredType {
    constexpr RegisteredType(GType gtype, GIInfoType info_type)
        : m_gtype(gtype), m_info_type(info_type) {}
    explicit RegisteredType(GIBaseInfo* info)
        : m_gtype(g_registered_type_info_get_g_type(info)),
          m_info_type(g_base_info_get_type(info)) {
        g_assert(m_gtype != G_TYPE_NONE &&
                 "Use RegisteredInterface for this type");
    }

    constexpr GType gtype() const { return m_gtype; }

    GType m_gtype;
    GIInfoType m_info_type : 5;
};

struct RegisteredInterface : BaseInfo {
    explicit RegisteredInterface(GIBaseInfo* info)
        : BaseInfo(info), m_gtype(g_registered_type_info_get_g_type(m_info)) {}

    constexpr GType gtype() const { return m_gtype; }

    GType m_gtype;
};

struct Callback : Nullable, BaseInfo {
    explicit Callback(GITypeInfo* type_info)
        : BaseInfo(g_type_info_get_interface(type_info)),
          m_scope(GI_SCOPE_TYPE_INVALID) {}

    inline void set_callback_destroy_pos(int pos) {
        g_assert(pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        m_destroy_pos = pos < 0 ? Argument::ABSENT : pos;
    }

    [[nodiscard]] constexpr bool has_callback_destroy() {
        return m_destroy_pos != Argument::ABSENT;
    }

    inline void set_callback_closure_pos(int pos) {
        g_assert(pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        m_closure_pos = pos < 0 ? Argument::ABSENT : pos;
    }

    [[nodiscard]] constexpr bool has_callback_closure() {
        return m_closure_pos != Argument::ABSENT;
    }

    uint8_t m_closure_pos = Argument::ABSENT;
    uint8_t m_destroy_pos = Argument::ABSENT;
    GIScopeType m_scope : 2;
};

struct Enum {
    explicit Enum(GIEnumInfo*);
    bool m_unsigned : 1;
    uint32_t m_min = 0;
    uint32_t m_max = 0;
};

struct Flags {
    explicit Flags(GIEnumInfo*);
    unsigned m_mask = 0;
};

struct CallerAllocates {
    size_t m_allocates_size = 0;
};

// Gjs::Arguments:
//
// Each argument, irrespective of the direction, is processed in three phases:
// - before calling the function [in]
// - after calling it, when converting the return value and out arguments [out]
// - at the end of the invocation, to release any allocated memory [release]
//
// Some types don't have direction (for example, caller_allocates is only out,
// and callback is only in), in which case it is implied.

struct SkipAll : Argument {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return skip();
    }

    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }

    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }

 protected:
    constexpr bool skip() { return true; }
};

struct Generic : SkipAll, Transferable, TypeInfo {};

struct GenericIn : Generic {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct GenericInOut : GenericIn, Positioned {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct GenericOut : GenericInOut {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    const ReturnValue* as_return_value() const override { return this; }
};

struct GenericReturn : ReturnValue {
    // No in!
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
};

struct SimpleOut : SkipAll, Positioned {
    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    };
};

struct ExplicitArray : GenericOut, Array, Nullable {
    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

struct ExplicitArrayIn : ExplicitArray {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    };
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct ExplicitArrayInOut : ExplicitArrayIn {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct ExplicitArrayOut : ExplicitArrayInOut {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct ReturnArray : ExplicitArrayOut {
    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        return GenericOut::in(cx, state, arg, value);
    };
};

using ArrayLengthOut = SimpleOut;

struct FallbackIn : GenericIn, Nullable {
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

using FallbackInOut = GenericInOut;
using FallbackOut = GenericOut;

struct NotIntrospectable : GenericIn {
    explicit NotIntrospectable(NotIntrospectableReason reason)
        : m_reason(reason) {}
    NotIntrospectableReason m_reason;

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct NullableIn : SkipAll, Nullable {
    inline bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                   JS::HandleValue) override {
        return handle_nullable(cx, arg, m_arg_name);
    }

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

struct Instance : NullableIn {
    //  Some calls accept null for the instance (thus we inherit from
    //  NullableIn), but generally in an object oriented language it's wrong to
    //  call a method on null.
    //  As per this we actually default to SkipAll methods.

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return skip();
    }
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }

    const Instance* as_instance() const override { return this; }

    //  The instance GType can be useful only in few cases such as GObjects and
    //  GInterfaces, so we don't store it by default, unless needed.
    //  See Function's code to see where this is relevant.
    virtual GType gtype() const { return G_TYPE_NONE; }
};

struct EnumIn : Instance, Enum {
    using Enum::Enum;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct FlagsIn : Instance, Flags {
    using Flags::Flags;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct RegisteredIn : Instance, RegisteredType, Transferable {
    using RegisteredType::RegisteredType;

    GType gtype() const override { return RegisteredType::gtype(); }
};

struct RegisteredInterfaceIn : Instance, RegisteredInterface, Transferable {
    using RegisteredInterface::RegisteredInterface;

    GType gtype() const override { return RegisteredInterface::gtype(); }
};

struct ForeignStructInstanceIn : RegisteredInterfaceIn {
    using RegisteredInterfaceIn::RegisteredInterfaceIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct ForeignStructIn : ForeignStructInstanceIn {
    using ForeignStructInstanceIn::ForeignStructInstanceIn;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct FallbackInterfaceIn : RegisteredInterfaceIn, TypeInfo {
    using RegisteredInterfaceIn::RegisteredInterfaceIn;

    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_g_argument(cx, value, &m_type_info, m_arg_name,
                                       GJS_ARGUMENT_ARGUMENT, m_transfer,
                                       flags(), arg);
    }
};

struct BoxedInTransferNone : RegisteredIn {
    using RegisteredIn::RegisteredIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    virtual GIBaseInfo* info() const { return nullptr; }
};

struct BoxedIn : BoxedInTransferNone {
    using BoxedInTransferNone::BoxedInTransferNone;
    // This is a smart argument, no release needed
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }
};

struct UnregisteredBoxedIn : BoxedIn, BaseInfo {
    explicit UnregisteredBoxedIn(GIInterfaceInfo* info)
        : BoxedIn(g_registered_type_info_get_g_type(info),
                  g_base_info_get_type(info)),
          BaseInfo(info) {}
    // This is a smart argument, no release needed
    GIBaseInfo* info() const override { return m_info; }
};

struct GValueIn : BoxedIn {
    using BoxedIn::BoxedIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct GValueInTransferNone : GValueIn {
    using GValueIn::GValueIn;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct GClosureInTransferNone : BoxedInTransferNone {
    using BoxedInTransferNone::BoxedInTransferNone;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct GClosureIn : GClosureInTransferNone {
    using GClosureInTransferNone::GClosureInTransferNone;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }
};

struct GBytesIn : BoxedIn {
    using BoxedIn::BoxedIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct GBytesInTransferNone : GBytesIn {
    using GBytesIn::GBytesIn;
    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg) override {
        return BoxedIn::release(cx, state, in_arg, out_arg);
    }
};

struct ObjectIn : RegisteredIn {
    using RegisteredIn::RegisteredIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // This is a smart argument, no release needed
};

struct InterfaceIn : RegisteredIn {
    using RegisteredIn::RegisteredIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // This is a smart argument, no release needed
};

struct FundamentalIn : RegisteredIn {
    using RegisteredIn::RegisteredIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // This is a smart argument, no release needed
};

struct UnionIn : RegisteredIn {
    using RegisteredIn::RegisteredIn;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // This is a smart argument, no release needed
};

struct NullIn : NullableIn {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct BooleanIn : SkipAll {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct NumericIn : SkipAll, BasicType {
    explicit NumericIn(GITypeTag tag) : BasicType(tag) {}
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct UnicharIn : SkipAll {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct GTypeIn : SkipAll {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct StringInTransferNone : NullableIn, String {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct StringIn : StringInTransferNone {
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }
};

struct FilenameInTransferNone : StringInTransferNone {
    FilenameInTransferNone() { m_filename = true; }
};

struct FilenameIn : FilenameInTransferNone {
    FilenameIn() { m_filename = true; }
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }
};

// .out is ignored for the instance parameter
struct GTypeStructInstanceIn : Instance {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // no out
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return invalid(cx, G_STRFUNC);
    };
};

struct ParamInstanceIn : Instance, Transferable {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    // no out
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return invalid(cx, G_STRFUNC);
    };
};

struct CallbackIn : SkipAll, Callback {
    using Callback::Callback;
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;

    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

using CArrayIn = ExplicitArrayIn;

using CArrayInOut = ExplicitArrayInOut;

using CArrayOut = ReturnArray;

struct CallerAllocatesOut : GenericOut, CallerAllocates {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    GjsArgumentFlags flags() const override {
        return GenericOut::flags() | GjsArgumentFlags::CALLER_ALLOCATES;
    }
};

GJS_JSAPI_RETURN_CONVENTION
bool NotIntrospectable::in(JSContext* cx, GjsFunctionCallState* state,
                           GIArgument*, JS::HandleValue) {
    static const char* reason_strings[] = {
        "callback out-argument",
        "DestroyNotify argument with no callback",
        "DestroyNotify argument with no user data",
        "type not supported for (transfer container)",
        "type not supported for (out caller-allocates)",
        "boxed type with transfer not registered as a GType",
        "union type not registered as a GType",
        "type not supported by introspection",
    };
    static_assert(
        G_N_ELEMENTS(reason_strings) == NotIntrospectableReason::LAST_REASON,
        "Explanations must match the values in NotIntrospectableReason");

    gjs_throw(cx,
              "Function %s() cannot be called: argument '%s' with type %s is "
              "not introspectable because it has a %s",
              state->display_name().get(), m_arg_name,
              g_type_tag_to_string(g_type_info_get_tag(&m_type_info)),
              reason_strings[m_reason]);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                   JS::HandleValue value) {
    return gjs_value_to_g_argument(cx, value, &m_type_info, m_arg_name,
                                   GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                   arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericInOut::in(JSContext* cx, GjsFunctionCallState* state,
                      GIArgument* arg, JS::HandleValue value) {
    if (!GenericIn::in(cx, state, arg, value))
        return false;

    state->out_cvalue(m_arg_pos) = state->inout_original_cvalue(m_arg_pos) =
        *arg;
    gjs_arg_set(arg, &state->out_cvalue(m_arg_pos));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayIn::in(JSContext* cx, GjsFunctionCallState* state,
                         GArgument* arg, JS::HandleValue value) {
    void* data;
    size_t length;

    if (!gjs_array_to_explicit_array(cx, value, &m_type_info, m_arg_name,
                                     GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                     &data, &length))
        return false;

    gjs_g_argument_set_array_length(m_tag, &state->in_cvalue(m_length_pos),
                                    length);
    gjs_arg_set(arg, data);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayInOut::in(JSContext* cx, GjsFunctionCallState* state,
                            GIArgument* arg, JS::HandleValue value) {
    if (!ExplicitArrayIn::in(cx, state, arg, value))
        return false;

    uint8_t length_pos = m_length_pos;
    uint8_t ix = m_arg_pos;

    if (!gjs_arg_get<void*>(arg)) {
        // Special case where we were given JS null to also pass null for
        // length, and not a pointer to an integer that derefs to 0.
        gjs_arg_unset<void*>(&state->in_cvalue(length_pos));
        gjs_arg_unset<int>(&state->out_cvalue(length_pos));
        gjs_arg_unset<int>(&state->inout_original_cvalue(length_pos));

        gjs_arg_unset<void*>(&state->out_cvalue(ix));
        gjs_arg_unset<void*>(&state->inout_original_cvalue(ix));
    } else {
        state->out_cvalue(length_pos) =
            state->inout_original_cvalue(length_pos) =
                state->in_cvalue(length_pos);
        gjs_arg_set(&state->in_cvalue(length_pos),
                    &state->out_cvalue(length_pos));

        state->out_cvalue(ix) = state->inout_original_cvalue(ix) = *arg;
        gjs_arg_set(arg, &state->out_cvalue(ix));
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool CallbackIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                    JS::HandleValue value) {
    GjsCallbackTrampoline* trampoline;
    ffi_closure* closure;

    if (value.isNull() && m_nullable) {
        closure = nullptr;
        trampoline = nullptr;
    } else {
        if (JS_TypeOfValue(cx, value) != JSTYPE_FUNCTION) {
            gjs_throw(cx, "Expected function for callback argument %s, got %s",
                      m_arg_name, JS::InformalValueTypeName(value));
            return false;
        }

        JS::RootedFunction func(cx, JS_GetObjectFunction(&value.toObject()));
        bool is_object_method = !!state->instance_object;
        trampoline = GjsCallbackTrampoline::create(cx, func, m_info, m_scope,
                                                   is_object_method, false);
        if (!trampoline)
            return false;
        if (m_scope == GI_SCOPE_TYPE_NOTIFIED && is_object_method) {
            auto* priv = ObjectInstance::for_js(cx, state->instance_object);
            if (!priv) {
                gjs_throw(cx, "Signal connected to wrong type of object");
                return false;
            }

            if (!priv->associate_closure(cx, trampoline))
                return false;
        }
        closure = trampoline->closure();
    }

    if (has_callback_destroy()) {
        GDestroyNotify destroy_notify = nullptr;
        if (trampoline) {
            /* Adding another reference and a DestroyNotify that unsets it */
            g_closure_ref(trampoline);
            destroy_notify = [](void* data) {
                g_assert(data);
                g_closure_unref(static_cast<GClosure*>(data));
            };
        }
        gjs_arg_set(&state->in_cvalue(m_destroy_pos), destroy_notify);
    }
    if (has_callback_closure())
        gjs_arg_set(&state->in_cvalue(m_closure_pos), trampoline);

    if (trampoline && m_scope == GI_SCOPE_TYPE_ASYNC) {
        // Add an extra reference that will be cleared when garbage collecting
        // async calls or never for notified callbacks without destroy
        g_closure_ref(trampoline);
    }

    bool keep_forever =
#if GI_CHECK_VERSION(1, 72, 0)
        m_scope == GI_SCOPE_TYPE_FOREVER ||
#endif
        m_scope == GI_SCOPE_TYPE_NOTIFIED && !has_callback_destroy();

    if (trampoline && keep_forever) {
        trampoline->mark_forever();
    }
    gjs_arg_set(arg, closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericOut::in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
                    JS::HandleValue) {
    // Default value in case a broken C function doesn't fill in the pointer
    return set_out_parameter(state, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CallerAllocatesOut::in(JSContext*, GjsFunctionCallState* state,
                            GIArgument* arg, JS::HandleValue) {
    void* blob = g_malloc0(m_allocates_size);
    gjs_arg_set(arg, blob);
    gjs_arg_set(&state->out_cvalue(m_arg_pos), blob);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool NullIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                JS::HandleValue) {
    return handle_nullable(cx, arg, m_arg_name);
}

GJS_JSAPI_RETURN_CONVENTION
bool BooleanIn::in(JSContext*, GjsFunctionCallState*, GIArgument* arg,
                   JS::HandleValue value) {
    gjs_arg_set(arg, JS::ToBoolean(value));
    return true;
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION inline static bool gjs_arg_set_from_js_value(
    JSContext* cx, const JS::HandleValue& value, GArgument* arg,
    Argument* gjs_arg) {
    bool out_of_range = false;

    if (!gjs_arg_set_from_js_value<T>(cx, value, arg, &out_of_range)) {
        if (out_of_range) {
            gjs_throw(cx, "Argument %s: value is out of range for %s",
                      gjs_arg->arg_name(), Gjs::static_type_name<T>());
        }

        return false;
    }

    gjs_debug_marshal(
        GJS_DEBUG_GFUNCTION, "%s set to value %s (type %s)",
        GjsAutoChar(gjs_argument_display_name(gjs_arg->arg_name(),
                                              GJS_ARGUMENT_ARGUMENT))
            .get(),
        std::to_string(gjs_arg_get<T>(arg)).c_str(),
        Gjs::static_type_name<T>());

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool NumericIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                   JS::HandleValue value) {
    switch (m_tag) {
        case GI_TYPE_TAG_INT8:
            return gjs_arg_set_from_js_value<int8_t>(cx, value, arg, this);
        case GI_TYPE_TAG_UINT8:
            return gjs_arg_set_from_js_value<uint8_t>(cx, value, arg, this);
        case GI_TYPE_TAG_INT16:
            return gjs_arg_set_from_js_value<int16_t>(cx, value, arg, this);
        case GI_TYPE_TAG_UINT16:
            return gjs_arg_set_from_js_value<uint16_t>(cx, value, arg, this);
        case GI_TYPE_TAG_INT32:
            return gjs_arg_set_from_js_value<int32_t>(cx, value, arg, this);
        case GI_TYPE_TAG_DOUBLE:
            return gjs_arg_set_from_js_value<double>(cx, value, arg, this);
        case GI_TYPE_TAG_FLOAT:
            return gjs_arg_set_from_js_value<float>(cx, value, arg, this);
        case GI_TYPE_TAG_INT64:
            return gjs_arg_set_from_js_value<int64_t>(cx, value, arg, this);
        case GI_TYPE_TAG_UINT64:
            return gjs_arg_set_from_js_value<uint64_t>(cx, value, arg, this);
        case GI_TYPE_TAG_UINT32:
            return gjs_arg_set_from_js_value<uint32_t>(cx, value, arg, this);
        default:
            g_assert_not_reached();
    }
}

GJS_JSAPI_RETURN_CONVENTION
bool UnicharIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                   JS::HandleValue value) {
    if (!value.isString())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::STRING);

    return gjs_unichar_from_string(cx, value, &gjs_arg_member<char32_t>(arg));
}

GJS_JSAPI_RETURN_CONVENTION
bool GTypeIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                 JS::HandleValue value) {
    if (value.isNull())
        return report_invalid_null(cx, m_arg_name);
    if (!value.isObject())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject gtype_obj(cx, &value.toObject());
    return gjs_gtype_get_actual_gtype(
        cx, gtype_obj, &gjs_arg_member<GType, GI_TYPE_TAG_GTYPE>(arg));
}

// Common code for most types that are pointers on the C side
bool Nullable::handle_nullable(JSContext* cx, GIArgument* arg,
                               const char* arg_name) {
    if (!m_nullable)
        return report_invalid_null(cx, arg_name);
    gjs_arg_unset<void*>(arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool StringInTransferNone::in(JSContext* cx, GjsFunctionCallState* state,
                              GIArgument* arg, JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    if (!value.isString())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::STRING);

    if (m_filename) {
        GjsAutoChar str;
        if (!gjs_string_to_filename(cx, value, &str))
            return false;
        gjs_arg_set(arg, str.release());
        return true;
    }

    JS::UniqueChars str = gjs_string_to_utf8(cx, value);
    if (!str)
        return false;
    gjs_arg_set(arg, g_strdup(str.get()));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool EnumIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                JS::HandleValue value) {
    int64_t number;
    if (!Gjs::js_value_to_c(cx, value, &number))
        return false;

    // Unpack the values from their uint32_t bitfield. See note in
    // Enum::Enum().
    int64_t min, max;
    if (m_unsigned) {
        min = m_min;
        max = m_max;
    } else {
        min = static_cast<int32_t>(m_min);
        max = static_cast<int32_t>(m_max);
    }

    if (number > max || number < min) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for enum argument %s",
                  number, m_arg_name);
        return false;
    }

    if (m_unsigned)
        gjs_arg_set<unsigned, GI_TYPE_TAG_INTERFACE>(arg, number);
    else
        gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(arg, number);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool FlagsIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                 JS::HandleValue value) {
    int64_t number;
    if (!Gjs::js_value_to_c(cx, value, &number))
        return false;

    if ((uint64_t(number) & m_mask) != uint64_t(number)) {
        gjs_throw(cx, "%" PRId64 " is not a valid value for flags argument %s",
                  number, m_arg_name);
        return false;
    }

    // We cast to unsigned because that's what makes sense, but then we
    // put it in the v_int slot because that's what we use to unmarshal
    // flags types at the moment.
    gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(arg, static_cast<unsigned>(number));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool ForeignStructInstanceIn::in(JSContext* cx, GjsFunctionCallState*,
                                 GIArgument* arg, JS::HandleValue value) {
    return gjs_struct_foreign_convert_to_g_argument(
        cx, value, m_info, m_arg_name, GJS_ARGUMENT_ARGUMENT, m_transfer,
        flags(), arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool GValueIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                  JS::HandleValue value) {
    if (value.isObject()) {
        JS::RootedObject obj(cx, &value.toObject());
        GType gtype;

        if (!gjs_gtype_get_actual_gtype(cx, obj, &gtype))
            return false;

        if (gtype == G_TYPE_VALUE) {
            gjs_arg_set(arg, BoxedBase::to_c_ptr<GValue>(cx, obj));
            state->ignore_release.insert(arg);
            return true;
        }
    }

    Gjs::AutoGValue gvalue;

    if (!gjs_value_to_g_value(cx, value, &gvalue))
        return false;

    gjs_arg_set(arg, g_boxed_copy(G_TYPE_VALUE, &gvalue));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool BoxedInTransferNone::in(JSContext* cx, GjsFunctionCallState* state,
                             GIArgument* arg, JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    GType gtype = RegisteredType::gtype();

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    if (gtype == G_TYPE_ERROR) {
        return ErrorBase::transfer_to_gi_argument(cx, object, arg,
                                                  GI_DIRECTION_IN, m_transfer);
    }

    return BoxedBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                              m_transfer, gtype, info());
}

// Unions include ClutterEvent and GdkEvent, which occur fairly often in an
// interactive application, so they're worth a special case in a different
// virtual function.
GJS_JSAPI_RETURN_CONVENTION
bool UnionIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                 JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    GType gtype = RegisteredType::gtype();

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return UnionBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                              m_transfer, gtype);
}

GJS_JSAPI_RETURN_CONVENTION
bool GClosureInTransferNone::in(JSContext* cx, GjsFunctionCallState* state,
                                GIArgument* arg, JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    if (!(JS_TypeOfValue(cx, value) == JSTYPE_FUNCTION))
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::FUNCTION);

    JS::RootedFunction func(cx, JS_GetObjectFunction(&value.toObject()));
    GClosure* closure = Gjs::Closure::create_marshaled(cx, func, "boxed");
    gjs_arg_set(arg, closure);
    g_closure_ref(closure);
    g_closure_sink(closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool GBytesIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                  JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, G_TYPE_BYTES);

    JS::RootedObject object(cx, &value.toObject());
    if (JS_IsUint8Array(object)) {
        gjs_arg_set(arg, gjs_byte_array_get_bytes(object));
        return true;
    }

    // The bytearray path is taking an extra ref irrespective of transfer
    // ownership, so we need to do the same here.
    return BoxedBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, GI_TRANSFER_EVERYTHING, G_TYPE_BYTES);
}

GJS_JSAPI_RETURN_CONVENTION
bool InterfaceIn::in(JSContext* cx, GjsFunctionCallState* state,
                     GIArgument* arg, JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    GType gtype = RegisteredType::gtype();

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());

    // Could be a GObject interface that's missing a prerequisite,
    // or could be a fundamental
    if (ObjectBase::typecheck(cx, object, nullptr, gtype,
                              GjsTypecheckNoThrow())) {
        return ObjectBase::transfer_to_gi_argument(
            cx, object, arg, GI_DIRECTION_IN, m_transfer, gtype);
    }

    // If this typecheck fails, then it's neither an object nor a
    // fundamental
    return FundamentalBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, m_transfer, gtype);
}

GJS_JSAPI_RETURN_CONVENTION
bool ObjectIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                  JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    GType gtype = RegisteredType::gtype();

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return ObjectBase::transfer_to_gi_argument(cx, object, arg, GI_DIRECTION_IN,
                                               m_transfer, gtype);
}

GJS_JSAPI_RETURN_CONVENTION
bool FundamentalIn::in(JSContext* cx, GjsFunctionCallState* state,
                       GIArgument* arg, JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    GType gtype = RegisteredType::gtype();

    if (!value.isObject())
        return report_gtype_mismatch(cx, m_arg_name, value, gtype);

    JS::RootedObject object(cx, &value.toObject());
    return FundamentalBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, m_transfer, gtype);
}

GJS_JSAPI_RETURN_CONVENTION
bool GTypeStructInstanceIn::in(JSContext* cx, GjsFunctionCallState*,
                               GIArgument* arg, JS::HandleValue value) {
    // Instance parameter is never nullable
    if (!value.isObject())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject obj(cx, &value.toObject());
    GType actual_gtype;
    if (!gjs_gtype_get_actual_gtype(cx, obj, &actual_gtype))
        return false;

    if (actual_gtype == G_TYPE_NONE) {
        gjs_throw(cx, "Invalid GType class passed for instance parameter");
        return false;
    }

    // We use peek here to simplify reference counting (we just ignore transfer
    // annotation, as GType classes are never really freed.) We know that the
    // GType class is referenced at least once when the JS constructor is
    // initialized.
    if (g_type_is_a(actual_gtype, G_TYPE_INTERFACE))
        gjs_arg_set(arg, g_type_default_interface_peek(actual_gtype));
    else
        gjs_arg_set(arg, g_type_class_peek(actual_gtype));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool ParamInstanceIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                         JS::HandleValue value) {
    // Instance parameter is never nullable
    if (!value.isObject())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::OBJECT);

    JS::RootedObject obj(cx, &value.toObject());
    if (!gjs_typecheck_param(cx, obj, G_TYPE_PARAM, true))
        return false;
    gjs_arg_set(arg, gjs_g_param_from_param(cx, obj));

    if (m_transfer == GI_TRANSFER_EVERYTHING)
        g_param_spec_ref(gjs_arg_get<GParamSpec*>(arg));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericInOut::out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                       JS::MutableHandleValue value) {
    return gjs_value_from_g_argument(cx, value, &m_type_info, arg, true);
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayInOut::out(JSContext* cx, GjsFunctionCallState* state,
                             GIArgument* arg, JS::MutableHandleValue value) {
    GIArgument* length_arg = &(state->out_cvalue(m_length_pos));
    size_t length = gjs_g_argument_get_array_length(m_tag, length_arg);

    return gjs_value_from_explicit_array(cx, value, &m_type_info, arg, length);
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericIn::release(JSContext* cx, GjsFunctionCallState* state,
                        GIArgument* in_arg, GIArgument*) {
    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
    return gjs_g_argument_release_in_arg(cx, transfer, &m_type_info, in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericOut::release(JSContext* cx, GjsFunctionCallState*,
                         GIArgument* in_arg [[maybe_unused]],
                         GIArgument* out_arg) {
    return gjs_g_argument_release(cx, m_transfer, &m_type_info, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool GenericInOut::release(JSContext* cx, GjsFunctionCallState* state,
                           GIArgument*, GIArgument* out_arg) {
    // For inout, transfer refers to what we get back from the function; for
    // the temporary C value we allocated, clearly we're responsible for
    // freeing it.

    GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
    if (!gjs_g_argument_release_in_arg(cx, GI_TRANSFER_NOTHING, &m_type_info,
                                       original_out_arg))
        return false;

    return gjs_g_argument_release(cx, m_transfer, &m_type_info, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayOut::release(JSContext* cx, GjsFunctionCallState* state,
                               GIArgument* in_arg [[maybe_unused]],
                               GIArgument* out_arg) {
    GIArgument* length_arg = &state->out_cvalue(m_length_pos);
    size_t length = gjs_g_argument_get_array_length(m_tag, length_arg);

    return gjs_g_argument_release_out_array(cx, m_transfer, &m_type_info,
                                            length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayIn::release(JSContext* cx, GjsFunctionCallState* state,
                              GIArgument* in_arg,
                              GIArgument* out_arg [[maybe_unused]]) {
    GIArgument* length_arg = &state->in_cvalue(m_length_pos);
    size_t length = gjs_g_argument_get_array_length(m_tag, length_arg);

    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

    return gjs_g_argument_release_in_array(cx, transfer, &m_type_info, length,
                                           in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool ExplicitArrayInOut::release(JSContext* cx, GjsFunctionCallState* state,
                                 GIArgument* in_arg [[maybe_unused]],
                                 GIArgument* out_arg) {
    GIArgument* length_arg = &state->in_cvalue(m_length_pos);
    size_t length = gjs_g_argument_get_array_length(m_tag, length_arg);

    // For inout, transfer refers to what we get back from the function; for
    // the temporary C value we allocated, clearly we're responsible for
    // freeing it.

    GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
    if (gjs_arg_get<void*>(original_out_arg) != gjs_arg_get<void*>(out_arg) &&
        !gjs_g_argument_release_in_array(cx, GI_TRANSFER_NOTHING, &m_type_info,
                                         length, original_out_arg))
        return false;

    return gjs_g_argument_release_out_array(cx, m_transfer, &m_type_info,
                                            length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CallerAllocatesOut::release(JSContext*, GjsFunctionCallState*,
                                 GIArgument* in_arg,
                                 GIArgument* out_arg [[maybe_unused]]) {
    g_free(gjs_arg_steal<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool CallbackIn::release(JSContext*, GjsFunctionCallState*, GIArgument* in_arg,
                         GIArgument* out_arg [[maybe_unused]]) {
    auto* closure = gjs_arg_get<ffi_closure*>(in_arg);
    if (!closure)
        return true;

    g_closure_unref(static_cast<GClosure*>(closure->user_data));
    // CallbackTrampolines are refcounted because for notified/async closures
    // it is possible to destroy it while in call, and therefore we cannot
    // check its scope at this point
    gjs_arg_unset<void*>(in_arg);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool StringInTransferNone::release(JSContext*, GjsFunctionCallState*,
                                   GIArgument* in_arg,
                                   GIArgument* out_arg [[maybe_unused]]) {
    g_free(gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool ForeignStructIn::release(JSContext* cx, GjsFunctionCallState* state,
                              GIArgument* in_arg,
                              GIArgument* out_arg [[maybe_unused]]) {
    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

    if (transfer == GI_TRANSFER_NOTHING)
        return gjs_struct_foreign_release_g_argument(cx, m_transfer, m_info,
                                                     in_arg);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool BoxedInTransferNone::release(JSContext*, GjsFunctionCallState*,
                                  GIArgument* in_arg,
                                  GIArgument* out_arg [[maybe_unused]]) {
    GType gtype = RegisteredType::gtype();
    g_assert(g_type_is_a(gtype, G_TYPE_BOXED));

    if (!gjs_arg_get<void*>(in_arg))
        return true;

    g_boxed_free(gtype, gjs_arg_get<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool GValueInTransferNone::release(JSContext* cx, GjsFunctionCallState* state,
                                   GIArgument* in_arg, GIArgument* out_arg) {
    if (state->ignore_release.erase(in_arg))
        return true;

    return BoxedInTransferNone::release(cx, state, in_arg, out_arg);
}

}  // namespace Arg

bool Argument::invalid(JSContext* cx, const char* func) const {
    gjs_throw(cx, "%s not implemented", func ? func : "Function");
    return false;
}

bool Argument::in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
                  JS::HandleValue) {
    return invalid(cx, G_STRFUNC);
}

bool Argument::out(JSContext* cx, GjsFunctionCallState*, GIArgument*,
                   JS::MutableHandleValue) {
    return invalid(cx, G_STRFUNC);
}

bool Argument::release(JSContext*, GjsFunctionCallState*, GIArgument*,
                       GIArgument*) {
    return true;
}

// This is a trick to print out the sizes of the structs at compile time, in
// an error message:
// template <int s> struct Measure;
// Measure<sizeof(Arg::)> arg_size;

#ifdef GJS_DO_ARGUMENTS_SIZE_CHECK
template <typename T>
constexpr size_t argument_maximum_size() {
    if constexpr (std::is_same_v<T, Arg::NumericIn>)
        return 24;
    // COMPAT: Work around cppcheck bug, fixed in cppcheck 2.6.
    // https://trac.cppcheck.net/ticket/10015
    if constexpr (std::is_same<T, Arg::ObjectIn>{} ||
                  std::is_same_v<T, Arg::BoxedIn>)
        return 40;
    else
        return 120;
}
#endif

template <typename T, Arg::Kind ArgKind, typename... Args>
std::unique_ptr<T> Argument::make(uint8_t index, const char* name,
                                  GITypeInfo* type_info, GITransfer transfer,
                                  GjsArgumentFlags flags, Args&&... args) {
#ifdef GJS_DO_ARGUMENTS_SIZE_CHECK
    static_assert(
        sizeof(T) <= argument_maximum_size<T>(),
        "Think very hard before increasing the size of Gjs::Arguments. "
        "One is allocated for every argument to every introspected function.");
#endif
    auto arg = std::make_unique<T>(args...);

    if constexpr (ArgKind == Arg::Kind::INSTANCE) {
        g_assert(index == Argument::ABSENT &&
                 "index was ignored in INSTANCE parameter");
        g_assert(name == nullptr && "name was ignored in INSTANCE parameter");
        arg->set_instance_parameter();
    } else if constexpr (ArgKind == Arg::Kind::RETURN_VALUE) {
        g_assert(index == Argument::ABSENT &&
                 "index was ignored in RETURN_VALUE parameter");
        g_assert(name == nullptr &&
                 "name was ignored in RETURN_VALUE parameter");
        arg->set_return_value();
    } else {
        if constexpr (std::is_base_of_v<Arg::Positioned, T>)
            arg->set_arg_pos(index);
        arg->m_arg_name = name;
    }

    arg->m_skip_in = (flags & GjsArgumentFlags::SKIP_IN);
    arg->m_skip_out = (flags & GjsArgumentFlags::SKIP_OUT);

    if constexpr (std::is_base_of_v<Arg::Nullable, T>)
        arg->m_nullable = (flags & GjsArgumentFlags::MAY_BE_NULL);

    if constexpr (std::is_base_of_v<Arg::Transferable, T>)
        arg->m_transfer = transfer;

    if constexpr (std::is_base_of_v<Arg::TypeInfo, T> &&
                  ArgKind != Arg::Kind::INSTANCE) {
        arg->m_type_info = std::move(*type_info);
    }

    return arg;
}

//  Needed for unique_ptr with incomplete type
ArgsCache::ArgsCache() = default;
ArgsCache::~ArgsCache() = default;

bool ArgsCache::initialize(JSContext* cx, GICallableInfo* callable) {
    if (!callable) {
        gjs_throw(cx, "Invalid callable provided");
        return false;
    }

    if (m_args) {
        gjs_throw(cx, "Arguments cache already initialized!");
        return false;
    }

    GITypeInfo type_info;
    g_callable_info_load_return_type(callable, &type_info);

    m_has_return = g_type_info_get_tag(&type_info) != GI_TYPE_TAG_VOID ||
                   g_type_info_is_pointer(&type_info);
    m_is_method = !!g_callable_info_is_method(callable);

    int size = g_callable_info_get_n_args(callable);
    size += (m_is_method ? 1 : 0);
    size += (m_has_return ? 1 : 0);

    if (size > Argument::MAX_ARGS) {
        gjs_throw(cx,
                  "Too many arguments, only %u are supported, while %d are "
                  "provided!",
                  Argument::MAX_ARGS, size);
        return false;
    }

    m_args = std::make_unique<Argument::UniquePtr[]>(size);
    return true;
}

void ArgsCache::clear() {
    m_args.reset();
}

template <typename T, Arg::Kind ArgKind, typename... Args>
T* ArgsCache::set_argument(uint8_t index, const char* name,
                           GITypeInfo* type_info, GITransfer transfer,
                           GjsArgumentFlags flags, Args&&... args) {
    std::unique_ptr<T> arg = Argument::make<T, ArgKind>(
        index, name, type_info, transfer, flags, args...);
    arg_get<ArgKind>(index) = std::move(arg);
    return static_cast<T*>(arg_get<ArgKind>(index).get());
}

template <typename T, Arg::Kind ArgKind, typename... Args>
T* ArgsCache::set_argument(uint8_t index, const char* name, GITransfer transfer,
                           GjsArgumentFlags flags, Args&&... args) {
    return set_argument<T, ArgKind>(index, name, nullptr, transfer, flags,
                                    args...);
}

template <typename T, Arg::Kind ArgKind, typename... Args>
T* ArgsCache::set_argument_auto(Args&&... args) {
    return set_argument<T, ArgKind>(std::forward<Args>(args)...);
}

template <typename T, Arg::Kind ArgKind, typename Tuple, typename... Args>
T* ArgsCache::set_argument_auto(Tuple&& tuple, Args&&... args) {
    // TODO(3v1n0): Would be nice to have a simple way to check we're handling a
    // tuple
    return std::apply(
        [&](auto&&... largs) {
            return set_argument<T, ArgKind>(largs...,
                                            std::forward<Args>(args)...);
        },
        tuple);
}

template <typename T>
T* ArgsCache::set_return(GITypeInfo* type_info, GITransfer transfer,
                         GjsArgumentFlags flags) {
    return set_argument<T, Arg::Kind::RETURN_VALUE>(Argument::ABSENT, nullptr,
                                                    type_info, transfer, flags);
}

template <typename T>
T* ArgsCache::set_instance(GITransfer transfer, GjsArgumentFlags flags) {
    return set_argument<T, Arg::Kind::INSTANCE>(Argument::ABSENT, nullptr,
                                                transfer, flags);
}

Argument* ArgsCache::instance() const {
    if (!m_is_method)
        return nullptr;

    return arg_get<Arg::Kind::INSTANCE>().get();
}

GType ArgsCache::instance_type() const {
    if (!m_is_method)
        return G_TYPE_NONE;

    return instance()->as_instance()->gtype();
}

Argument* ArgsCache::return_value() const {
    if (!m_has_return)
        return nullptr;

    return arg_get<Arg::Kind::RETURN_VALUE>().get();
}

GITypeInfo* ArgsCache::return_type() const {
    Argument* rval = return_value();
    if (!rval)
        return nullptr;

    return const_cast<GITypeInfo*>(rval->as_return_value()->type_info());
}

void ArgsCache::set_skip_all(uint8_t index, const char* name) {
    set_argument<Arg::SkipAll>(index, name, GI_TRANSFER_NOTHING,
                               GjsArgumentFlags::SKIP_ALL);
}

template <Arg::Kind ArgKind>
void ArgsCache::set_array_argument(GICallableInfo* callable, uint8_t gi_index,
                                   GITypeInfo* type_info, GIDirection direction,
                                   GIArgInfo* arg, GjsArgumentFlags flags,
                                   int length_pos) {
    Arg::ExplicitArray* array;
    GIArgInfo length_arg;
    g_callable_info_load_arg(callable, length_pos, &length_arg);
    GITypeInfo length_type;
    g_arg_info_load_type(&length_arg, &length_type);

    if constexpr (ArgKind == Arg::Kind::RETURN_VALUE) {
        GITransfer transfer = g_callable_info_get_caller_owns(callable);
        array = set_return<Arg::ReturnArray>(type_info, transfer,
                                             GjsArgumentFlags::NONE);
    } else {
        const char* arg_name = g_base_info_get_name(arg);
        GITransfer transfer = g_arg_info_get_ownership_transfer(arg);
        auto common_args =
            std::make_tuple(gi_index, arg_name, type_info, transfer, flags);

        if (direction == GI_DIRECTION_IN) {
            array = set_argument_auto<Arg::CArrayIn>(common_args);
            set_skip_all(length_pos, g_base_info_get_name(&length_arg));
        } else if (direction == GI_DIRECTION_INOUT) {
            array = set_argument_auto<Arg::CArrayInOut>(common_args);
            set_skip_all(length_pos, g_base_info_get_name(&length_arg));
        } else {
            array = set_argument_auto<Arg::CArrayOut>(common_args);
        }
    }

    if (ArgKind == Arg::Kind::RETURN_VALUE || direction == GI_DIRECTION_OUT) {
        // Even if we skip the length argument most of time, we need to
        // do some basic initialization here.
        set_argument<Arg::ArrayLengthOut>(
            length_pos, g_base_info_get_name(&length_arg), &length_type,
            GI_TRANSFER_NOTHING, flags | GjsArgumentFlags::SKIP_ALL);
    }

    array->set_array_length(length_pos, g_type_info_get_tag(&length_type));
}

void ArgsCache::build_return(GICallableInfo* callable, bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    if (!m_has_return) {
        *inc_counter_out = false;
        return;
    }

    GITypeInfo type_info;
    g_callable_info_load_return_type(callable, &type_info);
    GITransfer transfer = g_callable_info_get_caller_owns(callable);
    GITypeTag tag = g_type_info_get_tag(&type_info);

    *inc_counter_out = true;
    GjsArgumentFlags flags = GjsArgumentFlags::SKIP_IN;

    if (tag == GI_TYPE_TAG_ARRAY) {
        int length_pos = g_type_info_get_array_length(&type_info);
        if (length_pos >= 0) {
            set_array_argument<Arg::Kind::RETURN_VALUE>(
                callable, 0, &type_info, GI_DIRECTION_OUT, nullptr, flags,
                length_pos);
            return;
        }
    }

    // in() is ignored for the return value, but skip_in is not (it is used
    // in the failure release path)
    set_return<Arg::GenericReturn>(&type_info, transfer, flags);
}

namespace Arg {

Enum::Enum(GIEnumInfo* enum_info) {
    int64_t min = std::numeric_limits<int64_t>::max();
    int64_t max = std::numeric_limits<int64_t>::min();
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);

        if (value > max)
            max = value;
        if (value < min)
            min = value;
    }

    // From the docs for g_value_info_get_value(): "This will always be
    // representable as a 32-bit signed or unsigned value. The use of gint64 as
    // the return type is to allow both."
    // We stuff them both into unsigned 32-bit fields, and use a flag to tell
    // whether we have to compare them as signed.
    m_min = static_cast<uint32_t>(min);
    m_max = static_cast<uint32_t>(max);

    m_unsigned = (min >= 0 && max > std::numeric_limits<int32_t>::max());
}

Flags::Flags(GIEnumInfo* enum_info) {
    uint64_t mask = 0;
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GjsAutoValueInfo value_info = g_enum_info_get_value(enum_info, i);
        int64_t value = g_value_info_get_value(value_info);
        // From the docs for g_value_info_get_value(): "This will always be
        // representable as a 32-bit signed or unsigned value. The use of
        // gint64 as the return type is to allow both."
        // We stuff both into an unsigned, int-sized field, matching the
        // internal representation of flags in GLib (which uses guint).
        mask |= static_cast<unsigned>(value);
    }

    m_mask = mask;
}

}  // namespace Arg

namespace arg_cache {
[[nodiscard]] static inline bool is_gdk_atom(GIBaseInfo* info) {
    return strcmp("Atom", g_base_info_get_name(info)) == 0 &&
           strcmp("Gdk", g_base_info_get_namespace(info)) == 0;
}
}  // namespace arg_cache

template <Arg::Kind ArgKind>
void ArgsCache::build_interface_in_arg(uint8_t gi_index, GITypeInfo* type_info,
                                       GIBaseInfo* interface_info,
                                       GITransfer transfer, const char* name,
                                       GjsArgumentFlags flags) {
    GIInfoType interface_type = g_base_info_get_type(interface_info);

    auto base_args =
        std::make_tuple(gi_index, name, type_info, transfer, flags);
    auto common_args =
        std::tuple_cat(base_args, std::make_tuple(interface_info));

    // We do some transfer magic later, so let's ensure we don't mess up.
    // Should not happen in practice.
    if (G_UNLIKELY(transfer == GI_TRANSFER_CONTAINER)) {
        set_argument_auto<Arg::NotIntrospectable>(base_args,
                                                  INTERFACE_TRANSFER_CONTAINER);
        return;
    }

    switch (interface_type) {
        case GI_INFO_TYPE_ENUM:
            set_argument_auto<Arg::EnumIn, ArgKind>(common_args);
            return;

        case GI_INFO_TYPE_FLAGS:
            set_argument_auto<Arg::FlagsIn, ArgKind>(common_args);
            return;

        case GI_INFO_TYPE_STRUCT:
            if (g_struct_info_is_foreign(interface_info)) {
                if constexpr (ArgKind == Arg::Kind::INSTANCE)
                    set_argument_auto<Arg::ForeignStructInstanceIn, ArgKind>(
                        common_args);
                else
                    set_argument_auto<Arg::ForeignStructIn, ArgKind>(
                        common_args);
                return;
            }
            [[fallthrough]];
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_UNION: {
            GType gtype = g_registered_type_info_get_g_type(interface_info);

            if (interface_type == GI_INFO_TYPE_STRUCT && gtype == G_TYPE_NONE &&
                !g_struct_info_is_gtype_struct(interface_info)) {
                if constexpr (ArgKind != Arg::Kind::INSTANCE) {
                    // This covers cases such as GTypeInstance
                    set_argument_auto<Arg::FallbackInterfaceIn, ArgKind>(
                        common_args);
                    return;
                }
            }

            // Transfer handling is a bit complex here, because some of our in()
            // arguments know not to copy stuff if we don't need to.

            if (gtype == G_TYPE_VALUE) {
                if constexpr (ArgKind == Arg::Kind::INSTANCE)
                    set_argument_auto<Arg::BoxedIn, ArgKind>(common_args);
                else if (transfer == GI_TRANSFER_NOTHING)
                    set_argument_auto<Arg::GValueInTransferNone, ArgKind>(
                        common_args);
                else
                    set_argument_auto<Arg::GValueIn, ArgKind>(common_args);
                return;
            }

            if (arg_cache::is_gdk_atom(interface_info)) {
                // Fall back to the generic marshaller
                set_argument_auto<Arg::FallbackInterfaceIn, ArgKind>(
                    common_args);
                return;
            }

            if (gtype == G_TYPE_CLOSURE) {
                if (transfer == GI_TRANSFER_NOTHING &&
                    ArgKind != Arg::Kind::INSTANCE)
                    set_argument_auto<Arg::GClosureInTransferNone, ArgKind>(
                        common_args);
                else
                    set_argument_auto<Arg::GClosureIn, ArgKind>(common_args);
                return;
            }

            if (gtype == G_TYPE_BYTES) {
                if (transfer == GI_TRANSFER_NOTHING &&
                    ArgKind != Arg::Kind::INSTANCE)
                    set_argument_auto<Arg::GBytesInTransferNone, ArgKind>(
                        common_args);
                else
                    set_argument_auto<Arg::GBytesIn, ArgKind>(common_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                set_argument_auto<Arg::ObjectIn, ArgKind>(common_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                set_argument_auto<Arg::FallbackInterfaceIn, ArgKind>(
                    common_args);
                return;
            }

            if (interface_type == GI_INFO_TYPE_UNION) {
                if (gtype == G_TYPE_NONE) {
                    // Can't handle unions without a GType
                    set_argument_auto<Arg::NotIntrospectable>(
                        base_args, UNREGISTERED_UNION);
                    return;
                }

                set_argument_auto<Arg::UnionIn, ArgKind>(common_args);
                return;
            }

            if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                set_argument_auto<Arg::FundamentalIn, ArgKind>(common_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                set_argument_auto<Arg::InterfaceIn, ArgKind>(common_args);
                return;
            }

            // generic boxed type
            if (gtype == G_TYPE_NONE) {
                if (transfer != GI_TRANSFER_NOTHING) {
                    // Can't transfer ownership of a structure type not
                    // registered as a boxed
                    set_argument_auto<Arg::NotIntrospectable>(
                        base_args, UNREGISTERED_BOXED_WITH_TRANSFER);
                    return;
                }

                set_argument_auto<Arg::UnregisteredBoxedIn, ArgKind>(
                    common_args);
                return;
            }
            set_argument_auto<Arg::BoxedIn, ArgKind>(common_args);
            return;
        } break;

        case GI_INFO_TYPE_INVALID:
        case GI_INFO_TYPE_FUNCTION:
        case GI_INFO_TYPE_CALLBACK:
        case GI_INFO_TYPE_CONSTANT:
        case GI_INFO_TYPE_INVALID_0:
        case GI_INFO_TYPE_VALUE:
        case GI_INFO_TYPE_SIGNAL:
        case GI_INFO_TYPE_VFUNC:
        case GI_INFO_TYPE_PROPERTY:
        case GI_INFO_TYPE_FIELD:
        case GI_INFO_TYPE_ARG:
        case GI_INFO_TYPE_TYPE:
        case GI_INFO_TYPE_UNRESOLVED:
        default:
            // Don't know how to handle this interface type (should not happen
            // in practice, for typelibs emitted by g-ir-compiler)
            set_argument_auto<Arg::NotIntrospectable>(base_args,
                                                      UNSUPPORTED_TYPE);
    }
}

void ArgsCache::build_normal_in_arg(uint8_t gi_index, GITypeInfo* type_info,
                                    GIArgInfo* arg, GjsArgumentFlags flags) {
    // "Normal" in arguments are those arguments that don't require special
    // processing, and don't touch other arguments.
    // Main categories are:
    // - void*
    // - small numbers (fit in 32bit)
    // - big numbers (need a double)
    // - strings
    // - enums/flags (different from numbers in the way they're exposed in GI)
    // - objects (GObjects, boxed, unions, etc.)
    // - hashes
    // - sequences (null-terminated arrays, lists, etc.)

    const char* name = g_base_info_get_name(arg);
    GITransfer transfer = g_arg_info_get_ownership_transfer(arg);
    auto common_args =
        std::make_tuple(gi_index, name, type_info, transfer, flags);
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            set_argument_auto<Arg::NullIn>(common_args);
            break;

        case GI_TYPE_TAG_BOOLEAN:
            set_argument_auto<Arg::BooleanIn>(common_args);
            break;

        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
            set_argument_auto<Arg::NumericIn>(common_args, tag);
            break;

        case GI_TYPE_TAG_UNICHAR:
            set_argument_auto<Arg::UnicharIn>(common_args);
            break;

        case GI_TYPE_TAG_GTYPE:
            set_argument_auto<Arg::GTypeIn>(common_args);
            break;

        case GI_TYPE_TAG_FILENAME:
            if (transfer == GI_TRANSFER_NOTHING)
                set_argument_auto<Arg::FilenameInTransferNone>(common_args);
            else
                set_argument_auto<Arg::FilenameIn>(common_args);
            break;

        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING)
                set_argument_auto<Arg::StringInTransferNone>(common_args);
            else
                set_argument_auto<Arg::StringIn>(common_args);
            break;

        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(type_info);
            build_interface_in_arg(gi_index, type_info, interface_info,
                                   transfer, name, flags);
            return;
        }

        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
        default:
            // FIXME: Falling back to the generic marshaller
            set_argument_auto<Arg::FallbackIn>(common_args);
    }
}

void ArgsCache::build_instance(GICallableInfo* callable) {
    if (!m_is_method)
        return;

    GIBaseInfo* interface_info = g_base_info_get_container(callable);  // !owned

    GITransfer transfer =
        g_callable_info_get_instance_ownership_transfer(callable);

    // These cases could be covered by the generic marshaller, except that
    // there's no way to get GITypeInfo for a method's instance parameter.
    // Instead, special-case the arguments here that would otherwise go through
    // the generic marshaller.
    // See: https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/334
    GIInfoType info_type = g_base_info_get_type(interface_info);
    if (info_type == GI_INFO_TYPE_STRUCT &&
        g_struct_info_is_gtype_struct(interface_info)) {
        set_instance<Arg::GTypeStructInstanceIn>(transfer);
        return;
    }
    if (info_type == GI_INFO_TYPE_OBJECT) {
        GType gtype = g_registered_type_info_get_g_type(interface_info);

        if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            set_instance<Arg::ParamInstanceIn>(transfer);
            return;
        }
    }

    build_interface_in_arg<Arg::Kind::INSTANCE>(
        Argument::ABSENT, nullptr, interface_info, transfer, nullptr,
        GjsArgumentFlags::NONE);
}

void ArgsCache::build_arg(uint8_t gi_index, GIDirection direction,
                          GIArgInfo* arg, GICallableInfo* callable,
                          bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");
    GITypeInfo type_info;

    const char* arg_name = g_base_info_get_name(arg);
    g_arg_info_load_type(arg, &type_info);
    GITransfer transfer = g_arg_info_get_ownership_transfer(arg);

    GjsArgumentFlags flags = GjsArgumentFlags::NONE;
    if (g_arg_info_may_be_null(arg))
        flags |= GjsArgumentFlags::MAY_BE_NULL;
    if (g_arg_info_is_caller_allocates(arg))
        flags |= GjsArgumentFlags::CALLER_ALLOCATES;

    if (direction == GI_DIRECTION_IN)
        flags |= GjsArgumentFlags::SKIP_OUT;
    else if (direction == GI_DIRECTION_OUT)
        flags |= GjsArgumentFlags::SKIP_IN;
    *inc_counter_out = true;

    auto common_args =
        std::make_tuple(gi_index, arg_name, &type_info, transfer, flags);

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (direction == GI_DIRECTION_OUT &&
        (flags & GjsArgumentFlags::CALLER_ALLOCATES)) {
        if (type_tag != GI_TYPE_TAG_INTERFACE) {
            set_argument_auto<Arg::NotIntrospectable>(
                common_args, OUT_CALLER_ALLOCATES_NON_STRUCT);
            return;
        }

        GjsAutoBaseInfo interface_info = g_type_info_get_interface(&type_info);
        g_assert(interface_info);

        GIInfoType interface_type = g_base_info_get_type(interface_info);

        size_t size;
        if (interface_type == GI_INFO_TYPE_STRUCT) {
            size = g_struct_info_get_size(interface_info);
        } else if (interface_type == GI_INFO_TYPE_UNION) {
            size = g_union_info_get_size(interface_info);
        } else {
            set_argument_auto<Arg::NotIntrospectable>(
                common_args, OUT_CALLER_ALLOCATES_NON_STRUCT);
            return;
        }

        auto* gjs_arg = set_argument_auto<Arg::CallerAllocatesOut>(common_args);
        gjs_arg->m_allocates_size = size;

        return;
    }

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info = g_type_info_get_interface(&type_info);
        if (interface_info.type() == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                // Can't do callbacks for out or inout
                set_argument_auto<Arg::NotIntrospectable>(common_args,
                                                          CALLBACK_OUT);
                return;
            }

            if (strcmp(interface_info.name(), "DestroyNotify") == 0 &&
                strcmp(interface_info.ns(), "GLib") == 0) {
                // We don't know (yet) what to do with GDestroyNotify appearing
                // before a callback. If the callback comes later in the
                // argument list, then the invalid argument will be
                // overwritten with the 'skipped' one. If no callback follows,
                // then this is probably an unsupported function, so the
                // function invocation code will check this and throw.
                set_argument_auto<Arg::NotIntrospectable>(
                    common_args, DESTROY_NOTIFY_NO_CALLBACK);
                *inc_counter_out = false;
            } else {
                auto* gjs_arg =
                    set_argument_auto<Arg::CallbackIn>(common_args, &type_info);

                int destroy_pos = g_arg_info_get_destroy(arg);
                int closure_pos = g_arg_info_get_closure(arg);

                if (destroy_pos >= 0)
                    set_skip_all(destroy_pos);

                if (closure_pos >= 0)
                    set_skip_all(closure_pos);

                if (destroy_pos >= 0 && closure_pos < 0) {
                    set_argument_auto<Arg::NotIntrospectable>(
                        common_args, DESTROY_NOTIFY_NO_USER_DATA);
                    return;
                }

                gjs_arg->m_scope = g_arg_info_get_scope(arg);
                gjs_arg->set_callback_destroy_pos(destroy_pos);
                gjs_arg->set_callback_closure_pos(closure_pos);
            }

            return;
        }
    }

    if (type_tag == GI_TYPE_TAG_ARRAY &&
        g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
        int length_pos = g_type_info_get_array_length(&type_info);

        if (length_pos >= 0) {
            Argument* cached_length = argument(length_pos);
            bool skip_length = cached_length && !(cached_length->skip_in() &&
                                                  cached_length->skip_out());

            set_array_argument(callable, gi_index, &type_info, direction, arg,
                               flags, length_pos);

            if (length_pos < gi_index && skip_length) {
                // we already collected length_pos, remove it
                *inc_counter_out = false;
            }

            return;
        }
    }

    if (direction == GI_DIRECTION_IN)
        build_normal_in_arg(gi_index, &type_info, arg, flags);
    else if (direction == GI_DIRECTION_INOUT)
        set_argument_auto<Arg::FallbackInOut>(common_args);
    else
        set_argument_auto<Arg::FallbackOut>(common_args);

    return;
}

}  // namespace Gjs
