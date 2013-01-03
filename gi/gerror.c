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

#include <config.h>

#include <string.h>

#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include "boxed.h"
#include "enumeration.h"
#include "repo.h"
#include "gerror.h"

#include <util/log.h>

#include <girepository.h>

typedef struct {
    GIEnumInfo *info;
    GQuark domain;
    GError *gerror; /* NULL if we are the prototype and not an instance */
} Error;

enum {
    PROP_0,
    PROP_DOMAIN,
    PROP_CODE,
    PROP_MESSAGE
};

static struct JSClass gjs_error_class;

static void define_error_properties(JSContext *, JSObject *);

GJS_DEFINE_PRIV_FROM_JS(Error, gjs_error_class)

GJS_NATIVE_CONSTRUCTOR_DECLARE(error)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(error)
    Error *priv;
    Error *proto_priv;
    JSObject *proto;
    jsval v_message, v_code;
    gchar *message;

    /* Check early to avoid allocating memory for nothing */
    if (argc != 1 || !JSVAL_IS_OBJECT(argv[0])) {
        gjs_throw(context, "Invalid parameters passed to GError constructor, expected one object");
        return JS_FALSE;
    }

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(error);

    priv = g_slice_new0(Error);

    GJS_INC_COUNTER(gerror);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GERROR,
                        "GError constructor, obj %p priv %p",
                        object, priv);

    proto = JS_GetPrototype(context, object);
    gjs_debug_lifecycle(GJS_DEBUG_GERROR, "GError instance __proto__ is %p", proto);

    /* If we're the prototype, then post-construct we'll fill in priv->info.
     * If we are not the prototype, though, then we'll get ->info from the
     * prototype and then create a GObject if we don't have one already.
     */
    proto_priv = priv_from_js(context, proto);
    if (proto_priv == NULL) {
        gjs_debug(GJS_DEBUG_GERROR,
                  "Bad prototype set on GError? Must match JSClass of object. JS error should have been reported.");
        return JS_FALSE;
    }

    priv->info = proto_priv->info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->domain = proto_priv->domain;

    if (!gjs_object_require_property (context, JSVAL_TO_OBJECT(argv[0]),
                                      "GError constructor", "message", &v_message))
        return JS_FALSE;
    if (!gjs_object_require_property (context, JSVAL_TO_OBJECT(argv[0]),
                                      "GError constructor", "code", &v_code))
        return JS_FALSE;
    if (!gjs_string_to_utf8 (context, v_message, &message))
        return JS_FALSE;

    priv->gerror = g_error_new_literal (priv->domain, JSVAL_TO_INT(v_code),
                                        message);

    g_free (message);

    /* We assume this error will be thrown in the same line as the constructor */
    define_error_properties(context, object);

    GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);

    return JS_TRUE;
}

static void
error_finalize(JSContext *context,
               JSObject  *obj)
{
    Error *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GERROR,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    g_clear_error (&priv->gerror);

    if (priv->info) {
        g_base_info_unref( (GIBaseInfo*) priv->info);
        priv->info = NULL;
    }

    GJS_DEC_COUNTER(gerror);
    g_slice_free(Error, priv);
}

static JSBool
error_get_domain(JSContext *context, JSObject *obj, jsid id, jsval *vp)
{
    Error *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE;

    *vp = INT_TO_JSVAL(priv->domain);
    return JS_TRUE;
}

static JSBool
error_get_message(JSContext *context, JSObject *obj, jsid id, jsval *vp)
{
    Error *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE;

    if (priv->gerror == NULL) {
        /* Object is prototype, not instance */
        gjs_throw(context, "Can't get a field from a GError prototype");
        return JS_FALSE;
    }

    return gjs_string_from_utf8(context, priv->gerror->message, -1, vp);
}

static JSBool
error_get_code(JSContext *context, JSObject *obj, jsid id, jsval *vp)
{
    Error *priv;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE;

    if (priv->gerror == NULL) {
        /* Object is prototype, not instance */
        gjs_throw(context, "Can't get a field from a GError prototype");
        return JS_FALSE;
    }

    *vp = INT_TO_JSVAL(priv->gerror->code);
    return JS_TRUE;
}

static JSBool
error_to_string(JSContext *context, unsigned argc, jsval *vp)
{
    jsval v_self;
    JSObject *self;
    Error *priv;
    jsval v_out;
    gchar *descr;
    JSBool retval;

    v_self = JS_THIS(context, vp);
    if (!JSVAL_IS_OBJECT(v_self)) {
        /* Lie a bit here... */
        gjs_throw(context, "GLib.Error.prototype.toString() called on a non object");
        return JS_FALSE;
    }

    self = JSVAL_TO_OBJECT(v_self);
    priv = priv_from_js(context, self);

    if (priv == NULL)
        return JS_FALSE;

    v_out = JSVAL_VOID;
    retval = JS_FALSE;

    /* We follow the same pattern as standard JS errors, at the expense of
       hiding some useful information */

    if (priv->gerror == NULL) {
        descr = g_strdup_printf("%s.%s",
                                g_base_info_get_namespace(priv->info),
                                g_base_info_get_name(priv->info));

        if (!gjs_string_from_utf8(context, descr, -1, &v_out))
            goto out;
    } else {
        descr = g_strdup_printf("%s.%s: %s",
                                g_base_info_get_namespace(priv->info),
                                g_base_info_get_name(priv->info),
                                priv->gerror->message);

        if (!gjs_string_from_utf8(context, descr, -1, &v_out))
            goto out;
    }

    JS_SET_RVAL(context, vp, v_out);
    retval = JS_TRUE;

 out:
    g_free(descr);
    return retval;
}

static JSBool
error_constructor_value_of(JSContext *context, unsigned argc, jsval *vp)
{
    jsval v_self, v_prototype;
    Error *priv;
    jsval v_out;

    v_self = JS_THIS(context, vp);
    if (!JSVAL_IS_OBJECT(v_self)) {
        /* Lie a bit here... */
        gjs_throw(context, "GLib.Error.valueOf() called on a non object");
        return JS_FALSE;
    }

    if (!gjs_object_require_property(context,
                                     JSVAL_TO_OBJECT(v_self),
                                     "constructor",
                                     "prototype",
                                     &v_prototype))
        return JS_FALSE;

    if (!JSVAL_IS_OBJECT(v_prototype)) {
        gjs_throw(context, "GLib.Error.valueOf() called on something that is not"
                  " a constructor");
        return JS_FALSE;
    }

    priv = priv_from_js(context, JSVAL_TO_OBJECT(v_prototype));

    if (priv == NULL)
        return JS_FALSE;

    v_out = INT_TO_JSVAL(priv->domain);

    JS_SET_RVAL(context, vp, v_out);
    return TRUE;
}


/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static struct JSClass gjs_error_class = {
    "GLib_Error",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    error_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};

/* We need to shadow all fields of GError, to prevent calling the getter from GBoxed
   (which would trash memory accessing the instance private data) */
static JSPropertySpec gjs_error_proto_props[] = {
    { "domain", PROP_DOMAIN, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY, error_get_domain, NULL },
    { "code", PROP_CODE, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY, error_get_code, NULL },
    { "message", PROP_MESSAGE, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY, error_get_message, NULL },
    { NULL }
};

static JSFunctionSpec gjs_error_proto_funcs[] = {
    { "toString", error_to_string, 0, GJS_MODULE_PROP_FLAGS },
    JS_FS_END
};

static JSFunctionSpec gjs_error_constructor_funcs[] = {
    { "valueOf", error_constructor_value_of, 0, GJS_MODULE_PROP_FLAGS },
    JS_FS_END
};

JSObject*
gjs_lookup_error_constructor(JSContext    *context,
                             GIEnumInfo  *info)
{
    JSObject *ns;
    JSObject *constructor;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    constructor = NULL;
    if (gjs_define_error_class(context, ns, info,
                               &constructor, NULL))
        return constructor;
    else
        return NULL;
}

JSObject*
gjs_lookup_error_prototype(JSContext   *context,
                           GIEnumInfo  *info)
{
    JSObject *ns;
    JSObject *proto;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    proto = NULL;
    if (gjs_define_error_class(context, ns, info, NULL, &proto))
        return proto;
    else
        return NULL;
}

JSClass*
gjs_lookup_error_class(JSContext    *context,
                       GIEnumInfo   *info)
{
    JSObject *prototype;

    prototype = gjs_lookup_error_prototype(context, info);

    return JS_GetClass(prototype);
}

JSBool
gjs_define_error_class(JSContext    *context,
                       JSObject     *in_object,
                       GIEnumInfo   *info,
                       JSObject    **constructor_p,
                       JSObject    **prototype_p)
{
    const char *constructor_name;
    GIBoxedInfo *glib_error_info;
    JSObject *prototype, *parent_proto;
    JSObject *constructor;
    jsval value;
    Error *priv;

    /* See the comment in gjs_define_boxed_class() for an
     * explanation of how this all works; Error is pretty much the
     * same as Boxed (except that we inherit from GLib.Error).
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    if (gjs_object_get_property(context, in_object, constructor_name, &value)) {
        JSObject *constructor;

        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Existing property '%s' does not look like a constructor",
                         constructor_name);
            return JS_FALSE;
        }

        constructor = JSVAL_TO_OBJECT(value);

        gjs_object_get_property(context, constructor, "prototype", &value);
        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "error %s prototype property does not appear to exist or has wrong type", constructor_name);
            return JS_FALSE;
        } else {
            if (prototype_p)
                *prototype_p = JSVAL_TO_OBJECT(value);
            if (constructor_p)
                *constructor_p = constructor;

            return JS_TRUE;
        }
    }

    g_irepository_require(NULL, "GLib", "2.0", 0, NULL);
    glib_error_info = (GIBoxedInfo*) g_irepository_find_by_name(NULL, "GLib", "Error");
    parent_proto = gjs_lookup_boxed_prototype(context, glib_error_info);
    g_base_info_unref((GIBaseInfo*)glib_error_info);

    if (!gjs_init_class_dynamic(context, in_object,
                                parent_proto,
                                g_base_info_get_namespace( (GIBaseInfo*) info),
                                constructor_name,
                                &gjs_error_class,
                                gjs_error_constructor, 1,
                                /* props of prototype */
                                &gjs_error_proto_props[0],
                                /* funcs of prototype */
                                &gjs_error_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                &gjs_error_constructor_funcs[0],
                                &prototype,
                                &constructor)) {
        gjs_log_exception(context, NULL);
        gjs_fatal("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(gerror);
    priv = g_slice_new0(Error);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->domain = g_quark_from_string (g_enum_info_get_error_domain(priv->info));

    JS_SetPrivate(context, prototype, priv);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

    gjs_define_enum_values(context, constructor, priv->info);

    if (constructor_p)
        *constructor_p = constructor;

    if (prototype_p)
        *prototype_p = prototype;

    return JS_TRUE;
}

static GIEnumInfo *
find_error_domain_info(GQuark domain)
{
    GIEnumInfo *info;

    /* first an attempt without loading extra libraries */
    info = g_irepository_find_by_error_domain(NULL, domain);
    if (info)
        return info;

    /* load standard stuff */
    g_irepository_require(NULL, "GLib", "2.0", 0, NULL);
    g_irepository_require(NULL, "GObject", "2.0", 0, NULL);
    g_irepository_require(NULL, "Gio", "2.0", 0, NULL);
    info = g_irepository_find_by_error_domain(NULL, domain);
    if (info)
        return info;

    /* last attempt: load GIRepository (for invoke errors, rarely
       needed) */
    g_irepository_require(NULL, "GIRepository", "1.0", 0, NULL);
    info = g_irepository_find_by_error_domain(NULL, domain);

    return info;
}

/* define properties that JS Error() expose, such as
   fileName, lineNumber and stack
*/
static void
define_error_properties(JSContext *context,
                        JSObject  *obj)
{
    JSStackFrame *frame;
    JSScript *script;
    jsbytecode *pc;
    jsval v;
    GString *stack;
    const char *filename;
    GjsContext *gjs_context;

    /* find the JS frame that triggered the error */
    frame = NULL;
    while (JS_FrameIterator(context, &frame)) {
        if (JS_IsScriptFrame(context, frame))
            break;
    }

    /* someone called gjs_throw at top of the stack?
       well, no stack in that case
    */
    if (!frame)
        return;

    script = JS_GetFrameScript(context, frame);
    pc = JS_GetFramePC(context, frame);

    stack = g_string_new(NULL);
    gjs_context = JS_GetContextPrivate(context);
    gjs_context_print_stack_to_buffer(gjs_context, frame, stack);

    if (gjs_string_from_utf8(context, stack->str, stack->len, &v))
        JS_DefineProperty(context, obj, "stack", v,
                          NULL, NULL, JSPROP_ENUMERATE);

    filename = JS_GetScriptFilename(context, script);
    if (gjs_string_from_filename(context, filename, -1, &v))
        JS_DefineProperty(context, obj, "fileName", v,
                          NULL, NULL, JSPROP_ENUMERATE);

    v = INT_TO_JSVAL(JS_PCToLineNumber(context, script, pc));
    JS_DefineProperty(context, obj, "lineNumber", v,
                      NULL, NULL, JSPROP_ENUMERATE);

    g_string_free(stack, TRUE);
}

JSObject*
gjs_error_from_gerror(JSContext             *context,
                      GError                *gerror,
                      gboolean               add_stack)
{
    JSObject *obj;
    JSObject *proto;
    Error *priv;
    Error *proto_priv;
    GIEnumInfo *info;

    if (gerror == NULL)
        return NULL;

    info = find_error_domain_info(gerror->domain);

    if (!info) {
        /* We don't have error domain metadata */
        /* Marshal the error as a plain GError */
        GIBaseInfo *glib_boxed;
        JSObject *retval;

        glib_boxed = g_irepository_find_by_name(NULL, "GLib", "Error");
        retval = gjs_boxed_from_c_struct(context, glib_boxed, gerror, 0);

        g_base_info_unref(glib_boxed);
        return retval;
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    proto = gjs_lookup_error_prototype(context, info);
    proto_priv = priv_from_js(context, proto);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    GJS_INC_COUNTER(gerror);
    priv = g_slice_new0(Error);
    JS_SetPrivate(context, obj, priv);
    priv->info = info;
    priv->domain = proto_priv->domain;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gerror = g_error_copy(gerror);

    if (add_stack)
        define_error_properties(context, obj);

    return obj;
}

GError*
gjs_gerror_from_error(JSContext    *context,
                      JSObject     *obj)
{
    Error *priv;

    if (obj == NULL)
        return NULL;

    /* If this is a plain GBoxed (i.e. a GError without metadata),
       delegate marshalling.
    */
    if (gjs_typecheck_boxed (context, obj, NULL, G_TYPE_ERROR, JS_FALSE))
        return gjs_c_struct_from_boxed (context, obj);

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return NULL;

    if (priv->gerror == NULL) {
        gjs_throw(context,
                  "Object is %s.%s.prototype, not an object instance - cannot convert to a boxed instance",
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));
        return NULL;
    }

    return priv->gerror;
}

JSBool
gjs_typecheck_gerror (JSContext *context,
                      JSObject  *obj,
                      JSBool     throw)
{
    if (gjs_typecheck_boxed (context, obj, NULL, G_TYPE_ERROR, JS_FALSE))
        return TRUE;

    return do_base_typecheck(context, obj, throw);
}
