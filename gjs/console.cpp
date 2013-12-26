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
static char **coverage_paths = NULL;
static char *coverage_output_file = NULL;
static char *command = NULL;
static char *js_version= NULL;

static GOptionEntry entries[] = {
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, "Program passed in as a string", "COMMAND" },
    { "coverage-path", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &coverage_paths, "Add the directory DIR to the list of directories to generate coverage info for", "DIR" },
    { "coverage-output", 0, 0, G_OPTION_ARG_STRING, &coverage_output_file, "Write coverage output to a single FILE", "FILE", },
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

static GValue *
init_array_parameter(GArray      *array,
                     guint       index,
                     const gchar *name,
                     GType       type)
{
    g_print ("index %i array len %i\n", index, array->len);
    if (index >= array->len)
    {
        g_print ("expanding array size to %i\n", index + 1);
        g_array_set_size(array, array->len + 1);
    }

    GParameter *param = &(g_array_index(array, GParameter, index));
    param->name = name;
    param->value.g_type = 0;
    g_value_init(&param->value, type);
    return &param->value;
}

static void
clear_array_parameter_value(gpointer value)
{
    GParameter *parameter = (GParameter *) value;
    g_value_unset(&parameter->value);
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

    /* There are a few properties that we might want to
     * set on construction here that could be optional.
     * So we need to dynamically generate a list of
     * construction properties. */
    GArray *parameters = g_array_sized_new (FALSE, TRUE, sizeof (GParameter), 2);
    guint  n_param = 0;
    g_value_set_boxed(init_array_parameter(parameters, n_param++, "search-path", G_TYPE_STRV),
                      include_path);
    g_value_set_string(init_array_parameter(parameters, n_param++, "program-name", G_TYPE_STRING),
                       program_name);

    if (source_js_version != NULL)
        g_value_set_string(init_array_parameter(parameters, n_param++, "js-version", G_TYPE_STRING),
                           source_js_version);

    if (coverage_paths)
        g_value_set_boxed(init_array_parameter(parameters, n_param++, "coverage-paths", G_TYPE_STRV),
                          coverage_paths);

    if (coverage_output_file)
        g_value_set_string(init_array_parameter(parameters, n_param++, "coverage-output", G_TYPE_STRING),
                           coverage_output_file);

    g_array_set_clear_func(parameters, clear_array_parameter_value);

    js_context =
        GJS_CONTEXT(g_object_newv(GJS_TYPE_CONTEXT,
                                  n_param,
                                  (GParameter *) parameters->data));

    g_array_unref(parameters);

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
