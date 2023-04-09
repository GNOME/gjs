/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stdio.h>  // for stderr, open_memstream

#include <sstream>
#include <string>

#include <glib-object.h>
#include <glib.h>

#include <js/TypeDecls.h>
#include <js/friend/DumpFunctions.h>

#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/jsapi-util.h"

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
    GjsSmartPointer<GList> contexts = gjs_context_get_all();
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        GjsAutoUnref<GjsContext> context(GJS_CONTEXT(iter->data));
        gjs_context_print_stack_stderr(context);
    }
}

#ifdef HAVE_OPEN_MEMSTREAM
static std::string
stack_trace_string(GjsContext *context) {
    JSContext *cx = static_cast<JSContext *>(gjs_context_get_native_context(context));
    std::ostringstream out;
    FILE *stream;
    GjsAutoChar buf;
    size_t len;

    stream = open_memstream(buf.out(), &len);
    if (!stream) {
        out << "No stack trace for context " << context << ": "
               "open_memstream() failed\n\n";
        return out.str();
    }
    js::DumpBacktrace(cx, stream);
    fclose(stream);
    out << "== Stack trace for context " << context << " ==\n"
        << buf.get() << "\n";
    return out.str();
}
#endif

std::string
gjs_dumpstack_string() {
    std::string out;
    std::ostringstream all_traces;

#ifdef HAVE_OPEN_MEMSTREAM
    GjsSmartPointer<GList> contexts = gjs_context_get_all();
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        GjsAutoUnref<GjsContext> context(GJS_CONTEXT(iter->data));
        all_traces << stack_trace_string(context);
    }
    out = all_traces.str();
    out.resize(MAX(out.size() - 2, 0));
#else
    out = "No stack trace: no open_memstream() support. "
          "See https://bugzilla.mozilla.org/show_bug.cgi?id=1826290";
#endif

    return out;
}
