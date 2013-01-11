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

#include <sys/types.h>
#include <unistd.h>

#include <gjs/gjs-module.h>
#include <gi/object.h>
#include "system.h"

static JSBool
gjs_address_of(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    JSObject *target_obj;
    JSBool ret;
    char *pointer_string;
    jsval retval;

    if (!gjs_parse_args(context, "addressOf", "o", argc, argv, "object", &target_obj))
        return JS_FALSE;

    pointer_string = g_strdup_printf("%p", target_obj);

    ret = gjs_string_from_utf8(context, pointer_string, -1, &retval);
    g_free(pointer_string);

    if (ret)
        JS_SET_RVAL(context, vp, retval);

    return ret;
}

static JSBool
gjs_refcount(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    jsval retval;
    JSObject *target_obj;
    GObject *obj;

    if (!gjs_parse_args(context, "refcount", "o", argc, argv, "object", &target_obj))
        return JS_FALSE;

    if (!gjs_typecheck_object(context, target_obj,
                              G_TYPE_OBJECT, JS_TRUE))
        return JS_FALSE;

    obj = gjs_g_object_from_object(context, target_obj);
    if (obj == NULL)
        return JS_FALSE;

    retval = INT_TO_JSVAL(obj->ref_count);
    JS_SET_RVAL(context, vp, retval);
    return JS_TRUE;
}

static JSBool
gjs_breakpoint(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    if (!gjs_parse_args(context, "breakpoint", "", argc, argv))
        return JS_FALSE;
    G_BREAKPOINT();
    return JS_TRUE;
}

static JSBool
gjs_gc(JSContext *context,
       unsigned   argc,
       jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    if (!gjs_parse_args(context, "gc", "", argc, argv))
        return JS_FALSE;
    JS_GC(JS_GetRuntime(context));
    return JS_TRUE;
}

static JSBool
gjs_exit(JSContext *context,
         unsigned   argc,
         jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    gint32 ecode;
    if (!gjs_parse_args(context, "exit", "i", argc, argv, &ecode))
        return JS_FALSE;
    exit(ecode);
    return JS_TRUE;
}

JSBool
gjs_js_define_system_stuff(JSContext *context,
                           JSObject  *module)
{
    if (!JS_DefineFunction(context, module,
                           "addressOf",
                           (JSNative) gjs_address_of,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module,
                           "refcount",
                           (JSNative) gjs_refcount,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module,
                           "breakpoint",
                           (JSNative) gjs_breakpoint,
                           0, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module,
                           "gc",
                           (JSNative) gjs_gc,
                           0, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module,
                           "exit",
                           (JSNative) gjs_exit,
                           0, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}
