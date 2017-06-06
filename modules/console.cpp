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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_READLINE_READLINE_H
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>

#include "console.h"
#include "gjs/context.h"
#include "gjs/jsapi-private.h"
#include "gjs/jsapi-wrapper.h"

static void
gjs_console_print_error(const char *message, JSErrorReport *report)
{
    /* Code modified from SpiderMonkey js/src/jscntxt.cpp, js::PrintError() */

    if (!report) {
        fprintf(stderr, "%s\n", message);
        fflush(stderr);
        return;
    }

    char *prefix = nullptr;
    if (report->filename)
        prefix = g_strdup_printf("%s:", report->filename);
    if (report->lineno) {
        char *tmp = prefix;
        prefix = g_strdup_printf("%s%u:%u ", tmp ? tmp : "", report->lineno,
                                 report->column);
        g_free(tmp);
    }
    if (JSREPORT_IS_WARNING(report->flags)) {
        char *tmp = prefix;
        prefix = g_strdup_printf("%s%swarning: ",
                                 tmp ? tmp : "",
                                 JSREPORT_IS_STRICT(report->flags) ? "strict " : "");
        g_free(tmp);
    }

    /* embedded newlines -- argh! */
    const char *ctmp;
    while ((ctmp = strchr(message, '\n')) != 0) {
        ctmp++;
        if (prefix)
            fputs(prefix, stderr);
        fwrite(message, 1, ctmp - message, stderr);
        message = ctmp;
    }

    /* If there were no filename or lineno, the prefix might be empty */
    if (prefix)
        fputs(prefix, stderr);
    fputs(message, stderr);

    if (report->linebuf) {
        /* report->linebuf usually ends with a newline. */
        int n = strlen(report->linebuf);
        fprintf(stderr, ":\n%s%s%s%s",
                prefix,
                report->linebuf,
                (n > 0 && report->linebuf[n-1] == '\n') ? "" : "\n",
                prefix);
        n = report->tokenptr - report->linebuf;
        for (int i = 0, j = 0; i < n; i++) {
            if (report->linebuf[i] == '\t') {
                for (int k = (j + 8) & ~7; j < k; j++) {
                    fputc('.', stderr);
                }
                continue;
            }
            fputc('.', stderr);
            j++;
        }
        fputc('^', stderr);
    }
    fputc('\n', stderr);
    fflush(stderr);
    g_free(prefix);
}

static void
gjs_console_error_reporter(JSContext *cx, const char *message, JSErrorReport *report)
{
    gjs_console_print_error(message, report);
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
        JS::RootedValue v_exn(m_cx);
        (void) JS_GetPendingException(m_cx, &v_exn);

        JS::RootedObject exn(m_cx, &v_exn.toObject());
        JSErrorReport *report = JS_ErrorFromException(m_cx, exn);
        if (!report)
            g_error("Out of memory initializing ErrorReport");

        g_assert(!JSREPORT_IS_WARNING(report->flags));

        JS_ReportPendingException(m_cx);
    }
};

#ifdef HAVE_READLINE_READLINE_H
static bool
gjs_console_readline(JSContext *cx, char **bufp, FILE *file, const char *prompt)
{
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
static bool
gjs_console_readline(JSContext *cx, char **bufp, FILE *file, const char *prompt)
{
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, file))
        return false;
    *bufp = g_strdup(line);
    return true;
}
#endif

/* Return value of false indicates an uncatchable exception, rather than any
 * exception. (This is because the exception should be auto-printed around the
 * invocation of this function.)
 */
static bool
gjs_console_eval_and_print(JSContext       *cx,
                           JS::HandleObject global,
                           const char      *bytes,
                           size_t           length,
                           int              lineno)
{
    JS::CompileOptions options(cx);
    options.setUTF8(true)
           .setFileAndLine("typein", lineno);

    JS::RootedValue result(cx);
    if (!JS::Evaluate(cx, global, options, bytes, length, &result)) {
        if (!JS_IsExceptionPending(cx))
            return false;
    }

    gjs_schedule_gc_if_needed(cx);

    if (result.isUndefined())
        return true;

    JS::RootedString str(cx, JS::ToString(cx, result));
    if (!str)
        return true;

    char *display_str;
    display_str = gjs_value_debug_string(cx, result);
    if (display_str) {
        g_fprintf(stdout, "%s\n", display_str);
        g_free(display_str);
    }
    return true;
}

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
    FILE *file = stdin;

    JS_SetErrorReporter(JS_GetRuntime(context), gjs_console_error_reporter);

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
            if (!gjs_console_readline(context, &temp_buf, file,
                                      startline == lineno ? "gjs> " : ".... ")) {
                eof = true;
                break;
            }
            g_string_append(buffer, temp_buf);
            g_free(temp_buf);
            lineno++;
        } while (!JS_BufferIsCompilableUnit(context, global,
                                            buffer->str, buffer->len));

        AutoReportException are(context);
        if (!gjs_console_eval_and_print(context, global, buffer->str, buffer->len,
                                        startline)) {
            /* If this was an uncatchable exception, throw another uncatchable
             * exception on up to the surrounding JS::Evaluate() in main(). This
             * happens when you run gjs-console and type imports.system.exit(0);
             * at the prompt. If we don't throw another uncatchable exception
             * here, then it's swallowed and main() won't exit. */
            g_string_free(buffer, true);
            return false;
        }

        g_string_free(buffer, true);
    } while (!eof);

    g_fprintf(stdout, "\n");

    if (file != stdin)
        fclose(file);

    argv.rval().setUndefined();
    return true;
}

bool
gjs_define_console_stuff(JSContext              *context,
                         JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));
    return JS_DefineFunction(context, module, "interact", gjs_console_interact,
                             1, GJS_MODULE_PROP_FLAGS);
}
