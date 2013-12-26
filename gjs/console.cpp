/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include <gjs/gjs.h>

static char **include_path = NULL;
static char *command = NULL;
static char *js_version= NULL;

static GOptionEntry entries[] = {
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, "Program passed in as a string", "COMMAND" },
    { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, &include_path, "Add the directory DIR to the list of directories to search for js files.", "DIR" },
    { "js-version", 0, 0, G_OPTION_ARG_STRING, &js_version, "JavaScript version (e.g. \"default\", \"1.8\"", "JSVERSION" },
    { NULL }
};

G_GNUC_NORETURN
static void
print_help (GOptionContext *context,
            gboolean        main_help)
{
  gchar *help;

  help = g_option_context_get_help (context, main_help, NULL);
  g_print ("%s", help);
  g_free (help);

  exit (0);
}

int
main(int argc, char **argv)
{
    char *command_line;
    GOptionContext *context;
    GError *error = NULL;
    GjsContext *js_context;
    char *script;
    const char *filename;
    const char *program_name;
    gsize len;
    int code;
    const char *source_js_version;

    context = g_option_context_new(NULL);

    /* pass unknown through to the JS script */
    g_option_context_set_ignore_unknown_options(context, TRUE);
    g_option_context_set_help_enabled(context, FALSE);

    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error))
        g_error("option parsing failed: %s", error->message);

    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
            print_help(context, TRUE);
        else if (strcmp(argv[1], "--help-all") == 0)
            print_help(context, FALSE);
    }

    g_option_context_free (context);

    setlocale(LC_ALL, "");

    command_line = g_strjoinv(" ", argv);
    g_free(command_line);

    if (command != NULL) {
        script = command;
        source_js_version = gjs_context_scan_buffer_for_js_version(script, 1024);
        len = strlen(script);
        filename = "<command line>";
        program_name = argv[0];
    } else if (argc <= 1) {
        source_js_version = NULL;
        script = g_strdup("const Console = imports.console; Console.interact();");
        len = strlen(script);
        filename = "<stdin>";
        program_name = argv[0];
    } else /*if (argc >= 2)*/ {
        error = NULL;
        if (!g_file_get_contents(argv[1], &script, &len, &error)) {
            g_printerr("%s\n", error->message);
            exit(1);
        }
        source_js_version = gjs_context_scan_buffer_for_js_version(script, 1024);
        filename = argv[1];
        program_name = argv[1];
        argc--;
        argv++;
    }

    /* If user explicitly specifies a version, use it */
    if (js_version != NULL)
        source_js_version = js_version;
    if (source_js_version != NULL)
        js_context = (GjsContext*) g_object_new(GJS_TYPE_CONTEXT,
                                  "search-path", include_path,
                                  "js-version", source_js_version,
                                  "program-name", program_name,
                                  NULL);
    else
        js_context = (GjsContext*) g_object_new(GJS_TYPE_CONTEXT,
                                  "search-path", include_path,
                                  "program-name", program_name,
                                  NULL);

    /* prepare command line arguments */
    if (!gjs_context_define_string_array(js_context, "ARGV",
                                         argc - 1, (const char**)argv + 1,
                                         &error)) {
        g_printerr("Failed to defined ARGV: %s", error->message);
        exit(1);
    }

    /* evaluate the script */
    error = NULL;
    if (!gjs_context_eval(js_context, script, len,
                          filename, &code, &error)) {
        g_free(script);
        g_printerr("%s\n", error->message);
        exit(1);
    }
    
    g_object_unref(js_context);

    g_free(script);
    exit(code);
}
