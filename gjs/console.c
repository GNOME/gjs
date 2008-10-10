/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  LiTL, LLC
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
#include <stdlib.h>

#include <util/log.h>
#include <gjs/context.h>
#include <gjs/mem.h>

static char **include_path = NULL;

static GOptionEntry entries[] = {
    { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, &include_path, "Add the directory DIR to the list of directories to search for js files.", "DIR" },
    { NULL }
};

int
main(int argc, char **argv)
{
    GOptionContext *context;
    GError *error = NULL;
    GjsContext *js_context;
    char *script;
    gsize len;
    int code;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);
    if (!g_option_context_parse(context, &argc, &argv, &error))
        g_error("option parsing failed: %s", error->message);

    if (argc < 2) {
        /* FIXME add interpretation of stdin, and REPL */
        g_printerr("Specify a script to run on the command line\n");
        exit(1);
    }

    g_type_init();

    error = NULL;
    if (!g_file_get_contents(argv[1], &script, &len, &error)) {
        g_printerr("%s\n", error->message);
        exit(1);
    }

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Creating new context to eval console script");
    js_context = gjs_context_new_with_search_path(include_path);

    /* prepare command line arguments */
    if (!gjs_context_define_string_array(js_context, "ARGV",
                                         argc - 2, (const char**)argv + 2,
                                         &error)) {
        g_printerr("Failed to defined ARGV: %s", error->message);
        exit(1);
    }

    /* evaluate the script */
    error = NULL;
    if (!gjs_context_eval(js_context, script, len,
                          argv[1], &code, &error)) {
        g_printerr("%s\n", error->message);
        exit(1);
    }

    gjs_memory_report("before destroying context", FALSE);
    g_object_unref(js_context);
    gjs_memory_report("after destroying context", TRUE);

    exit(code);
}
