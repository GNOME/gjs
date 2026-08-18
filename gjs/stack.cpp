/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stdio.h>  // for stderr

#include <algorithm>  // for min
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

void gjs_context_print_stack_stderr(GjsContext* self) {
    auto* cx = static_cast<JSContext*>(gjs_context_get_native_context(self));

    g_printerr("== Stack trace for context %p ==\n", self);
    js::DumpBacktrace(cx, stderr);
}

void gjs_dumpstack() {
    Gjs::SmartPointer<GList> contexts{gjs_context_get_all()};

    for (GList* iter = contexts; iter; iter = iter->next) {
        Gjs::AutoUnref<GjsContext> gjs_context{GJS_CONTEXT(iter->data)};
        gjs_context_print_stack_stderr(gjs_context);
    }
}

std::string gjs_dumpstack_string() {
    Gjs::SmartPointer<GList> contexts{gjs_context_get_all()};
    js::Sprinter printer;
    if (!printer.init())
        return "No stack trace: out of memory";

    for (GList* iter = contexts; iter; iter = iter->next) {
        Gjs::AutoUnref<GjsContext> gjs_context{GJS_CONTEXT(iter->data)};
        auto* cx = static_cast<JSContext*>(
            gjs_context_get_native_context(gjs_context));

        printer.printf("== Stack trace for context 0x%p ==\n",
                       gjs_context.get());
        js::DumpBacktrace(cx, printer);
        printer.putChar('\n');
    }

    size_t len = printer.length();
    JS::UniqueChars all_traces = printer.release();
    if (!all_traces)
        return "No stack trace: out of memory";

    // COMPAT: 2zu in C++23
    return {all_traces.get(), len - std::min(len, size_t{2})};
}
