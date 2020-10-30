/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <inttypes.h>
#include <stddef.h>  // for size_t
#include <stdint.h>
#include <string.h>

#include <functional>  // for mem_fn
#include <limits>
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
#include <js/experimental/TypedData.h>
#include <jsapi.h>        // for InformalValueTypeName, JS_TypeOfValue
#include <jspubtd.h>      // for JSTYPE_FUNCTION
#include <mozilla/Maybe.h>

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
#include "gi/info.h"
#include "gi/js-value-inl.h"
#include "gi/object.h"
#include "gi/param.h"
#include "gi/union.h"
#include "gi/value.h"
#include "gi/wrapperutils.h"  // for GjsTypecheckNoThrow
#include "gjs/auto.h"
#include "gjs/byteArray.h"
#include "gjs/enum-utils.h"  // for operator&, operator|=, operator|
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

using mozilla::Maybe, mozilla::Nothing, mozilla::Some;

enum ExpectedType {
    OBJECT,
    FUNCTION,
    STRING,
    LAST,
};

static const char* expected_type_names[] = {"object", "function", "string"};
static_assert(G_N_ELEMENTS(expected_type_names) == ExpectedType::LAST,
              "Names must match the values in ExpectedType");

static constexpr void gjs_gi_argument_set_array_length(GITypeTag tag,
                                                       GIArgument* arg,
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

// Overload operator| so that Visual Studio won't complain
// when converting unsigned char to GjsArgumentFlags
GjsArgumentFlags operator|(
    GjsArgumentFlags const& v1, GjsArgumentFlags const& v2) {
    return static_cast<GjsArgumentFlags>(std::underlying_type<GjsArgumentFlags>::type(v1) |
                                         std::underlying_type<GjsArgumentFlags>::type(v2));
}

namespace Gjs {
namespace Arg {

// Arguments Interfaces:
//
// Each of these types are meant to be used to extend each Gjs::Argument
// implementation, taking advantage of the C++ multiple inheritance.

struct BasicType {
    constexpr explicit BasicType(GITypeTag tag) : m_tag(tag) {
        g_assert(GI_TYPE_TAG_IS_BASIC(tag));
    }

    GITypeTag m_tag : 5;
};

struct HasTypeInfo {
    explicit HasTypeInfo(GITypeInfo* type_info)
        : m_type_info(std::move(*type_info)) {}

    constexpr GITypeInfo* type_info() const {
        // Should be const GITypeInfo*, but G-I APIs won't accept that
        return const_cast<GITypeInfo*>(&m_type_info);
    }

    Maybe<ReturnTag> return_tag() const { return Some(ReturnTag{type_info()}); }

    GITypeInfo m_type_info{};
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

struct Positioned {
    void set_arg_pos(int pos) {
        g_assert(pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        m_arg_pos = pos;
    }

    constexpr bool set_out_parameter(GjsFunctionCallState* state,
                                     GIArgument* arg) {
        gjs_arg_unset(&state->out_cvalue(m_arg_pos));
        // The value passed to the function is actually the address of the out
        // C value
        gjs_arg_set(arg, &state->out_cvalue(m_arg_pos));
        return true;
    }

    constexpr bool set_inout_parameter(GjsFunctionCallState* state,
                                       GIArgument* arg) {
        state->out_cvalue(m_arg_pos) = state->inout_original_cvalue(m_arg_pos) =
            *arg;
        gjs_arg_set(arg, &state->out_cvalue(m_arg_pos));
        return true;
    }

    uint8_t m_arg_pos = 0;
};

struct ExplicitArray {
    uint8_t m_length_pos;
    GIDirection m_length_direction : 2;

    ExplicitArray(int pos, GIDirection direction)
        : m_length_pos(pos), m_length_direction(direction) {
        g_assert(pos >= 0 && pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
    }
};

struct BasicCArray {
    constexpr explicit BasicCArray(GITypeTag element_tag)
        : m_element_tag(element_tag) {
        g_assert(GI_TYPE_TAG_IS_BASIC(element_tag));
    }

    GITypeTag m_element_tag;
};

struct ZeroTerminatedArray {
    constexpr explicit ZeroTerminatedArray(GITypeInfo*) {}

    bool in(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
            const char* arg_name, GjsArgumentFlags flags,
            JS::HandleValue value) {
        return gjs_value_to_basic_array_gi_argument(
            cx, value, element_tag, GI_ARRAY_TYPE_C, arg, arg_name,
            GJS_ARGUMENT_ARGUMENT, flags);
    }

    bool out(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
             JS::MutableHandleValue value) {
        return gjs_array_from_basic_zero_terminated_array(
            cx, value, element_tag, gjs_arg_get<void*>(arg));
    }

    void release_container(GIArgument* arg) {
        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
    }

    void release_contents(GIArgument* arg) {
        char** array = gjs_arg_get<char**>(arg);
        for (size_t ix = 0; array[ix]; ix++)
            g_free(array[ix]);
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct GArrayContainer {
    constexpr explicit GArrayContainer(GITypeInfo*) {}

    bool in(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
            const char* arg_name, GjsArgumentFlags flags,
            JS::HandleValue value) {
        return gjs_value_to_basic_array_gi_argument(
            cx, value, element_tag, GI_ARRAY_TYPE_ARRAY, arg, arg_name,
            GJS_ARGUMENT_ARGUMENT, flags);
    }

    bool out(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
             JS::MutableHandleValue value_out) {
        return gjs_value_from_basic_garray_gi_argument(cx, value_out,
                                                       element_tag, arg);
    }

    void release_container(GIArgument* arg) {
        g_clear_pointer(&gjs_arg_member<GArray*>(arg), g_array_unref);
    }

    void release_contents(GIArgument* arg) {
        GArray* array = gjs_arg_get<GArray*>(arg);
        for (size_t ix = 0; ix < array->len; ix++)
            g_free(g_array_index(array, char*, ix));
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct GPtrArrayContainer {
    constexpr explicit GPtrArrayContainer(GITypeInfo*) {}

    bool in(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
            const char* arg_name, GjsArgumentFlags flags,
            JS::HandleValue value) {
        return gjs_value_to_basic_array_gi_argument(
            cx, value, element_tag, GI_ARRAY_TYPE_PTR_ARRAY, arg, arg_name,
            GJS_ARGUMENT_ARGUMENT, flags);
    }

    bool out(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
             JS::MutableHandleValue value_out) {
        return gjs_value_from_basic_gptrarray_gi_argument(cx, value_out,
                                                          element_tag, arg);
    }

    void release_container(GIArgument* arg) {
        g_clear_pointer(&gjs_arg_member<GPtrArray*>(arg), g_ptr_array_unref);
    }

    void release_contents(GIArgument* arg) {
        GPtrArray* array = gjs_arg_get<GPtrArray*>(arg);
        g_ptr_array_foreach(
            array, [](void* ptr, void*) { g_free(ptr); }, nullptr);
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct FixedSizeArray {
    explicit FixedSizeArray(GITypeInfo* type_info)
        : m_fixed_size(g_type_info_get_array_fixed_size(type_info)) {
        g_assert(m_fixed_size >= 0);
    }

    int m_fixed_size = -1;

    bool in(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
            const char* arg_name, GjsArgumentFlags flags,
            JS::HandleValue value) {
        return gjs_value_to_basic_array_gi_argument(
            cx, value, element_tag, GI_ARRAY_TYPE_C, arg, arg_name,
            GJS_ARGUMENT_ARGUMENT, flags);
    }

    bool out(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
             JS::MutableHandleValue value) {
        return gjs_value_from_basic_fixed_size_array_gi_argument(
            cx, value, element_tag, m_fixed_size, arg);
    }

    void release_container(GIArgument* arg) {
        g_clear_pointer(&gjs_arg_member<void*>(arg), g_free);
    }

    void release_contents(GIArgument* arg) {
        char** array = gjs_arg_get<char**>(arg);
        for (int ix = 0; ix < m_fixed_size; ix++)
            g_free(array[ix]);
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct GListContainer {
    explicit GListContainer(GITypeInfo* type_info)
        : m_double_link(g_type_info_get_tag(type_info) == GI_TYPE_TAG_GLIST) {}
    bool m_double_link : 1;

    bool in(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
            const char* arg_name, GjsArgumentFlags, JS::HandleValue value) {
        if (m_double_link) {
            return gjs_value_to_basic_glist_gi_argument(
                cx, value, element_tag, arg, arg_name, GJS_ARGUMENT_ARGUMENT);
        }
        return gjs_value_to_basic_gslist_gi_argument(
            cx, value, element_tag, arg, arg_name, GJS_ARGUMENT_ARGUMENT);
    }

    bool out(JSContext* cx, GITypeTag element_tag, GIArgument* arg,
             JS::MutableHandleValue value) {
        if (m_double_link)
            return gjs_array_from_basic_glist_gi_argument(cx, value,
                                                          element_tag, arg);
        return gjs_array_from_basic_gslist_gi_argument(cx, value, element_tag,
                                                       arg);
    }

    void release_container(GIArgument* arg) {
        if (m_double_link)
            g_clear_pointer(&gjs_arg_member<GList*>(arg), g_list_free);
        else
            g_clear_pointer(&gjs_arg_member<GSList*>(arg), g_slist_free);
    }

    void release_contents(GIArgument* arg) {
        GFunc free_gfunc = [](void* data, void*) { g_free(data); };

        if (m_double_link) {
            GList* list = gjs_arg_get<GList*>(arg);
            g_list_foreach(list, free_gfunc, nullptr);
        } else {
            GSList* list = gjs_arg_get<GSList*>(arg);
            g_slist_foreach(list, free_gfunc, nullptr);
        }
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{container_tag()});
    }
    constexpr GITypeTag container_tag() const {
        return m_double_link ? GI_TYPE_TAG_GLIST : GI_TYPE_TAG_GSLIST;
    }
};

struct GHashContainer {
    explicit GHashContainer(GITypeInfo* type_info)
        : m_value_tag(g_type_info_get_tag(
              GI::AutoTypeInfo{g_type_info_get_param_type(type_info, 1)})) {}
    constexpr GITypeTag value_tag() const { return m_value_tag; }

    // Key type is managed by the basic container
    GITypeTag m_value_tag;
};

struct HasIntrospectionInfo {
    constexpr explicit HasIntrospectionInfo(GIBaseInfo* info,
                                            const TakeOwnership& add_ref)
        : m_info(info, add_ref) {}
    constexpr explicit HasIntrospectionInfo(GIBaseInfo* info) : m_info(info) {}

    GI::AutoBaseInfo m_info;
};

// boxed / union / GObject
struct GTypedType {
    explicit GTypedType(GType gtype) : m_gtype(gtype) {}
    constexpr GType gtype() const { return m_gtype; }

 protected:
    GType m_gtype;
};

struct RegisteredType : GTypedType {
    RegisteredType(GType gtype, GIInfoType info_type)
        : GTypedType(gtype), m_info_type(info_type) {}
    explicit RegisteredType(GIRegisteredTypeInfo* info)
        : GTypedType(g_registered_type_info_get_g_type(info)),
          m_info_type(g_base_info_get_type(info)) {
        g_assert(m_gtype != G_TYPE_NONE &&
                 "Use RegisteredInterface for this type");
    }

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_INTERFACE, m_info_type, true});
    }

    GIInfoType m_info_type : 5;
};

struct RegisteredInterface : HasIntrospectionInfo, GTypedType {
    explicit RegisteredInterface(GIRegisteredTypeInfo* info)
        : HasIntrospectionInfo(info, TakeOwnership{}),
          GTypedType(g_registered_type_info_get_g_type(m_info)) {}

    Maybe<ReturnTag> return_tag() const {
        return Some(ReturnTag{GI_TYPE_TAG_INTERFACE,
                              g_base_info_get_type(m_info), true});
    }
};

struct Callback : Nullable, HasIntrospectionInfo {
    explicit Callback(GICallbackInfo* info, int closure_pos, int destroy_pos,
                      GIScopeType scope)
        : HasIntrospectionInfo(info, TakeOwnership{}),
          m_closure_pos(closure_pos < 0 ? Argument::ABSENT : closure_pos),
          m_destroy_pos(destroy_pos < 0 ? Argument::ABSENT : destroy_pos),
          m_scope(scope) {
        g_assert(destroy_pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
        g_assert(closure_pos <= Argument::MAX_ARGS &&
                 "No more than 253 arguments allowed");
    }

    [[nodiscard]] constexpr bool has_callback_destroy() {
        return m_destroy_pos != Argument::ABSENT;
    }

    [[nodiscard]] constexpr bool has_callback_closure() {
        return m_closure_pos != Argument::ABSENT;
    }

    uint8_t m_closure_pos;
    uint8_t m_destroy_pos;
    GIScopeType m_scope : 3;
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
    explicit CallerAllocates(size_t size) : m_allocates_size(size) {}
    size_t m_allocates_size;
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

struct Fallback : Transferable, HasTypeInfo {
    using HasTypeInfo::HasTypeInfo;
};

struct FallbackIn : SkipAll, Fallback, Nullable {
    using Fallback::Fallback;

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

struct FallbackInOut : SkipAll, Positioned, Fallback {
    using Fallback::Fallback;

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct FallbackOut : FallbackInOut {
    using FallbackInOut::FallbackInOut;

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct FallbackReturn : FallbackOut {
    using FallbackOut::FallbackOut;
    // No in!
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }

    Maybe<ReturnTag> return_tag() const override {
        return HasTypeInfo::return_tag();
    }
};

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
struct NumericOut : SkipAll, Positioned {
    static_assert(std::is_arithmetic_v<T>, "Not arithmetic type");
    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return Gjs::c_value_to_js_checked<T, TAG>(cx, gjs_arg_get<T, TAG>(arg),
                                                  value);
    }
};

using BooleanOut = NumericOut<gboolean, GI_TYPE_TAG_BOOLEAN>;

template <typename T, GITypeTag TAG>
struct NumericReturn : SkipAll {
    static_assert(std::is_arithmetic_v<T>, "Not arithmetic type");
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return Gjs::c_value_to_js_checked<T, TAG>(cx, gjs_arg_get<T, TAG>(arg),
                                                  value);
    }
    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{TAG});
    }
};

using BooleanReturn = NumericReturn<gboolean, GI_TYPE_TAG_BOOLEAN>;

struct SimpleOut : SkipAll, Positioned {
    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    };
};

struct BasicTypeReturn : SkipAll, BasicType {
    using BasicType::BasicType;

    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return gjs_value_from_basic_gi_argument(cx, value, m_tag, arg);
    }
    bool release(JSContext*, GjsFunctionCallState*,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg) override {
        gjs_gi_argument_release_basic(GI_TRANSFER_NOTHING, m_tag,
                                      Argument::flags(), out_arg);
        return true;
    }

    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{m_tag});
    }
};

struct BasicTypeOut : BasicTypeReturn, Positioned {
    using BasicTypeReturn::BasicTypeReturn;

    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
};

struct BasicTypeInOut : BasicTypeOut {
    using BasicTypeOut::BasicTypeOut;
    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        if (!gjs_value_to_basic_gi_argument(cx, value, m_tag, arg, arg_name(),
                                            GJS_ARGUMENT_ARGUMENT, flags()))
            return false;

        return set_inout_parameter(state, arg);
    }
    bool release(JSContext*, GjsFunctionCallState* state,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg [[maybe_unused]]) override {
        GIArgument* original_out_arg =
            &(state->inout_original_cvalue(m_arg_pos));
        gjs_gi_argument_release_basic(GI_TRANSFER_NOTHING, m_tag, flags(),
                                      original_out_arg);
        return true;
    }
};

struct BasicTypeTransferableReturn : BasicTypeReturn, Transferable {
    using BasicTypeReturn::BasicTypeReturn;
    bool release(JSContext*, GjsFunctionCallState*,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg) override {
        gjs_gi_argument_release_basic(m_transfer, m_tag, Argument::flags(),
                                      out_arg);
        return true;
    }
};

struct BasicTypeTransferableOut : BasicTypeTransferableReturn, Positioned {
    using BasicTypeTransferableReturn::BasicTypeTransferableReturn;

    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
};

struct BasicTypeTransferableInOut : BasicTypeInOut, Transferable {
    using BasicTypeInOut::BasicTypeInOut;

    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg) override {
        if (!BasicTypeInOut::release(cx, state, in_arg, out_arg))
            return false;

        if (m_transfer != GI_TRANSFER_NOTHING)
            gjs_gi_argument_release_basic(m_transfer, m_tag, flags(), out_arg);

        return true;
    }
};

struct ErrorIn : SkipAll, Transferable, Nullable {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_gerror_gi_argument(cx, value, m_transfer, arg,
                                               m_arg_name,
                                               GJS_ARGUMENT_ARGUMENT, flags());
    }

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

// The tag is normally used in containers for the element type, but for explicit
// arrays we use it for the length argument type. Specific implementations can
// override this
struct BasicTypeContainerReturn : BasicTypeTransferableReturn, Nullable {
    explicit BasicTypeContainerReturn(GITypeTag element_tag)
        : BasicTypeTransferableReturn(element_tag) {}

    explicit BasicTypeContainerReturn(GITypeInfo* type_info)
        : BasicTypeContainerReturn(g_type_info_get_tag(
              GI::AutoTypeInfo{g_type_info_get_param_type(type_info, 0)})) {}

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
    Maybe<ReturnTag> return_tag() const override {
        // in Return subclasses, this must be overridden with the container tag
        g_return_val_if_reached(Nothing{});
    }
    constexpr GITypeTag element_tag() { return m_tag; }
};

struct BasicTypeContainerOut : BasicTypeContainerReturn, Positioned {
    using BasicTypeContainerReturn::BasicTypeContainerReturn;
};

struct BasicTypeContainerIn : BasicTypeContainerReturn {
    using BasicTypeContainerReturn::BasicTypeContainerReturn;

    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }
};

template <class Marshaller, class Container>
struct BasicTypeContainer : Marshaller, Container {
    explicit BasicTypeContainer(GITypeInfo* type_info)
        : Marshaller(type_info), Container(type_info) {}

    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerIn>) {
            return Container::in(cx, Marshaller::element_tag(), arg,
                                 Marshaller::arg_name(), Marshaller::flags(),
                                 value);
        }
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerReturn>)
            return Marshaller::invalid(cx, G_STRFUNC);
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerOut>)
            return Marshaller::set_out_parameter(state, arg);
        g_return_val_if_reached(false);
    }

    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerIn>)
            return Marshaller::skip();
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerReturn> ||
                      std::is_same_v<Marshaller, BasicTypeContainerOut>) {
            return Container::out(cx, Marshaller::element_tag(), arg, value);
        }
        g_return_val_if_reached(false);
    }
    bool release(JSContext*, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg) override {
        GITransfer transfer = Marshaller::m_transfer;
        GITypeTag element_tag = Marshaller::element_tag();

        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerIn>) {
            if (!state->call_completed())
                transfer = GI_TRANSFER_NOTHING;

            if (!gjs_arg_get<void*>(in_arg) ||
                transfer == GI_TRANSFER_EVERYTHING)
                return true;

            if (Gjs::basic_type_needs_release(element_tag))
                Container::release_contents(in_arg);
            if (transfer != GI_TRANSFER_CONTAINER)
                Container::release_container(in_arg);

            return true;
        }
        if constexpr (std::is_same_v<Marshaller, BasicTypeContainerReturn> ||
                      std::is_same_v<Marshaller, BasicTypeContainerOut>) {
            if (!gjs_arg_get<void*>(out_arg) || transfer == GI_TRANSFER_NOTHING)
                return true;

            if (Gjs::basic_type_needs_release(element_tag) &&
                transfer != GI_TRANSFER_CONTAINER)
                Container::release_contents(out_arg);
            Container::release_container(out_arg);

            return true;
        }
        g_return_val_if_reached(false);
    }

    Maybe<ReturnTag> return_tag() const override {
        return Container::return_tag();
    }
};

using BasicGListReturn =
    BasicTypeContainer<BasicTypeContainerReturn, GListContainer>;
using BasicGListIn = BasicTypeContainer<BasicTypeContainerIn, GListContainer>;
using BasicGListOut = BasicTypeContainer<BasicTypeContainerOut, GListContainer>;

using BasicCZeroTerminatedArrayReturn =
    BasicTypeContainer<BasicTypeContainerReturn, ZeroTerminatedArray>;
using BasicCZeroTerminatedArrayIn =
    BasicTypeContainer<BasicTypeContainerIn, ZeroTerminatedArray>;
using BasicCZeroTerminatedArrayOut =
    BasicTypeContainer<BasicTypeContainerOut, ZeroTerminatedArray>;

using BasicCFixedSizeArrayReturn =
    BasicTypeContainer<BasicTypeContainerReturn, FixedSizeArray>;
using BasicCFixedSizeArrayIn =
    BasicTypeContainer<BasicTypeContainerIn, FixedSizeArray>;
using BasicCFixedSizeArrayOut =
    BasicTypeContainer<BasicTypeContainerOut, FixedSizeArray>;

using BasicGArrayReturn =
    BasicTypeContainer<BasicTypeContainerReturn, GArrayContainer>;
using BasicGArrayIn = BasicTypeContainer<BasicTypeContainerIn, GArrayContainer>;
using BasicGArrayOut =
    BasicTypeContainer<BasicTypeContainerOut, GArrayContainer>;

using BasicGPtrArrayReturn =
    BasicTypeContainer<BasicTypeContainerReturn, GPtrArrayContainer>;
using BasicGPtrArrayIn =
    BasicTypeContainer<BasicTypeContainerIn, GPtrArrayContainer>;
using BasicGPtrArrayOut =
    BasicTypeContainer<BasicTypeContainerOut, GPtrArrayContainer>;

struct BasicGHashReturn : BasicTypeTransferableReturn,
                          GHashContainer,
                          Nullable {
    explicit BasicGHashReturn(GITypeInfo* type_info)
        : BasicTypeTransferableReturn(g_type_info_get_tag(
              GI::AutoTypeInfo{g_type_info_get_param_type(type_info, 0)})),
          GHashContainer(type_info) {
        g_assert(GI_TYPE_TAG_IS_BASIC(m_value_tag));
    }

    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return gjs_value_from_basic_ghash(cx, value, m_tag, m_value_tag,
                                          gjs_arg_get<GHashTable*>(arg));
    }
    bool release(JSContext*, GjsFunctionCallState*,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg) override {
        if (m_transfer == GI_TRANSFER_NOTHING)
            return true;

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION,
            "Releasing GIArgument ghash out param or return value");

        gjs_gi_argument_release_basic_ghash(m_transfer, m_tag, m_value_tag,
                                            out_arg);
        return true;
    }

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{GI_TYPE_TAG_GHASH});
    }
};

struct BasicGHashIn : BasicGHashReturn {
    using BasicGHashReturn::BasicGHashReturn;

    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_basic_ghash_gi_argument(
            cx, value, m_tag, m_value_tag, m_transfer, arg, m_arg_name,
            GJS_ARGUMENT_ARGUMENT, flags());
    }
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }
    bool release(JSContext*, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg [[maybe_unused]]) override {
        // GI_TRANSFER_EVERYTHING: we don't own the argument anymore.
        // GI_TRANSFER_CONTAINER: See FIXME in gjs_array_to_g_list(); currently
        //   an error and we won't get here.
        if (!state->call_completed() || m_transfer != GI_TRANSFER_NOTHING)
            return true;

        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "Releasing GIArgument ghash in param");

        gjs_gi_argument_release_basic_ghash(m_transfer, m_tag, m_value_tag,
                                            in_arg);
        return true;
    }
};

struct BasicGHashOut : BasicGHashReturn, Positioned {
    using BasicGHashReturn::BasicGHashReturn;

    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
};

struct ByteArrayReturn : SkipAll, Transferable {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return invalid(cx, G_STRFUNC);
    }
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return gjs_value_from_byte_array_gi_argument(cx, value, arg);
    }
    bool release(JSContext*, GjsFunctionCallState*,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg) override {
        if (m_transfer != GI_TRANSFER_NOTHING)
            gjs_gi_argument_release_byte_array(out_arg);
        return true;
    }

    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct ByteArrayIn : SkipAll, Transferable {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_byte_array_gi_argument(cx, value, arg, arg_name(),
                                                   flags());
    }
    bool release(JSContext*, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg [[maybe_unused]]) override {
        if (!state->call_completed() || m_transfer != GI_TRANSFER_NOTHING)
            return true;

        gjs_gi_argument_release_byte_array(in_arg);
        return true;
    }
};

struct ByteArrayOut : ByteArrayReturn, Positioned {
    using ByteArrayReturn::ByteArrayReturn;

    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
};

struct ExplicitArrayBase : BasicTypeContainerReturn, ExplicitArray {
    ExplicitArrayBase(int length_pos, GITypeTag length_tag,
                      GIDirection length_direction)
        : BasicTypeContainerReturn(length_tag),
          ExplicitArray(length_pos, length_direction) {}
};

struct CArrayIn : ExplicitArrayBase, HasTypeInfo {
    CArrayIn(GITypeInfo* type_info, int length_pos, GITypeTag length_tag,
             GIDirection length_direction)
        : ExplicitArrayBase(length_pos, length_tag, length_direction),
          HasTypeInfo(type_info) {}

    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    };
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

// Positioned must come before HasTypeInfo for struct packing reasons, otherwise
// this could inherit from CArrayIn
struct CArrayInOut : ExplicitArrayBase, Positioned, HasTypeInfo {
    CArrayInOut(GITypeInfo* type_info, int length_pos, GITypeTag length_tag,
                GIDirection length_direction)
        : ExplicitArrayBase(length_pos, length_tag, length_direction),
          HasTypeInfo(type_info) {}
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct CArrayOut : CArrayInOut {
    using CArrayInOut::CArrayInOut;

    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        if (m_length_direction != GI_DIRECTION_OUT) {
            gjs_throw(cx,
                      "Using different length argument direction for array %s"
                      "is not supported for out arrays",
                      m_arg_name);
            return false;
        }
        return set_out_parameter(state, arg);
    };
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

using ArrayLengthOut = SimpleOut;

struct NotIntrospectable : SkipAll {
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

    Maybe<const Instance*> as_instance() const override { return Some(this); }

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

struct FallbackInterfaceIn : RegisteredInterfaceIn {
    using RegisteredInterfaceIn::RegisteredInterfaceIn;

    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_interface_gi_argument(
            cx, value, m_info, m_transfer, arg, m_arg_name,
            GJS_ARGUMENT_ARGUMENT, flags());
    }
};

struct GdkAtomIn : NullableIn {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
            JS::HandleValue value) override {
        return gjs_value_to_gdk_atom_gi_argument(cx, value, arg, m_arg_name,
                                                 GJS_ARGUMENT_ARGUMENT);
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

struct UnregisteredBoxedIn : BoxedIn, HasIntrospectionInfo {
    explicit UnregisteredBoxedIn(GIStructInfo* info)
        : BoxedIn(g_registered_type_info_get_g_type(info),
                  g_base_info_get_type(info)),
          HasIntrospectionInfo(info, TakeOwnership{}) {}
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
    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg) override;
};

struct GBytesInTransferNone : GBytesIn {
    using GBytesIn::GBytesIn;
    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument* out_arg) override {
        return BoxedInTransferNone::release(cx, state, in_arg, out_arg);
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

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
struct NumericIn : SkipAll {
    static_assert(std::is_arithmetic_v<T>, "Not arithmetic type");
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
struct NumericInOut : NumericIn<T, TAG>, Positioned {
    static_assert(std::is_arithmetic_v<T>, "Not arithmetic type");
    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        if (!NumericIn<T, TAG>::in(cx, state, arg, value))
            return false;

        return set_inout_parameter(state, arg);
    }
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return Gjs::c_value_to_js_checked<T, TAG>(cx, gjs_arg_get<T, TAG>(arg),
                                                  value);
    }
};

using BooleanInOut = NumericInOut<gboolean, GI_TYPE_TAG_BOOLEAN>;

struct UnicharIn : SkipAll {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

struct GTypeIn : SkipAll {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
};

template <GITypeTag TAG = GI_TYPE_TAG_UTF8>
struct StringInTransferNone : NullableIn {
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct StringIn : StringInTransferNone<GI_TYPE_TAG_UTF8> {
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override {
        return skip();
    }
};

template <GITransfer TRANSFER = GI_TRANSFER_NOTHING>
struct StringOutBase : SkipAll {
    bool out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
             JS::MutableHandleValue value) override {
        return Gjs::c_value_to_js(cx, gjs_arg_get<char*>(arg), value);
    }
    bool release(JSContext* cx, GjsFunctionCallState*, GIArgument*,
                 GIArgument* out_arg [[maybe_unused]]) override {
        if constexpr (TRANSFER == GI_TRANSFER_NOTHING) {
            return skip();
        } else if constexpr (TRANSFER == GI_TRANSFER_EVERYTHING) {
            g_clear_pointer(&gjs_arg_member<char*>(out_arg), g_free);
            return true;
        } else {
            return invalid(cx, G_STRFUNC);
        }
    }
};

template <GITransfer TRANSFER = GI_TRANSFER_NOTHING>
struct StringReturn : StringOutBase<TRANSFER> {
    bool in(JSContext* cx, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override {
        return Argument::invalid(cx, G_STRFUNC);
    }

    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{GI_TYPE_TAG_UTF8});
    }
};

template <GITransfer TRANSFER = GI_TRANSFER_NOTHING>
struct StringOut : StringOutBase<TRANSFER>, Positioned {
    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    }
};

using FilenameInTransferNone = StringInTransferNone<GI_TYPE_TAG_FILENAME>;

struct FilenameIn : FilenameInTransferNone {
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
 private:
    ffi_closure *m_ffi_closure;
};

struct BasicExplicitCArrayOut : ExplicitArrayBase, BasicCArray, Positioned {
    explicit BasicExplicitCArrayOut(GITypeTag element_tag, int length_pos,
                                    GITypeTag length_tag,
                                    GIDirection length_direction)
        : ExplicitArrayBase(length_pos, length_tag, length_direction),
          BasicCArray(element_tag) {}

    bool in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue) override {
        return set_out_parameter(state, arg);
    };
    bool out(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
             JS::MutableHandleValue value) override {
        GIArgument* length_arg = &(state->out_cvalue(m_length_pos));
        size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

        return gjs_value_from_basic_explicit_array(cx, value, m_element_tag,
                                                   arg, length);
    }
    bool release(JSContext*, GjsFunctionCallState* state,
                 [[maybe_unused]] GIArgument* in_arg,
                 GIArgument* out_arg) override {
        GIArgument* length_arg = &state->out_cvalue(m_length_pos);
        size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

        gjs_gi_argument_release_basic_out_array(m_transfer, m_element_tag,
                                                length, out_arg);
        return true;
    }

    Maybe<ReturnTag> return_tag() const override {
        return Some(ReturnTag{GI_TYPE_TAG_ARRAY});
    }
};

struct BasicExplicitCArrayIn : BasicExplicitCArrayOut {
    using BasicExplicitCArrayOut::BasicExplicitCArrayOut;

    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        void* data;
        size_t length;

        if (!gjs_array_to_basic_explicit_array(
                cx, value, m_element_tag, m_arg_name, GJS_ARGUMENT_ARGUMENT,
                flags(), &data, &length))
            return false;

        gjs_gi_argument_set_array_length(m_tag, &state->in_cvalue(m_length_pos),
                                         length);
        gjs_arg_set(arg, data);
        return true;
    }
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }
    bool release(JSContext*, GjsFunctionCallState* state, GIArgument* in_arg,
                 [[maybe_unused]] GIArgument* out_arg) override {
        GIArgument* length_arg = &state->in_cvalue(m_length_pos);
        size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

        GITransfer transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

        gjs_gi_argument_release_basic_in_array(transfer, m_element_tag, length,
                                               in_arg);
        return true;
    }
};

struct BasicExplicitCArrayInOut : BasicExplicitCArrayIn {
    using BasicExplicitCArrayIn::BasicExplicitCArrayIn;

    bool in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
            JS::HandleValue value) override {
        if (!BasicExplicitCArrayIn::in(cx, state, arg, value))
            return false;

        if (!gjs_arg_get<void*>(arg)) {
            // Special case where we were given JS null to also pass null for
            // length, and not a pointer to an integer that derefs to 0.
            gjs_arg_unset(&state->in_cvalue(m_length_pos));
            gjs_arg_unset(&state->out_cvalue(m_length_pos));
            gjs_arg_unset(&state->inout_original_cvalue(m_length_pos));

            gjs_arg_unset(&state->out_cvalue(m_arg_pos));
            gjs_arg_unset(&state->inout_original_cvalue(m_arg_pos));

            return true;
        }

        state->out_cvalue(m_length_pos) =
            state->inout_original_cvalue(m_length_pos) =
                state->in_cvalue(m_length_pos);
        gjs_arg_set(&state->in_cvalue(m_length_pos),
                    &state->out_cvalue(m_length_pos));
        return set_inout_parameter(state, arg);
    }
    bool out(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
             JS::MutableHandleValue value) override {
        GIArgument* length_arg = &(state->out_cvalue(m_length_pos));
        size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

        return gjs_value_from_basic_explicit_array(cx, value, m_element_tag,
                                                   arg, length);
    }
    bool release(JSContext*, GjsFunctionCallState* state,
                 GIArgument* in_arg [[maybe_unused]],
                 GIArgument* out_arg) override {
        GIArgument* length_arg = &state->out_cvalue(m_length_pos);
        size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

        GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
        if (gjs_arg_get<void*>(original_out_arg) !=
            gjs_arg_get<void*>(out_arg)) {
            GITransfer transfer =
                state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
            gjs_gi_argument_release_basic_in_array(transfer, m_element_tag,
                                                   length, original_out_arg);
        }

        gjs_gi_argument_release_basic_out_array(m_transfer, m_element_tag,
                                                length, out_arg);
        return true;
    }
};

struct CallerAllocatesOut : FallbackOut, CallerAllocates {
    CallerAllocatesOut(GITypeInfo* type_info, size_t size)
        : FallbackOut(type_info), CallerAllocates(size) {}
    bool in(JSContext*, GjsFunctionCallState*, GIArgument*,
            JS::HandleValue) override;
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;

    GjsArgumentFlags flags() const override {
        return FallbackOut::flags() | GjsArgumentFlags::CALLER_ALLOCATES;
    }
};

struct BoxedCallerAllocatesOut : CallerAllocatesOut, GTypedType {
    BoxedCallerAllocatesOut(GITypeInfo* type_info, size_t size, GType gtype)
        : CallerAllocatesOut(type_info, size), GTypedType(gtype) {}
    bool release(JSContext*, GjsFunctionCallState*, GIArgument*,
                 GIArgument*) override;
};

struct ZeroTerminatedArrayInOut : FallbackInOut {
    using FallbackInOut::FallbackInOut;
    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument*,
                 GIArgument* out_arg) override {
        GITransfer transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
        GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
        if (!gjs_gi_argument_release_in_array(cx, transfer, &m_type_info,
                                              original_out_arg))
            return false;

        transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_EVERYTHING;
        return gjs_gi_argument_release_out_array(cx, transfer, &m_type_info,
                                                 out_arg);
    }
};

struct ZeroTerminatedArrayIn : FallbackIn {
    using FallbackIn::FallbackIn;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }

    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument*) override {
        GITransfer transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

        return gjs_gi_argument_release_in_array(cx, transfer, &m_type_info,
                                                in_arg);
    }

    GjsArgumentFlags flags() const override {
        return Argument::flags() | Nullable::flags();
    }
};

struct FixedSizeArrayIn : FallbackIn {
    using FallbackIn::FallbackIn;
    bool out(JSContext*, GjsFunctionCallState*, GIArgument*,
             JS::MutableHandleValue) override {
        return skip();
    }

    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument* in_arg,
                 GIArgument*) override {
        GITransfer transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

        int size = g_type_info_get_array_fixed_size(&m_type_info);
        return gjs_gi_argument_release_in_array(cx, transfer, &m_type_info,
                                                size, in_arg);
    }
};

struct FixedSizeArrayInOut : FallbackInOut {
    using FallbackInOut::FallbackInOut;
    bool release(JSContext* cx, GjsFunctionCallState* state, GIArgument*,
                 GIArgument* out_arg) override {
        GITransfer transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
        GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
        int size = g_type_info_get_array_fixed_size(&m_type_info);
        if (!gjs_gi_argument_release_in_array(cx, transfer, &m_type_info, size,
                                              original_out_arg))
            return false;

        transfer =
            state->call_completed() ? m_transfer : GI_TRANSFER_EVERYTHING;
        return gjs_gi_argument_release_out_array(cx, transfer, &m_type_info,
                                                 size, out_arg);
    }
};

GJS_JSAPI_RETURN_CONVENTION
bool NotIntrospectable::in(JSContext* cx, GjsFunctionCallState* state,
                           GIArgument*, JS::HandleValue) {
    const char* reason_string = "invalid introspection";

    switch (m_reason) {
        case CALLBACK_OUT:
            reason_string = "callback out-argument";
            break;
        case DESTROY_NOTIFY_NO_CALLBACK:
            reason_string = "DestroyNotify argument with no callback";
            break;
        case DESTROY_NOTIFY_NO_USER_DATA:
            reason_string = "DestroyNotify argument with no user data";
            break;
        case INTERFACE_TRANSFER_CONTAINER:
            reason_string = "type not supported for (transfer container)";
            break;
        case OUT_CALLER_ALLOCATES_NON_STRUCT:
            reason_string = "type not supported for (out caller-allocates)";
            break;
        case UNREGISTERED_BOXED_WITH_TRANSFER:
            reason_string =
                "boxed type with transfer not registered as a GType";
            break;
        case UNREGISTERED_UNION:
            reason_string = "union type not registered as a GType";
            break;
        case UNSUPPORTED_TYPE:
            reason_string = "type not supported by introspection";
            break;
        case LAST_REASON:
            g_assert_not_reached();
    }

    gjs_throw(cx,
              "Function %s() cannot be called: argument '%s' is not "
              "introspectable because it has a %s",
              state->display_name().get(), m_arg_name, reason_string);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackIn::in(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                    JS::HandleValue value) {
    return gjs_value_to_gi_argument(cx, value, &m_type_info, m_arg_name,
                                    GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                    arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackInOut::in(JSContext* cx, GjsFunctionCallState* state,
                       GIArgument* arg, JS::HandleValue value) {
    return gjs_value_to_gi_argument(cx, value, &m_type_info, m_arg_name,
                                    GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                    arg) &&
           set_inout_parameter(state, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayIn::in(JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
                  JS::HandleValue value) {
    void* data;
    size_t length;

    if (m_length_direction != GI_DIRECTION_IN) {
        gjs_throw(cx,
                  "Using different length argument direction for array %s is "
                  "not supported for in arrays",
                  m_arg_name);
        return false;
    }

    if (!gjs_array_to_explicit_array(cx, value, &m_type_info, m_arg_name,
                                     GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                     &data, &length))
        return false;

    gjs_gi_argument_set_array_length(m_tag, &state->in_cvalue(m_length_pos),
                                     length);
    gjs_arg_set(arg, data);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayInOut::in(JSContext* cx, GjsFunctionCallState* state,
                     GIArgument* arg, JS::HandleValue value) {
    if (m_length_direction != GI_DIRECTION_INOUT) {
        gjs_throw(cx,
                  "Using different length argument direction for array %s is "
                  "not supported for inout arrays",
                  m_arg_name);
        return false;
    }

    void* data;
    size_t length;
    if (!gjs_array_to_explicit_array(cx, value, type_info(), m_arg_name,
                                     GJS_ARGUMENT_ARGUMENT, m_transfer, flags(),
                                     &data, &length))
        return false;

    gjs_gi_argument_set_array_length(m_tag, &state->in_cvalue(m_length_pos),
                                     length);
    gjs_arg_set(arg, data);

    uint8_t length_pos = m_length_pos;
    uint8_t ix = m_arg_pos;

    if (!gjs_arg_get<void*>(arg)) {
        // Special case where we were given JS null to also pass null for
        // length, and not a pointer to an integer that derefs to 0.
        gjs_arg_unset(&state->in_cvalue(length_pos));
        gjs_arg_unset(&state->out_cvalue(length_pos));
        gjs_arg_unset(&state->inout_original_cvalue(length_pos));

        gjs_arg_unset(&state->out_cvalue(ix));
        gjs_arg_unset(&state->inout_original_cvalue(ix));
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
    void* closure;

    if (value.isNull() && m_nullable) {
        closure = nullptr;
        trampoline = nullptr;
        m_ffi_closure = nullptr;
    } else {
        if (JS_TypeOfValue(cx, value) != JSTYPE_FUNCTION) {
            gjs_throw(cx, "Expected function for callback argument %s, got %s",
                      m_arg_name, JS::InformalValueTypeName(value));
            return false;
        }

        JS::RootedObject callable(cx, &value.toObject());
        bool is_object_method = !!state->instance_object;
        trampoline = GjsCallbackTrampoline::create(
            cx, callable, m_info, m_scope, is_object_method, false);
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
        m_ffi_closure = trampoline->get_ffi_closure();
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
        // async calls
        g_closure_ref(trampoline);
    }

    bool keep_forever =
        !has_callback_destroy() &&
        (m_scope == GI_SCOPE_TYPE_FOREVER || m_scope == GI_SCOPE_TYPE_NOTIFIED);

    if (trampoline && keep_forever) {
        trampoline->mark_forever();
    }
    gjs_arg_set(arg, closure);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackOut::in(JSContext*, GjsFunctionCallState* state, GIArgument* arg,
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

template <typename T, GITypeTag TAG>
GJS_JSAPI_RETURN_CONVENTION bool NumericIn<T, TAG>::in(JSContext* cx,
                                                       GjsFunctionCallState*,
                                                       GIArgument* arg,
                                                       JS::HandleValue value) {
    bool out_of_range = false;

    if (!gjs_arg_set_from_js_value<T, TAG>(cx, value, arg, &out_of_range)) {
        if (out_of_range) {
            gjs_throw(cx, "Argument %s: value is out of range for %s",
                      arg_name(), Gjs::static_type_name<T, TAG>());
        }

        return false;
    }

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "%s set to value %s (type %s)",
                      Gjs::AutoChar{gjs_argument_display_name(
                                        arg_name(), GJS_ARGUMENT_ARGUMENT)}
                          .get(),
                      std::to_string(gjs_arg_get<T, TAG>(arg)).c_str(),
                      Gjs::static_type_name<T, TAG>());

    return true;
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
    gjs_arg_unset(arg);
    return true;
}

template <GITypeTag TAG>
GJS_JSAPI_RETURN_CONVENTION bool StringInTransferNone<TAG>::in(
    JSContext* cx, GjsFunctionCallState* state, GIArgument* arg,
    JS::HandleValue value) {
    if (value.isNull())
        return NullableIn::in(cx, state, arg, value);

    if (!value.isString())
        return report_typeof_mismatch(cx, m_arg_name, value,
                                      ExpectedType::STRING);

    if constexpr (TAG == GI_TYPE_TAG_FILENAME) {
        AutoChar str;
        if (!gjs_string_to_filename(cx, value, &str))
            return false;
        gjs_arg_set(arg, str.release());
        return true;
    } else if constexpr (TAG == GI_TYPE_TAG_UTF8) {
        JS::UniqueChars str = gjs_string_to_utf8(cx, value);
        if (!str)
            return false;
        gjs_arg_set(arg, js_chars_to_glib(std::move(str)).release());
        return true;
    } else {
        return invalid(cx, G_STRFUNC);
    }
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
        gjs_throw(cx,
                  "0x%" PRId64 " is not a valid value for flags argument %s",
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
    return gjs_struct_foreign_convert_to_gi_argument(
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

    JS::RootedObject callable(cx, &value.toObject());
    GClosure* closure = Gjs::Closure::create_marshaled(cx, callable, "boxed");
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
        state->ignore_release.insert(arg);
        gjs_arg_set(arg, gjs_byte_array_get_bytes(object));
        return true;
    }

    // The bytearray path is taking an extra ref irrespective of transfer
    // ownership, so we need to do the same here.
    return BoxedBase::transfer_to_gi_argument(
        cx, object, arg, GI_DIRECTION_IN, GI_TRANSFER_EVERYTHING, G_TYPE_BYTES);
}

GJS_JSAPI_RETURN_CONVENTION
bool GBytesIn::release(JSContext* cx, GjsFunctionCallState* state,
                       GIArgument* in_arg, GIArgument* out_arg) {
    if (state->ignore_release.erase(in_arg))
        return BoxedIn::release(cx, state, in_arg, out_arg);

    return BoxedInTransferNone::release(cx, state, in_arg, out_arg);
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
bool FallbackInOut::out(JSContext* cx, GjsFunctionCallState*, GIArgument* arg,
                        JS::MutableHandleValue value) {
    return gjs_value_from_gi_argument(cx, value, &m_type_info, arg, true);
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayInOut::out(JSContext* cx, GjsFunctionCallState* state,
                      GIArgument* arg, JS::MutableHandleValue value) {
    GIArgument* length_arg;

    if (m_length_direction != GI_DIRECTION_IN)
        length_arg = &(state->out_cvalue(m_length_pos));
    else
        length_arg = &(state->in_cvalue(m_length_pos));

    size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

    return gjs_value_from_explicit_array(cx, value, &m_type_info, m_transfer,
                                         arg, length);
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackIn::release(JSContext* cx, GjsFunctionCallState* state,
                         GIArgument* in_arg, GIArgument*) {
    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
    return gjs_gi_argument_release_in_arg(cx, transfer, &m_type_info, in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackOut::release(JSContext* cx, GjsFunctionCallState*,
                          GIArgument* in_arg [[maybe_unused]],
                          GIArgument* out_arg) {
    return gjs_gi_argument_release(cx, m_transfer, &m_type_info, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool FallbackInOut::release(JSContext* cx, GjsFunctionCallState* state,
                            GIArgument*, GIArgument* out_arg) {
    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;
    GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);

    // Assume that inout transfer means that in and out transfer are the same.
    // See https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/192

    if (gjs_arg_get<void*>(original_out_arg) != gjs_arg_get<void*>(out_arg) &&
        !gjs_gi_argument_release_in_arg(cx, transfer, &m_type_info,
                                        original_out_arg))
        return false;

    return gjs_gi_argument_release(cx, transfer, &m_type_info, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayOut::release(JSContext* cx, GjsFunctionCallState* state,
                        GIArgument* in_arg [[maybe_unused]],
                        GIArgument* out_arg) {
    GIArgument* length_arg = &state->out_cvalue(m_length_pos);
    size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

    return gjs_gi_argument_release_out_array(cx, m_transfer, &m_type_info,
                                             length, out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayIn::release(JSContext* cx, GjsFunctionCallState* state,
                       GIArgument* in_arg,
                       GIArgument* out_arg [[maybe_unused]]) {
    GIArgument* length_arg = &state->in_cvalue(m_length_pos);
    size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);

    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

    return gjs_gi_argument_release_in_array(cx, transfer, &m_type_info, length,
                                            in_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CArrayInOut::release(JSContext* cx, GjsFunctionCallState* state,
                          GIArgument* in_arg [[maybe_unused]],
                          GIArgument* out_arg) {
    GIArgument* length_arg = &state->out_cvalue(m_length_pos);
    size_t length = gjs_gi_argument_get_array_length(m_tag, length_arg);
    GITransfer transfer =
        state->call_completed() ? m_transfer : GI_TRANSFER_NOTHING;

    GIArgument* original_out_arg = &state->inout_original_cvalue(m_arg_pos);
    // Due to https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/192
    // Here we've to guess what to do, but in general is "better" to leak than
    // crash, so let's assume that in/out transfer is matching.
    if (gjs_arg_get<void*>(original_out_arg) != gjs_arg_get<void*>(out_arg)) {
        if (!gjs_gi_argument_release_in_array(cx, transfer, &m_type_info,
                                              length, original_out_arg))
            return false;
    }

    return gjs_gi_argument_release_out_array(cx, transfer, &m_type_info, length,
                                             out_arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool CallerAllocatesOut::release(JSContext*, GjsFunctionCallState*,
                                 GIArgument* in_arg,
                                 GIArgument* out_arg [[maybe_unused]]) {
    g_free(gjs_arg_steal<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool BoxedCallerAllocatesOut::release(JSContext*, GjsFunctionCallState*,
                                      GIArgument* in_arg, GIArgument*) {
    g_boxed_free(m_gtype, gjs_arg_steal<void*>(in_arg));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
bool CallbackIn::release(JSContext*, GjsFunctionCallState*, GIArgument* in_arg,
                         GIArgument* out_arg [[maybe_unused]]) {
    ffi_closure *closure = m_ffi_closure;
    if (!closure)
        return true;

    g_closure_unref(static_cast<GClosure*>(closure->user_data));
    // CallbackTrampolines are refcounted because for notified/async closures
    // it is possible to destroy it while in call, and therefore we cannot
    // check its scope at this point
    gjs_arg_unset(in_arg);
    return true;
}

template <GITypeTag TAG>
GJS_JSAPI_RETURN_CONVENTION bool StringInTransferNone<TAG>::release(
    JSContext*, GjsFunctionCallState*, GIArgument* in_arg,
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
        return gjs_struct_foreign_release_gi_argument(cx, m_transfer, m_info,
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
    if constexpr (std::is_same_v<T, Arg::BasicTypeInOut> ||
                  std::is_same_v<T, Arg::BasicTypeOut> ||
                  std::is_same_v<T, Arg::BasicTypeReturn> ||
                  std::is_same_v<T, Arg::ByteArrayIn> ||
                  std::is_same_v<T, Arg::ByteArrayOut> ||
                  std::is_same_v<T, Arg::ByteArrayReturn> ||
                  std::is_same_v<T, Arg::ErrorIn> ||
                  std::is_same_v<T, Arg::GdkAtomIn> ||
                  std::is_same_v<T, Arg::NumericIn<int>>)
        return 24;
    if constexpr (std::is_same_v<T, Arg::BasicCFixedSizeArrayIn> ||
                  std::is_same_v<T, Arg::BasicCFixedSizeArrayOut> ||
                  std::is_same_v<T, Arg::BasicCFixedSizeArrayReturn> ||
                  std::is_same_v<T, Arg::BasicCZeroTerminatedArrayIn> ||
                  std::is_same_v<T, Arg::BasicCZeroTerminatedArrayOut> ||
                  std::is_same_v<T, Arg::BasicCZeroTerminatedArrayReturn> ||
                  std::is_same_v<T, Arg::BasicGArrayIn> ||
                  std::is_same_v<T, Arg::BasicGArrayOut> ||
                  std::is_same_v<T, Arg::BasicGArrayReturn> ||
                  std::is_same_v<T, Arg::BasicGListIn> ||
                  std::is_same_v<T, Arg::BasicGListOut> ||
                  std::is_same_v<T, Arg::BasicGListReturn> ||
                  std::is_same_v<T, Arg::BasicGPtrArrayIn> ||
                  std::is_same_v<T, Arg::BasicGPtrArrayOut> ||
                  std::is_same_v<T, Arg::BasicGPtrArrayReturn> ||
                  std::is_same_v<T, Arg::BasicTypeTransferableInOut> ||
                  std::is_same_v<T, Arg::BasicTypeTransferableOut> ||
                  std::is_same_v<T, Arg::BasicTypeTransferableReturn>)
        return 32;
    if constexpr (std::is_same_v<T, Arg::BasicExplicitCArrayIn> ||
                  std::is_same_v<T, Arg::BasicExplicitCArrayInOut> ||
                  std::is_same_v<T, Arg::BasicExplicitCArrayOut> ||
                  std::is_same_v<T, Arg::BasicGHashIn> ||
                  std::is_same_v<T, Arg::BasicGHashOut> ||
                  std::is_same_v<T, Arg::BasicGHashReturn> ||
                  std::is_same_v<T, Arg::BoxedIn> ||
                  std::is_same_v<T, Arg::ObjectIn>)
        return 40;
    if constexpr (std::is_same_v<T, Arg::CArrayIn> ||
                  std::is_same_v<T, Arg::CArrayInOut> ||
                  std::is_same_v<T, Arg::CArrayInOut>)
        return 104;
    if constexpr (std::is_same_v<T, Arg::BoxedCallerAllocatesOut>)
        return 120;
    else
        return 112;
}
#endif

template <typename T, Arg::Kind ArgKind>
void Argument::init_common(const Init& init, T* arg) {
#ifdef GJS_DO_ARGUMENTS_SIZE_CHECK
    static_assert(
        sizeof(T) <= argument_maximum_size<T>(),
        "Think very hard before increasing the size of Gjs::Arguments. "
        "One is allocated for every argument to every introspected function.");
#endif
    if constexpr (ArgKind == Arg::Kind::INSTANCE) {
        g_assert(init.index == Argument::ABSENT &&
                 "index was ignored in INSTANCE parameter");
        g_assert(init.name == nullptr &&
                 "name was ignored in INSTANCE parameter");
        arg->set_instance_parameter();
    } else if constexpr (ArgKind == Arg::Kind::RETURN_VALUE) {
        g_assert(init.index == Argument::ABSENT &&
                 "index was ignored in RETURN_VALUE parameter");
        g_assert(init.name == nullptr &&
                 "name was ignored in RETURN_VALUE parameter");
        arg->set_return_value();
    } else {
        if constexpr (std::is_base_of_v<Arg::Positioned, T>)
            arg->set_arg_pos(init.index);
        arg->m_arg_name = init.name;
    }

    arg->m_skip_in = (init.flags & GjsArgumentFlags::SKIP_IN);
    arg->m_skip_out = (init.flags & GjsArgumentFlags::SKIP_OUT);

    if constexpr (std::is_base_of_v<Arg::Nullable, T>)
        arg->m_nullable = (init.flags & GjsArgumentFlags::MAY_BE_NULL);

    if constexpr (std::is_base_of_v<Arg::Transferable, T>)
        arg->m_transfer = init.transfer;
}

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

    m_args = new ArgumentPtr[size]{};
    return true;
}

template <Arg::Kind ArgKind, typename T>
constexpr void ArgsCache::set_argument(T* arg, const Argument::Init& init) {
    Argument::init_common<T, ArgKind>(init, arg);
    arg_get<ArgKind>(init.index) = arg;
}

template <typename T>
constexpr void ArgsCache::set_return(T* arg, GITransfer transfer,
                                     GjsArgumentFlags flags) {
    set_argument<Arg::Kind::RETURN_VALUE>(
        arg, {Argument::ABSENT, nullptr, transfer, flags});
}

template <typename T>
constexpr void ArgsCache::set_instance(T* arg, GITransfer transfer,
                                       GjsArgumentFlags flags) {
    set_argument<Arg::Kind::INSTANCE>(
        arg, {Argument::ABSENT, nullptr, transfer, flags});
}

Maybe<GType> ArgsCache::instance_type() const {
    return instance()
        .andThen(std::mem_fn(&Argument::as_instance))
        .map(std::mem_fn(&Arg::Instance::gtype));
}

Maybe<Arg::ReturnTag> ArgsCache::return_tag() const {
    return return_value().andThen(std::mem_fn(&Argument::return_tag));
}

void ArgsCache::set_skip_all(uint8_t index, const char* name) {
    set_argument(new Arg::SkipAll(), {index, name, GI_TRANSFER_NOTHING,
                                      GjsArgumentFlags::SKIP_ALL});
}

void ArgsCache::init_out_array_length_argument(GIArgInfo* length_arg,
                                               GjsArgumentFlags flags,
                                               int length_pos) {
    // Even if we skip the length argument most of time, we need to do some
    // basic initialization here.
    g_assert(length_pos <= Argument::MAX_ARGS && "too many arguments");
    uint8_t validated_length_pos = length_pos;
    set_argument(
        new Arg::ArrayLengthOut(),
        {validated_length_pos, g_base_info_get_name(length_arg),
         GI_TRANSFER_NOTHING,
         static_cast<GjsArgumentFlags>(flags | GjsArgumentFlags::SKIP_ALL)});
}

void ArgsCache::set_array_argument(GICallableInfo* callable, uint8_t gi_index,
                                   GITypeInfo* type_info, GIDirection direction,
                                   GIArgInfo* arg, GjsArgumentFlags flags,
                                   int length_pos) {
    g_assert(length_pos >= 0);
    g_assert(g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C);

    GI::AutoTypeInfo element_type{g_type_info_get_param_type(type_info, 0)};
    GITypeTag element_tag = g_type_info_get_tag(element_type);
    bool element_is_pointer = g_type_info_is_pointer(element_type);

    GIArgInfo length_arg;
    g_callable_info_load_arg(callable, length_pos, &length_arg);
    GITypeInfo length_type;
    g_arg_info_load_type(&length_arg, &length_type);
    GITypeTag length_tag = g_type_info_get_tag(&length_type);
    GIDirection length_direction = g_arg_info_get_direction(&length_arg);

    const char* arg_name = g_base_info_get_name(arg);
    GITransfer transfer = g_arg_info_get_ownership_transfer(arg);
    Argument::Init common_args{gi_index, arg_name, transfer, flags};

    if (direction == GI_DIRECTION_IN) {
        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            set_argument(
                new Arg::BasicExplicitCArrayIn(element_tag, length_pos,
                                               length_tag, length_direction),
                common_args);
        } else {
            set_argument(new Arg::CArrayIn(type_info, length_pos, length_tag,
                                           length_direction),
                         common_args);
        }
        set_skip_all(length_pos, g_base_info_get_name(&length_arg));
    } else if (direction == GI_DIRECTION_INOUT) {
        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            set_argument(
                new Arg::BasicExplicitCArrayInOut(element_tag, length_pos,
                                                  length_tag, length_direction),
                common_args);
        } else {
            set_argument(new Arg::CArrayInOut(type_info, length_pos, length_tag,
                                              length_direction),
                         common_args);
        }
        set_skip_all(length_pos, g_base_info_get_name(&length_arg));
    } else {
        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            set_argument(
                new Arg::BasicExplicitCArrayOut(element_tag, length_pos,
                                                length_tag, length_direction),
                common_args);
        } else {
            set_argument(new Arg::CArrayOut(type_info, length_pos, length_tag,
                                            length_direction),
                         common_args);
        }
    }

    if (direction == GI_DIRECTION_OUT)
        init_out_array_length_argument(&length_arg, flags, length_pos);
}

void ArgsCache::set_array_return(GICallableInfo* callable,
                                 GITypeInfo* type_info, GjsArgumentFlags flags,
                                 int length_pos) {
    g_assert(length_pos >= 0);
    g_assert(g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C);

    GI::AutoTypeInfo element_type{g_type_info_get_param_type(type_info, 0)};
    GITypeTag element_tag = g_type_info_get_tag(element_type);
    bool element_is_pointer = g_type_info_is_pointer(element_type);

    GIArgInfo length_arg;
    g_callable_info_load_arg(callable, length_pos, &length_arg);
    GITypeInfo length_type;
    g_arg_info_load_type(&length_arg, &length_type);
    GITypeTag length_tag = g_type_info_get_tag(&length_type);
    GIDirection length_direction = g_arg_info_get_direction(&length_arg);

    GITransfer transfer = g_callable_info_get_caller_owns(callable);
    if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
        set_return(new Arg::BasicExplicitCArrayOut(
                       element_tag, length_pos, length_tag, length_direction),
                   transfer, GjsArgumentFlags::NONE);
    } else {
        set_return(new Arg::CArrayOut(type_info, length_pos, length_tag,
                                      length_direction),
                   transfer, GjsArgumentFlags::NONE);
    }

    init_out_array_length_argument(&length_arg, flags, length_pos);
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

    if (g_callable_info_may_return_null(callable))
        flags |= GjsArgumentFlags::MAY_BE_NULL;

    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN:
            set_return(new Arg::BooleanReturn(), transfer, flags);
            return;

        case GI_TYPE_TAG_INT8:
            set_return(new Arg::NumericReturn<int8_t, GI_TYPE_TAG_INT8>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_INT16:
            set_return(new Arg::NumericReturn<int16_t, GI_TYPE_TAG_INT16>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_INT32:
            set_return(new Arg::NumericReturn<int32_t, GI_TYPE_TAG_INT32>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_UINT8:
            set_return(new Arg::NumericReturn<uint8_t, GI_TYPE_TAG_UINT8>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_UINT16:
            set_return(new Arg::NumericReturn<uint16_t, GI_TYPE_TAG_UINT16>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_UINT32:
            set_return(new Arg::NumericReturn<uint32_t, GI_TYPE_TAG_UINT32>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_INT64:
            set_return(new Arg::NumericReturn<int64_t, GI_TYPE_TAG_INT64>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_UINT64:
            set_return(new Arg::NumericReturn<uint64_t, GI_TYPE_TAG_UINT64>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_FLOAT:
            set_return(new Arg::NumericReturn<float, GI_TYPE_TAG_FLOAT>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_DOUBLE:
            set_return(new Arg::NumericReturn<double, GI_TYPE_TAG_DOUBLE>(),
                       transfer, flags);
            return;

        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING) {
                set_return(new Arg::StringReturn<GI_TRANSFER_NOTHING>(),
                           transfer, flags);
                return;
            } else {
                set_return(new Arg::StringReturn<GI_TRANSFER_EVERYTHING>(),
                           transfer, flags);
                return;
            }

        case GI_TYPE_TAG_ARRAY: {
            int length_pos = g_type_info_get_array_length(&type_info);
            if (length_pos >= 0) {
                set_array_return(callable, &type_info, flags, length_pos);
                return;
            }

            GIArrayType array_type = g_type_info_get_array_type(&type_info);
            if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                set_return(new Arg::ByteArrayReturn(), transfer, flags);
                return;
            }

            GI::AutoTypeInfo element_type{
                g_type_info_get_param_type(&type_info, 0)};
            GITypeTag element_tag = g_type_info_get_tag(element_type);
            bool element_is_pointer = g_type_info_is_pointer(element_type);

            if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
                if (array_type == GI_ARRAY_TYPE_C) {
                    if (g_type_info_is_zero_terminated(&type_info)) {
                        set_return(new Arg::BasicCZeroTerminatedArrayReturn(
                                       &type_info),
                                   transfer, flags);
                        return;
                    }
                    int fixed_size =
                        g_type_info_get_array_fixed_size(&type_info);
                    if (fixed_size >= 0) {
                        set_return(
                            new Arg::BasicCFixedSizeArrayReturn(&type_info),
                            transfer, flags);
                        return;
                    }
                } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
                    set_return(new Arg::BasicGArrayReturn(&type_info), transfer,
                               flags);
                    return;
                } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
                    set_return(new Arg::BasicGPtrArrayReturn(&type_info),
                               transfer, flags);
                    return;
                }
            }

            [[fallthrough]];
        }

        default:
            break;
    }

    bool is_pointer = g_type_info_is_pointer(&type_info);
    if (Gjs::is_basic_type(tag, is_pointer)) {
        // void return type + pointer is not a basic type
        if (transfer == GI_TRANSFER_NOTHING) {
            set_return(new Arg::BasicTypeReturn(tag), transfer, flags);
        } else {
            set_return(new Arg::BasicTypeTransferableReturn(tag), transfer,
                       flags);
        }
        return;
    } else if (tag == GI_TYPE_TAG_GLIST || tag == GI_TYPE_TAG_GSLIST) {
        GI::AutoTypeInfo element_type{
            g_type_info_get_param_type(&type_info, 0)};
        GITypeTag element_tag = g_type_info_get_tag(element_type);
        bool element_is_pointer = g_type_info_is_pointer(element_type);

        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            set_return(new Arg::BasicGListReturn(&type_info), transfer, flags);
            return;
        }
    } else if (tag == GI_TYPE_TAG_GHASH) {
        GI::AutoTypeInfo hash_key_type{
            g_type_info_get_param_type(&type_info, 0)};
        GITypeTag key_tag = g_type_info_get_tag(hash_key_type);
        bool key_is_pointer = g_type_info_is_pointer(hash_key_type);

        GI::AutoTypeInfo hash_value_type{
            g_type_info_get_param_type(&type_info, 1)};
        GITypeTag value_tag = g_type_info_get_tag(hash_value_type);
        bool value_is_pointer = g_type_info_is_pointer(hash_value_type);

        if (Gjs::is_basic_type(key_tag, key_is_pointer) &&
            Gjs::is_basic_type(value_tag, value_is_pointer)) {
            set_return(new Arg::BasicGHashReturn(&type_info), transfer, flags);
            return;
        }
    }

    // in() is ignored for the return value, but skip_in is not (it is used
    // in the failure release path)
    set_return(new Arg::FallbackReturn(&type_info), transfer, flags);
}

namespace Arg {

Enum::Enum(GIEnumInfo* enum_info) {
    int64_t min = std::numeric_limits<int64_t>::max();
    int64_t max = std::numeric_limits<int64_t>::min();
    int n = g_enum_info_get_n_values(enum_info);
    for (int i = 0; i < n; i++) {
        GI::AutoValueInfo value_info{g_enum_info_get_value(enum_info, i)};
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
        GI::AutoValueInfo value_info{g_enum_info_get_value(enum_info, i)};
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
void ArgsCache::build_interface_in_arg(
    const Argument::Init& base_args, GIBaseInfo* interface_info) {
    GIInfoType interface_type = g_base_info_get_type(interface_info);

    // We do some transfer magic later, so let's ensure we don't mess up.
    // Should not happen in practice.
    if (G_UNLIKELY(base_args.transfer == GI_TRANSFER_CONTAINER)) {
        set_argument<ArgKind>(
            new Arg::NotIntrospectable(INTERFACE_TRANSFER_CONTAINER),
            base_args);
        return;
    }

    switch (interface_type) {
        case GI_INFO_TYPE_ENUM:
            set_argument<ArgKind>(new Arg::EnumIn(interface_info), base_args);
            return;

        case GI_INFO_TYPE_FLAGS:
            set_argument<ArgKind>(new Arg::FlagsIn(interface_info), base_args);
            return;

        case GI_INFO_TYPE_STRUCT:
            if (g_struct_info_is_foreign(interface_info)) {
                if constexpr (ArgKind == Arg::Kind::INSTANCE)
                    set_argument<ArgKind>(
                        new Arg::ForeignStructInstanceIn(interface_info),
                        base_args);
                else
                    set_argument<ArgKind>(
                        new Arg::ForeignStructIn(interface_info), base_args);
                return;
            }
            [[fallthrough]];
        case GI_INFO_TYPE_BOXED:
        case GI_INFO_TYPE_OBJECT:
        case GI_INFO_TYPE_INTERFACE:
        case GI_INFO_TYPE_UNION: {
            if (arg_cache::is_gdk_atom(interface_info)) {
                if constexpr (ArgKind != Arg::Kind::INSTANCE) {
                    set_argument<ArgKind>(new Arg::GdkAtomIn(), base_args);
                    return;
                }
            }

            GType gtype = g_registered_type_info_get_g_type(interface_info);

            if (interface_type == GI_INFO_TYPE_STRUCT && gtype == G_TYPE_NONE &&
                !g_struct_info_is_gtype_struct(interface_info)) {
                if constexpr (ArgKind != Arg::Kind::INSTANCE) {
                    // This covers cases such as GTypeInstance
                    set_argument<ArgKind>(
                        new Arg::FallbackInterfaceIn(interface_info),
                        base_args);
                    return;
                }
            }

            // Transfer handling is a bit complex here, because some of our in()
            // arguments know not to copy stuff if we don't need to.

            if (gtype == G_TYPE_VALUE) {
                if constexpr (ArgKind == Arg::Kind::INSTANCE)
                    set_argument<ArgKind>(new Arg::BoxedIn(interface_info),
                                          base_args);
                else if (base_args.transfer == GI_TRANSFER_NOTHING)
                    set_argument<ArgKind>(
                        new Arg::GValueInTransferNone(interface_info),
                        base_args);
                else
                    set_argument<ArgKind>(new Arg::GValueIn(interface_info),
                                          base_args);
                return;
            }


            if (gtype == G_TYPE_CLOSURE) {
                if (base_args.transfer == GI_TRANSFER_NOTHING &&
                    ArgKind != Arg::Kind::INSTANCE)
                    set_argument<ArgKind>(
                        new Arg::GClosureInTransferNone(interface_info),
                        base_args);
                else
                    set_argument<ArgKind>(new Arg::GClosureIn(interface_info),
                                          base_args);
                return;
            }

            if (gtype == G_TYPE_BYTES) {
                if (base_args.transfer == GI_TRANSFER_NOTHING &&
                    ArgKind != Arg::Kind::INSTANCE)
                    set_argument<ArgKind>(
                        new Arg::GBytesInTransferNone(interface_info),
                        base_args);
                else
                    set_argument<ArgKind>(new Arg::GBytesIn(interface_info),
                                          base_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                set_argument<ArgKind>(new Arg::ObjectIn(interface_info),
                                      base_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_PARAM)) {
                if constexpr (ArgKind != Arg::Kind::INSTANCE) {
                    set_argument<ArgKind>(
                        new Arg::FallbackInterfaceIn(interface_info),
                        base_args);
                    return;
                }
            }

            if (interface_type == GI_INFO_TYPE_UNION) {
                if (gtype == G_TYPE_NONE) {
                    // Can't handle unions without a GType
                    set_argument<ArgKind>(
                        new Arg::NotIntrospectable(UNREGISTERED_UNION),
                        base_args);
                    return;
                }

                set_argument<ArgKind>(new Arg::UnionIn(interface_info),
                                      base_args);
                return;
            }

            if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                set_argument<ArgKind>(new Arg::FundamentalIn(interface_info),
                                      base_args);
                return;
            }

            if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                set_argument<ArgKind>(new Arg::InterfaceIn(interface_info),
                                      base_args);
                return;
            }

            // generic boxed type
            if (gtype == G_TYPE_NONE) {
                if (base_args.transfer != GI_TRANSFER_NOTHING) {
                    // Can't transfer ownership of a structure type not
                    // registered as a boxed
                    set_argument<ArgKind>(new Arg::NotIntrospectable(
                                              UNREGISTERED_BOXED_WITH_TRANSFER),
                                          base_args);
                    return;
                }

                set_argument<ArgKind>(
                    new Arg::UnregisteredBoxedIn(interface_info), base_args);
                return;
            }
            set_argument<ArgKind>(new Arg::BoxedIn(interface_info), base_args);
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
            set_argument<ArgKind>(new Arg::NotIntrospectable(UNSUPPORTED_TYPE),
                                  base_args);
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
    Argument::Init common_args{gi_index, name, transfer, flags};
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            set_argument(new Arg::NullIn(), common_args);
            break;

        case GI_TYPE_TAG_BOOLEAN:
            set_argument(new Arg::BooleanIn(), common_args);
            break;

        case GI_TYPE_TAG_INT8:
            set_argument(new Arg::NumericIn<int8_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT16:
            set_argument(new Arg::NumericIn<int16_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT32:
            set_argument(new Arg::NumericIn<int32_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT8:
            set_argument(new Arg::NumericIn<uint8_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT16:
            set_argument(new Arg::NumericIn<uint16_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT32:
            set_argument(new Arg::NumericIn<uint32_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT64:
            set_argument(new Arg::NumericIn<int64_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT64:
            set_argument(new Arg::NumericIn<uint64_t>(), common_args);
            return;

        case GI_TYPE_TAG_FLOAT:
            set_argument(new Arg::NumericIn<float>(), common_args);
            return;

        case GI_TYPE_TAG_DOUBLE:
            set_argument(new Arg::NumericIn<double>(), common_args);
            return;

        case GI_TYPE_TAG_UNICHAR:
            set_argument(new Arg::UnicharIn(), common_args);
            break;

        case GI_TYPE_TAG_GTYPE:
            set_argument(new Arg::GTypeIn(), common_args);
            break;

        case GI_TYPE_TAG_FILENAME:
            if (transfer == GI_TRANSFER_NOTHING)
                set_argument(new Arg::FilenameInTransferNone(), common_args);
            else
                set_argument(new Arg::FilenameIn(), common_args);
            break;

        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING)
                set_argument(new Arg::StringInTransferNone<GI_TYPE_TAG_UTF8>(),
                             common_args);
            else
                set_argument(new Arg::StringIn(), common_args);
            break;

        case GI_TYPE_TAG_INTERFACE: {
            GI::AutoBaseInfo interface_info{
                g_type_info_get_interface(type_info)};
            build_interface_in_arg(common_args, interface_info);
            return;
        }

        case GI_TYPE_TAG_ERROR:
            set_argument(new Arg::ErrorIn(), common_args);
            return;

        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST: {
            GI::AutoTypeInfo element_type{
                g_type_info_get_param_type(type_info, 0)};
            GITypeTag element_tag = g_type_info_get_tag(element_type);
            bool element_is_pointer = g_type_info_is_pointer(element_type);

            if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
                set_argument(new Arg::BasicGListIn(type_info), common_args);
                return;
            }

            // Fall back to the generic marshaller
            set_argument(new Arg::FallbackIn(type_info), common_args);
            return;
        }

        case GI_TYPE_TAG_GHASH: {
            GI::AutoTypeInfo hash_key_type{
                g_type_info_get_param_type(type_info, 0)};
            GITypeTag key_tag = g_type_info_get_tag(hash_key_type);
            bool key_is_pointer = g_type_info_is_pointer(hash_key_type);

            GI::AutoTypeInfo hash_value_type{
                g_type_info_get_param_type(type_info, 1)};
            GITypeTag value_tag = g_type_info_get_tag(hash_value_type);
            bool value_is_pointer = g_type_info_is_pointer(hash_value_type);

            if (Gjs::is_basic_type(key_tag, key_is_pointer) &&
                Gjs::is_basic_type(value_tag, value_is_pointer)) {
                set_argument(new Arg::BasicGHashIn(type_info), common_args);
                return;
            }

            // Fall back to the generic marshaller
            set_argument(new Arg::FallbackIn(type_info), common_args);
            return;
        }

        case GI_TYPE_TAG_ARRAY: {
            GIArrayType array_type = g_type_info_get_array_type(type_info);
            if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
                set_argument(new Arg::ByteArrayIn(), common_args);
                return;
            }

            GI::AutoTypeInfo element_type{
                g_type_info_get_param_type(type_info, 0)};
            GITypeTag element_tag = g_type_info_get_tag(element_type);
            bool element_is_pointer = g_type_info_is_pointer(element_type);
            bool element_is_basic =
                Gjs::is_basic_type(element_tag, element_is_pointer);

            if (element_is_basic) {
                if (array_type == GI_ARRAY_TYPE_ARRAY) {
                    set_argument(new Arg::BasicGArrayIn(type_info),
                                 common_args);
                    return;
                }

                if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
                    set_argument(new Arg::BasicGPtrArrayIn(type_info),
                                 common_args);
                    return;
                }

                if (array_type == GI_ARRAY_TYPE_C) {
                    if (g_type_info_is_zero_terminated(type_info)) {
                        set_argument(
                            new Arg::BasicCZeroTerminatedArrayIn(type_info),
                            common_args);
                        return;
                    }
                    if (g_type_info_get_array_fixed_size(type_info) >= 0) {
                        set_argument(new Arg::BasicCFixedSizeArrayIn(type_info),
                                     common_args);
                        return;
                    }
                }
            }

            if (array_type == GI_ARRAY_TYPE_C) {
                if (g_type_info_is_zero_terminated(type_info)) {
                    set_argument(new Arg::ZeroTerminatedArrayIn(type_info),
                                 common_args);
                    return;
                }
                if (g_type_info_get_array_fixed_size(type_info) >= 0) {
                    set_argument(new Arg::FixedSizeArrayIn(type_info),
                                 common_args);
                    return;
                }
            }
            [[fallthrough]];
        }

        default:
            // FIXME: Falling back to the generic marshaller
            set_argument(new Arg::FallbackIn(type_info), common_args);
    }
}

void ArgsCache::build_normal_out_arg(uint8_t gi_index, GITypeInfo* type_info,
                                     GIArgInfo* arg, GjsArgumentFlags flags) {
    const char* name = g_base_info_get_name(arg);
    GITransfer transfer = g_arg_info_get_ownership_transfer(arg);
    Argument::Init common_args{gi_index, name, transfer, flags};
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN:
            set_argument(new Arg::BooleanOut(), common_args);
            break;

        case GI_TYPE_TAG_INT8:
            set_argument(new Arg::NumericOut<int8_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT16:
            set_argument(new Arg::NumericOut<int16_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT32:
            set_argument(new Arg::NumericOut<int32_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT8:
            set_argument(new Arg::NumericOut<uint8_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT16:
            set_argument(new Arg::NumericOut<uint16_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT32:
            set_argument(new Arg::NumericOut<uint32_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT64:
            set_argument(new Arg::NumericOut<int64_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT64:
            set_argument(new Arg::NumericOut<uint64_t>(), common_args);
            return;

        case GI_TYPE_TAG_FLOAT:
            set_argument(new Arg::NumericOut<float>(), common_args);
            return;

        case GI_TYPE_TAG_DOUBLE:
            set_argument(new Arg::NumericOut<double>(), common_args);
            return;

        case GI_TYPE_TAG_UTF8:
            if (transfer == GI_TRANSFER_NOTHING) {
                set_argument(new Arg::StringOut<GI_TRANSFER_NOTHING>(),
                             common_args);
            } else {
                set_argument(new Arg::StringOut<GI_TRANSFER_EVERYTHING>(),
                             common_args);
            }
            return;

        default: {
        }
    }

    bool is_pointer = g_type_info_is_pointer(type_info);
    if (Gjs::is_basic_type(tag, is_pointer)) {
        if (transfer == GI_TRANSFER_NOTHING) {
            set_argument(new Arg::BasicTypeOut(tag), common_args);
        } else {
            set_argument(new Arg::BasicTypeTransferableOut(tag), common_args);
        }
        return;
    } else if (tag == GI_TYPE_TAG_ARRAY) {
        GIArrayType array_type = g_type_info_get_array_type(type_info);
        if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            set_argument(new Arg::ByteArrayOut(), common_args);
            return;
        }

        GI::AutoTypeInfo element_type{g_type_info_get_param_type(type_info, 0)};
        GITypeTag element_tag = g_type_info_get_tag(element_type);
        bool element_is_pointer = g_type_info_is_pointer(element_type);

        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            if (array_type == GI_ARRAY_TYPE_C) {
                if (g_type_info_is_zero_terminated(type_info)) {
                    set_argument(
                        new Arg::BasicCZeroTerminatedArrayOut(type_info),
                        common_args);
                    return;
                }

                int fixed_size = g_type_info_get_array_fixed_size(type_info);
                if (fixed_size >= 0) {
                    set_argument(new Arg::BasicCFixedSizeArrayOut(type_info),
                                 common_args);
                    return;
                }
            } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
                set_argument(new Arg::BasicGArrayOut(type_info), common_args);
                return;
            } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
                set_argument(new Arg::BasicGPtrArrayOut(type_info),
                             common_args);
                return;
            }
        }
    }

    if (tag == GI_TYPE_TAG_GLIST || tag == GI_TYPE_TAG_GSLIST) {
        GI::AutoTypeInfo element_type{g_type_info_get_param_type(type_info, 0)};
        GITypeTag element_tag = g_type_info_get_tag(element_type);
        bool element_is_pointer = g_type_info_is_pointer(element_type);

        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            set_argument(new Arg::BasicGListOut(type_info), common_args);
            return;
        }
    } else if (tag == GI_TYPE_TAG_GHASH) {
        GI::AutoTypeInfo key_type{g_type_info_get_param_type(type_info, 0)};
        GITypeTag key_tag = g_type_info_get_tag(key_type);
        bool key_is_pointer = g_type_info_is_pointer(key_type);

        GI::AutoTypeInfo value_type{g_type_info_get_param_type(type_info, 1)};
        GITypeTag value_tag = g_type_info_get_tag(value_type);
        bool value_is_pointer = g_type_info_is_pointer(value_type);

        if (Gjs::is_basic_type(key_tag, key_is_pointer) &&
            Gjs::is_basic_type(value_tag, value_is_pointer)) {
            set_argument(new Arg::BasicGHashOut(type_info), common_args);
            return;
        }
    } else if (tag == GI_TYPE_TAG_ARRAY) {
        GIArrayType array_type = g_type_info_get_array_type(type_info);
        if (array_type == GI_ARRAY_TYPE_BYTE_ARRAY) {
            set_argument(new Arg::ByteArrayOut(), common_args);
            return;
        }

        GI::AutoTypeInfo element_type{g_type_info_get_param_type(type_info, 0)};
        GITypeTag element_tag = g_type_info_get_tag(element_type);
        bool element_is_pointer = g_type_info_is_pointer(element_type);

        if (Gjs::is_basic_type(element_tag, element_is_pointer)) {
            if (array_type == GI_ARRAY_TYPE_C) {
                if (g_type_info_is_zero_terminated(type_info)) {
                    set_argument(
                        new Arg::BasicCZeroTerminatedArrayOut(type_info),
                        common_args);
                    return;
                }

                int fixed_size = g_type_info_get_array_fixed_size(type_info);
                if (fixed_size >= 0) {
                    set_argument(new Arg::BasicCFixedSizeArrayOut(type_info),
                                 common_args);
                    return;
                }
            } else if (array_type == GI_ARRAY_TYPE_ARRAY) {
                set_argument(new Arg::BasicGArrayOut(type_info), common_args);
                return;
            } else if (array_type == GI_ARRAY_TYPE_PTR_ARRAY) {
                set_argument(new Arg::BasicGPtrArrayOut(type_info),
                             common_args);
                return;
            }
        }
    }

    set_argument(new Arg::FallbackOut(type_info), common_args);
}

void ArgsCache::build_normal_inout_arg(uint8_t gi_index, GITypeInfo* type_info,
                                       GIArgInfo* arg, GjsArgumentFlags flags) {
    const char* name = g_base_info_get_name(arg);
    GITransfer transfer = g_arg_info_get_ownership_transfer(arg);
    Argument::Init common_args{gi_index, name, transfer, flags};
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN:
            set_argument(new Arg::BooleanInOut(), common_args);
            break;

        case GI_TYPE_TAG_INT8:
            set_argument(new Arg::NumericInOut<int8_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT16:
            set_argument(new Arg::NumericInOut<int16_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT32:
            set_argument(new Arg::NumericInOut<int32_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT8:
            set_argument(new Arg::NumericInOut<uint8_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT16:
            set_argument(new Arg::NumericInOut<uint16_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT32:
            set_argument(new Arg::NumericInOut<uint32_t>(), common_args);
            return;

        case GI_TYPE_TAG_INT64:
            set_argument(new Arg::NumericInOut<int64_t>(), common_args);
            return;

        case GI_TYPE_TAG_UINT64:
            set_argument(new Arg::NumericInOut<uint64_t>(), common_args);
            return;

        case GI_TYPE_TAG_FLOAT:
            set_argument(new Arg::NumericInOut<float>(), common_args);
            return;

        case GI_TYPE_TAG_DOUBLE:
            set_argument(new Arg::NumericInOut<double>(), common_args);
            return;

        case GI_TYPE_TAG_ARRAY:
            if (g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
                if (g_type_info_is_zero_terminated(type_info)) {
                    set_argument(new Arg::ZeroTerminatedArrayInOut(type_info),
                                 common_args);
                    return;
                }
                if (g_type_info_get_array_fixed_size(type_info) >= 0) {
                    set_argument(new Arg::FixedSizeArrayInOut(type_info),
                                 common_args);
                    return;
                }
            }
            set_argument(new Arg::FallbackInOut(type_info), common_args);
            return;

        default: {
        }
    }

    bool is_pointer = g_type_info_is_pointer(type_info);
    if (Gjs::is_basic_type(tag, is_pointer)) {
        if (transfer == GI_TRANSFER_NOTHING) {
            set_argument(new Arg::BasicTypeInOut(tag), common_args);
        } else {
            set_argument(new Arg::BasicTypeTransferableInOut(tag), common_args);
        }
        return;
    }

    set_argument(new Arg::FallbackInOut(type_info), common_args);
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
        set_instance(new Arg::GTypeStructInstanceIn(), transfer);
        return;
    }
    if (info_type == GI_INFO_TYPE_OBJECT) {
        GType gtype = g_registered_type_info_get_g_type(interface_info);

        if (g_type_is_a(gtype, G_TYPE_PARAM)) {
            set_instance(new Arg::ParamInstanceIn(), transfer);
            return;
        }
    }

    build_interface_in_arg<Arg::Kind::INSTANCE>(
        {Argument::ABSENT, nullptr, transfer, GjsArgumentFlags::NONE},
        interface_info);
}

static constexpr bool type_tag_is_scalar(GITypeTag tag) {
    return GI_TYPE_TAG_IS_NUMERIC(tag) || tag == GI_TYPE_TAG_BOOLEAN ||
           tag == GI_TYPE_TAG_GTYPE;
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

    Argument::Init common_args{gi_index, arg_name, transfer, flags};

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (direction == GI_DIRECTION_OUT &&
        (flags & GjsArgumentFlags::CALLER_ALLOCATES)) {
        size_t size = 0;

        if (type_tag == GI_TYPE_TAG_ARRAY) {
            GIArrayType array_type = g_type_info_get_array_type(&type_info);

            switch (array_type) {
                case GI_ARRAY_TYPE_C: {
                    GI::AutoTypeInfo param_info;
                    int n_elements =
                        g_type_info_get_array_fixed_size(&type_info);

                    if (n_elements <= 0)
                        break;

                    param_info = g_type_info_get_param_type(&type_info, 0);
                    GITypeTag param_tag = g_type_info_get_tag(param_info);

                    size = gjs_type_get_element_size(param_tag, param_info);
                    size *= n_elements;
                    break;
                }
                default:
                    break;
            }
        } else if (!type_tag_is_scalar(type_tag) &&
                   !g_type_info_is_pointer(&type_info)) {
            // Scalar out parameters should not be annotated with
            // caller-allocates, which is for structured types that need to be
            // allocated in order for the function to fill them in.
            size = gjs_type_get_element_size(type_tag, &type_info);
        }

        if (!size) {
            set_argument(
                new Arg::NotIntrospectable(OUT_CALLER_ALLOCATES_NON_STRUCT),
                common_args);
            return;
        }

        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            GI::AutoBaseInfo interface_info{
                g_type_info_get_interface(&type_info)};
            GType gtype = g_registered_type_info_get_g_type(interface_info);
            if (g_type_is_a(gtype, G_TYPE_BOXED)) {
                set_argument(
                    new Arg::BoxedCallerAllocatesOut(&type_info, size, gtype),
                    common_args);
                return;
            }
        }

        set_argument(new Arg::CallerAllocatesOut(&type_info, size),
                     common_args);
        return;
    }

    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GI::AutoBaseInfo interface_info{g_type_info_get_interface(&type_info)};
        if (interface_info.type() == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                // Can't do callbacks for out or inout
                set_argument(new Arg::NotIntrospectable(CALLBACK_OUT),
                             common_args);
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
                set_argument(
                    new Arg::NotIntrospectable(DESTROY_NOTIFY_NO_CALLBACK),
                    common_args);
                *inc_counter_out = false;
            } else {
                int destroy_pos = g_arg_info_get_destroy(arg);
                int closure_pos = g_arg_info_get_closure(arg);

                if (destroy_pos >= 0)
                    set_skip_all(destroy_pos);

                if (closure_pos >= 0)
                    set_skip_all(closure_pos);

                if (destroy_pos >= 0 && closure_pos < 0) {
                    set_argument(
                        new Arg::NotIntrospectable(DESTROY_NOTIFY_NO_USER_DATA),
                        common_args);
                    return;
                }

                set_argument(
                    new Arg::CallbackIn(interface_info, closure_pos,
                                        destroy_pos, g_arg_info_get_scope(arg)),
                    common_args);
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
        build_normal_inout_arg(gi_index, &type_info, arg, flags);
    else
        build_normal_out_arg(gi_index, &type_info, arg, flags);

    return;
}

}  // namespace Gjs
