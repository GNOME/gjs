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
#include <time.h>

#include <gjs/gjs-module.h>
#include <gi/object.h>
#include "system.h"

static JSBool
gjs_address_of(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JSObject *target_obj;
    JSBool ret;
    char *pointer_string;
    jsval retval;

    if (!gjs_parse_call_args(context, "addressOf", "o", argv, "object", &target_obj))
        return JS_FALSE;

    pointer_string = g_strdup_printf("%p", target_obj);

    ret = gjs_string_from_utf8(context, pointer_string, -1, &retval);
    g_free(pointer_string);

    if (ret)
        argv.rval().set(retval);

    return ret;
}

static JSBool
gjs_refcount(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    jsval retval;
    JSObject *target_obj;
    GObject *obj;

    if (!gjs_parse_call_args(context, "refcount", "o", argv, "object", &target_obj))
        return JS_FALSE;

    if (!gjs_typecheck_object(context, target_obj,
                              G_TYPE_OBJECT, JS_TRUE))
        return JS_FALSE;

    obj = gjs_g_object_from_object(context, target_obj);
    if (obj == NULL)
        return JS_FALSE;

    retval = INT_TO_JSVAL(obj->ref_count);
    argv.rval().set(retval);
    return JS_TRUE;
}

static JSBool
gjs_breakpoint(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    if (!gjs_parse_call_args(context, "breakpoint", "", argv))
        return JS_FALSE;
    G_BREAKPOINT();
    argv.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_gc(JSContext *context,
       unsigned   argc,
       jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    if (!gjs_parse_call_args(context, "gc", "", argv))
        return JS_FALSE;
    JS_GC(JS_GetRuntime(context));
    argv.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_exit(JSContext *context,
         unsigned   argc,
         jsval     *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    gint32 ecode;
    if (!gjs_parse_call_args(context, "exit", "i", argv, "ecode", &ecode))
        return JS_FALSE;
    exit(ecode);
    return JS_TRUE;
}

static JSBool
gjs_clear_date_caches(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    JS::CallReceiver rec = JS::CallReceiverFromVp(vp);
    JS_BeginRequest(context);

    // Workaround for a bug in SpiderMonkey where tzset is not called before
    // localtime_r, see https://bugzilla.mozilla.org/show_bug.cgi?id=1004706
    tzset();

    JS_ClearDateCaches(context);
    JS_EndRequest(context);

    rec.rval().set(JSVAL_VOID);
    return JS_TRUE;
}

static JSFunctionSpec module_funcs[] = {
    { "addressOf", JSOP_WRAPPER (gjs_address_of), 1, GJS_MODULE_PROP_FLAGS },
    { "refcount", JSOP_WRAPPER (gjs_refcount), 1, GJS_MODULE_PROP_FLAGS },
    { "breakpoint", JSOP_WRAPPER (gjs_breakpoint), 0, GJS_MODULE_PROP_FLAGS },
    { "gc", JSOP_WRAPPER (gjs_gc), 0, GJS_MODULE_PROP_FLAGS },
    { "exit", JSOP_WRAPPER (gjs_exit), 0, GJS_MODULE_PROP_FLAGS },
    { "clearDateCaches", JSOP_WRAPPER (gjs_clear_date_caches), 0, GJS_MODULE_PROP_FLAGS },
    { NULL },
};

JSBool
gjs_js_define_system_stuff(JSContext  *context,
                           JSObject  **module_out)
{
    GjsContext *gjs_context;
    char *program_name;
    jsval value;
    JSBool retval;
    JSObject *module;

    module = JS_NewObject (context, NULL, NULL, NULL);

    if (!JS_DefineFunctions(context, module, &module_funcs[0]))
        return JS_FALSE;

    retval = JS_FALSE;

    gjs_context = (GjsContext*) JS_GetContextPrivate(context);
    g_object_get(gjs_context,
                 "program-name", &program_name,
                 NULL);

    if (!gjs_string_from_utf8(context, program_name,
                              -1, &value))
        goto out;

    /* The name is modeled after program_invocation_name,
       part of the glibc */
    if (!JS_DefineProperty(context, module,
                           "programInvocationName",
                           value,
                           JS_PropertyStub,
                           JS_StrictPropertyStub,
                           GJS_MODULE_PROP_FLAGS | JSPROP_READONLY))
        goto out;

    if (!JS_DefineProperty(context, module,
                           "version",
                           INT_TO_JSVAL(GJS_VERSION),
                           JS_PropertyStub,
                           JS_StrictPropertyStub,
                           GJS_MODULE_PROP_FLAGS | JSPROP_READONLY))
        goto out;

    retval = JS_TRUE;

 out:
    g_free(program_name);
    *module_out = module;

    return retval;
}
