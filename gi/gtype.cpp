/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2012  Red Hat, Inc.
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

#include "gtype.h"

#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <util/log.h>
#include <girepository.h>

GJS_DEFINE_PROTO_ABSTRACT("GIRepositoryGType", gtype, 0);

/* priv_from_js adds a "*", so this returns "void *" */
GJS_DEFINE_PRIV_FROM_JS(void, gjs_gtype_class);

static GQuark
gjs_get_gtype_wrapper_quark(void)
{
    static gsize once_init = 0;
    static GQuark value = 0;
    if (g_once_init_enter(&once_init)) {
        value = g_quark_from_string("gjs-gtype-wrapper");
        g_once_init_leave(&once_init, 1);
    }
    return value;
}

static void
gjs_gtype_finalize(JSFreeOp *fop,
                   JSObject *obj)
{
    GType gtype = GPOINTER_TO_SIZE(JS_GetPrivate(obj));

    /* proto doesn't have a private set */
    if (G_UNLIKELY(gtype == 0))
        return;

    g_type_set_qdata(gtype, gjs_get_gtype_wrapper_quark(), NULL);
}

static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    GType gtype;
    gchar *strval;
    JSBool ret;
    jsval retval;
    
    gtype = GPOINTER_TO_SIZE(priv_from_js(context, obj));

    if (gtype == 0)
        strval = g_strdup("[object GType prototype]");
    else
        strval = g_strdup_printf("[object GType for '%s']",
                                 g_type_name(gtype));
    ret = gjs_string_from_utf8(context, strval, -1, &retval);
    if (ret)
        JS_SET_RVAL(context, vp, retval);
    g_free(strval);
    return ret;
}

static JSBool
get_name_func (JSContext *context,
               JS::HandleObject obj,
               JS::HandleId id,
               JS::MutableHandleValue vp)
{
    GType gtype;
    JSBool ret;
    jsval retval;

    gtype = GPOINTER_TO_SIZE(priv_from_js(context, obj));

    if (gtype == 0) {
        JS_SET_RVAL(context, vp.address(), JSVAL_NULL);
        return TRUE;
    } else {
        ret = gjs_string_from_utf8(context, g_type_name(gtype), -1, &retval);
        if (ret)
            JS_SET_RVAL(context, vp.address(), retval);
        return ret;
    }
}

/* Properties */
JSPropertySpec gjs_gtype_proto_props[] = {
    { "name", 0,
      JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_SHARED,
      JSOP_WRAPPER((JSPropertyOp)get_name_func),
      JSOP_WRAPPER(JS_StrictPropertyStub) },
    { NULL },
};

/* Functions */
JSFunctionSpec gjs_gtype_proto_funcs[] = {
    { "toString", JSOP_WRAPPER((JSNative)to_string_func), 0, 0 },
    { NULL }
};

JSObject *
gjs_gtype_create_gtype_wrapper (JSContext *context,
                                GType      gtype)
{
    JSObject *object;
    JSObject *global;

    JS_BeginRequest(context);

    /* put constructor for GIRepositoryGType() in the global namespace */
    global = gjs_get_import_global(context);
    gjs_gtype_create_proto(context, global, "GIRepositoryGType", NULL);

    object = (JSObject*) g_type_get_qdata(gtype, gjs_get_gtype_wrapper_quark());
    if (object != NULL)
        goto out;

    object = JS_NewObject(context, &gjs_gtype_class, NULL, NULL);
    if (object == NULL)
        goto out;

    JS_SetPrivate(object, GSIZE_TO_POINTER(gtype));
    g_type_set_qdata(gtype, gjs_get_gtype_wrapper_quark(), object);

 out:
    JS_EndRequest(context);
    return object;
}

static GType
_gjs_gtype_get_actual_gtype (JSContext *context,
                             JSObject  *object,
                             int        recurse)
{
    GType gtype = G_TYPE_INVALID;
    jsval gtype_val = JSVAL_VOID;

    JS_BeginRequest(context);
    if (JS_InstanceOf(context, object, &gjs_gtype_class, NULL)) {
        gtype = GPOINTER_TO_SIZE(priv_from_js(context, object));
        goto out;
    }

    /* OK, we don't have a GType wrapper object -- grab the "$gtype"
     * property on that and hope it's a GType wrapper object */
    if (!JS_GetProperty(context, object, "$gtype", &gtype_val) ||
        !JSVAL_IS_OBJECT(gtype_val)) {

        /* OK, so we're not a class. But maybe we're an instance. Check
           for "constructor" and recurse on that. */
        if (!JS_GetProperty(context, object, "constructor", &gtype_val))
            goto out;
    }

    if (recurse > 0 && JSVAL_IS_OBJECT(gtype_val))
        gtype = _gjs_gtype_get_actual_gtype(context, JSVAL_TO_OBJECT(gtype_val), recurse - 1);

 out:
    JS_EndRequest(context);
    return gtype;
}

GType
gjs_gtype_get_actual_gtype (JSContext *context,
                            JSObject  *object)
{
    /* 2 means: recurse at most three times (including this
       call).
       The levels are calculated considering that, in the
       worst case we need to go from instance to class, from
       class to GType object and from GType object to
       GType value.
     */

    return _gjs_gtype_get_actual_gtype(context, object, 2);
}

JSBool
gjs_typecheck_gtype (JSContext             *context,
                     JSObject              *obj,
                     JSBool                 throw_error)
{
    return do_base_typecheck(context, obj, throw_error);
}
