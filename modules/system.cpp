/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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
#include <time.h>

#include "gjs/jsapi-wrapper.h"
#include <js/Date.h>

#include <gjs/context.h>

#include "gi/object.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "system.h"

/* Note that this cannot be relied on to test whether two objects are the same!
 * SpiderMonkey can move objects around in memory during garbage collection,
 * and it can also deduplicate identical instances of objects in memory. */
static bool
gjs_address_of(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    JS::RootedObject target_obj(context);

    if (!gjs_parse_call_args(context, "addressOf", argv, "o",
                             "object", &target_obj))
        return false;

    GjsAutoChar pointer_string = g_strdup_printf("%p", target_obj.get());
    return gjs_string_from_utf8(context, pointer_string, argv.rval());
}

static bool
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

static bool
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

static bool
gjs_dump_heap(JSContext *cx,
              unsigned   argc,
              JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsAutoChar filename;

    if (!gjs_parse_call_args(cx, "dumpHeap", args, "|F", "filename", &filename))
        return false;

    if (filename) {
        FILE *fp = fopen(filename, "a");
        js::DumpHeap(cx, fp, js::IgnoreNurseryObjects);
        fclose(fp);
    } else {
        js::DumpHeap(cx, stdout, js::IgnoreNurseryObjects);
    }

    args.rval().setUndefined();
    return true;
}

static bool
gjs_gc(JSContext *context,
       unsigned   argc,
       JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    if (!gjs_parse_call_args(context, "gc", argv, ""))
        return false;
    JS_GC(context);
    argv.rval().setUndefined();
    return true;
}

static bool
gjs_exit(JSContext *context,
         unsigned   argc,
         JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    gint32 ecode;
    if (!gjs_parse_call_args(context, "exit", argv, "i",
                             "ecode", &ecode))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    gjs->exit(ecode);
    return false;  /* without gjs_throw() == "throw uncatchable exception" */
}

static bool
gjs_clear_date_caches(JSContext *context,
                      unsigned   argc,
                      JS::Value *vp)
{
    JS::CallArgs rec = JS::CallArgsFromVp(argc, vp);
    JS_BeginRequest(context);

    // Workaround for a bug in SpiderMonkey where tzset is not called before
    // localtime_r, see https://bugzilla.mozilla.org/show_bug.cgi?id=1004706
    tzset();

    JS::ResetTimeZone();
    JS_EndRequest(context);

    rec.rval().setUndefined();
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FN("addressOf", gjs_address_of, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("refcount", gjs_refcount, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("breakpoint", gjs_breakpoint, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("dumpHeap", gjs_dump_heap, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("gc", gjs_gc, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("exit", gjs_exit, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("clearDateCaches", gjs_clear_date_caches, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

bool
gjs_js_define_system_stuff(JSContext              *context,
                           JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));

    if (!JS_DefineFunctions(context, module, &module_funcs[0]))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    const char* program_name = gjs->program_name();

    JS::RootedValue value(context);
    return gjs_string_from_utf8(context, program_name, &value) &&
           /* The name is modeled after program_invocation_name, part of glibc
            */
           JS_DefinePropertyById(context, module,
                                 gjs->atoms().program_invocation_name(), value,
                                 GJS_MODULE_PROP_FLAGS | JSPROP_READONLY) &&
           JS_DefinePropertyById(context, module, gjs->atoms().version(),
                                 GJS_VERSION,
                                 GJS_MODULE_PROP_FLAGS | JSPROP_READONLY);
}
