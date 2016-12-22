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
#include <mozilla/Maybe.h>

#include "jsapi-wrapper.h"
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

/*
 * GJS_GET_PRIV:
 * @cx: JSContext pointer passed into JSNative function
 * @argc: Number of arguments passed into JSNative function
 * @vp: Argument value array passed into JSNative function
 * @args: Name for JS::CallArgs variable defined by this code snippet
 * @to: Name for JS::RootedObject variable referring to function's this
 * @type: Type of private data
 * @priv: Name for private data variable defined by this code snippet
 *
 * A convenience macro for getting the private data from GJS classes using
 * priv_from_js().
 * Throws an error if the 'this' object is not the right type.
 * Use in any JSNative function.
 */
#define GJS_GET_PRIV(cx, argc, vp, args, to, type, priv)  \
    GJS_GET_THIS(cx, argc, vp, args, to);                 \
    if (!do_base_typecheck(cx, to, true))                 \
        return false;                                     \
    type *priv = priv_from_js(cx, to)

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
    gjs_##cname##_finalize                                                     \
}; \
JS::Value                                                                      \
gjs_##cname##_create_proto(JSContext *context,                                 \
                           JS::HandleObject module,                            \
                           const char      *proto_name,                        \
                           JS::HandleObject parent)                            \
{ \
    JS::RootedValue rval(context);                                             \
    JS::RootedObject global(context, gjs_get_import_global(context));          \
    JS::RootedId class_name(context,                                           \
        gjs_intern_string_to_id(context, gjs_##cname##_class.name));           \
    if (!JS_GetPropertyById(context, global, class_name, &rval))               \
        return JS::NullValue(); \
    if (rval.isUndefined()) { \
        JS::RootedObject prototype(context,                                    \
            JS_InitClass(context, global, parent, &gjs_##cname##_class, ctor,  \
                         0, &gjs_##cname##_proto_props[0],                     \
                         &gjs_##cname##_proto_funcs[0],                        \
                         NULL, NULL));                                         \
        if (prototype == NULL) { \
            return JS::NullValue(); \
        } \
        if (!gjs_object_require_property( \
                context, global, NULL, \
                class_name, &rval)) { \
            return JS::NullValue(); \
        } \
        if (!JS_DefineProperty(context, module, proto_name, \
                               rval, GJS_MODULE_PROP_FLAGS))                   \
            return JS::NullValue(); \
        if (gtype != G_TYPE_NONE) { \
            JS::RootedObject rval_obj(context, &rval.toObject());              \
            JS::RootedObject gtype_obj(context,                                \
                gjs_gtype_create_gtype_wrapper(context, gtype));               \
            JS_DefineProperty(context, rval_obj, "$gtype", gtype_obj,          \
                              JSPROP_PERMANENT);                               \
        } \
    } \
    return rval; \
}

/**
 * GJS_NATIVE_CONSTRUCTOR_DECLARE:
 * Prototype a constructor.
 */
#define GJS_NATIVE_CONSTRUCTOR_DECLARE(name)            \
static bool                                             \
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
        if (!argv.isConstructing()) {                                          \
            gjs_throw_constructor_error(context);                              \
            return false;                                                      \
        }                                                                      \
        object = JS_NewObjectForConstructor(context, &gjs_##name##_class, argv); \
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
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);       \
        gjs_throw_abstract_constructor_error(context, args);    \
        return false;                                           \
    }

bool gjs_init_context_standard(JSContext              *context,
                               JS::MutableHandleObject global);

JSObject*   gjs_get_import_global            (JSContext       *context);

JS::Value   gjs_get_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot);
void        gjs_set_global_slot              (JSContext       *context,
                                              GjsGlobalSlot    slot,
                                              JS::Value        value);

bool gjs_object_require_property(JSContext             *context,
                                 JS::HandleObject       obj,
                                 const char            *obj_description,
                                 JS::HandleId           property_name,
                                 JS::MutableHandleValue value);

/* This is intended to be overloaded with more types as the opportunity arises */
bool gjs_object_require_converted_property_value(JSContext       *context,
                                                 JS::HandleObject obj,
                                                 const char      *description,
                                                 JS::HandleId     property_name,
                                                 uint32_t        *value);

bool gjs_init_class_dynamic(JSContext              *context,
                            JS::HandleObject        in_object,
                            JS::HandleObject        parent_proto,
                            const char             *ns_name,
                            const char             *class_name,
                            JSClass                *clasp,
                            JSNative                constructor_native,
                            unsigned                nargs,
                            JSPropertySpec         *ps,
                            JSFunctionSpec         *fs,
                            JSPropertySpec         *static_ps,
                            JSFunctionSpec         *static_fs,
                            JS::MutableHandleObject prototype,
                            JS::MutableHandleObject constructor);

void gjs_throw_constructor_error             (JSContext       *context);

void gjs_throw_abstract_constructor_error(JSContext    *context,
                                          JS::CallArgs& args);

bool gjs_typecheck_instance(JSContext       *context,
                            JS::HandleObject obj,
                            JSClass         *static_clasp,
                            bool             throw_error);

JSObject *gjs_construct_object_dynamic(JSContext                  *context,
                                       JS::HandleObject            proto,
                                       const JS::HandleValueArray& args);

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
  GJS_STRING_LAST
} GjsConstString;

jsid              gjs_context_get_const_string  (JSContext       *context,
                                                 GjsConstString   string);

bool gjs_object_get_property_const(JSContext             *cx,
                                   JS::HandleObject       obj,
                                   GjsConstString         property_name,
                                   JS::MutableHandleValue value_p);

const char * gjs_strip_unix_shebang(const char *script,
                                    gssize     *script_len,
                                    int        *new_start_line_number);

G_END_DECLS

/* Overloaded functions, must be outside G_DECLS. More types are intended to be
 * added as the opportunity arises. */

bool gjs_object_require_property_value(JSContext       *cx,
                                       JS::HandleObject obj,
                                       const char      *description,
                                       JS::HandleId     property_name,
                                       bool            *value);

bool gjs_object_require_property_value(JSContext       *cx,
                                       JS::HandleObject obj,
                                       const char      *description,
                                       JS::HandleId     property_name,
                                       int32_t         *value);

bool gjs_object_require_property_value(JSContext       *cx,
                                       JS::HandleObject obj,
                                       const char      *description,
                                       JS::HandleId     property_name,
                                       char           **value);

bool gjs_object_require_property_value(JSContext              *cx,
                                       JS::HandleObject        obj,
                                       const char             *description,
                                       JS::HandleId            property_name,
                                       JS::MutableHandleObject value);

#endif  /* __GJS_JSAPI_UTIL_H__ */
