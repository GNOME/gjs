/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include "jsapi-util.h"
#include "jsapi-wrapper.h"
#include "runtime.h"

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct RuntimeData {
  unsigned refcount;
  bool in_gc_sweep;
};

bool
gjs_runtime_is_sweeping (JSRuntime *runtime)
{
  RuntimeData *data = (RuntimeData*) JS_GetRuntimePrivate(runtime);

  return data->in_gc_sweep;
}

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
    bool success = false;
    char *utf8 = NULL;
    char *upper_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, JS::StringValue(src), &utf8))
        goto out;

    upper_case_utf8 = g_utf8_strup (utf8, -1);

    if (!gjs_string_from_utf8(context, upper_case_utf8, -1, retval))
        goto out;

    success = true;

out:
    g_free(utf8);
    g_free(upper_case_utf8);

    return success;
}

static bool
gjs_locale_to_lower_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    bool success = false;
    char *utf8 = NULL;
    char *lower_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, JS::StringValue(src), &utf8))
        goto out;

    lower_case_utf8 = g_utf8_strdown (utf8, -1);

    if (!gjs_string_from_utf8(context, lower_case_utf8, -1, retval))
        goto out;

    success = true;

out:
    g_free(utf8);
    g_free(lower_case_utf8);

    return success;
}

static bool
gjs_locale_compare (JSContext *context,
                    JS::HandleString src_1,
                    JS::HandleString src_2,
                    JS::MutableHandleValue retval)
{
    bool success = false;
    char *utf8_1 = NULL, *utf8_2 = NULL;

    if (!gjs_string_to_utf8(context, JS::StringValue(src_1), &utf8_1) ||
        !gjs_string_to_utf8(context, JS::StringValue(src_2), &utf8_2))
        goto out;

    retval.setInt32(g_utf8_collate(utf8_1, utf8_2));

    success = true;

out:
    g_free(utf8_1);
    g_free(utf8_2);

    return success;
}

static bool
gjs_locale_to_unicode (JSContext  *context,
                       const char *src,
                       JS::MutableHandleValue retval)
{
    bool success;
    char *utf8;
    GError *error = NULL;

    utf8 = g_locale_to_utf8(src, -1, NULL, NULL, &error);
    if (!utf8) {
        gjs_throw(context,
                  "Failed to convert locale string to UTF8: %s",
                  error->message);
        g_error_free(error);
        return false;
    }

    success = gjs_string_from_utf8(context, utf8, -1, retval);
    g_free (utf8);

    return success;
}

static void
destroy_runtime(gpointer data)
{
    JSRuntime *runtime = (JSRuntime *) data;
    RuntimeData *rtdata = (RuntimeData *) JS_GetRuntimePrivate(runtime);

    JS_DestroyRuntime(runtime);
    g_free(rtdata);
}

static GPrivate thread_runtime = G_PRIVATE_INIT(destroy_runtime);

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
                      bool              isCompartment,
                      void             *user_data)
{
  RuntimeData *data = static_cast<RuntimeData *>(user_data);

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

  if (status == JSFINALIZE_GROUP_START)
    data->in_gc_sweep = true;
  else if (status == JSFINALIZE_GROUP_END)
    data->in_gc_sweep = false;
}

/* Destroys the current thread's runtime regardless of refcount. No-op if there
 * is no runtime */
static void
gjs_destroy_runtime_for_current_thread(void)
{
    g_private_replace(&thread_runtime, NULL);
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
    g_assert(JS_Init());
    gjs_is_inited = true;
    break;

  case DLL_THREAD_DETACH:
    gjs_destroy_runtime_for_current_thread();
    break;

  case DLL_PROCESS_DETACH:
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
        /* No-op if the runtime was already destroyed */
        gjs_destroy_runtime_for_current_thread();
        JS_ShutDown();
    }

    operator bool() {
        return true;
    }
};

static GjsInit gjs_is_inited;
#endif

static JSRuntime *
gjs_runtime_for_current_thread(void)
{
    JSRuntime *runtime = (JSRuntime *) g_private_get(&thread_runtime);
    RuntimeData *data;

    if (!runtime) {
        g_assert(gjs_is_inited);
        runtime = JS_NewRuntime(32 * 1024 * 1024 /* max bytes */);
        if (runtime == NULL)
            g_error("Failed to create javascript runtime");

        data = g_new0(RuntimeData, 1);
        JS_SetRuntimePrivate(runtime, data);

        JS_SetNativeStackQuota(runtime, 1024*1024);
        JS_SetGCParameter(runtime, JSGC_MAX_BYTES, 0xffffffff);
        JS_SetLocaleCallbacks(runtime, &gjs_locale_callbacks);
        JS_AddFinalizeCallback(runtime, gjs_finalize_callback, data);
        JS_SetErrorReporter(runtime, gjs_error_reporter);

        g_private_set(&thread_runtime, runtime);
    }

    return runtime;
}

/* These two act on the current thread's runtime. In the future they will go
 * away because SpiderMonkey is going to merge JSContext and JSRuntime.
 */

/* Creates a new runtime with one reference if there is no runtime yet */
JSRuntime *
gjs_runtime_ref(void)
{
    JSRuntime *rt = static_cast<JSRuntime *>(gjs_runtime_for_current_thread());
    RuntimeData *data = static_cast<RuntimeData *>(JS_GetRuntimePrivate(rt));
    g_atomic_int_inc(&data->refcount);
    return rt;
}

/* No-op if there is no runtime */
void
gjs_runtime_unref(void)
{
    JSRuntime *rt = static_cast<JSRuntime *>(g_private_get(&thread_runtime));
    if (rt == NULL)
        return;
    RuntimeData *data = static_cast<RuntimeData *>(JS_GetRuntimePrivate(rt));
    if (g_atomic_int_dec_and_test(&data->refcount))
        gjs_destroy_runtime_for_current_thread();
}
