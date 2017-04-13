/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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
#include <stdbool.h>

#include <glib-object.h>

#include "jsapi-wrapper.h"
#include "gjs/runtime.h"
#include "gi/gtype.h"

class GjsAutoChar : public std::unique_ptr<char, decltype(&g_free)> {
public:
    typedef std::unique_ptr<char, decltype(&g_free)> U;

    GjsAutoChar(char *str = nullptr) : U(str, g_free) {}

    operator const char *() {
        return get();
    }

    void operator= (const char* str) {
        reset(const_cast<char*>(str));
    }
};

template <typename T>
class GjsAutoUnref : public std::unique_ptr<T, decltype(&g_object_unref)> {
public:
    typedef std::unique_ptr<T, decltype(&g_object_unref)> U;

    GjsAutoUnref(T *ptr = nullptr) : U(ptr, g_object_unref) {}

    operator T *() {
        return U::get();
    }
};

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

typedef enum {
    GJS_GLOBAL_SLOT_IMPORTS,
    GJS_GLOBAL_SLOT_BYTE_ARRAY_PROTOTYPE,
    GJS_GLOBAL_SLOT_LAST,
} GjsGlobalSlot;

typedef struct GjsRootedArray GjsRootedArray;

/* Flags that should be set on properties exported from native code modules.
 * Basically set these on API, but do NOT set them on data.
 *
 * READONLY:  forbid setting prop to another value
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

bool gjs_init_context_standard(JSContext              *context,
                               JS::MutableHandleObject global);

JSObject*   gjs_get_import_global            (JSContext       *context);

JS::Value   gjs_get_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot);
void        gjs_set_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot,
                                              JS::Value        value);

void gjs_throw_constructor_error             (JSContext       *context);

void gjs_throw_abstract_constructor_error(JSContext    *context,
                                          JS::CallArgs& args);

JSObject*   gjs_build_string_array           (JSContext       *context,
                                              gssize           array_length,
                                              char           **array_values);

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
                                              const char      *error_class,
                                              const char      *error_name,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (4, 5);
void        gjs_throw_literal                (JSContext       *context,
                                              const char      *string);
void        gjs_throw_g_error                (JSContext       *context,
                                              GError          *error);

bool        gjs_log_exception                (JSContext       *context);

bool gjs_log_exception_full(JSContext       *context,
                            JS::HandleValue  exc,
                            JS::HandleString message);

char *gjs_value_debug_string(JSContext      *context,
                             JS::HandleValue value);

bool gjs_call_function_value(JSContext                  *context,
                             JS::HandleObject            obj,
                             JS::HandleValue             fval,
                             const JS::HandleValueArray& args,
                             JS::MutableHandleValue      rval);

void        gjs_error_reporter               (JSContext       *context,
                                              const char      *message,
                                              JSErrorReport   *report);

bool        gjs_string_to_utf8               (JSContext       *context,
                                              const JS::Value  string_val,
                                              char           **utf8_string_p);
bool gjs_string_from_utf8(JSContext             *context,
                          const char            *utf8_string,
                          ssize_t                n_bytes,
                          JS::MutableHandleValue value_p);

bool        gjs_string_to_filename           (JSContext       *context,
                                              const JS::Value  string_val,
                                              char           **filename_string_p);
bool gjs_string_from_filename(JSContext             *context,
                              const char            *filename_string,
                              ssize_t                n_bytes,
                              JS::MutableHandleValue value_p);

bool gjs_string_get_char16_data(JSContext *context,
                                JS::Value  value,
                                char16_t **data_p,
                                size_t    *len_p);

bool gjs_string_to_ucs4(JSContext      *cx,
                        JS::HandleValue value,
                        gunichar      **ucs4_string_p,
                        size_t         *len_p);
bool gjs_string_from_ucs4(JSContext             *cx,
                          const gunichar        *ucs4_string,
                          ssize_t                n_chars,
                          JS::MutableHandleValue value_p);

bool        gjs_get_string_id                (JSContext       *context,
                                              jsid             id,
                                              char           **name_p);
jsid        gjs_intern_string_to_id          (JSContext       *context,
                                              const char      *string);

bool        gjs_unichar_from_string          (JSContext       *context,
                                              JS::Value        string,
                                              gunichar        *result);

const char* gjs_get_type_name                (JS::Value        value);

/* Functions intended for more "internal" use */

void gjs_maybe_gc (JSContext *context);

bool gjs_context_get_frame_info(JSContext                              *context,
                                mozilla::Maybe<JS::MutableHandleValue>& stack,
                                mozilla::Maybe<JS::MutableHandleValue>& fileName,
                                mozilla::Maybe<JS::MutableHandleValue>& lineNumber);

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
  GJS_STRING_KEEP_ALIVE_MARKER,
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
  GJS_STRING_NAME,
  GJS_STRING_X,
  GJS_STRING_Y,
  GJS_STRING_WIDTH,
  GJS_STRING_HEIGHT,
  GJS_STRING_MODULE_PATH,
  GJS_STRING_LAST
} GjsConstString;

const char * gjs_strip_unix_shebang(const char *script,
                                    gssize     *script_len,
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

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                JS::HandleValue  value,
                                unsigned         flags,
                                JSNative         getter = nullptr,
                                JSNative         setter = nullptr);

bool gjs_object_define_property(JSContext       *cx,
                                JS::HandleObject obj,
                                GjsConstString   property_name,
                                JS::HandleObject value,
                                unsigned         flags,
                                JSNative         getter = nullptr,
                                JSNative         setter = nullptr);

JS::HandleId gjs_context_get_const_string(JSContext     *cx,
                                          GjsConstString string);

/* Overloaded functions, must be outside G_DECLS. More types are intended to be
 * added as the opportunity arises. */

bool gjs_object_require_property(JSContext             *context,
                                 JS::HandleObject       obj,
                                 const char            *obj_description,
                                 JS::HandleId           property_name,
                                 JS::MutableHandleValue value);

bool gjs_object_require_property(JSContext       *cx,
                                 JS::HandleObject obj,
                                 const char      *description,
                                 JS::HandleId     property_name,
                                 bool            *value);

bool gjs_object_require_property(JSContext       *cx,
                                 JS::HandleObject obj,
                                 const char      *description,
                                 JS::HandleId     property_name,
                                 int32_t         *value);

bool gjs_object_require_property(JSContext       *cx,
                                 JS::HandleObject obj,
                                 const char      *description,
                                 JS::HandleId     property_name,
                                 char           **value);

bool gjs_object_require_property(JSContext              *cx,
                                 JS::HandleObject        obj,
                                 const char             *description,
                                 JS::HandleId            property_name,
                                 JS::MutableHandleObject value);

bool gjs_object_require_converted_property(JSContext       *context,
                                           JS::HandleObject obj,
                                           const char      *description,
                                           JS::HandleId     property_name,
                                           uint32_t        *value);

/* Here, too, we have wrappers that take a GjsConstString. */

template<typename T>
bool gjs_object_require_property(JSContext        *cx,
                                 JS::HandleObject  obj,
                                 const char       *description,
                                 GjsConstString    property_name,
                                 T                 value)
{
    return gjs_object_require_property(cx, obj, description,
                                       gjs_context_get_const_string(cx, property_name),
                                       value);
}

template<typename T>
bool gjs_object_require_converted_property(JSContext       *cx,
                                           JS::HandleObject obj,
                                           const char      *description,
                                           GjsConstString   property_name,
                                           T                value)
{
    return gjs_object_require_converted_property(cx, obj, description,
                                                 gjs_context_get_const_string(cx, property_name),
                                                 value);
}

#endif  /* __GJS_JSAPI_UTIL_H__ */
