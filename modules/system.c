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
#include <jsapi.h>

#include "system.h"

static JSBool
gjs_address_of(JSContext *context,
               uintN      argc,
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
    if (ret)
        JS_SET_RVAL(context, vp, retval);

    return ret;
}

static JSBool
gjs_refcount(JSContext *context,
             uintN      argc,
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
               uintN      argc,
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
       uintN      argc,
       jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    if (!gjs_parse_args(context, "gc", "", argc, argv))
        return JS_FALSE;
    JS_GC(context);
    return JS_TRUE;
}

static JSBool
gjs_getpid(JSContext *context,
           uintN      argc,
           jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    jsval rval;
    if (!gjs_parse_args(context, "getpid", "", argc, argv))
        return JS_FALSE;
    rval = INT_TO_JSVAL(getpid());
    JS_SET_RVAL(context, vp, rval);
    return JS_TRUE;
}

static JSBool
gjs_getuid(JSContext *context,
           uintN      argc,
           jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    jsval rval;
    if (!gjs_parse_args(context, "getuid", "", argc, argv))
        return JS_FALSE;
    rval = INT_TO_JSVAL(getuid());
    JS_SET_RVAL(context, vp, rval);
    return JS_TRUE;
}

static JSBool
gjs_getgid(JSContext *context,
           uintN      argc,
           jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    jsval rval;
    if (!gjs_parse_args(context, "getgid", "", argc, argv))
        return JS_FALSE;
    rval = INT_TO_JSVAL(getgid());
    JS_SET_RVAL(context, vp, rval);
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
                           "getpid",
                           (JSNative) gjs_getpid,
                           0, GJS_MODULE_PROP_FLAGS))
        return FALSE;

    if (!JS_DefineFunction(context, module,
                           "getuid",
                           (JSNative) gjs_getuid,
                           0, GJS_MODULE_PROP_FLAGS))
        return FALSE;

    if (!JS_DefineFunction(context, module,
                           "getgid",
                           (JSNative) gjs_getgid,
                           0, GJS_MODULE_PROP_FLAGS))
        return FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("system", gjs_js_define_system_stuff)
