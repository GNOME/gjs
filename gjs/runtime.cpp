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

#include "compat.h"
#include "runtime.h"

struct RuntimeData {
  JSBool in_gc_sweep;
};

JSBool
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
static JSBool
gjs_locale_to_upper_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *upper_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    upper_case_utf8 = g_utf8_strup (utf8, -1);

    if (!gjs_string_from_utf8(context, upper_case_utf8, -1, retval.address()))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(upper_case_utf8);

    return success;
}

static JSBool
gjs_locale_to_lower_case (JSContext *context,
                          JS::HandleString src,
                          JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8 = NULL;
    char *lower_case_utf8 = NULL;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src), &utf8))
        goto out;

    lower_case_utf8 = g_utf8_strdown (utf8, -1);

    if (!gjs_string_from_utf8(context, lower_case_utf8, -1, retval.address()))
        goto out;

    success = JS_TRUE;

out:
    g_free(utf8);
    g_free(lower_case_utf8);

    return success;
}

static JSBool
gjs_locale_compare (JSContext *context,
                    JS::HandleString src_1,
                    JS::HandleString src_2,
                    JS::MutableHandleValue retval)
{
    JSBool success = JS_FALSE;
    char *utf8_1 = NULL, *utf8_2 = NULL;
    int result;

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(src_1), &utf8_1) ||
        !gjs_string_to_utf8(context, STRING_TO_JSVAL(src_2), &utf8_2))
        goto out;

    result = g_utf8_collate (utf8_1, utf8_2);
    retval.set(INT_TO_JSVAL(result));

    success = JS_TRUE;

out:
    g_free(utf8_1);
    g_free(utf8_2);

    return success;
}

static JSBool
gjs_locale_to_unicode (JSContext  *context,
                       const char *src,
                       JS::MutableHandleValue retval)
{
    JSBool success;
    char *utf8;
    GError *error = NULL;

    utf8 = g_locale_to_utf8(src, -1, NULL, NULL, &error);
    if (!utf8) {
        gjs_throw(context,
                  "Failed to convert locale string to UTF8: %s",
                  error->message);
        g_error_free(error);
        return JS_FALSE;
    }

    success = gjs_string_from_utf8(context, utf8, -1, retval.address());
    g_free (utf8);

    return success;
}

static void
destroy_runtime(gpointer data)
{
    JSRuntime *runtime = (JSRuntime *) data;
    RuntimeData *rtdata = (RuntimeData *) JS_GetRuntimePrivate(runtime);

    g_free(rtdata);
    JS_DestroyRuntime(runtime);
}

static GPrivate thread_runtime = G_PRIVATE_INIT(destroy_runtime);

static JSLocaleCallbacks gjs_locale_callbacks =
{
    gjs_locale_to_upper_case,
    gjs_locale_to_lower_case,
    gjs_locale_compare,
    gjs_locale_to_unicode
};

void
gjs_finalize_callback(JSFreeOp         *fop,
                      JSFinalizeStatus  status,
                      JSBool            isCompartment)
{
  JSRuntime *runtime;
  RuntimeData *data;

  runtime = fop->runtime();
  data = (RuntimeData*) JS_GetRuntimePrivate(runtime);

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
       TRUE.

     Comments in mozjs24 imply that this behavior might
     change in the future, but it hasn't changed in
     mozilla-central as of 2014-02-23. In addition to
     that, the mozilla-central version has a huge comment
     in a different portion of the file, explaining
     why finalization of objects can't be mixed with JS
     code, so we can probably rely on this behavior.
  */

  if (status == JSFINALIZE_GROUP_START)
    data->in_gc_sweep = JS_TRUE;
  else if (status == JSFINALIZE_GROUP_END)
    data->in_gc_sweep = JS_FALSE;
}

JSRuntime *
gjs_runtime_for_current_thread(void)
{
    JSRuntime *runtime = (JSRuntime *) g_private_get(&thread_runtime);
    RuntimeData *data;

    if (!runtime) {
        runtime = JS_NewRuntime(32*1024*1024 /* max bytes */, JS_USE_HELPER_THREADS);
        if (runtime == NULL)
            g_error("Failed to create javascript runtime");

        data = g_new0(RuntimeData, 1);
        JS_SetRuntimePrivate(runtime, data);

        JS_SetNativeStackQuota(runtime, 1024*1024);
        JS_SetGCParameter(runtime, JSGC_MAX_BYTES, 0xffffffff);
        JS_SetLocaleCallbacks(runtime, &gjs_locale_callbacks);
        JS_SetFinalizeCallback(runtime, gjs_finalize_callback);

        g_private_set(&thread_runtime, runtime);
    }

    return runtime;
}
