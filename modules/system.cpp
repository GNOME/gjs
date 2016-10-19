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

#include <gjs/context.h>

#include "gi/object.h"
#include "gjs/jsapi-util-args.h"
#include "system.h"

static JSBool
gjs_address_of(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JS::RootedObject target_obj(context);
    bool ret;
    char *pointer_string;

    if (!gjs_parse_call_args(context, "addressOf", argv, "o",
                             "object", &target_obj))
        return false;

    pointer_string = g_strdup_printf("%p", target_obj.get());

    ret = gjs_string_from_utf8(context, pointer_string, -1, argv.rval());

    g_free(pointer_string);
    return ret;
}

static JSBool
gjs_refcount(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JS::RootedObject target_obj(context);
    GObject *obj;

    if (!gjs_parse_call_args(context, "refcount", argv, "o",
                             "object", &target_obj))
        return false;

    if (!gjs_typecheck_object(context, target_obj, G_TYPE_OBJECT, true))
        return false;

    obj = gjs_g_object_from_object(context, target_obj);
    if (obj == NULL)
        return false;

    argv.rval().setInt32(obj->ref_count);
    return true;
}

static JSBool
gjs_breakpoint(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    if (!gjs_parse_call_args(context, "breakpoint", argv, ""))
        return false;
    G_BREAKPOINT();
    argv.rval().setUndefined();
    return true;
}

static JSBool
gjs_gc(JSContext *context,
       unsigned   argc,
       JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    if (!gjs_parse_call_args(context, "gc", argv, ""))
        return false;
    JS_GC(JS_GetRuntime(context));
    argv.rval().setUndefined();
    return true;
}

static JSBool
gjs_exit(JSContext *context,
         unsigned   argc,
         JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    gint32 ecode;
    if (!gjs_parse_call_args(context, "exit", argv, "i",
                             "ecode", &ecode))
        return false;
    exit(ecode);
    return true;
}

static JSBool
gjs_clear_date_caches(JSContext *context,
                      unsigned   argc,
                      JS::Value *vp)
{
    JS::CallReceiver rec = JS::CallReceiverFromVp(vp);
    JS_BeginRequest(context);

    // Workaround for a bug in SpiderMonkey where tzset is not called before
    // localtime_r, see https://bugzilla.mozilla.org/show_bug.cgi?id=1004706
    tzset();

    JS_ClearDateCaches(context);
    JS_EndRequest(context);

    rec.rval().setUndefined();
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FS("addressOf", gjs_address_of, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS("refcount", gjs_refcount, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS("breakpoint", gjs_breakpoint, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS("gc", gjs_gc, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS("exit", gjs_exit, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS("clearDateCaches", gjs_clear_date_caches, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};

bool
gjs_js_define_system_stuff(JSContext              *context,
                           JS::MutableHandleObject module)
{
    GjsContext *gjs_context;
    char *program_name;
    bool retval;

    module.set(JS_NewObject(context, NULL, NULL, NULL));

    if (!JS_DefineFunctions(context, module, &module_funcs[0]))
        return false;

    retval = false;

    gjs_context = (GjsContext*) JS_GetContextPrivate(context);
    g_object_get(gjs_context,
                 "program-name", &program_name,
                 NULL);

    JS::RootedValue value(context);
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
                           JS::Int32Value(GJS_VERSION),
                           JS_PropertyStub,
                           JS_StrictPropertyStub,
                           GJS_MODULE_PROP_FLAGS | JSPROP_READONLY))
        goto out;

    retval = true;

 out:
    g_free(program_name);
    return retval;
}
