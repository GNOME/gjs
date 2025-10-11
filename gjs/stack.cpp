/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stdio.h>  // for stderr

#include <sstream>
#include <string>

#include <glib-object.h>
#include <glib.h>

#include <js/Printer.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/friend/DumpFunctions.h>

#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/context.h"

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
    Gjs::SmartPointer<GList> contexts{gjs_context_get_all()};
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        Gjs::AutoUnref<GjsContext> context{GJS_CONTEXT(iter->data)};
        gjs_context_print_stack_stderr(context);
    }
}

std::string
gjs_dumpstack_string() {
    std::string out;
    std::ostringstream all_traces;

    Gjs::SmartPointer<GList> contexts{gjs_context_get_all()};
    js::Sprinter printer;
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        Gjs::AutoUnref<GjsContext> context{GJS_CONTEXT(iter->data)};
        if (!printer.init()) {
            all_traces << "No stack trace for context " << context.get()
                       << ": out of memory\n\n";
            break;
        }
        auto* cx =
            static_cast<JSContext*>(gjs_context_get_native_context(context));
        js::DumpBacktrace(cx, printer);
        JS::UniqueChars trace = printer.release();
        all_traces << "== Stack trace for context " << context.get() << " ==\n"
                   << trace.get() << "\n";
    }
    out = all_traces.str();
    out.resize(MAX(out.size() - 2, 0));

    return out;
}
