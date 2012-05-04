/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
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

#include "format.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <glib.h>
#include <jsapi.h>

static JSBool
gjs_format_int_alternative_output(JSContext *context,
                                  uintN      argc,
                                  jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    char *str;
    jsval rval;
    int intval;
    JSBool ret;

    if (!gjs_parse_args(context, "format_int_alternative_output", "i", argc, argv,
                        "intval", &intval))
        return JS_FALSE;

    str = g_strdup_printf("%Id", intval);
    ret = gjs_string_from_utf8(context, str, -1, &rval);
    if (ret)
        JS_SET_RVAL(context, vp, rval);
    g_free (str);

    return ret;
}

JSBool
gjs_define_format_stuff(JSContext      *context,
                        JSObject      *module_obj)
{
    if (!JS_DefineFunction(context, module_obj,
                           "format_int_alternative_output",
                           (JSNative)gjs_format_int_alternative_output,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("formatNative", gjs_define_format_stuff)
