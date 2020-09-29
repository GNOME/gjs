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

#include <js/GCPolicyAPI.h>  // for IgnoreGCPolicy
#include <js/Id.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jspubtd.h>     // for JSProtoKey

#include "gjs/macros.h"

class JSErrorReport;
namespace JS {
class CallArgs;
}

struct GjsAutoTakeOwnership {};

template <typename T, typename F = void, void (*free_func)(F*) = free,
          F* (*ref_func)(F*) = nullptr>
struct GjsAutoPointer {
    using Ptr = std::add_pointer_t<T>;
    using ConstPtr = std::add_pointer_t<std::add_const_t<T>>;

    constexpr GjsAutoPointer(Ptr ptr = nullptr)  // NOLINT(runtime/explicit)
        : m_ptr(ptr) {}
    constexpr GjsAutoPointer(Ptr ptr, const GjsAutoTakeOwnership&)
        : GjsAutoPointer(ptr) {
        // FIXME: should use if constexpr (...), but that doesn't work with
        // ubsan, which generates a null pointer check making it not a constexpr
        // anymore: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71962 - Also a
        // bogus warning, https://gcc.gnu.org/bugzilla/show_bug.cgi?id=94554
        m_ptr = copy();
    }
    constexpr GjsAutoPointer(ConstPtr ptr, const GjsAutoTakeOwnership& o)
        : GjsAutoPointer(const_cast<Ptr>(ptr), o) {}
    constexpr GjsAutoPointer(GjsAutoPointer&& other) : GjsAutoPointer() {
        this->swap(other);
    }

    constexpr GjsAutoPointer& operator=(Ptr ptr) {
        reset(ptr);
        return *this;
    }

    GjsAutoPointer& operator=(GjsAutoPointer&& other) {
        this->swap(other);
        return *this;
    }

    constexpr T operator*() const { return *m_ptr; }
    constexpr Ptr operator->() { return m_ptr; }
    constexpr ConstPtr operator->() const { return m_ptr; }
    constexpr operator Ptr() { return m_ptr; }
    constexpr operator Ptr() const { return m_ptr; }
    constexpr operator ConstPtr() const { return m_ptr; }
    constexpr operator bool() const { return m_ptr != nullptr; }

    constexpr Ptr get() { return m_ptr; }
    constexpr ConstPtr get() const { return m_ptr; }

    constexpr Ptr release() {
        auto* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    constexpr void reset(Ptr ptr = nullptr) {
        // FIXME: Should use if constexpr (...) as above
        auto ffunc = free_func;
        Ptr old_ptr = m_ptr;
        m_ptr = ptr;
        if (old_ptr && ffunc)
            ffunc(old_ptr);
    }

    constexpr void swap(GjsAutoPointer& other) {
        std::swap(this->m_ptr, other.m_ptr);
    }

    /* constexpr */ ~GjsAutoPointer() {  // one day, with -std=c++2a
        reset();
    }

    [[nodiscard]] constexpr Ptr copy() const {
        // FIXME: Should use std::enable_if_t<ref_func != nullptr, Ptr>
        if (!m_ptr)
            return nullptr;

        auto rf = ref_func;
        g_assert(rf);
        return reinterpret_cast<Ptr>(ref_func(m_ptr));
    }

    template <typename C>
    [[nodiscard]] constexpr C* as() const {
        return const_cast<C*>(reinterpret_cast<const C*>(m_ptr));
    }

 private:
    Ptr m_ptr;
};

template <typename T>
using GjsAutoFree = GjsAutoPointer<T>;

struct GjsAutoCharFuncs {
    static char* dup(char* str) { return g_strdup(str); }
    static void free(char* str) { g_free(str); }
};
using GjsAutoChar =
    GjsAutoPointer<char, char, GjsAutoCharFuncs::free, GjsAutoCharFuncs::dup>;

using GjsAutoStrv = GjsAutoPointer<char*, char*, g_strfreev>;

template <typename T>
using GjsAutoUnref = GjsAutoPointer<T, void, g_object_unref, g_object_ref>;

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
    GjsAutoBaseInfo(GIBaseInfo* ptr = nullptr)  // NOLINT(runtime/explicit)
        : GjsAutoPointer(ptr) {}

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

void gjs_throw_constructor_error             (JSContext       *context);

void gjs_throw_abstract_constructor_error(JSContext    *context,
                                          JS::CallArgs& args);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_build_string_array(JSContext* cx,
                                 const std::vector<std::string>& strings);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_define_string_array(JSContext* cx, JS::HandleObject obj,
                                  const char* array_name,
                                  const std::vector<std::string>& strings,
                                  unsigned attrs);

void        gjs_throw                        (JSContext       *context,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (2, 3);
void        gjs_throw_custom                 (JSContext       *context,
                                              JSProtoKey       error_kind,
                                              const char      *error_name,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (4, 5);
void        gjs_throw_literal                (JSContext       *context,
                                              const char      *string);
bool gjs_throw_gerror_message(JSContext* cx, GError* error);

bool        gjs_log_exception                (JSContext       *context);

bool gjs_log_exception_uncaught(JSContext* cx);

bool gjs_log_exception_full(JSContext* cx, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags level);

[[nodiscard]] char* gjs_value_debug_string(JSContext* cx,
                                           JS::HandleValue value);

void gjs_warning_reporter(JSContext*, JSErrorReport* report);

GJS_JSAPI_RETURN_CONVENTION
JS::UniqueChars gjs_string_to_utf8(JSContext* cx, const JS::Value string_val);
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

[[nodiscard]] std::u16string gjs_utf8_script_to_utf16(const char* script,
                                                      ssize_t len);

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

[[nodiscard]] char* gjs_hyphen_to_underscore(const char* str);

#if defined(G_OS_WIN32) && (defined(_MSC_VER) && (_MSC_VER >= 1900))
[[nodiscard]] std::wstring gjs_win32_vc140_utf8_to_utf16(const char* str);
#endif

#endif  // GJS_JSAPI_UTIL_H_
