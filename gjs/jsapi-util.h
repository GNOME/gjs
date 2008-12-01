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

#ifndef __GJS_GJS_H__
#warning Include <gjs/gjs.h> instead of <gjs/jsapi-util.h>
#endif

#include <jsapi.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct GjsRootedArray GjsRootedArray;

/* Flags that should be set on properties exported from native code modules.
 * Basically set these on API, but do NOT set them on data.
 *
 * READONLY:  forbid setting prop to another value
 * PERMANENT: forbid deleting the prop
 * ENUMERATE: allows copyProperties to work among other reasons to have it
 */
#define GJS_MODULE_PROP_FLAGS (JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_ENUMERATE)

/* priv_from_js_with_typecheck checks that the object is in fact an
 * instance of the specified class before accessing its private data.
 * Keep in mind that the function can return JS_TRUE and still fill the
 * out parameter with NULL if the object is the prototype for a class
 * without the JSCLASS_CONSTRUCT_PROTOTYPE flag or if it the class simply
 * does not have any private data.
 */
#define GJS_DEFINE_PRIV_FROM_JS(type, class) \
    __attribute__((unused)) static JSBool           \
    priv_from_js_with_typecheck(JSContext *context,     \
                                JSObject  *object,  \
                                type      **out)    \
    {\
        if (!out) \
            return JS_FALSE; \
        if (!JS_InstanceOf(context, object, &class, NULL)) \
            return JS_FALSE; \
        *out = JS_GetInstancePrivate(context, object, &class, NULL); \
        return JS_TRUE; \
    }\
    static type*\
    priv_from_js(JSContext *context, \
                 JSObject  *object)  \
    {\
        return JS_GetInstancePrivate(context, object, &class, NULL); \
    }


#define GJS_DEFINE_DYNAMIC_PRIV_FROM_JS(type, class) \
    __attribute__((unused)) static JSBool\
    priv_from_js_with_typecheck(JSContext *context, \
                                JSObject  *object,  \
                                type      **out)    \
    {\
        type *result; \
        if (!out) \
            return JS_FALSE; \
        result = gjs_get_instance_private_dynamic_with_typecheck(context, object, &class, NULL); \
        if (result == NULL) \
            return JS_FALSE; \
        *out = result; \
        return JS_TRUE; \
    }\
    static type*\
    priv_from_js(JSContext *context, \
                 JSObject  *object)  \
    {\
        return gjs_get_instance_private_dynamic(context, object, &class, NULL); \
    }

void*       gjs_runtime_get_data             (JSRuntime       *runtime,
                                                 const char      *name);
void        gjs_runtime_set_data             (JSRuntime       *runtime,
                                                 const char      *name,
                                                 void            *data,
                                                 GDestroyNotify   dnotify);
JSContext*  gjs_runtime_get_load_context     (JSRuntime       *runtime);
void        gjs_runtime_clear_load_context   (JSRuntime       *runtime);
JSContext*  gjs_runtime_get_call_context     (JSRuntime       *runtime);
void        gjs_runtime_clear_call_context   (JSRuntime       *runtime);
gboolean    gjs_object_has_property          (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *property_name);
gboolean    gjs_object_get_property          (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *property_name,
                                              jsval           *value_p);
gboolean    gjs_object_require_property      (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *property_name,
                                              jsval           *value_p);
JSObject *  gjs_init_class_dynamic           (JSContext       *context,
                                              JSObject        *in_object,
                                              JSObject        *parent_proto,
                                              const char      *ns_name,
                                              const char      *class_name,
                                              JSClass         *clasp,
                                              JSNative         constructor,
                                              uintN            nargs,
                                              JSPropertySpec  *ps,
                                              JSFunctionSpec  *fs,
                                              JSPropertySpec  *static_ps,
                                              JSFunctionSpec  *static_fs);
gboolean    gjs_check_constructing           (JSContext       *context);

void* gjs_get_instance_private_dynamic                (JSContext  *context,
                                                       JSObject   *obj,
                                                       JSClass    *static_clasp,
                                                       jsval      *argv);
void* gjs_get_instance_private_dynamic_with_typecheck (JSContext  *context,
                                                       JSObject   *obj,
                                                       JSClass    *static_clasp,
                                                       jsval      *argv);

JSObject*   gjs_construct_object_dynamic     (JSContext       *context,
                                              JSObject        *proto,
                                              uintN            argc,
                                              jsval           *argv);
JSObject*   gjs_define_string_array          (JSContext       *context,
                                              JSObject        *obj,
                                              const char      *array_name,
                                              gssize           array_length,
                                              const char     **array_values,
                                              uintN            attrs);
void        gjs_throw                        (JSContext       *context,
                                              const char      *format,
                                              ...)  G_GNUC_PRINTF (2, 3);
JSBool      gjs_log_exception                (JSContext       *context,
                                              char           **message_p);
JSBool      gjs_log_and_keep_exception       (JSContext       *context,
                                              char           **message_p);
JSBool      gjs_move_exception               (JSContext       *src_context,
                                              JSContext       *dest_context);
void        gjs_log_exception_props          (JSContext       *context,
                                              jsval            exc);
#ifdef __GJS_UTIL_LOG_H__
void        gjs_log_object_props             (JSContext       *context,
                                              JSObject        *obj,
                                              GjsDebugTopic    topic,
                                              const char      *prefix);
#endif
const char* gjs_value_debug_string           (JSContext       *context,
                                              jsval            value);
void        gjs_explain_scope                (JSContext       *context,
                                              const char      *title);
JSBool      gjs_call_function_value          (JSContext       *context,
                                              JSObject        *obj,
                                              jsval            fval,
                                              uintN            argc,
                                              jsval           *argv,
                                              jsval           *rval);
void        gjs_error_reporter               (JSContext       *context,
                                              const char      *message,
                                              JSErrorReport   *report);
JSBool      gjs_get_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              jsval            id,
                                              jsval           *value_p);
JSBool      gjs_set_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              jsval            id,
                                              jsval           *value_p);
JSBool      gjs_add_prop_verbose_stub        (JSContext       *context,
                                              JSObject        *obj,
                                              jsval            id,
                                              jsval           *value_p);
JSBool      gjs_delete_prop_verbose_stub     (JSContext       *context,
                                              JSObject        *obj,
                                              jsval            id,
                                              jsval           *value_p);
JSBool      gjs_string_to_utf8               (JSContext       *context,
                                              const            jsval string_val,
                                              char           **utf8_string_p);
JSBool      gjs_string_from_utf8             (JSContext       *context,
                                              const char      *utf8_string,
                                              gsize            n_bytes,
                                              jsval           *value_p);
JSBool      gjs_string_to_filename           (JSContext       *context,
                                              const jsval      string_val,
                                              char           **filename_string_p);
JSBool      gjs_string_from_filename         (JSContext       *context,
                                              const char      *filename_string,
                                              gsize            n_bytes,
                                              jsval           *value_p);
const char* gjs_string_get_ascii             (jsval            value);
const char* gjs_string_get_ascii_checked     (JSContext       *context,
                                              jsval            value);
JSBool      gjs_get_string_id                (jsval            id_val,
                                              const char     **name_p);
const char* gjs_get_type_name                (jsval            value);

jsval       gjs_date_from_time_t             (JSContext *context, time_t time);

GjsRootedArray*   gjs_rooted_array_new        (void);
void              gjs_rooted_array_append     (JSContext        *context,
                                               GjsRootedArray *array,
                                               jsval             value);
jsval             gjs_rooted_array_get        (JSContext        *context,
                                               GjsRootedArray *array,
                                               int               i);
jsval*            gjs_rooted_array_get_data   (JSContext        *context,
                                               GjsRootedArray *array);
int               gjs_rooted_array_get_length (JSContext        *context,
                                               GjsRootedArray *array);
jsval*            gjs_rooted_array_free       (JSContext        *context,
                                               GjsRootedArray *array,
                                               gboolean          free_segment);
void              gjs_set_values              (JSContext        *context,
                                               jsval            *locations,
                                               int               n_locations,
                                               jsval             initializer);
void              gjs_root_value_locations    (JSContext        *context,
                                               jsval            *locations,
                                               int               n_locations);
void              gjs_unroot_value_locations  (JSContext        *context,
                                               jsval            *locations,
                                               int               n_locations);


G_END_DECLS

#endif  /* __GJS_JSAPI_UTIL_H__ */
