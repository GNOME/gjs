/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#ifdef HAVE_LIBREADLINE
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <gjs/jsapi-private.h>

#include "console.h"

static void
gjs_console_error_reporter(JSContext *cx, const char *message, JSErrorReport *report)
{
    int i, j, k, n;
    char *prefix, *tmp;
    const char *ctmp;

    if (!report) {
        fprintf(stderr, "%s\n", message);
        return;
    }

    prefix = NULL;
    if (report->filename)
        prefix = g_strdup_printf("%s:", report->filename);
    if (report->lineno) {
        tmp = prefix;
        prefix = g_strdup_printf("%s%u: ", tmp ? tmp : "", report->lineno);
        g_free(tmp);
    }
    if (JSREPORT_IS_WARNING(report->flags)) {
        tmp = prefix;
        prefix = g_strdup_printf("%s%swarning: ",
                                 tmp ? tmp : "",
                                 JSREPORT_IS_STRICT(report->flags) ? "strict " : "");
        g_free(tmp);
    }

    /* embedded newlines -- argh! */
    while ((ctmp = strchr(message, '\n')) != NULL) {
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

    if (!report->linebuf) {
        fputc('\n', stderr);
        goto out;
    }

    /* report->linebuf usually ends with a newline. */
    n = strlen(report->linebuf);
    fprintf(stderr, ":\n%s%s%s%s",
            prefix,
            report->linebuf,
            (n > 0 && report->linebuf[n-1] == '\n') ? "" : "\n",
            prefix);
    n = ((char*)report->tokenptr) - ((char*) report->linebuf);
    for (i = j = 0; i < n; i++) {
        if (report->linebuf[i] == '\t') {
            for (k = (j + 8) & ~7; j < k; j++) {
                fputc('.', stderr);
            }
            continue;
        }
        fputc('.', stderr);
        j++;
    }
    fputs("^\n", stderr);
 out:
    g_free(prefix);
}

#ifdef HAVE_LIBREADLINE
static JSBool
gjs_console_readline(JSContext *cx, char **bufp, FILE *file, const char *prompt)
{
    char *line;
    line = readline(prompt);
    if (!line)
        return JS_FALSE;
    if (line[0] != '\0')
        add_history(line);
    *bufp = line;
    return JS_TRUE;
}
#else
static JSBool
gjs_console_readline(JSContext *cx, char **bufp, FILE *file, const char *prompt)
{
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, file))
        return JS_FALSE;
    *bufp = g_strdup(line);
    return JS_TRUE;
}
#endif

JSBool
gjs_console_interact(JSContext *context,
                     unsigned   argc,
                     jsval     *vp)
{
    JS::CallReceiver rec = JS::CallReceiverFromVp(vp);
    JSObject *object = JSVAL_TO_OBJECT(rec.thisv());
    gboolean eof = FALSE;
    jsval result;
    JSString *str;
    GString *buffer = NULL;
    char *temp_buf = NULL;
    int lineno;
    int startline;
    FILE *file = stdin;

    JS_SetErrorReporter(context, gjs_console_error_reporter);

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
                eof = JS_TRUE;
                break;
            }
            g_string_append(buffer, temp_buf);
            g_free(temp_buf);
            lineno++;
        } while (!JS_BufferIsCompilableUnit(context, object, buffer->str, buffer->len));

        JS::CompileOptions options(context);
        options.setUTF8(true)
               .setFileAndLine("typein", startline);
        js::RootedObject rootedObj(context, object);
        JS::Evaluate(context, rootedObj, options, buffer->str, buffer->len,  &result);

        gjs_schedule_gc_if_needed(context);

        if (JS_GetPendingException(context, &result)) {
            str = JS_ValueToString(context, result);
            JS_ClearPendingException(context);
        } else if (JSVAL_IS_VOID(result)) {
            goto next;
        } else {
            str = JS_ValueToString(context, result);
        }

        if (str) {
            char *display_str;
            display_str = gjs_value_debug_string(context, result);
            if (display_str != NULL) {
                g_fprintf(stdout, "%s\n", display_str);
                g_free(display_str);
            }
        }

 next:
        g_string_free(buffer, TRUE);
    } while (!eof);

    g_fprintf(stdout, "\n");

    if (file != stdin)
        fclose(file);

    return JS_TRUE;
}

JSBool
gjs_define_console_stuff(JSContext  *context,
                         JSObject  **module_out)
{
    JSObject *module;

    module = JS_NewObject (context, NULL, NULL, NULL);

    if (!JS_DefineFunction(context, module,
                           "interact",
                           (JSNative) gjs_console_interact,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    *module_out = module;
    return JS_TRUE;
}
