/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

#include <config.h>

#include <stdio.h>  // for stderr

#include <glib-object.h>
#include <glib.h>

#include <js/TypeDecls.h>
#include <jsfriendapi.h>  // for DumpBacktrace

#include "gjs/context.h"

// Avoid static_assert in MSVC builds
namespace JS {
template <typename T> struct GCPolicy;

template <>
struct GCPolicy<void*> : public IgnoreGCPolicy<void*> {};
}

void
gjs_context_print_stack_stderr(GjsContext *context)
{
    JSContext *cx = (JSContext*) gjs_context_get_native_context(context);

    g_printerr("== Stack trace for context %p ==\n", context);
    js::DumpBacktrace(cx, stderr);
}

void
gjs_dumpstack(void)
{
    GList *contexts = gjs_context_get_all();
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        GjsContext *context = (GjsContext*)iter->data;
        gjs_context_print_stack_stderr(context);
        g_object_unref(context);
    }
    g_list_free(contexts);
}
