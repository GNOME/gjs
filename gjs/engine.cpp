/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include "jsapi-wrapper.h"
#include <js/Initialization.h>

#include "context-private.h"
#include "engine.h"
#include "gi/object.h"
#include "jsapi-util.h"
#include "util/log.h"

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Implementations of locale-specific operations; these are used
 * in the implementation of String.localeCompare(), Date.toLocaleDateString(),
 * and so forth. We take the straight-forward approach of converting
 * to UTF-8, using the appropriate GLib functions, and converting
 * back if necessary.
 */
static bool
gjs_locale_to_upper_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JS::UniqueChars utf8(JS_EncodeStringToUTF8(context, src));
    if (!utf8)
        return false;

    GjsAutoChar upper_case_utf8 = g_utf8_strup(utf8.get(), -1);
    return gjs_string_from_utf8(context, upper_case_utf8, retval);
}

static bool
gjs_locale_to_lower_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JS::UniqueChars utf8(JS_EncodeStringToUTF8(context, src));
    if (!utf8)
        return false;

    GjsAutoChar lower_case_utf8 = g_utf8_strdown(utf8.get(), -1);
    return gjs_string_from_utf8(context, lower_case_utf8, retval);
}

static bool
gjs_locale_compare (JSContext *context,
                    JS::HandleString src_1,
                    JS::HandleString src_2,
                    JS::MutableHandleValue retval)
{
    JS::UniqueChars utf8_1(JS_EncodeStringToUTF8(context, src_1));
    if (!utf8_1)
        return false;

    JS::UniqueChars utf8_2(JS_EncodeStringToUTF8(context, src_2));
    if (!utf8_2)
        return false;

    retval.setInt32(g_utf8_collate(utf8_1.get(), utf8_2.get()));

    return true;
}

static bool
gjs_locale_to_unicode (JSContext  *context,
                       const char *src,
                       JS::MutableHandleValue retval)
{
    GError *error = NULL;

    GjsAutoChar utf8 = g_locale_to_utf8(src, -1, NULL, NULL, &error);
    if (!utf8) {
        gjs_throw(context,
                  "Failed to convert locale string to UTF8: %s",
                  error->message);
        g_error_free(error);
        return false;
    }

    return gjs_string_from_utf8(context, utf8, retval);
}

static JSLocaleCallbacks gjs_locale_callbacks =
{
    gjs_locale_to_upper_case,
    gjs_locale_to_lower_case,
    gjs_locale_compare,
    gjs_locale_to_unicode
};

static void
gjs_finalize_callback(JSFreeOp         *fop,
                      JSFinalizeStatus  status,
                      void             *data)
{
    auto js_context = static_cast<GjsContext *>(data);

  /* Implementation note for mozjs 24:
     sweeping happens in two phases, in the first phase all
     GC things from the allocation arenas are queued for
     sweeping, then the actual sweeping happens.
     The first phase is marked by JSFINALIZE_GROUP_START,
     the second one by JSFINALIZE_GROUP_END, and finally
     we will see JSFINALIZE_COLLECTION_END at the end of
     all GC.
     (see jsgc.cpp, BeginSweepPhase/BeginSweepingZoneGroup
     and SweepPhase, all called from IncrementalCollectSlice).
     Incremental GC muds the waters, because BeginSweepPhase
     is always run to entirety, but SweepPhase can be run
     incrementally and mixed with JS code runs or even
     native code, when MaybeGC/IncrementalGC return.

     Luckily for us, objects are treated specially, and
     are not really queued for deferred incremental
     finalization (unless they are marked for background
     sweeping). Instead, they are finalized immediately
     during phase 1, so the following guarantees are
     true (and we rely on them)
     - phase 1 of GC will begin and end in the same JSAPI
       call (ie, our callback will be called with GROUP_START
       and the triggering JSAPI call will not return until
       we see a GROUP_END)
     - object finalization will begin and end in the same
       JSAPI call
     - therefore, if there is a finalizer frame somewhere
       in the stack, gjs_runtime_is_sweeping() will return
       true.

     Comments in mozjs24 imply that this behavior might
     change in the future, but it hasn't changed in
     mozilla-central as of 2014-02-23. In addition to
     that, the mozilla-central version has a huge comment
     in a different portion of the file, explaining
     why finalization of objects can't be mixed with JS
     code, so we can probably rely on this behavior.
  */

  if (status == JSFINALIZE_GROUP_PREPARE)
        _gjs_context_set_sweeping(js_context, true);
  else if (status == JSFINALIZE_GROUP_END)
        _gjs_context_set_sweeping(js_context, false);
}

static void
on_garbage_collect(JSContext *cx,
                   JSGCStatus status,
                   void      *data)
{
    /* We finalize any pending toggle refs before doing any garbage collection,
     * so that we can collect the JS wrapper objects, and in order to minimize
     * the chances of objects having a pending toggle up queued when they are
     * garbage collected. */
    if (status == JSGC_BEGIN)
        gjs_object_clear_toggles();
}

static bool
on_enqueue_promise_job(JSContext       *cx,
                       JS::HandleObject callback,
                       JS::HandleObject allocation_site,
                       JS::HandleObject global,
                       void            *data)
{
    auto gjs_context = static_cast<GjsContext *>(data);
    return _gjs_context_enqueue_job(gjs_context, callback);
}

static void on_promise_unhandled_rejection(
    JSContext* cx, JS::HandleObject promise,
    JS::PromiseRejectionHandlingState state, void* data) {
    auto gjs_context = static_cast<GjsContext *>(data);
    uint64_t id = JS::GetPromiseID(promise);

    if (state == JS::PromiseRejectionHandlingState::Handled) {
        /* This happens when catching an exception from an await expression. */
        _gjs_context_unregister_unhandled_promise_rejection(gjs_context, id);
        return;
    }

    JS::RootedObject allocation_site(cx, JS::GetPromiseAllocationSite(promise));
    GjsAutoChar stack = gjs_format_stack_trace(cx, allocation_site);
    _gjs_context_register_unhandled_promise_rejection(gjs_context, id,
                                                      std::move(stack));
}

#ifdef G_OS_WIN32
HMODULE gjs_dll;
static bool gjs_is_inited = false;

BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
DWORD     fdwReason,
LPVOID    lpvReserved)
{
  switch (fdwReason)
  {
  case DLL_PROCESS_ATTACH:
    gjs_dll = hinstDLL;
    gjs_is_inited = JS_Init();
    break;

  case DLL_THREAD_DETACH:
    JS_ShutDown ();
    break;

  default:
    /* do nothing */
    ;
    }

  return TRUE;
}

#else
class GjsInit {
public:
    GjsInit() {
        if (!JS_Init())
            g_error("Could not initialize Javascript");
    }

    ~GjsInit() {
        JS_ShutDown();
    }

    operator bool() {
        return true;
    }
};

static GjsInit gjs_is_inited;
#endif

JSContext *
gjs_create_js_context(GjsContext *js_context)
{
    g_assert(gjs_is_inited);
    JSContext *cx = JS_NewContext(32 * 1024 * 1024 /* max bytes */);
    if (!cx)
        return nullptr;

    if (!JS::InitSelfHostedCode(cx))
        return nullptr;

    // commented are defaults in moz-24
    JS_SetNativeStackQuota(cx, 1024 * 1024);
    JS_SetGCParameter(cx, JSGC_MAX_MALLOC_BYTES, 128 * 1024 * 1024);
    JS_SetGCParameter(cx, JSGC_MAX_BYTES, -1);
    JS_SetGCParameter(cx, JSGC_MODE, JSGC_MODE_INCREMENTAL);
    JS_SetGCParameter(cx, JSGC_SLICE_TIME_BUDGET, 10); /* ms */
    // JS_SetGCParameter(cx, JSGC_HIGH_FREQUENCY_TIME_LIMIT, 1000); /* ms */
    JS_SetGCParameter(cx, JSGC_DYNAMIC_MARK_SLICE, true);
    JS_SetGCParameter(cx, JSGC_DYNAMIC_HEAP_GROWTH, true);
    // JS_SetGCParameter(cx, JSGC_LOW_FREQUENCY_HEAP_GROWTH, 150);
    // JS_SetGCParameter(cx, JSGC_HIGH_FREQUENCY_HEAP_GROWTH_MIN, 150);
    // JS_SetGCParameter(cx, JSGC_HIGH_FREQUENCY_HEAP_GROWTH_MAX, 300);
    // JS_SetGCParameter(cx, JSGC_HIGH_FREQUENCY_LOW_LIMIT, 100);
    // JS_SetGCParameter(cx, JSGC_HIGH_FREQUENCY_HIGH_LIMIT, 500);
    // JS_SetGCParameter(cx, JSGC_ALLOCATION_THRESHOLD, 30);
    // JS_SetGCParameter(cx, JSGC_DECOMMIT_THRESHOLD, 32);

    /* set ourselves as the private data */
    JS_SetContextPrivate(cx, js_context);

    JS_AddFinalizeCallback(cx, gjs_finalize_callback, js_context);
    JS_SetGCCallback(cx, on_garbage_collect, js_context);
    JS_SetLocaleCallbacks(JS_GetRuntime(cx), &gjs_locale_callbacks);
    JS::SetWarningReporter(cx, gjs_warning_reporter);
    JS::SetGetIncumbentGlobalCallback(cx, gjs_get_import_global);
    JS::SetEnqueuePromiseJobCallback(cx, on_enqueue_promise_job, js_context);
    JS::SetPromiseRejectionTrackerCallback(cx, on_promise_unhandled_rejection,
                                           js_context);

    /* setExtraWarnings: Be extra strict about code that might hide a bug */
    if (!g_getenv("GJS_DISABLE_EXTRA_WARNINGS")) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling extra warnings");
        JS::ContextOptionsRef(cx).setExtraWarnings(true);
    }

    bool enable_jit = !(g_getenv("GJS_DISABLE_JIT"));
    if (enable_jit) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling JIT");
    }
    JS::ContextOptionsRef(cx)
        .setIon(enable_jit)
        .setBaseline(enable_jit)
        .setAsmJS(enable_jit);

    return cx;
}
