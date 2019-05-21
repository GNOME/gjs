/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set ts=8 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <config.h>  // for HAVE_READLINE_READLINE_H

#include <stdio.h>   // for fputc, fputs, stderr, size_t, fflush
#include <string.h>  // for strchr

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf

#include "gjs/jsapi-wrapper.h"
#include "js/CompilationAndEvaluation.h"
#include "js/SourceText.h"
#include "js/Warnings.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "modules/console.h"

enum class PrintErrorKind { Error, Warning, StrictWarning, Note };

template <typename T>
static void print_single_error(T* report, PrintErrorKind kind);
static void print_error_line(const char* prefix, JSErrorReport* report);
static void print_error_line(const char*, JSErrorNotes::Note*) {}

static void
gjs_console_print_error(JSErrorReport *report)
{
    // Code modified from SpiderMonkey js/src/vm/JSContext.cpp, js::PrintError()

    g_assert(report);

    PrintErrorKind kind = PrintErrorKind::Error;
    if (JSREPORT_IS_WARNING(report->flags)) {
        if (JSREPORT_IS_STRICT(report->flags))
            kind = PrintErrorKind::StrictWarning;
        else
            kind = PrintErrorKind::Warning;
    }
    print_single_error(report, kind);

    if (report->notes) {
        for (auto&& note : *report->notes)
            print_single_error(note.get(), PrintErrorKind::Note);
    }

    return;
}

template <typename T>
static void print_single_error(T* report, PrintErrorKind kind) {
    JS::UniqueChars prefix;
    if (report->filename)
        prefix.reset(g_strdup_printf("%s:", report->filename));

    if (report->lineno) {
        prefix.reset(g_strdup_printf("%s%u:%u ", prefix ? prefix.get() : "",
                                     report->lineno, report->column));
    }

    if (kind != PrintErrorKind::Error) {
        const char* kindPrefix = nullptr;
        switch (kind) {
            case PrintErrorKind::Warning:
                kindPrefix = "warning";
                break;
            case PrintErrorKind::StrictWarning:
                kindPrefix = "strict warning";
                break;
            case PrintErrorKind::Note:
                kindPrefix = "note";
                break;
            case PrintErrorKind::Error:
            default:
                g_assert_not_reached();
        }

        prefix.reset(
            g_strdup_printf("%s%s: ", prefix ? prefix.get() : "", kindPrefix));
    }

    const char* message = report->message().c_str();

    /* embedded newlines -- argh! */
    const char *ctmp;
    while ((ctmp = strchr(message, '\n')) != 0) {
        ctmp++;
        if (prefix)
            fputs(prefix.get(), stderr);
        mozilla::Unused << fwrite(message, 1, ctmp - message, stderr);
        message = ctmp;
    }

    /* If there were no filename or lineno, the prefix might be empty */
    if (prefix)
        fputs(prefix.get(), stderr);
    fputs(message, stderr);

    print_error_line(prefix.get(), report);
    fputc('\n', stderr);

    fflush(stderr);
}

static void print_error_line(const char* prefix, JSErrorReport* report) {
    if (const char16_t* linebuf = report->linebuf()) {
        size_t n = report->linebufLength();

        fputs(":\n", stderr);
        if (prefix)
            fputs(prefix, stderr);

        for (size_t i = 0; i < n; i++)
            fputc(static_cast<char>(linebuf[i]), stderr);

        // linebuf usually ends with a newline. If not, add one here.
        if (n == 0 || linebuf[n - 1] != '\n')
            fputc('\n', stderr);

        if (prefix)
            fputs(prefix, stderr);

        n = report->tokenOffset();
        for (size_t i = 0, j = 0; i < n; i++) {
            if (linebuf[i] == '\t') {
                for (size_t k = (j + 8) & ~7; j < k; j++)
                    fputc('.', stderr);
                continue;
            }
            fputc('.', stderr);
            j++;
        }
        fputc('^', stderr);
    }
}

static void gjs_console_warning_reporter(JSContext*, JSErrorReport* report) {
    gjs_console_print_error(report);
}

/* Based on js::shell::AutoReportException from SpiderMonkey. */
class AutoReportException {
    JSContext *m_cx;

    JSErrorReport* error_from_exception_value(JS::HandleValue v_exn) const {
        if (!v_exn.isObject())
            return nullptr;
        JS::RootedObject exn(m_cx, &v_exn.toObject());
        return JS_ErrorFromException(m_cx, exn);
    }

    JSObject* stack_from_exception_value(JS::HandleValue v_exn) const {
        if (!v_exn.isObject())
            return nullptr;
        JS::RootedObject exn(m_cx, &v_exn.toObject());
        return ExceptionStackOrNull(exn);
    }

public:
    explicit AutoReportException(JSContext *cx) : m_cx(cx) {}

    ~AutoReportException() {
        if (!JS_IsExceptionPending(m_cx))
            return;

        /* Get exception object before printing and clearing exception. */
        JS::RootedValue v_exn(m_cx);
        (void) JS_GetPendingException(m_cx, &v_exn);

        JSErrorReport* report = error_from_exception_value(v_exn);
        if (report) {
            g_assert(!JSREPORT_IS_WARNING(report->flags));
            gjs_console_print_error(report);
        } else {
            GjsAutoChar display_str = gjs_value_debug_string(m_cx, v_exn);
            g_printerr("error: %s\n", display_str.get());
            return;
        }

        JS::RootedObject stack(m_cx, stack_from_exception_value(v_exn));
        if (stack) {
            GjsAutoChar stack_str = gjs_format_stack_trace(m_cx, stack);
            if (!stack_str)
                g_printerr("(Unable to print stack trace)\n");
            else
                g_printerr("%s", stack_str.get());
        }

        JS_ClearPendingException(m_cx);
    }
};

#ifdef HAVE_READLINE_READLINE_H
GJS_USE
static bool gjs_console_readline(char** bufp, const char* prompt) {
    char *line;
    line = readline(prompt);
    if (!line)
        return false;
    if (line[0] != '\0')
        add_history(line);
    *bufp = line;
    return true;
}
#else
GJS_USE
static bool gjs_console_readline(char** bufp, const char* prompt) {
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return false;
    *bufp = g_strdup(line);
    return true;
}
#endif

/* Return value of false indicates an uncatchable exception, rather than any
 * exception. (This is because the exception should be auto-printed around the
 * invocation of this function.)
 */
GJS_USE
static bool
gjs_console_eval_and_print(JSContext  *cx,
                           const char *bytes,
                           size_t      length,
                           int         lineno)
{
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
