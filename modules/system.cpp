/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>  // for GJS_VERSION

#include <errno.h>
#include <stdio.h>   // for FILE, fclose, stdout
#include <string.h>  // for strerror
#include <time.h>    // for tzset

#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Date.h>                // for ResetTimeZone
#include <js/GCAPI.h>               // for JS_GC
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>        // for JS_DefinePropertyById, JS_DefineF...
#include <jsfriendapi.h>  // for DumpHeap, IgnoreNurseryObjects

#include "gi/object.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "modules/system.h"
#include "util/log.h"

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

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_address_of_gobject(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject target_obj(cx);
    GObject *obj;

    if (!gjs_parse_call_args(cx, "addressOfGObject", argv, "o", "object",
                             &target_obj))
        return false;

    if (!ObjectBase::to_c_ptr(cx, target_obj, &obj)) {
        gjs_throw(cx, "Object %p is not a GObject", &target_obj);
        return false;
    }

    GjsAutoChar pointer_string = g_strdup_printf("%p", obj);
    return gjs_string_from_utf8(cx, pointer_string, argv.rval());
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

    if (!ObjectBase::to_c_ptr(context, target_obj, &obj))
        return false;
    if (!obj) {
        // Object already disposed, treat as refcount 0
        argv.rval().setInt32(0);
        return true;
    }

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
        if (!fp) {
            gjs_throw(cx, "Cannot dump heap to %s: %s", filename.get(),
                      strerror(errno));
            return false;
        }
        js::DumpHeap(cx, fp, js::IgnoreNurseryObjects);
        fclose(fp);
    } else {
        js::DumpHeap(cx, stdout, js::IgnoreNurseryObjects);
    }

    gjs_debug(GJS_DEBUG_CONTEXT, "Heap dumped to %s",
              filename ? filename.get() : "stdout");

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

static bool gjs_clear_date_caches(JSContext*, unsigned argc, JS::Value* vp) {
    JS::CallArgs rec = JS::CallArgsFromVp(argc, vp);

    // Workaround for a bug in SpiderMonkey where tzset is not called before
    // localtime_r, see https://bugzilla.mozilla.org/show_bug.cgi?id=1004706
    tzset();

    JS::ResetTimeZone();

    rec.rval().setUndefined();
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FN("addressOf", gjs_address_of, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("addressOfGObject", gjs_address_of_gobject, 1, GJS_MODULE_PROP_FLAGS),
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
