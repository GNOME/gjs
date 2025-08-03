/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.
// SPDX-FileCopyrightText: 2019 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2021 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>  // for GJS_VERSION

#include <stdint.h>
#include <stdio.h>
#include <time.h>    // for tzset

#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/Date.h>                // for ResetTimeZone
#include <js/ErrorReport.h>         // for ReportUncatchableException
#include <js/GCAPI.h>               // for JS_GC
#include <js/JSON.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>     // for NullValue
#include <js/friend/DumpFunctions.h>
#include <jsapi.h>        // for JS_GetFunctionObject, JS_NewPlainObject
#include <jsfriendapi.h>  // for GetFunctionNativeReserved, NewFunctionByIdW...

#include "gi/object.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/profiler-private.h"
#include "modules/system.h"
#include "util/log.h"
#include "util/misc.h"  // for LogFile

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

    Gjs::AutoChar pointer_string{g_strdup_printf("%p", target_obj.get())};
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

    Gjs::AutoChar pointer_string{g_strdup_printf("%p", obj)};
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

// This can reduce performance, so should be used for debugging only.
// js::CollectNurseryBeforeDump promotes any live objects in the nursery to the
// tenured heap. This is slow, but this way, we are certain to get an accurate
// picture of the heap.
static bool
gjs_dump_heap(JSContext *cx,
              unsigned   argc,
              JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    Gjs::AutoChar filename;

    if (!gjs_parse_call_args(cx, "dumpHeap", args, "|F", "filename", &filename))
        return false;

    LogFile file(filename);
    if (file.has_error()) {
        gjs_throw(cx, "Cannot dump heap to %s: %s", filename.get(),
                  file.errmsg());
        return false;
    }
    js::DumpHeap(cx, file.fp(), js::CollectNurseryBeforeDump);

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
    JS::ReportUncatchableException(context);
    return false;
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

static bool write_gc_info(const char16_t* buf, uint32_t len, void* data) {
    auto* fp = static_cast<FILE*>(data);

    long bytes_written;  // NOLINT(runtime/int): the GLib API requires this type
    Gjs::AutoChar utf8{g_utf16_to_utf8(reinterpret_cast<const uint16_t*>(buf),
                                       len, /* items_read = */ nullptr,
                                       &bytes_written, /* error = */ nullptr)};
    if (!utf8)
        utf8 = g_strdup("<invalid string>");

    fwrite(utf8, 1, bytes_written, fp);
    return true;
}

static bool gjs_dump_memory_info(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    Gjs::AutoChar filename;
    if (!gjs_parse_call_args(cx, "dumpMemoryInfo", args, "|F", "filename",
                             &filename))
        return false;

    int64_t gc_counters[Gjs::GCCounters::N_COUNTERS];

    // The object returned from NewMemoryInfoObject has gcBytes and mallocBytes
    // properties which are the sum (over all zones) of bytes used. gcBytes is
    // the number of bytes in garbage-collectable things (GC things).
    // mallocBytes is the number of bytes allocated with malloc (reported with
    // JS::AddAssociatedMemory).
    //
    // This info leaks internal state of the JS engine, which is why it is not
    // returned to the caller, only dumped to a file and piped to Sysprof.
    //
    // The object also has a zone property with its own gcBytes and mallocBytes
    // properties, representing the bytes used in the zone that the memory
    // object belongs to. We only have one zone in GJS's context, so
    // zone.gcBytes and zone.mallocBytes are a good measure for how much memory
    // the actual user program is occupying. These are the values that we expose
    // as counters in Sysprof. The difference between these values and the sum
    // values is due to the self-hosting zone and atoms zone, that represent
    // overhead of the JS engine.

    JS::RootedObject gc_info(cx, js::gc::NewMemoryInfoObject(cx));
    if (!gc_info)
        return false;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    int32_t val;
    JS::RootedObject zone_info(cx);
    if (!gjs_object_require_property(cx, gc_info, "gc.zone", atoms.zone(),
                                     &zone_info) ||
        !gjs_object_require_property(cx, zone_info, "gc.zone.gcBytes",
                                     atoms.gc_bytes(), &val))
        return false;
    gc_counters[Gjs::GCCounters::GC_HEAP_BYTES] = int64_t(val);
    if (!gjs_object_require_property(cx, zone_info, "gc.zone.mallocBytes",
                                     atoms.malloc_bytes(), &val))
        return false;
    gc_counters[Gjs::GCCounters::MALLOC_HEAP_BYTES] = int64_t(val);

    auto* gjs = GjsContextPrivate::from_cx(cx);
    if (gjs->profiler() &&
        !_gjs_profiler_sample_gc_memory_info(gjs->profiler(), gc_counters)) {
        gjs_throw(cx, "Could not write GC counters to profiler");
        return false;
    }

    LogFile file(filename);
    if (file.has_error()) {
        gjs_throw(cx, "Cannot dump memory info to %s: %s", filename.get(),
                  file.errmsg());
        return false;
    }

    fprintf(file.fp(), "# GC Memory Info Object #\n\n```json\n");
    JS::RootedValue v_gc_info(cx, JS::ObjectValue(*gc_info));
    JS::RootedValue spacing(cx, JS::Int32Value(2));
    if (!JS_Stringify(cx, &v_gc_info, nullptr, spacing, write_gc_info,
                      file.fp()))
        return false;
    fprintf(file.fp(), "\n```\n");

    args.rval().setUndefined();
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FN("addressOf", gjs_address_of, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("addressOfGObject", gjs_address_of_gobject, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("refcount", gjs_refcount, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("breakpoint", gjs_breakpoint, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("dumpHeap", gjs_dump_heap, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("dumpMemoryInfo", gjs_dump_memory_info, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("gc", gjs_gc, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("exit", gjs_exit, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("clearDateCaches", gjs_clear_date_caches, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

static bool get_program_args(JSContext* cx, unsigned argc, JS::Value* vp) {
    static const size_t SLOT_ARGV = 0;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsContextPrivate* priv = GjsContextPrivate::from_cx(cx);

    JS::RootedValue v_argv(
        cx, js::GetFunctionNativeReserved(&args.callee(), SLOT_ARGV));

    if (v_argv.isUndefined()) {
        // First time this property is accessed, build the array
        JS::RootedObject argv(cx, priv->build_args_array());
        if (!argv)
            return false;
        js::SetFunctionNativeReserved(&args.callee(), SLOT_ARGV,
                                      JS::ObjectValue(*argv));
        args.rval().setObject(*argv);
    } else {
        args.rval().set(v_argv);
    }

    return true;
}

bool
gjs_js_define_system_stuff(JSContext              *context,
                           JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));

    if (!JS_DefineFunctions(context, module, &module_funcs[0]))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    const char* program_name = gjs->program_name();
    const char* program_path = gjs->program_path();

    JS::RootedValue v_program_invocation_name(context);
    JS::RootedValue v_program_path(context, JS::NullValue());
    if (program_path) {
        if (!gjs_string_from_utf8(context, program_path, &v_program_path))
            return false;
    }

    JS::RootedObject program_args_getter(
        context,
        JS_GetFunctionObject(js::NewFunctionByIdWithReserved(
            context, get_program_args, 0, 0, gjs->atoms().program_args())));

    return program_args_getter &&
           gjs_string_from_utf8(context, program_name,
                                &v_program_invocation_name) &&
           /* The name is modeled after program_invocation_name, part of glibc
            */
           JS_DefinePropertyById(context, module,
                                 gjs->atoms().program_invocation_name(),
                                 v_program_invocation_name,
                                 GJS_MODULE_PROP_FLAGS | JSPROP_READONLY) &&
           JS_DefinePropertyById(context, module, gjs->atoms().program_path(),
                                 v_program_path,
                                 GJS_MODULE_PROP_FLAGS | JSPROP_READONLY) &&
           JS_DefinePropertyById(context, module, gjs->atoms().program_args(),
                                 program_args_getter, nullptr,
                                 GJS_MODULE_PROP_FLAGS) &&
           JS_DefinePropertyById(context, module, gjs->atoms().version(),
                                 GJS_VERSION,
                                 GJS_MODULE_PROP_FLAGS | JSPROP_READONLY);
}
