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

#include <stdbool.h>

#include <glib-object.h>

#include "gjs/compat.h"
#include "gjs/runtime.h"
#include "gi/gtype.h"

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
    GJS_GLOBAL_SLOT_KEEP_ALIVE,
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
 * Helper methods to access private data:
 *
 * do_base_typecheck: checks that object has the right JSClass, and possibly
 *                    throw a TypeError exception if the check fails
 * priv_from_js: accesses the object private field; as a debug measure,
 *               it also checks that the object is of a compatible
 *               JSClass, but it doesn't raise an exception (it
 *               wouldn't be of much use, if subsequent code crashes on
 *               NULL)
 * priv_from_js_with_typecheck: a convenience function to call
 *                              do_base_typecheck and priv_from_js
 */
#define GJS_DEFINE_PRIV_FROM_JS(type, klass)                          \
    __attribute__((unused)) static inline bool                          \
    do_base_typecheck(JSContext       *context,                         \
                      JS::HandleObject object,                          \
                      bool             throw_error)                     \
    {                                                                   \
        return gjs_typecheck_instance(context, object, &klass, throw_error);  \
    }                                                                   \
    static inline type *                                                \
    priv_from_js(JSContext       *context,                              \
                 JS::HandleObject object)                               \
    {                                                                   \
        type *priv;                                                     \
        JS_BeginRequest(context);                                       \
        priv = (type*) JS_GetInstancePrivate(context, object, &klass, NULL);  \
        JS_EndRequest(context);                                         \
        return priv;                                                    \
    }                                                                   \
    __attribute__((unused)) static bool                                 \
    priv_from_js_with_typecheck(JSContext       *context,               \
                                JS::HandleObject object,                \
                                type           **out)                   \
    {                                                                   \
        if (!do_base_typecheck(context, object, false))                 \
            return false;                                               \
        *out = priv_from_js(context, object);                           \
        return true;                                                    \
    }

/**
 * GJS_DEFINE_PROTO:
 * @tn: The name of the prototype, as a string
 * @cn: The name of the prototype, separated by _
 * @flags: additional JSClass flags, such as JSCLASS_BACKGROUND_FINALIZE
 *
 * A convenience macro for prototype implementations.
 */
#define GJS_DEFINE_PROTO(tn, cn, flags) \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn); \
_GJS_DEFINE_PROTO_FULL(tn, cn, gjs_##cn##_constructor, G_TYPE_NONE, flags)

/**
 * GJS_DEFINE_PROTO_ABSTRACT:
 * @tn: The name of the prototype, as a string
 * @cn: The name of the prototype, separated by _
 *
 * A convenience macro for prototype implementations.
 * Similar to GJS_DEFINE_PROTO but marks the prototype as abstract,
 * you won't be able to instantiate it using the new keyword
 */
#define GJS_DEFINE_PROTO_ABSTRACT(tn, cn, flags) \
_GJS_DEFINE_PROTO_FULL(tn, cn, NULL, G_TYPE_NONE, flags)

#define GJS_DEFINE_PROTO_WITH_GTYPE(tn, cn, gtype, flags)   \
GJS_NATIVE_CONSTRUCTOR_DECLARE(cn); \
_GJS_DEFINE_PROTO_FULL(tn, cn, gjs_##cn##_constructor, gtype, flags)

#define GJS_DEFINE_PROTO_ABSTRACT_WITH_GTYPE(tn, cn, gtype, flags)   \
_GJS_DEFINE_PROTO_FULL(tn, cn, NULL, gtype, flags)

#define _GJS_DEFINE_PROTO_FULL(type_name, cname, ctor, gtype, jsclass_flags)     \
extern JSPropertySpec gjs_##cname##_proto_props[]; \
extern JSFunctionSpec gjs_##cname##_proto_funcs[]; \
static void gjs_##cname##_finalize(JSFreeOp *fop, JSObject *obj); \
static bool gjs_##cname##_new_resolve(JSContext *context, \
                                      JSObject  *obj, \
                                      JS::Value  id, \
                                      unsigned   flags, \
                                      JSObject **objp) \
{ \
    return true; \
} \
static struct JSClass gjs_##cname##_class = { \
    type_name, \
    JSCLASS_HAS_PRIVATE | \
    JSCLASS_NEW_RESOLVE | jsclass_flags, \
    JS_PropertyStub, \
    JS_DeletePropertyStub, \
    JS_PropertyStub, \
    JS_StrictPropertyStub, \
    JS_EnumerateStub,\
    (JSResolveOp) gjs_##cname##_new_resolve, \
    JS_ConvertStub, \
    gjs_##cname##_finalize, \
    JSCLASS_NO_OPTIONAL_MEMBERS \
}; \
JS::Value gjs_##cname##_create_proto(JSContext *context, JSObject *module, const char *proto_name, JSObject *parent) \
{ \
    JS::Value rval; \
    JSObject *global = gjs_get_import_global(context); \
    jsid class_name = gjs_intern_string_to_id(context, gjs_##cname##_class.name); \
    if (!JS_GetPropertyById(context, global, class_name, &rval))                       \
        return JS::NullValue(); \
    if (rval.isUndefined()) { \
        JS::Value value; \
        JSObject *prototype = JS_InitClass(context, global,     \
                                 parent, \
                                 &gjs_##cname##_class, \
                                 ctor, \
                                 0, \
                                 &gjs_##cname##_proto_props[0], \
                                 &gjs_##cname##_proto_funcs[0], \
                                 NULL, \
                                 NULL); \
        if (prototype == NULL) { \
            return JS::NullValue(); \
        } \
        if (!gjs_object_require_property( \
                context, global, NULL, \
                class_name, &rval)) { \
            return JS::NullValue(); \
        } \
        if (!JS_DefineProperty(context, module, proto_name, \
                               rval, NULL, NULL, GJS_MODULE_PROP_FLAGS)) \
            return JS::NullValue(); \
        if (gtype != G_TYPE_NONE) { \
            value = JS::ObjectOrNullValue(gjs_gtype_create_gtype_wrapper(context, gtype)); \
            JS_DefineProperty(context, &rval.toObject(), "$gtype", value, \
                              NULL, NULL, JSPROP_PERMANENT);            \
        } \
    } \
    return rval; \
}

/**
 * GJS_NATIVE_CONSTRUCTOR_DECLARE:
 * Prototype a constructor.
 */
#define GJS_NATIVE_CONSTRUCTOR_DECLARE(name)            \
static JSBool                                           \
gjs_##name##_constructor(JSContext  *context,           \
                         unsigned    argc,              \
                         JS::Value  *vp)

/**
 * GJS_NATIVE_CONSTRUCTOR_VARIABLES:
 * Declare variables necessary for the constructor; should
 * be at the very top.
 */
#define GJS_NATIVE_CONSTRUCTOR_VARIABLES(name)          \
    JS::RootedObject object(context, NULL);                         \
    JS::CallArgs argv G_GNUC_UNUSED = JS::CallArgsFromVp(argc, vp);

/**
 * GJS_NATIVE_CONSTRUCTOR_PRELUDE:
 * Call after the initial variable declaration.
 */
#define GJS_NATIVE_CONSTRUCTOR_PRELUDE(name)                                   \
    {                                                                          \
        if (!JS_IsConstructing(context, vp)) {                                 \
            gjs_throw_constructor_error(context);                              \
            return false;                                                      \
        }                                                                      \
        object = JS_NewObjectForConstructor(context, &gjs_##name##_class, vp); \
        if (object == NULL)                                                    \
            return false;                                                      \
    }

/**
 * GJS_NATIVE_CONSTRUCTOR_FINISH:
 * Call this at the end of a constructor when it's completed
 * successfully.
 */
#define GJS_NATIVE_CONSTRUCTOR_FINISH(name)             \
    argv.rval().setObject(*object);

/**
 * GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT:
 * Defines a constructor whose only purpose is to throw an error
 * and fail. To be used with classes that require a constructor (because they have
 * instances), but whose constructor cannot be used from JS code.
 */
#define GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(name)            \
    GJS_NATIVE_CONSTRUCTOR_DECLARE(name)                        \
    {                                                           \
        gjs_throw_abstract_constructor_error(context, vp);      \
        return false;                                           \
    }

bool        gjs_init_context_standard        (JSContext       *context,
                                              JSObject       **global_out);

JSObject*   gjs_get_import_global            (JSContext       *context);

JS::Value   gjs_get_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot);
void        gjs_set_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot,
                                              JS::Value        value);

bool        gjs_object_require_property      (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *obj_description,
                                              jsid             property_name,
                                              JS::Value       *value_p);

bool        gjs_init_class_dynamic           (JSContext       *context,
                                              JSObject        *in_object,
                                              JSObject        *parent_proto,
                                              const char      *ns_name,
                                              const char      *class_name,
                                              JSClass         *clasp,
                                              JSNative         constructor,
                                              unsigned         nargs,
                                              JSPropertySpec  *ps,
                                              JSFunctionSpec  *fs,
                                              JSPropertySpec  *static_ps,
                                              JSFunctionSpec  *static_fs,
                                              JSObject       **constructor_p,
                                              JSObject       **prototype_p);
void gjs_throw_constructor_error             (JSContext       *context);
void gjs_throw_abstract_constructor_error    (JSContext       *context,
                                              JS::Value       *vp);

bool        gjs_typecheck_instance            (JSContext  *context,
                                               JSObject   *obj,
                                               JSClass    *static_clasp,
                                               bool        _throw);

JSObject*   gjs_construct_object_dynamic     (JSContext       *context,
                                              JSObject        *proto,
                                              unsigned         argc,
                                              JS::Value       *argv);
JSObject*   gjs_build_string_array           (JSContext       *context,
                                              gssize           array_length,
                                              char           **array_values);
JSObject*   gjs_define_string_array          (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *array_name,
                                              gssize           array_length,
                                              const char     **array_values,
                                              unsigned         attrs);
void        gjs_throw                        (JSContext       *context,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (2, 3);
void        gjs_throw_custom                 (JSContext       *context,
                                              const char      *error_class,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (3, 4);
void        gjs_throw_literal                (JSContext       *context,
                                              const char      *string);
void        gjs_throw_g_error                (JSContext       *context,
                                              GError          *error);

bool        gjs_log_exception                (JSContext       *context);
bool        gjs_log_and_keep_exception       (JSContext       *context);
bool        gjs_move_exception               (JSContext       *src_context,
                                              JSContext       *dest_context);
bool        gjs_log_exception_full           (JSContext       *context,
                                              JS::Value        exc,
                                              JSString        *message);

#ifdef __GJS_UTIL_LOG_H__
void        gjs_log_object_props             (JSContext       *context,
                                              JSObject        *obj,
                                              GjsDebugTopic    topic,
                                              const char      *prefix);
#endif
char*       gjs_value_debug_string           (JSContext       *context,
                                              JS::Value        value);
void        gjs_explain_scope                (JSContext       *context,
                                              const char      *title);
bool        gjs_call_function_value          (JSContext       *context,
                                              JSObject        *obj,
                                              JS::Value        fval,
                                              unsigned         argc,
                                              JS::Value       *argv,
                                              JS::Value       *rval);
void        gjs_error_reporter               (JSContext       *context,
                                              const char      *message,
                                              JSErrorReport   *report);
JSObject*   gjs_get_global_object            (JSContext *cx);
bool        gjs_get_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              JS::Value        id,
                                              JS::Value       *value_p);
bool        gjs_set_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              JS::Value        id,
                                              JS::Value       *value_p);
bool        gjs_add_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              JS::Value        id,
                                              JS::Value       *value_p);
bool        gjs_delete_prop_verbose_stub     (JSContext       *context,
                                              JSObject        *obj,
                                              JS::Value        id,
                                              JS::Value       *value_p);

JSBool      gjs_string_to_utf8               (JSContext       *context,
                                              const JS::Value  string_val,
                                              char           **utf8_string_p);
bool        gjs_string_from_utf8             (JSContext       *context,
                                              const char      *utf8_string,
                                              gssize           n_bytes,
                                              JS::Value       *value_p);
JSBool      gjs_string_to_filename           (JSContext       *context,
                                              const JS::Value  string_val,
                                              char           **filename_string_p);
bool        gjs_string_from_filename         (JSContext       *context,
                                              const char      *filename_string,
                                              gssize           n_bytes,
                                              JS::Value       *value_p);
bool        gjs_string_get_uint16_data       (JSContext       *context,
                                              JS::Value        value,
                                              guint16        **data_p,
                                              gsize           *len_p);
bool        gjs_get_string_id                (JSContext       *context,
                                              jsid             id,
                                              char           **name_p);
jsid        gjs_intern_string_to_id          (JSContext       *context,
                                              const char      *string);

bool        gjs_unichar_from_string          (JSContext       *context,
                                              JS::Value        string,
                                              gunichar        *result);

const char* gjs_get_type_name                (JS::Value        value);

bool        gjs_parse_args                   (JSContext  *context,
                                              const char *function_name,
                                              const char *format,
                                              unsigned   argc,
                                              JS::Value  *argv,
                                              ...);

bool        gjs_parse_call_args              (JSContext    *context,
                                              const char   *function_name,
                                              const char   *format,
                                              JS::CallArgs &args,
                                              ...);

/* Functions intended for more "internal" use */

void gjs_maybe_gc (JSContext *context);

bool              gjs_context_get_frame_info (JSContext  *context,
                                              JS::Value  *stack,
                                              JS::Value  *fileName,
                                              JS::Value  *lineNumber);

bool              gjs_eval_with_scope        (JSContext    *context,
                                              JSObject     *object,
                                              const char   *script,
                                              gssize        script_len,
                                              const char   *filename,
                                              JS::Value    *retval_p);

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
  GJS_STRING_LAST
} GjsConstString;

jsid              gjs_context_get_const_string  (JSContext       *context,
                                                 GjsConstString   string);
bool              gjs_object_get_property_const (JSContext       *context,
                                                 JSObject        *obj,
                                                 GjsConstString   property_name,
                                                 JS::Value       *value_p);

const char * gjs_strip_unix_shebang(const char *script,
                                    gssize     *script_len,
                                    int        *new_start_line_number);

G_END_DECLS

#endif  /* __GJS_JSAPI_UTIL_H__ */
