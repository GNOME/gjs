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

#include <string>  // for string, u16string
#include <type_traits>  // for enable_if_t, add_pointer_t, add_const_t
#include <utility>      // IWYU pragma: keep
#include <vector>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCAPI.h>
#include <js/GCPolicyAPI.h>  // for IgnoreGCPolicy
#include <js/Id.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jspubtd.h>     // for JSProtoKey

#include "gjs/macros.h"

class JSErrorReport;
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

struct GjsAutoTakeOwnership {};

template <typename F = void>
using GjsAutoPointerRefFunction = F* (*)(F*);

template <typename F = void>
using GjsAutoPointerFreeFunction = void (*)(F*);

template <typename T, typename F = void,
          GjsAutoPointerFreeFunction<F> free_func = free,
          GjsAutoPointerRefFunction<F> ref_func = nullptr>
struct GjsAutoPointer {
    using Tp =
        std::conditional_t<std::is_array_v<T>, std::remove_extent_t<T>, T>;
    using Ptr = std::add_pointer_t<Tp>;
    using ConstPtr = std::add_pointer_t<std::add_const_t<Tp>>;

 private:
    template <typename FunctionType, FunctionType function>
    static constexpr bool has_function() {
        using NullType = std::integral_constant<FunctionType, nullptr>;
        using ActualType = std::integral_constant<FunctionType, function>;

        return !std::is_same_v<ActualType, NullType>;
    }

 public:
    static constexpr bool has_free_function() {
        return has_function<GjsAutoPointerFreeFunction<F>, free_func>();
    }

    static constexpr bool has_ref_function() {
        return has_function<GjsAutoPointerRefFunction<F>, ref_func>();
    }

    constexpr GjsAutoPointer(Ptr ptr = nullptr)  // NOLINT(runtime/explicit)
        : m_ptr(ptr) {}
    template <typename U, typename = std::enable_if_t<std::is_same_v<U, Tp> &&
                                                      std::is_array_v<T>>>
    explicit constexpr GjsAutoPointer(U ptr[]) : m_ptr(ptr) {}

    constexpr GjsAutoPointer(Ptr ptr, const GjsAutoTakeOwnership&)
        : GjsAutoPointer(ptr) {
        m_ptr = copy();
    }
    constexpr GjsAutoPointer(ConstPtr ptr, const GjsAutoTakeOwnership& o)
        : GjsAutoPointer(const_cast<Ptr>(ptr), o) {}
    constexpr GjsAutoPointer(GjsAutoPointer&& other) : GjsAutoPointer() {
        this->swap(other);
    }
    constexpr GjsAutoPointer(GjsAutoPointer const& other) : GjsAutoPointer() {
        *this = other;
    }

    constexpr GjsAutoPointer& operator=(Ptr ptr) {
        reset(ptr);
        return *this;
    }

    GjsAutoPointer& operator=(GjsAutoPointer&& other) {
        this->swap(other);
        return *this;
    }

    GjsAutoPointer& operator=(GjsAutoPointer const& other) {
        GjsAutoPointer dup(other.get(), GjsAutoTakeOwnership());
        this->swap(dup);
        return *this;
    }

    template <typename U = T>
    constexpr std::enable_if_t<!std::is_array_v<U>, Ptr> operator->() {
        return m_ptr;
    }

    template <typename U = T>
    constexpr std::enable_if_t<!std::is_array_v<U>, ConstPtr> operator->()
        const {
        return m_ptr;
    }

    constexpr Tp operator*() const { return *m_ptr; }
    constexpr operator Ptr() { return m_ptr; }
    constexpr operator Ptr() const { return m_ptr; }
    constexpr operator ConstPtr() const { return m_ptr; }
    constexpr operator bool() const { return m_ptr != nullptr; }

    constexpr Ptr get() const { return m_ptr; }
    constexpr Ptr* out() { return &m_ptr; }

    constexpr Ptr release() {
        auto* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    constexpr void reset(Ptr ptr = nullptr) {
        Ptr old_ptr = m_ptr;
        m_ptr = ptr;

        if constexpr (has_free_function()) {
            if (old_ptr) {
                if constexpr (std::is_array_v<T>)
                    free_func(reinterpret_cast<T*>(old_ptr));
                else
                    free_func(old_ptr);
            }
        }
    }

    constexpr void swap(GjsAutoPointer& other) {
        std::swap(this->m_ptr, other.m_ptr);
    }

    /* constexpr */ ~GjsAutoPointer() {  // one day, with -std=c++2a
        reset();
    }

    template <typename U = T>
    [[nodiscard]] constexpr std::enable_if_t<!std::is_array_v<U>, Ptr> copy()
        const {
        static_assert(has_ref_function(), "No ref function provided");
        return m_ptr ? reinterpret_cast<Ptr>(ref_func(m_ptr)) : nullptr;
    }

    template <typename C>
    [[nodiscard]] constexpr C* as() const {
        return const_cast<C*>(reinterpret_cast<const C*>(m_ptr));
    }

 private:
    Ptr m_ptr;
};

template <typename T, GjsAutoPointerFreeFunction<T> free_func = free,
          GjsAutoPointerRefFunction<T> ref_func = nullptr>
struct GjsAutoPointerSimple : GjsAutoPointer<T, T, free_func, ref_func> {
    using GjsAutoPointer<T, T, free_func, ref_func>::GjsAutoPointer;
};

template <typename T, typename F = void,
          GjsAutoPointerFreeFunction<F> free_func,
          GjsAutoPointerRefFunction<F> ref_func>
constexpr bool operator==(
    GjsAutoPointer<T, F, free_func, ref_func> const& lhs,
    GjsAutoPointer<T, F, free_func, ref_func> const& rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
using GjsAutoFree = GjsAutoPointer<T>;

struct GjsAutoCharFuncs {
    static char* dup(char* str) { return g_strdup(str); }
    static void free(char* str) { g_free(str); }
};
using GjsAutoChar =
    GjsAutoPointer<char, char, GjsAutoCharFuncs::free, GjsAutoCharFuncs::dup>;

using GjsAutoChar16 = GjsAutoPointer<uint16_t, void, &g_free>;

struct GjsAutoErrorFuncs {
    static GError* error_copy(GError* error) { return g_error_copy(error); }
};
using GjsAutoError =
    GjsAutoPointer<GError, GError, g_error_free, GjsAutoErrorFuncs::error_copy>;

using GjsAutoStrv = GjsAutoPointer<char*, char*, g_strfreev, g_strdupv>;

template <typename T>
using GjsAutoUnref = GjsAutoPointer<T, void, g_object_unref, g_object_ref>;

using GjsAutoGVariant =
    GjsAutoPointer<GVariant, GVariant, g_variant_unref, g_variant_ref>;

template <typename V, typename T>
constexpr void GjsAutoPointerDeleter(T v) {
    if constexpr (std::is_array_v<V>)
        delete[] reinterpret_cast<std::remove_extent_t<V>*>(v);
    else
        delete v;
}

template <typename T>
using GjsAutoCppPointer = GjsAutoPointer<T, T, GjsAutoPointerDeleter<T>>;

template <typename T = GTypeClass>
struct GjsAutoTypeClass : GjsAutoPointer<T, void, &g_type_class_unref> {
    GjsAutoTypeClass(gpointer ptr = nullptr)  // NOLINT(runtime/explicit)
        : GjsAutoPointer<T, void, g_type_class_unref>(static_cast<T*>(ptr)) {}
    explicit GjsAutoTypeClass(GType gtype)
        : GjsAutoTypeClass(g_type_class_ref(gtype)) {}
};

// Use this class for owning a GIBaseInfo* of indeterminate type. Any type (e.g.
// GIFunctionInfo*, GIObjectInfo*) will fit. If you know that the info is of a
// certain type (e.g. you are storing the return value of a function that
// returns GIFunctionInfo*,) use one of the derived classes below.
struct GjsAutoBaseInfo : GjsAutoPointer<GIBaseInfo, GIBaseInfo,
                                        g_base_info_unref, g_base_info_ref> {
    using GjsAutoPointer::GjsAutoPointer;

    [[nodiscard]] const char* name() const {
        return g_base_info_get_name(*this);
    }
    [[nodiscard]] const char* ns() const {
        return g_base_info_get_namespace(*this);
    }
    [[nodiscard]] GIInfoType type() const {
        return g_base_info_get_type(*this);
    }
};

// Use GjsAutoInfo, preferably its typedefs below, when you know for sure that
// the info is either of a certain type or null.
template <GIInfoType TAG>
struct GjsAutoInfo : GjsAutoBaseInfo {
    using GjsAutoBaseInfo::GjsAutoBaseInfo;

    // Normally one-argument constructors should be explicit, but we are trying
    // to conform to the interface of std::unique_ptr here.
    GjsAutoInfo(GIBaseInfo* ptr = nullptr)  // NOLINT(runtime/explicit)
        : GjsAutoBaseInfo(ptr) {
        validate();
    }

    void reset(GIBaseInfo* other = nullptr) {
        GjsAutoBaseInfo::reset(other);
        validate();
    }

    // You should not need this method, because you already know the answer.
    GIInfoType type() = delete;

 private:
    void validate() const {
        if (GIBaseInfo* base = *this)
            g_assert(g_base_info_get_type(base) == TAG);
    }
};

using GjsAutoEnumInfo = GjsAutoInfo<GI_INFO_TYPE_ENUM>;
using GjsAutoFieldInfo = GjsAutoInfo<GI_INFO_TYPE_FIELD>;
using GjsAutoFunctionInfo = GjsAutoInfo<GI_INFO_TYPE_FUNCTION>;
using GjsAutoInterfaceInfo = GjsAutoInfo<GI_INFO_TYPE_INTERFACE>;
using GjsAutoObjectInfo = GjsAutoInfo<GI_INFO_TYPE_OBJECT>;
using GjsAutoPropertyInfo = GjsAutoInfo<GI_INFO_TYPE_PROPERTY>;
using GjsAutoStructInfo = GjsAutoInfo<GI_INFO_TYPE_STRUCT>;
using GjsAutoTypeInfo = GjsAutoInfo<GI_INFO_TYPE_TYPE>;
using GjsAutoValueInfo = GjsAutoInfo<GI_INFO_TYPE_VALUE>;
using GjsAutoVFuncInfo = GjsAutoInfo<GI_INFO_TYPE_VFUNC>;

// GICallableInfo can be one of several tags, so we have to have a separate
// class, and use GI_IS_CALLABLE_INFO() to validate.
struct GjsAutoCallableInfo : GjsAutoBaseInfo {
    using GjsAutoBaseInfo::GjsAutoBaseInfo;

    GjsAutoCallableInfo(GIBaseInfo* ptr = nullptr)  // NOLINT(runtime/explicit)
        : GjsAutoBaseInfo(ptr) {
        validate();
    }

    void reset(GIBaseInfo* other = nullptr) {
        GjsAutoBaseInfo::reset(other);
        validate();
    }

 private:
    void validate() const {
        if (*this)
            g_assert(GI_IS_CALLABLE_INFO(get()));
    }
};

template <typename T>
struct GjsSmartPointer : GjsAutoPointer<T> {
    using GjsAutoPointer<T>::GjsAutoPointer;
};

template <>
struct GjsSmartPointer<char*> : GjsAutoStrv {
    using GjsAutoStrv::GjsAutoPointer;
};

template <>
struct GjsSmartPointer<GStrv> : GjsAutoStrv {
    using GjsAutoStrv::GjsAutoPointer;
};

template <>
struct GjsSmartPointer<GObject> : GjsAutoUnref<GObject> {
    using GjsAutoUnref<GObject>::GjsAutoUnref;
};

template <>
struct GjsSmartPointer<GIBaseInfo> : GjsAutoBaseInfo {
    using GjsAutoBaseInfo::GjsAutoBaseInfo;
};

template <>
struct GjsSmartPointer<GError> : GjsAutoError {
    using GjsAutoError::GjsAutoError;
};

template <>
struct GjsSmartPointer<GVariant> : GjsAutoGVariant {
    using GjsAutoGVariant::GjsAutoPointer;
};

template <>
struct GjsSmartPointer<GList> : GjsAutoPointer<GList, GList, g_list_free> {
    using GjsAutoPointer::GjsAutoPointer;
};

template <>
struct GjsSmartPointer<GSList> : GjsAutoPointer<GSList, GSList, g_slist_free> {
    using GjsAutoPointer::GjsAutoPointer;
};

/* For use of GjsAutoInfo<TAG> in GC hash maps */
namespace JS {
template <GIInfoType TAG>
struct GCPolicy<GjsAutoInfo<TAG>> : public IgnoreGCPolicy<GjsAutoInfo<TAG>> {};
}  // namespace JS

using GjsAutoParam = GjsAutoPointer<GParamSpec, GParamSpec, g_param_spec_unref,
                                    g_param_spec_ref>;

/* For use of GjsAutoParam in GC hash maps */
namespace JS {
template <>
struct GCPolicy<GjsAutoParam> : public IgnoreGCPolicy<GjsAutoParam> {};
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

[[nodiscard]] JSObject* gjs_get_import_global(JSContext* cx);

[[nodiscard]] JSObject* gjs_get_internal_global(JSContext* cx);

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
                                                    JSProtoKey error_kind,
                                                    const char* error_name,
                                                    const char* format, ...);
void        gjs_throw_literal                (JSContext       *context,
                                              const char      *string);
bool gjs_throw_gerror_message(JSContext* cx, GError* error);

bool        gjs_log_exception                (JSContext       *context);

bool gjs_log_exception_uncaught(JSContext* cx);

bool gjs_log_exception_full(JSContext* cx, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags level);

[[nodiscard]] std::string gjs_value_debug_string(JSContext* cx,
                                                 JS::HandleValue value);

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
bool gjs_string_to_filename(JSContext       *cx,
                            const JS::Value  string_val,
                            GjsAutoChar     *filename_string);

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
GjsAutoChar gjs_format_stack_trace(JSContext       *cx,
                                   JS::HandleObject saved_frame);

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

[[nodiscard]] std::string gjs_debug_string(JSString* str);
[[nodiscard]] std::string gjs_debug_symbol(JS::Symbol* const sym);
[[nodiscard]] std::string gjs_debug_object(JSObject* obj);
[[nodiscard]] std::string gjs_debug_value(JS::Value v);
[[nodiscard]] std::string gjs_debug_id(jsid id);

[[nodiscard]] GjsAutoChar gjs_hyphen_to_underscore(const char* str);
[[nodiscard]] GjsAutoChar gjs_hyphen_to_camel(const char* str);

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
    macro(GJS_API_CALL, 3)
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

}  // namespace Gjs

[[nodiscard]] const char* gjs_explain_gc_reason(JS::GCReason reason);

#endif  // GJS_JSAPI_UTIL_H_
