/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __GJS_JSAPI_UTIL_H__
#define __GJS_JSAPI_UTIL_H__

#include <memory>
#include <string>
#include <stdbool.h>

#include <glib-object.h>

#include "gi/gtype.h"
#include "gjs/macros.h"
#include "jsapi-wrapper.h"

#ifdef __GNUC__
#define GJS_ALWAYS_INLINE __attribute__((always_inline))
#else
#define GJS_ALWAYS_INLINE
#endif

struct GjsAutoTakeOwnership {};

template <typename T, typename F,
          void (*free_func)(F*) = nullptr, F* (*ref_func)(F*) = nullptr>
struct GjsAutoPointer : std::unique_ptr<T, decltype(free_func)> {
    GjsAutoPointer(T* ptr = nullptr)  // NOLINT(runtime/explicit)
        : GjsAutoPointer::unique_ptr(ptr, *free_func) {}
    GjsAutoPointer(T* ptr, const GjsAutoTakeOwnership&)
        : GjsAutoPointer(nullptr) {
        auto ref = ref_func;  // use if constexpr ... once we're on C++17
        this->reset(ptr && ref ? reinterpret_cast<T*>(ref(ptr)) : ptr);
    }

    operator T*() const { return this->get(); }
    T& operator[](size_t i) const { return static_cast<T*>(*this)[i]; }

    GJS_USE
    T* copy() const { return reinterpret_cast<T*>(ref_func(this->get())); }

    template <typename C>
    GJS_USE C* as() const {
        return const_cast<C*>(reinterpret_cast<const C*>(this->get()));
    }
};

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

    GJS_USE const char* name() const { return g_base_info_get_name(*this); }
    GJS_USE const char* ns() const { return g_base_info_get_namespace(*this); }
    GJS_USE GIInfoType type() const { return g_base_info_get_type(*this); }
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
        if (GIBaseInfo* base = *this)
            g_assert(GI_IS_CALLABLE_INFO(base));
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

G_BEGIN_DECLS

#define GJS_UTIL_ERROR gjs_util_error_quark ()
GQuark gjs_util_error_quark (void);
enum {
  GJS_UTIL_ERROR_NONE,
  GJS_UTIL_ERROR_ARGUMENT_INVALID,
  GJS_UTIL_ERROR_ARGUMENT_UNDERFLOW,
  GJS_UTIL_ERROR_ARGUMENT_OVERFLOW,
  GJS_UTIL_ERROR_ARGUMENT_TYPE_MISMATCH
};

typedef struct GjsRootedArray GjsRootedArray;

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
#define GJS_GET_THIS(cx, argc, vp, args, to)                   \
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);          \
    JS::RootedObject to(cx, &args.computeThis(cx).toObject())

GJS_USE
JSObject*   gjs_get_import_global            (JSContext       *context);

void gjs_throw_constructor_error             (JSContext       *context);

void gjs_throw_abstract_constructor_error(JSContext    *context,
                                          JS::CallArgs& args);

GJS_JSAPI_RETURN_CONVENTION
JSObject*   gjs_build_string_array           (JSContext       *context,
                                              gssize           array_length,
                                              char           **array_values);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_define_string_array(JSContext       *context,
                                  JS::HandleObject obj,
                                  const char      *array_name,
                                  ssize_t          array_length,
                                  const char     **array_values,
                                  unsigned         attrs);

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

bool gjs_log_exception_full(JSContext       *context,
                            JS::HandleValue  exc,
                            JS::HandleString message);

GJS_USE
char *gjs_value_debug_string(JSContext      *context,
                             JS::HandleValue value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_call_function_value(JSContext                  *context,
                             JS::HandleObject            obj,
                             JS::HandleValue             fval,
                             const JS::HandleValueArray& args,
                             JS::MutableHandleValue      rval);

void gjs_warning_reporter(JSContext     *cx,
                          JSErrorReport *report);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_string_to_utf8(JSContext* cx, const JS::Value string_val,
                        JS::UniqueChars* utf8_string_p);
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
GJS_USE
jsid        gjs_intern_string_to_id          (JSContext       *context,
                                              const char      *string);

GJS_JSAPI_RETURN_CONVENTION
bool        gjs_unichar_from_string          (JSContext       *context,
                                              JS::Value        string,
                                              gunichar        *result);

/* Functions intended for more "internal" use */

void gjs_maybe_gc (JSContext *context);
void gjs_schedule_gc_if_needed(JSContext *cx);
void gjs_gc_if_needed(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_eval_with_scope(JSContext             *context,
                         JS::HandleObject       object,
                         const char            *script,
                         ssize_t                script_len,
                         const char            *filename,
                         JS::MutableHandleValue retval);

typedef enum {
  GJS_STRING_CONSTRUCTOR,
  GJS_STRING_PROTOTYPE,
  GJS_STRING_LENGTH,
  GJS_STRING_IMPORTS,
  GJS_STRING_PARENT_MODULE,
  GJS_STRING_MODULE_INIT,
  GJS_STRING_SEARCH_PATH,
  GJS_STRING_PRIVATE_NS_MARKER,
  GJS_STRING_GI_MODULE,
  GJS_STRING_GI_VERSIONS,
  GJS_STRING_GI_OVERRIDES,
  GJS_STRING_GOBJECT_INIT,
  GJS_STRING_INSTANCE_INIT,
  GJS_STRING_NEW_INTERNAL,
  GJS_STRING_NEW,
  GJS_STRING_MESSAGE,
  GJS_STRING_CODE,
  GJS_STRING_STACK,
  GJS_STRING_FILENAME,
  GJS_STRING_LINE_NUMBER,
  GJS_STRING_COLUMN_NUMBER,
  GJS_STRING_NAME,
  GJS_STRING_X,
  GJS_STRING_Y,
  GJS_STRING_WIDTH,
  GJS_STRING_HEIGHT,
  GJS_STRING_MODULE_PATH,
  GJS_STRING_LAST
} GjsConstString;

GJS_USE
const char * gjs_strip_unix_shebang(const char *script,
                                    size_t     *script_len,
                                    int        *new_start_line_number);

/* These four functions wrap JS_GetPropertyById(), etc., but with a
 * GjsConstString constant instead of a jsid. */

bool gjs_object_get_property(JSContext             *cx,
                             JS::HandleObject       obj,
                             GjsConstString         property_name,
                             JS::MutableHandleValue value_p);

bool gjs_object_set_property(JSContext       *cx,
                             JS::HandleObject obj,
                             GjsConstString   property_name,
                             JS::HandleValue  value);

bool gjs_object_has_property(JSContext       *cx,
                             JS::HandleObject obj,
                             GjsConstString   property_name,
                             bool            *found);

G_END_DECLS

GJS_JSAPI_RETURN_CONVENTION
GjsAutoChar gjs_format_stack_trace(JSContext       *cx,
                                   JS::HandleObject saved_frame);

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                JS::HandleValue  value,
                                unsigned         flags);

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                JS::HandleObject value,
                                unsigned         flags);

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                JS::HandleString value,
                                unsigned         flags);

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                uint32_t         value,
                                unsigned         flags);

JS::HandleId gjs_context_get_const_string(JSContext     *cx,
                                          GjsConstString string);

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

/* Here, too, we have wrappers that take a GjsConstString. */

template <typename T>
GJS_JSAPI_RETURN_CONVENTION bool gjs_object_require_property(
    JSContext* cx, JS::HandleObject obj, const char* description,
    GjsConstString property_name, T value) {
    return gjs_object_require_property(cx, obj, description,
                                       gjs_context_get_const_string(cx, property_name),
                                       value);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION bool gjs_object_require_converted_property(
    JSContext* cx, JS::HandleObject obj, const char* description,
    GjsConstString property_name, T value) {
    return gjs_object_require_converted_property(cx, obj, description,
                                                 gjs_context_get_const_string(cx, property_name),
                                                 value);
}

GJS_USE std::string gjs_debug_string(JSString* str);
GJS_USE std::string gjs_debug_symbol(JS::Symbol* const sym);
GJS_USE std::string gjs_debug_object(JSObject* obj);
GJS_USE std::string gjs_debug_value(JS::Value v);
GJS_USE std::string gjs_debug_id(jsid id);

GJS_USE
char* gjs_hyphen_to_underscore(const char* str);

#endif  /* __GJS_JSAPI_UTIL_H__ */
