/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2018-2020  Canonical, Ltd

#ifndef GJS_JSAPI_UTIL_H_
#define GJS_JSAPI_UTIL_H_

#include <config.h>

#include <stdint.h>
#include <stdlib.h>     // for free
#include <sys/types.h>  // for ssize_t

#include <limits>
#include <string>  // for string, u16string
#include <type_traits>  // for enable_if_t, add_pointer_t, add_const_t
#include <vector>

#include <glib-object.h>
#include <glib.h>

#include <js/BigInt.h>
#include <js/ErrorReport.h>  // for JSExnType
#include <js/GCAPI.h>
#include <js/GCPolicyAPI.h>  // for IgnoreGCPolicy
#include <js/Id.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars

#include "gjs/auto.h"
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

/*
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
    if (!args.computeThis(cx, &to))                   \
        return false;

void gjs_throw_constructor_error             (JSContext       *context);

void gjs_throw_abstract_constructor_error(JSContext* cx,
                                          const JS::CallArgs& args);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_build_string_array(JSContext* cx,
                                 const std::vector<std::string>& strings);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_define_string_array(JSContext* cx, JS::HandleObject obj,
                                  const char* array_name,
                                  const std::vector<std::string>& strings,
                                  unsigned attrs);

[[gnu::format(printf, 2, 3)]] void gjs_throw(JSContext* cx, const char* format,
                                             ...);
[[gnu::format(printf, 4, 5)]] void gjs_throw_custom(JSContext* cx,
                                                    JSExnType error_kind,
                                                    const char* error_name,
                                                    const char* format, ...);
void        gjs_throw_literal                (JSContext       *context,
                                              const char      *string);
bool gjs_throw_gerror_message(JSContext* cx, Gjs::AutoError const&);

bool        gjs_log_exception                (JSContext       *context);

bool gjs_log_exception_uncaught(JSContext* cx);

void gjs_log_exception_full(JSContext* cx, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags level);

void gjs_warning_reporter(JSContext*, JSErrorReport* report);

GJS_JSAPI_RETURN_CONVENTION
JS::UniqueChars gjs_string_to_utf8(JSContext* cx, const JS::Value string_val);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_utf8_n(JSContext* cx, JS::HandleString str, JS::UniqueChars* output,
                          size_t* output_len);
GJS_JSAPI_RETURN_CONVENTION
JSString* gjs_lossy_string_from_utf8(JSContext* cx, const char* utf8_string);
GJS_JSAPI_RETURN_CONVENTION
JSString* gjs_lossy_string_from_utf8_n(JSContext* cx, const char* utf8_string,
                                       size_t len);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_utf8(JSContext             *context,
                          const char            *utf8_string,
                          JS::MutableHandleValue value_p);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_utf8_n(JSContext             *cx,
                            const char            *utf8_chars,
                            size_t                 len,
                            JS::MutableHandleValue out);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_filename(JSContext*, const JS::Value,
                            Gjs::AutoChar* filename_string);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_filename(JSContext             *context,
                              const char            *filename_string,
                              ssize_t                n_bytes,
                              JS::MutableHandleValue value_p);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_get_char16_data(JSContext       *cx,
                                JS::HandleString str,
                                char16_t       **data_p,
                                size_t          *len_p);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_ucs4(JSContext       *cx,
                        JS::HandleString value,
                        gunichar       **ucs4_string_p,
                        size_t          *len_p);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_from_ucs4(JSContext             *cx,
                          const gunichar        *ucs4_string,
                          ssize_t                n_chars,
                          JS::MutableHandleValue value_p);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_get_string_id(JSContext* cx, jsid id, JS::UniqueChars* name_p);
GJS_JSAPI_RETURN_CONVENTION
jsid        gjs_intern_string_to_id          (JSContext       *context,
                                              const char      *string);

GJS_JSAPI_RETURN_CONVENTION
bool        gjs_unichar_from_string          (JSContext       *context,
                                              JS::Value        string,
                                              gunichar        *result);

/* Functions intended for more "internal" use */

void gjs_maybe_gc (JSContext *context);
void gjs_gc_if_needed(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
JS::UniqueChars format_saved_frame(JSContext* cx, JS::HandleObject saved_frame,
                                   size_t indent = 0);

/* Overloaded functions, must be outside G_DECLS. More types are intended to be
 * added as the opportunity arises. */

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext             *context,
                                 JS::HandleObject       obj,
                                 const char            *obj_description,
                                 JS::HandleId           property_name,
                                 JS::MutableHandleValue value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext       *cx,
                                 JS::HandleObject obj,
                                 const char      *description,
                                 JS::HandleId     property_name,
                                 bool            *value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext       *cx,
                                 JS::HandleObject obj,
                                 const char      *description,
                                 JS::HandleId     property_name,
                                 int32_t         *value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext* cx, JS::HandleObject obj,
                                 const char* description,
                                 JS::HandleId property_name,
                                 JS::UniqueChars* value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_property(JSContext              *cx,
                                 JS::HandleObject        obj,
                                 const char             *description,
                                 JS::HandleId            property_name,
                                 JS::MutableHandleObject value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_require_converted_property(JSContext       *context,
                                           JS::HandleObject obj,
                                           const char      *description,
                                           JS::HandleId     property_name,
                                           uint32_t        *value);

[[nodiscard]] std::string gjs_debug_bigint(JS::BigInt* bi);
[[nodiscard]] std::string gjs_debug_string(JSString* str);
[[nodiscard]] std::string gjs_debug_symbol(JS::Symbol* const sym);
[[nodiscard]] std::string gjs_debug_object(JSObject* obj);
[[nodiscard]] std::string gjs_debug_callable(JSObject* callable);
[[nodiscard]] std::string gjs_debug_value(JS::Value v);
[[nodiscard]] std::string gjs_debug_id(jsid id);

[[nodiscard]] Gjs::AutoChar gjs_hyphen_to_underscore(const char* str);
[[nodiscard]] Gjs::AutoChar gjs_hyphen_to_camel(const char* str);

#if defined(G_OS_WIN32) && (defined(_MSC_VER) && (_MSC_VER >= 1900))
[[nodiscard]] std::wstring gjs_win32_vc140_utf8_to_utf16(const char* str);
#endif

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
        static_cast<int>(JS::GCReason::FIRST_FIREFOX_REASON) + ix);
FOREACH_GC_REASON(DEFINE_GC_REASON);
#undef DEFINE_GC_REASON

#define COUNT_GC_REASON(name, ix) +1
static constexpr size_t N_REASONS = 0 FOREACH_GC_REASON(COUNT_GC_REASON);
#undef COUNT_GC_REASON
};

template <typename T>
[[nodiscard]] bool bigint_is_out_of_range(JS::BigInt* bi, T* clamped) {
    static_assert(sizeof(T) == 8, "64-bit types only");
    g_assert(bi && "bigint cannot be null");
    g_assert(clamped && "forgot out parameter");

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "Checking if BigInt %s is out of range for type %s",
                      gjs_debug_bigint(bi).c_str(), Gjs::static_type_name<T>());

    if (JS::BigIntFits(bi, clamped)) {
        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION, "BigInt %s is in the range of type %s",
            std::to_string(*clamped).c_str(), Gjs::static_type_name<T>());
        return false;
    }

    if (JS::BigIntIsNegative(bi)) {
        *clamped = std::numeric_limits<T>::min();
    } else {
        *clamped = std::numeric_limits<T>::max();
    }

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                      "BigInt %s is not in the range of type %s, clamped to %s",
                      gjs_debug_bigint(bi).c_str(), Gjs::static_type_name<T>(),
                      std::to_string(*clamped).c_str());
    return true;
}

}  // namespace Gjs

[[nodiscard]] const char* gjs_explain_gc_reason(JS::GCReason reason);

#endif  // GJS_JSAPI_UTIL_H_
