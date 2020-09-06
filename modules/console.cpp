/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set ts=8 sw=4 et tw=78: */
// SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: 1998 Netscape Communications Corporation

#include <config.h>  // for HAVE_READLINE_READLINE_H

#ifdef HAVE_READLINE_READLINE_H
#    include <stdio.h>  // include before readline/readline.h

#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf

#include <js/CallArgs.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Warnings.h>
#include <jsapi.h>  // for JS_IsExceptionPending, Exce...

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "modules/console.h"

namespace mozilla {
union Utf8Unit;
}

static void gjs_console_warning_reporter(JSContext* cx, JSErrorReport* report) {
    JS::PrintError(cx, stderr, report, /* reportWarnings = */ true);
}

/* Based on js::shell::AutoReportException from SpiderMonkey. */
class AutoReportException {
    JSContext *m_cx;

public:
    explicit AutoReportException(JSContext *cx) : m_cx(cx) {}

    ~AutoReportException() {
        if (!JS_IsExceptionPending(m_cx))
            return;

        /* Get exception object before printing and clearing exception. */
        JS::ExceptionStack exnStack(m_cx);
        JS::ErrorReportBuilder report(m_cx);
        if (!JS::StealPendingExceptionStack(m_cx, &exnStack) ||
            !report.init(m_cx, exnStack,
                         JS::ErrorReportBuilder::NoSideEffects)) {
            g_printerr("(Unable to print exception)\n");
            JS_ClearPendingException(m_cx);
            return;
        }

        g_assert(!report.report()->isWarning());

        JS::PrintError(m_cx, stderr, report, /* reportWarnings = */ false);

        if (exnStack.stack()) {
            GjsAutoChar stack_str =
                gjs_format_stack_trace(m_cx, exnStack.stack());
            if (!stack_str)
                g_printerr("(Unable to print stack trace)\n");
            else
                g_printerr("%s", stack_str.get());
        }

        JS_ClearPendingException(m_cx);
    }
};

[[nodiscard]] static bool gjs_console_readline(char** bufp,
                                               const char* prompt) {
#ifdef HAVE_READLINE_READLINE_H
    char *line;
    line = readline(prompt);
    if (!line)
        return false;
    if (line[0] != '\0')
        add_history(line);
    *bufp = line;
#else   // !HAVE_READLINE_READLINE_H
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return false;
    *bufp = g_strdup(line);
#endif  // !HAVE_READLINE_READLINE_H
    return true;
}

/* Return value of false indicates an uncatchable exception, rather than any
 * exception. (This is because the exception should be auto-printed around the
 * invocation of this function.)
 */
[[nodiscard]] static bool gjs_console_eval_and_print(JSContext* cx,
                                                     const char* bytes,
                                                     size_t length,
                                                     int lineno) {
    JS::SourceText<mozilla::Utf8Unit> source;
    if (!source.init(cx, bytes, length, JS::SourceOwnership::Borrowed))
        return false;

    JS::CompileOptions options(cx);
    options.setFileAndLine("typein", lineno);

    JS::RootedValue result(cx);
    if (!JS::Evaluate(cx, options, source, &result)) {
        if (!JS_IsExceptionPending(cx))
            return false;
    }

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    gjs->schedule_gc_if_needed();

    if (result.isUndefined())
        return true;

    char *display_str;
    display_str = gjs_value_debug_string(cx, result);
    if (display_str) {
        g_fprintf(stdout, "%s\n", display_str);
        g_free(display_str);
    }
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_console_interact(JSContext *context,
                     unsigned   argc,
                     JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    bool eof = false;
    JS::RootedObject global(context, gjs_get_import_global(context));
    GString *buffer = NULL;
    char *temp_buf = NULL;
    int lineno;
    int startline;

    JS::SetWarningReporter(context, gjs_console_warning_reporter);

        /* It's an interactive filehandle; drop into read-eval-print loop. */
    lineno = 1;
    do {
        /*
         * Accumulate lines until we get a 'compilable unit' - one that either
         * generates an error (before running out of source) or that compiles
         * cleanly.  This should be whenever we get a complete statement that
         * coincides with the end of a line.
         */
        startline = lineno;
        buffer = g_string_new("");
        do {
            if (!gjs_console_readline(
                    &temp_buf, startline == lineno ? "gjs> " : ".... ")) {
                eof = true;
                break;
            }
            g_string_append(buffer, temp_buf);
            g_free(temp_buf);
            lineno++;
        } while (!JS_Utf8BufferIsCompilableUnit(context, global, buffer->str,
                                                buffer->len));

        bool ok;
        {
            AutoReportException are(context);
            ok = gjs_console_eval_and_print(context, buffer->str, buffer->len,
                                            startline);
        }
        g_string_free(buffer, true);

        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
        ok = gjs->run_jobs_fallible() && ok;

        if (!ok) {
            /* If this was an uncatchable exception, throw another uncatchable
             * exception on up to the surrounding JS::Evaluate() in main(). This
             * happens when you run gjs-console and type imports.system.exit(0);
             * at the prompt. If we don't throw another uncatchable exception
             * here, then it's swallowed and main() won't exit. */
            return false;
        }
    } while (!eof);

    g_fprintf(stdout, "\n");

    argv.rval().setUndefined();
    return true;
}

bool
gjs_define_console_stuff(JSContext              *context,
                         JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunctionById(context, module, atoms.interact(),
                                 gjs_console_interact, 1,
                                 GJS_MODULE_PROP_FLAGS);
}
