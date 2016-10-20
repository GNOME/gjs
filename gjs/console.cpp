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

#include "coverage.h"

static char **include_path = NULL;
static char **coverage_prefixes = NULL;
static char *coverage_output_path = NULL;
static char *command = NULL;
static gboolean print_version = false;

static const char *GJS_COVERAGE_CACHE_FILE_NAME = ".internal-gjs-coverage-cache";

static GOptionEntry entries[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &print_version, "Print GJS version and exit" },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, "Program passed in as a string", "COMMAND" },
    { "coverage-prefix", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &coverage_prefixes, "Add the prefix PREFIX to the list of files to generate coverage info for", "PREFIX" },
    { "coverage-output", 0, 0, G_OPTION_ARG_STRING, &coverage_output_path, "Write coverage output to a directory DIR. This option is mandatory when using --coverage-path", "DIR", },
    { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, &include_path, "Add the directory DIR to the list of directories to search for js files.", "DIR" },
    { NULL }
};

static char **
strndupv(int n, char **strv)
{
    int ix;
    if (n == 0)
        return NULL;
    char **retval = g_new(char *, n + 1);
    for (ix = 0; ix < n; ix++)
        retval[ix] = g_strdup(strv[ix]);
    retval[n] = NULL;
    return retval;
}

int
main(int argc, char **argv)
{
    GOptionContext *context;
    GError *error = NULL;
    GjsContext *js_context;
    GjsCoverage *coverage = NULL;
    char *script;
    const char *filename;
    const char *program_name;
    gsize len;
    int code, argc_copy = argc, gjs_argc = argc, script_argc, ix;
    char **argv_copy = g_strdupv(argv), **argv_copy_addr = argv_copy;
    char **gjs_argv, **gjs_argv_addr;
    char * const *script_argv;

    setlocale(LC_ALL, "");

    context = g_option_context_new(NULL);

    g_option_context_set_ignore_unknown_options(context, true);
    g_option_context_set_help_enabled(context, false);

    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc_copy, &argv_copy, &error))
        g_error("option parsing failed: %s", error->message);

    /* Split options so we pass unknown ones through to the JS script */
    for (ix = 1; ix < argc; ix++) {
        /* Check if a file was given and split after it */
        if (argc_copy >= 2 && strcmp(argv[ix], argv_copy[1]) == 0) {
            /* Filename given; split after this argument */
            gjs_argc = ix + 1;
            break;
        }

        /* Check if -c or --command was given and split after following arg */
        if (command != NULL &&
            (strcmp(argv[ix], "-c") == 0 || strcmp(argv[ix], "--command") == 0)) {
            gjs_argc = ix + 2;
            break;
        }
    }
    gjs_argv_addr = gjs_argv = strndupv(gjs_argc, argv);
    script_argc = argc - gjs_argc;
    script_argv = argv + gjs_argc;
    g_strfreev(argv_copy_addr);

    /* Parse again, only the GJS options this time */
    include_path = NULL;
    coverage_prefixes = NULL;
    coverage_output_path = NULL;
    command = NULL;
    print_version = false;
    g_option_context_set_ignore_unknown_options(context, false);
    g_option_context_set_help_enabled(context, true);
    if (!g_option_context_parse(context, &gjs_argc, &gjs_argv, &error))
        g_error("option parsing failed: %s", error->message);

    g_option_context_free (context);

    if (print_version) {
        g_print("%s\n", PACKAGE_STRING);
        exit(0);
    }

    if (command != NULL) {
        script = command;
        len = strlen(script);
        filename = "<command line>";
        program_name = gjs_argv[0];
    } else if (gjs_argc == 1) {
        script = g_strdup("const Console = imports.console; Console.interact();");
        len = strlen(script);
        filename = "<stdin>";
        program_name = gjs_argv[0];
    } else {
        /* All unprocessed options should be in script_argv */
        g_assert(gjs_argc == 2);
        error = NULL;
        if (!g_file_get_contents(gjs_argv[1], &script, &len, &error)) {
            g_printerr("%s\n", error->message);
            exit(1);
        }
        filename = gjs_argv[1];
        program_name = gjs_argv[1];
    }

    js_context = (GjsContext*) g_object_new(GJS_TYPE_CONTEXT,
                                            "search-path", include_path,
                                            "program-name", program_name,
                                            NULL);

    if (coverage_prefixes) {
        if (!coverage_output_path)
            g_error("--coverage-output is required when taking coverage statistics");

        char *path_to_cache_file = g_build_filename(coverage_output_path,
                                                    GJS_COVERAGE_CACHE_FILE_NAME,
                                                    NULL);
        coverage = gjs_coverage_new_from_cache((const gchar **) coverage_prefixes,
                                               js_context,
                                               path_to_cache_file);
        g_free(path_to_cache_file);
    }

    /* prepare command line arguments */
    if (!gjs_context_define_string_array(js_context, "ARGV",
                                         script_argc, (const char **) script_argv,
                                         &error)) {
        code = 1;
        g_printerr("Failed to defined ARGV: %s", error->message);
        g_clear_error(&error);
        goto out;
    }

    /* evaluate the script */
    if (!gjs_context_eval(js_context, script, len,
                          filename, &code, &error)) {
        code = 1;
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        goto out;
    }

 out:
    g_strfreev(gjs_argv_addr);

    /* Probably doesn't make sense to write statistics on failure */
    if (coverage && code == 0)
        gjs_coverage_write_statistics(coverage,
                                      coverage_output_path);
 
    g_object_unref(js_context);
    g_free(script);
    exit(code);
}
