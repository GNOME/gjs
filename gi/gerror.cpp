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

#include "boxed.h"
#include "enumeration.h"
#include "gjs/compat.h"
#include "gjs/mem.h"
#include "repo.h"
#include "gerror.h"

#include <util/log.h>

#include <girepository.h>

typedef struct {
    GIEnumInfo *info;
    GQuark domain;
    GError *gerror; /* NULL if we are the prototype and not an instance */
} Error;

extern struct JSClass gjs_error_class;

static void define_error_properties(JSContext *, JSObject *);

GJS_DEFINE_PRIV_FROM_JS(Error, gjs_error_class)

GJS_NATIVE_CONSTRUCTOR_DECLARE(error)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(error)
    Error *priv;
    Error *proto_priv;
    jsid message_name, code_name;
    JS::Value v_message, v_code;
    gchar *message;

    /* Check early to avoid allocating memory for nothing */
    if (argc != 1 || !argv[0].isObject()) {
        gjs_throw(context, "Invalid parameters passed to GError constructor, expected one object");
        return false;
    }

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(error);

    priv = g_slice_new0(Error);

    GJS_INC_COUNTER(gerror);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GERROR,
                        "GError constructor, obj %p priv %p",
                        object.get(), priv);

    JS::RootedObject proto(context);
    JS_GetPrototype(context, object, proto.address());
    gjs_debug_lifecycle(GJS_DEBUG_GERROR, "GError instance __proto__ is %p",
                        proto.get());

    /* If we're the prototype, then post-construct we'll fill in priv->info.
     * If we are not the prototype, though, then we'll get ->info from the
     * prototype and then create a GObject if we don't have one already.
     */
    proto_priv = priv_from_js(context, proto);
    if (proto_priv == NULL) {
        gjs_debug(GJS_DEBUG_GERROR,
                  "Bad prototype set on GError? Must match JSClass of object. JS error should have been reported.");
        return false;
    }

    priv->info = proto_priv->info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->domain = proto_priv->domain;

    message_name = gjs_context_get_const_string(context, GJS_STRING_MESSAGE);
    code_name = gjs_context_get_const_string(context, GJS_STRING_CODE);
    if (!gjs_object_require_property(context, &argv[0].toObject(),
                                     "GError constructor", message_name, &v_message))
        return false;
    if (!gjs_object_require_property(context, &argv[0].toObject(),
                                     "GError constructor", code_name, &v_code))
        return false;
    if (!gjs_string_to_utf8 (context, v_message, &message))
        return false;

    priv->gerror = g_error_new_literal(priv->domain, v_code.toInt32(),
                                       message);

    g_free (message);

    /* We assume this error will be thrown in the same line as the constructor */
    define_error_properties(context, object);

    GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);

    return true;
}

static void
error_finalize(JSFreeOp *fop,
               JSObject *obj)
{
    Error *priv;

    priv = (Error*) JS_GetPrivate(obj);
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
error_get_domain(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, obj, Error, priv);

    if (priv == NULL)
        return false;

    args.rval().setInt32(priv->domain);
    return true;
}

static JSBool
error_get_message(JSContext *context,
                  unsigned   argc,
                  JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, obj, Error, priv);
    JS::Value retval;
    bool ret = false;

    if (priv == NULL)
        return false;

    if (priv->gerror == NULL) {
        /* Object is prototype, not instance */
        gjs_throw(context, "Can't get a field from a GError prototype");
        return false;
    }

    // FIXME: root gjs_string_from_utf8()
    ret = gjs_string_from_utf8(context, priv->gerror->message, -1, &retval);
    if (ret)
        args.rval().set(retval);
    return ret;
}

static JSBool
error_get_code(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, obj, Error, priv);

    if (priv == NULL)
        return false;

    if (priv->gerror == NULL) {
        /* Object is prototype, not instance */
        gjs_throw(context, "Can't get a field from a GError prototype");
        return false;
    }

    args.rval().setInt32(priv->gerror->code);
    return true;
}

static JSBool
error_to_string(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, self, Error, priv);
    JS::Value v_out;
    gchar *descr;
    bool retval;

    if (priv == NULL)
        return false;

    v_out = JS::UndefinedValue();
    retval = false;

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

    rec.rval().set(v_out);
    retval = true;

 out:
    g_free(descr);
    return retval;
}

static JSBool
error_constructor_value_of(JSContext *context,
                           unsigned   argc,
                           JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, rec, self);
    JS::Value v_prototype;
    Error *priv;
    jsid prototype_name;

    prototype_name = gjs_context_get_const_string(context, GJS_STRING_PROTOTYPE);
    if (!gjs_object_require_property(context, self, "constructor",
                                     prototype_name, &v_prototype))
        return false;

    if (!v_prototype.isObject()) {
        gjs_throw(context, "GLib.Error.valueOf() called on something that is not"
                  " a constructor");
        return false;
    }

    JS::RootedObject prototype(context, &v_prototype.toObject());
    priv = priv_from_js(context, prototype);

    if (priv == NULL)
        return false;

    rec.rval().setInt32(priv->domain);
    return true;
}


/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_error_class = {
    "GLib_Error",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_BACKGROUND_FINALIZE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    error_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

/* We need to shadow all fields of GError, to prevent calling the getter from GBoxed
   (which would trash memory accessing the instance private data) */
JSPropertySpec gjs_error_proto_props[] = {
    JS_PSG("domain", error_get_domain, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY),
    JS_PSG("code", error_get_code, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY),
    JS_PSG("message", error_get_message, GJS_MODULE_PROP_FLAGS | JSPROP_READONLY),
    JS_PS_END
};

JSFunctionSpec gjs_error_proto_funcs[] = {
    JS_FS("toString", error_to_string, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};

static JSFunctionSpec gjs_error_constructor_funcs[] = {
    JS_FS("valueOf", error_constructor_value_of, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};

void
gjs_define_error_class(JSContext    *context,
                       JSObject     *in_object,
                       GIEnumInfo   *info)
{
    const char *constructor_name;
    GIBoxedInfo *glib_error_info;
    JSObject *prototype, *parent_proto;
    JSObject *constructor;
    Error *priv;

    /* See the comment in gjs_define_boxed_class() for an
     * explanation of how this all works; Error is pretty much the
     * same as Boxed (except that we inherit from GLib.Error).
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    g_irepository_require(NULL, "GLib", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    glib_error_info = (GIBoxedInfo*) g_irepository_find_by_name(NULL, "GLib", "Error");
    parent_proto = gjs_lookup_generic_prototype(context, glib_error_info);
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
        gjs_log_exception(context);
        g_error("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(gerror);
    priv = g_slice_new0(Error);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->domain = g_quark_from_string (g_enum_info_get_error_domain(priv->info));

    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

    gjs_define_enum_values(context, constructor, priv->info);
    gjs_define_enum_static_methods(context, constructor, priv->info);
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
    g_irepository_require(NULL, "GLib", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    g_irepository_require(NULL, "GObject", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    g_irepository_require(NULL, "Gio", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    info = g_irepository_find_by_error_domain(NULL, domain);
    if (info)
        return info;

    /* last attempt: load GIRepository (for invoke errors, rarely
       needed) */
    g_irepository_require(NULL, "GIRepository", "1.0", (GIRepositoryLoadFlags) 0, NULL);
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
    jsid stack_name, filename_name, linenumber_name;
    JS::Value stack, fileName, lineNumber;

    if (!gjs_context_get_frame_info (context,
                                     &stack,
                                     &fileName,
                                     &lineNumber))
        return;

    stack_name = gjs_context_get_const_string(context, GJS_STRING_STACK);
    filename_name = gjs_context_get_const_string(context, GJS_STRING_FILENAME);
    linenumber_name = gjs_context_get_const_string(context, GJS_STRING_LINE_NUMBER);

    JS_DefinePropertyById(context, obj, stack_name, stack,
                          NULL, NULL, JSPROP_ENUMERATE);

    JS_DefinePropertyById(context, obj, filename_name, fileName,
                          NULL, NULL, JSPROP_ENUMERATE);

    JS_DefinePropertyById(context, obj, linenumber_name, lineNumber,
                          NULL, NULL, JSPROP_ENUMERATE);
}

JSObject*
gjs_error_from_gerror(JSContext             *context,
                      GError                *gerror,
                      bool                   add_stack)
{
    JSObject *obj;
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
        retval = gjs_boxed_from_c_struct(context, glib_boxed, gerror, (GjsBoxedCreationFlags) 0);

        g_base_info_unref(glib_boxed);
        return retval;
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info));

    JS::RootedObject proto(context, gjs_lookup_generic_prototype(context, info));
    proto_priv = priv_from_js(context, proto);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    GJS_INC_COUNTER(gerror);
    priv = g_slice_new0(Error);
    JS_SetPrivate(obj, priv);
    priv->info = info;
    priv->domain = proto_priv->domain;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gerror = g_error_copy(gerror);

    if (add_stack)
        define_error_properties(context, obj);

    return obj;
}

GError*
gjs_gerror_from_error(JSContext       *context,
                      JS::HandleObject obj)
{
    Error *priv;

    if (obj == NULL)
        return NULL;

    /* If this is a plain GBoxed (i.e. a GError without metadata),
       delegate marshalling.
    */
    if (gjs_typecheck_boxed (context, obj, NULL, G_TYPE_ERROR, false))
        return (GError*) gjs_c_struct_from_boxed (context, obj);

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

bool
gjs_typecheck_gerror (JSContext       *context,
                      JS::HandleObject obj,
                      bool             throw_error)
{
    if (gjs_typecheck_boxed (context, obj, NULL, G_TYPE_ERROR, false))
        return true;

    return do_base_typecheck(context, obj, throw_error);
}
