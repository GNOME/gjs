/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>

#include <config.h>

#include <stdint.h>

#ifdef _WIN32
#    include <windows.h>
#endif

#include <utility>  // for move

#include <gio/gio.h>
#include <glib.h>

#include <js/Context.h>
#include <js/ContextOptions.h>
#include <js/GCAPI.h>           // for JS_SetGCParameter, JS_AddFin...
#include <js/Initialization.h>  // for JS_Init, JS_ShutDown
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/Stack.h>  // for JS_SetNativeStackQuota
#include <js/TypeDecls.h>
#include <js/Warnings.h>
#include <js/experimental/SourceHook.h>
#include <jsapi.h>  // for JS_SetGlobalJitCompilerOption
#include <mozilla/UniquePtr.h>

#include "gjs/context-private.h"
#include "gjs/engine.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"

static void gjs_finalize_callback(JS::GCContext*, JSFinalizeStatus status,
                                  void* data) {
    auto* gjs = static_cast<GjsContextPrivate*>(data);
    gjs->set_finalize_status(status);
}

static void on_promise_unhandled_rejection(
    JSContext* cx, bool mutedErrors [[maybe_unused]], JS::HandleObject promise,
    JS::PromiseRejectionHandlingState state, void* data) {
    auto gjs = static_cast<GjsContextPrivate*>(data);
    uint64_t id = JS::GetPromiseID(promise);

    if (state == JS::PromiseRejectionHandlingState::Handled) {
        /* This happens when catching an exception from an await expression. */
        gjs->unregister_unhandled_promise_rejection(id);
        return;
    }

    JS::RootedObject allocation_site(cx, JS::GetPromiseAllocationSite(promise));
    GjsAutoChar stack = gjs_format_stack_trace(cx, allocation_site);
    gjs->register_unhandled_promise_rejection(id, std::move(stack));
}

bool gjs_load_internal_source(JSContext* cx, const char* filename, char** src,
                              size_t* length) {
    GError* error = nullptr;
    const char* path = filename + 11;  // len("resource://")
    GBytes* script_bytes =
        g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!script_bytes)
        return gjs_throw_gerror_message(cx, error);

    *src = static_cast<char*>(g_bytes_unref_to_data(script_bytes, length));
    return true;
}

class GjsSourceHook : public js::SourceHook {
    bool load(JSContext* cx, const char* filename,
              char16_t** two_byte_source [[maybe_unused]], char** utf8_source,
              size_t* length) {
        // caller owns the source, per documentation of SourceHook
        return gjs_load_internal_source(cx, filename, utf8_source, length);
    }
};

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
      case DLL_PROCESS_ATTACH: {
          gjs_dll = hinstDLL;
          const char* reason = JS_InitWithFailureDiagnostic();
          if (reason)
              g_error("Could not initialize JavaScript: %s", reason);
          gjs_is_inited = true;
      } break;

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
        const char* reason = JS_InitWithFailureDiagnostic();
        if (reason)
            g_error("Could not initialize JavaScript: %s", reason);
    }

    ~GjsInit() {
        JS_ShutDown();
    }

    explicit operator bool() const { return true; }
};

static GjsInit gjs_is_inited;
#endif

JSContext* gjs_create_js_context(GjsContextPrivate* uninitialized_gjs) {
    g_assert(gjs_is_inited);
    JSContext *cx = JS_NewContext(32 * 1024 * 1024 /* max bytes */);
    if (!cx)
        return nullptr;

    if (!JS::InitSelfHostedCode(cx)) {
        JS_DestroyContext(cx);
        return nullptr;
    }

    // For additional context on these options, see
    // https://searchfox.org/mozilla-esr91/rev/c49725508e97c1e2e2bb3bf9ed0ba14b2016abac/js/public/GCAPI.h#53
    JS_SetNativeStackQuota(cx, 1024 * 1024);
    JS_SetGCParameter(cx, JSGC_MAX_BYTES, -1);
    JS_SetGCParameter(cx, JSGC_INCREMENTAL_GC_ENABLED, 1);
    JS_SetGCParameter(cx, JSGC_SLICE_TIME_BUDGET_MS, 10); /* ms */

    /* set ourselves as the private data */
    JS_SetContextPrivate(cx, uninitialized_gjs);

    JS_AddFinalizeCallback(cx, gjs_finalize_callback, uninitialized_gjs);
    JS::SetWarningReporter(cx, gjs_warning_reporter);
    JS::SetJobQueue(cx, dynamic_cast<JS::JobQueue*>(uninitialized_gjs));
    JS::SetPromiseRejectionTrackerCallback(cx, on_promise_unhandled_rejection,
                                           uninitialized_gjs);

    // We use this to handle "lazy sources" that SpiderMonkey doesn't need to
    // keep in memory. Most sources should be kept in memory, but we can skip
    // doing that for the realm bootstrap code, as it is already in memory in
    // the form of a GResource. Instead we use the "source hook" to retrieve it.
    auto hook = mozilla::MakeUnique<GjsSourceHook>();
    js::SetSourceHook(cx, std::move(hook));

    if (g_getenv("GJS_DISABLE_EXTRA_WARNINGS")) {
        g_warning(
            "GJS_DISABLE_EXTRA_WARNINGS has been removed, GJS no longer logs "
            "extra warnings.");
    }

    bool enable_jit = !(g_getenv("GJS_DISABLE_JIT"));
    if (enable_jit) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling JIT");
    }
    JS::ContextOptionsRef(cx).setAsmJS(enable_jit);

    uint32_t value = enable_jit ? 1 : 0;

    JS_SetGlobalJitCompilerOption(
        cx, JSJitCompilerOption::JSJITCOMPILER_ION_ENABLE, value);
    JS_SetGlobalJitCompilerOption(
        cx, JSJitCompilerOption::JSJITCOMPILER_BASELINE_ENABLE, value);
    JS_SetGlobalJitCompilerOption(
        cx, JSJitCompilerOption::JSJITCOMPILER_BASELINE_INTERPRETER_ENABLE, value);

    return cx;
}
