/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018-2020  Canonical, Ltd

#pragma once

#include <config.h>

#include <stdint.h>
#include <stdlib.h>     // for free
#include <sys/types.h>  // for ssize_t

#include <concepts>  // for semiregular
#include <format>
#include <limits>
#include <string>  // for string, u16string
#include <string_view>
#include <type_traits>  // for add_pointer_t, add_const_t
#include <utility>      // for forward
#include <vector>

#include <glib-object.h>
#include <glib.h>

#include <js/BigInt.h>
#include <js/CharacterEncoding.h>  // for ConstUTF8CharsZ
#include <js/ErrorReport.h>  // for JSExnType
#include <js/GCAPI.h>
#include <js/GCPolicyAPI.h>  // for IgnoreGCPolicy
#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>       // for InformalValueTypeName

#include "gjs/auto.h"
#include "gjs/format-utils.h"
#include "gjs/gerror-result.h"
#include "gjs/macros.h"
#include "util/log.h"

#if GJS_VERBOSE_ENABLE_MARSHAL
#    include "gi/arg-types-inl.h"  // for static_type_name
#endif

namespace JS {
class CallArgs;

struct Dummy {};
using GTypeNotUint64 =
    std::conditional_t<!std::is_same_v<GType, uint64_t>, GType, Dummy>;

// The GC sweep method should ignore FundamentalTable and GTypeTable's key types
// Forward declarations
template <>
struct GCPolicy<void*> : public IgnoreGCPolicy<void*> {};
// We need GCPolicy<GType> for GTypeTable. SpiderMonkey already defines
// GCPolicy<uint64_t> which is equal to GType on some systems; for others we
// need to define it. (macOS's uint64_t is unsigned long long, which is a
// different type from unsigned long, even if they are the same width)
template <>
struct GCPolicy<GTypeNotUint64> : public IgnoreGCPolicy<GTypeNotUint64> {};
}  // namespace JS

/* Flags that should be set on properties exported from native code modules.
 * Basically set these on API, but do NOT set them on data.
 *
 * PERMANENT: forbid deleting the prop
 * ENUMERATE: allows copyProperties to work among other reasons to have it
 */
#define GJS_MODULE_PROP_FLAGS (JSPROP_PERMANENT | JSPROP_ENUMERATE)

/**
 * GJS_GET_THIS:
 * @cx: JSContext pointer passed into JSNative function
 * @argc: Number of arguments passed into JSNative function
 * @vp: Argument value array passed into JSNative function
 * @args: Name for JS::CallArgs variable defined by this code snippet
 * @to: Name for JS::RootedObject variable referring to function's this
 *
 * A convenience macro for getting the 'this' object a function was called with.
 * Use in any JSNative function.
 */
#define GJS_GET_THIS(cx, argc, vp, args, to)          \
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp); \
    JS::RootedObject to(cx);                          \
    if (!(args).computeThis(cx, &(to)))               \
        return false;

void gjs_throw_constructor_error(JSContext*);

void gjs_throw_abstract_constructor_error(JSContext*, const JS::CallArgs&);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_build_string_array(JSContext*, const std::vector<std::string>&);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_define_string_array(JSContext*, JS::HandleObject,
                                  const char* array_name,
                                  const std::vector<std::string>&,
                                  unsigned attrs);

void gjs_throw_full(JSContext* cx, JSExnType error_kind, const char* error_name,
                    std::string_view msg);

/* Throws an exception, like "throw new Error(message)"
 *
 * If an exception is already set in the context, this will NOT overwrite it.
 * That's an important semantic since we want the "root cause" exception. To
 * overwrite, use JS_ClearPendingException() first.
 */
template <typename... Args>
void gjs_throw(JSContext* cx, std::format_string<Args...> format,
               Args&&... args) {
    gjs_throw_full(cx, JSEXN_ERR, nullptr,
                   std::format(format, std::forward<Args>(args)...));
}

/* Like gjs_throw, but allows to customize the error class and 'name' property.
 * Mainly used for throwing TypeError instead of error.
 */
template <typename... Args>
void gjs_throw_custom(JSContext* cx, JSExnType kind, const char* error_name,
                      std::format_string<Args...> format, Args&&... args) {
    gjs_throw_full(cx, kind, error_name,
                   std::format(format, std::forward<Args>(args)...));
}

bool gjs_throw_gerror_message(JSContext*, Gjs::AutoError const&);

bool gjs_log_exception(JSContext*);

bool gjs_log_exception_uncaught(JSContext*);

void gjs_log_exception_full(JSContext*, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags);

void gjs_warning_reporter(JSContext*, JSErrorReport*);

GJS_JSAPI_RETURN_CONVENTION
JS::UniqueChars gjs_string_to_utf8(JSContext*, JS::Value string_val);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_utf8_n(JSContext*, JS::HandleString str,
                          JS::UniqueChars* output, size_t* output_len);
GJS_JSAPI_RETURN_CONVENTION
JSString* gjs_lossy_string_from_utf8(JSContext*, const char* utf8_string);
GJS_JSAPI_RETURN_CONVENTION
JSString* gjs_lossy_string_from_utf8_n(JSContext*, const char* utf8_string,
                                       size_t len);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_utf8(JSContext*, const char* utf8_string,
                          JS::MutableHandleValue);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_utf8_n(JSContext*, const char* utf8_chars, size_t len,
                            JS::MutableHandleValue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_filename(JSContext*, JS::Value,
                            Gjs::AutoChar* filename_string);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_filename(JSContext*, const char* filename_string,
                              ssize_t n_bytes, JS::MutableHandleValue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_get_char16_data(JSContext*, JS::HandleString, char16_t** data_p,
                                size_t* len_p);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_ucs4(JSContext*, JS::HandleString, gunichar** ucs4_string_p,
                        size_t* len_p);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_ucs4(JSContext*, const gunichar* ucs4_string,
                          ssize_t n_chars, JS::MutableHandleValue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_get_string_id(JSContext*, jsid, JS::UniqueChars* name_p);
GJS_JSAPI_RETURN_CONVENTION
jsid gjs_intern_string_to_id(JSContext*, const char* string);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_unichar_from_string(JSContext*, JS::Value string_val,
                             gunichar* result);

// Functions intended for more "internal" use

void gjs_maybe_gc(JSContext*);
void gjs_gc_if_needed(JSContext*);

GJS_JSAPI_RETURN_CONVENTION
JS::UniqueChars format_saved_frame(JSContext*, JS::HandleObject saved_frame,
                                   size_t indent = 0);

/* Overloaded functions. More types are intended to be added as the opportunity
 * arises. */

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext*, JS::HandleObject,
                                 const char* obj_description,
                                 JS::HandleId property_name,
                                 JS::MutableHandleValue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext*, JS::HandleObject,
                                 const char* description,
                                 JS::HandleId property_name, bool* value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext*, JS::HandleObject,
                                 const char* description,
                                 JS::HandleId property_name, int32_t* value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext*, JS::HandleObject,
                                 const char* description,
                                 JS::HandleId property_name,
                                 JS::UniqueChars* value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext*, JS::HandleObject,
                                 const char* description,
                                 JS::HandleId property_name,
                                 JS::MutableHandleObject value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_converted_property(JSContext*, JS::HandleObject,
                                           const char* description,
                                           JS::HandleId property_name,
                                           uint32_t*);

[[nodiscard]] std::string gjs_debug_bigint(JS::BigInt*);
[[nodiscard]] std::string gjs_debug_string(JSString*);
[[nodiscard]] std::string gjs_debug_symbol(JS::Symbol*);
[[nodiscard]] std::string gjs_debug_object(JSObject*);
[[nodiscard]] std::string gjs_debug_callable(JSObject* callable);
[[nodiscard]] std::string gjs_debug_value(JS::Value);
[[nodiscard]] std::string gjs_debug_id(jsid);

[[nodiscard]] Gjs::AutoChar gjs_hyphen_to_underscore(const char*);
[[nodiscard]] Gjs::AutoChar gjs_hyphen_to_camel(const char*);
[[nodiscard]] std::string gjs_hyphen_from_camel(std::string_view);

// Custom GC reasons; SpiderMonkey includes a bunch of "Firefox reasons" which
// don't apply when embedding the JS engine, so we repurpose them for our own
// reasons.

// clang-format off
#define FOREACH_GC_REASON(macro)  \
    macro(LINUX_RSS_TRIGGER, 0)   \
    macro(GJS_CONTEXT_DISPOSE, 1) \
    macro(BIG_HAMMER, 2)          \
    macro(GJS_API_CALL, 3)        \
    macro(LOW_MEMORY, 4)
// clang-format on

namespace Gjs {

struct GCReason {
#define DEFINE_GC_REASON(name, ix)                     \
    static constexpr JS::GCReason name = JS::GCReason( \
        static_cast<int>(JS::GCReason::FIRST_FIREFOX_REASON) + (ix));
FOREACH_GC_REASON(DEFINE_GC_REASON);
#undef DEFINE_GC_REASON

#define COUNT_GC_REASON(name, ix) +1  // NOLINT(bugprone-macro-parentheses)
static constexpr size_t N_REASONS = 0 FOREACH_GC_REASON(COUNT_GC_REASON);
#undef COUNT_GC_REASON
};

template <typename T>
    requires(sizeof(T) == 8)
[[nodiscard]]
bool bigint_is_out_of_range(JS::BigInt* bi, T* clamped) {
    g_assert(bi && "bigint cannot be null");
    g_assert(clamped && "forgot out parameter");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Checking if BigInt {} is out of range for type {}", bi,
                      Gjs::static_type_name<T>());

    if (JS::BigIntFits(bi, clamped)) {
        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "BigInt {} is in the range of type {}", *clamped,
                          Gjs::static_type_name<T>());
        return false;
    }

    if (JS::BigIntIsNegative(bi)) {
        *clamped = std::numeric_limits<T>::min();
    } else {
        *clamped = std::numeric_limits<T>::max();
    }

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "BigInt {} is not in the range of type {}, clamped to {}",
                      bi, Gjs::static_type_name<T>(), *clamped);
    return true;
}

}  // namespace Gjs

[[nodiscard]] const char* gjs_explain_gc_reason(JS::GCReason);

// Formatters for various JSAPI types

template <>
struct std::formatter<JS::UniqueChars> : std::formatter<const char*> {
    auto format(const JS::UniqueChars& str, std::format_context& cx) const {
        return formatter<const char*>::format(str ? str.get() : "(null)", cx);
    }
};

template <>
struct std::formatter<JS::ConstUTF8CharsZ> : std::formatter<const char*> {
    auto format(JS::ConstUTF8CharsZ str, std::format_context& cx) const {
        return formatter<const char*>::format(str ? str.c_str() : "(null)", cx);
    }
};

template <>
struct std::formatter<JS::Value> : Gjs::FormatterBase<'t', '?'> {
    auto format(JS::Value v, std::format_context& cx) const {
        switch (spec()) {
            case 't':
                return std::format_to(cx.out(), "{}",
                                      JS::InformalValueTypeName(v));
            case '?':
                return std::format_to(cx.out(), "JS::Value {:#x}",
                                      v.asRawBits());
            default:
                return std::format_to(cx.out(), "{}", gjs_debug_value(v));
        }
    }
};

template <>
struct std::formatter<JSString*> : Gjs::FormatterBase<'?'> {
    auto format(JSString* s, std::format_context& cx) const {
        if (spec() == '?')
            return std::format_to(cx.out(), "JSString {}",
                                  static_cast<void*>(s));
        return std::format_to(cx.out(), "{}", gjs_debug_string(s));
    }
};

template <>
struct std::formatter<JS::PropertyKey> : Gjs::FormatterBase<'?'> {
    auto format(JS::PropertyKey id, std::format_context& cx) const {
        if (spec() == '?')
            return std::format_to(cx.out(), "JSID {:#x}", id.asRawBits());
        return std::format_to(cx.out(), "{}", gjs_debug_id(id));
    }
};

template <>
struct std::formatter<JSObject*> : Gjs::FormatterBase<'?'> {
    auto format(JSObject* o, std::format_context& cx) const {
        if (spec() == '?')
            return std::format_to(cx.out(), "Object {}", static_cast<void*>(o));
        return std::format_to(cx.out(), "{}", gjs_debug_object(o));
    }
};

template <>
struct std::formatter<JS::BigInt*> : Gjs::FormatterBase<'?'> {
    auto format(JS::BigInt* bi, std::format_context& cx) const {
        if (spec() == '?')
            return std::format_to(cx.out(), "JS::BigInt {}",
                                  static_cast<void*>(bi));
        return std::format_to(cx.out(), "{}", gjs_debug_bigint(bi));
    }
};

// Limit the Rooted/Handle/MutableHandle templates to types that can already be
// formatted. COMPAT: Replace with std::formattable in C++23
template <typename T>
concept RootedFormattable = std::semiregular<std::formatter<T>>;

template <RootedFormattable T>
struct std::formatter<JS::Rooted<T>> : std::formatter<T> {
    auto format(const JS::Rooted<T>& v, std::format_context& cx) const {
        return formatter<T>::format(v.get(), cx);
    }
};

template <RootedFormattable T>
struct std::formatter<JS::Handle<T>> : std::formatter<T> {
    auto format(JS::Handle<T> v, std::format_context& cx) const {
        return formatter<T>::format(v.get(), cx);
    }
};

template <RootedFormattable T>
struct std::formatter<JS::MutableHandle<T>> : std::formatter<T> {
    auto format(JS::MutableHandle<T> v, std::format_context& cx) const {
        return formatter<T>::format(v.get(), cx);
    }
};
