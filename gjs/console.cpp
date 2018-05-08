/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <gio/gio.h>

#include <gjs/gjs.h>

static char **include_path = NULL;
static char **coverage_prefixes = NULL;
static char *coverage_output_path = NULL;
static char *profile_output_path = nullptr;
static char *command = NULL;
static gboolean print_version = false;
static gboolean print_js_version = false;
static bool enable_profiler = false;

static gboolean parse_profile_arg(const char *, const char *, void *, GError **);

/* Keep in sync with entries in check_script_args_for_stray_gjs_args() */
static GOptionEntry entries[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &print_version, "Print GJS version and exit" },
    { "jsversion", 0, 0, G_OPTION_ARG_NONE, &print_js_version,
        "Print version of the JS engine and exit" },
    { "command", 'c', 0, G_OPTION_ARG_STRING, &command, "Program passed in as a string", "COMMAND" },
    { "coverage-prefix", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &coverage_prefixes, "Add the prefix PREFIX to the list of files to generate coverage info for", "PREFIX" },
    { "coverage-output", 0, 0, G_OPTION_ARG_STRING, &coverage_output_path, "Write coverage output to a directory DIR. This option is mandatory when using --coverage-path", "DIR", },
    { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, &include_path, "Add the directory DIR to the list of directories to search for js files.", "DIR" },
    { "profile", 0, G_OPTION_FLAG_OPTIONAL_ARG | G_OPTION_FLAG_FILENAME,
        G_OPTION_ARG_CALLBACK, reinterpret_cast<void *>(&parse_profile_arg),
        "Enable the profiler and write output to FILE (default: gjs-$PID.syscap)",
        "FILE" },
    { NULL }
};

static char **
strndupv(int           n,
         char * const *strv)
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

static char **
strcatv(char **strv1,
        char **strv2)
{
    if (strv1 == NULL && strv2 == NULL)
        return NULL;
    if (strv1 == NULL)
        return g_strdupv(strv2);
    if (strv2 == NULL)
        return g_strdupv(strv1);

    unsigned len1 = g_strv_length(strv1);
    unsigned len2 = g_strv_length(strv2);
    char **retval = g_new(char *, len1 + len2 + 1);
    unsigned ix;

    for (ix = 0; ix < len1; ix++)
        retval[ix] = g_strdup(strv1[ix]);
    for (ix = 0; ix < len2; ix++)
        retval[len1 + ix] = g_strdup(strv2[ix]);
    retval[len1 + len2] = NULL;

    return retval;
}

static gboolean
parse_profile_arg(const char *option_name,
                  const char *value,
                  void       *data,
                  GError    **error_out)
{
    enable_profiler = true;
    g_free(profile_output_path);
    if (value)
        profile_output_path = g_strdup(value);
    return true;
}

static gboolean
check_stray_profile_arg(const char *option_name,
                        const char *value,
                        void       *data,
                        GError    **error_out)
{
    g_warning("You used the --profile option after the script on the GJS "
              "command line. Support for this will be removed in a future "
              "version. Place the option before the script or use the "
              "GJS_ENABLE_PROFILER environment variable.");
    return parse_profile_arg(option_name, value, data, error_out);
}

static void
check_script_args_for_stray_gjs_args(int           argc,
                                     char * const *argv)
{
    GError *error = NULL;
    char **new_coverage_prefixes = NULL;
    char *new_coverage_output_path = NULL;
    char **new_include_paths = NULL;
    /* Keep in sync with entries[] at the top */
    static GOptionEntry script_check_entries[] = {
        { "coverage-prefix", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &new_coverage_prefixes },
        { "coverage-output", 0, 0, G_OPTION_ARG_STRING, &new_coverage_output_path },
        { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, &new_include_paths },
        { "profile", 0, G_OPTION_FLAG_OPTIONAL_ARG | G_OPTION_FLAG_FILENAME,
          G_OPTION_ARG_CALLBACK, reinterpret_cast<void *>(&check_stray_profile_arg) },
        { NULL }
    };
    char **argv_copy = g_new(char *, argc + 2);
    int ix;

    argv_copy[0] = g_strdup("dummy"); /* Fake argv[0] for GOptionContext */
    for (ix = 0; ix < argc; ix++)
        argv_copy[ix + 1] = g_strdup(argv[ix]);
    argv_copy[argc + 1] = NULL;

    GOptionContext *script_options = g_option_context_new(NULL);
    g_option_context_set_ignore_unknown_options(script_options, true);
    g_option_context_set_help_enabled(script_options, false);
    g_option_context_add_main_entries(script_options, script_check_entries, NULL);
    if (!g_option_context_parse_strv(script_options, &argv_copy, &error)) {
        g_warning("Scanning script arguments failed: %s", error->message);
        g_error_free(error);
        g_strfreev(argv_copy);
        return;
    }

    if (new_coverage_prefixes != NULL) {
        g_warning("You used the --coverage-prefix option after the script on "
                  "the GJS command line. Support for this will be removed in a "
                  "future version. Place the option before the script or use "
                  "the GJS_COVERAGE_PREFIXES environment variable.");
        char **old_coverage_prefixes = coverage_prefixes;
        coverage_prefixes = strcatv(old_coverage_prefixes, new_coverage_prefixes);
        g_strfreev(old_coverage_prefixes);
    }
    if (new_include_paths != NULL) {
        g_warning("You used the --include-path option after the script on the "
                  "GJS command line. Support for this will be removed in a "
                  "future version. Place the option before the script or use "
                  "the GJS_PATH environment variable.");
        char **old_include_paths = include_path;
        include_path = strcatv(old_include_paths, new_include_paths);
        g_strfreev(old_include_paths);
    }
    if (new_coverage_output_path != NULL) {
        g_warning("You used the --coverage-output option after the script on "
                  "the GJS command line. Support for this will be removed in a "
                  "future version. Place the option before the script or use "
                  "the GJS_COVERAGE_OUTPUT environment variable.");
        g_free(coverage_output_path);
        coverage_output_path = new_coverage_output_path;
    }

    g_option_context_free(script_options);
    g_strfreev(argv_copy);
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
    int code, gjs_argc = argc, script_argc, ix;
    char **argv_copy = g_strdupv(argv), **argv_copy_addr = argv_copy;
    char **gjs_argv, **gjs_argv_addr;
    char * const *script_argv;
    const char *env_coverage_output_path;
    const char *env_coverage_prefixes;
    bool interactive_mode = false;

    setlocale(LC_ALL, "");

    context = g_option_context_new(NULL);

    g_option_context_set_ignore_unknown_options(context, true);
    g_option_context_set_help_enabled(context, false);

    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse_strv(context, &argv_copy, &error))
        g_error("option parsing failed: %s", error->message);

    /* Split options so we pass unknown ones through to the JS script */
    int argc_copy = g_strv_length(argv_copy);
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
    print_js_version = false;
    g_option_context_set_ignore_unknown_options(context, false);
    g_option_context_set_help_enabled(context, true);
    if (!g_option_context_parse_strv(context, &gjs_argv, &error))
        g_error("option parsing failed: %s", error->message);

    g_option_context_free (context);

    if (print_version) {
        g_print("%s\n", PACKAGE_STRING);
        exit(0);
    }

    if (print_js_version) {
        g_print("%s\n", gjs_get_js_version());
        exit(0);
    }

    gjs_argc = g_strv_length(gjs_argv);
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
        interactive_mode = true;
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

    /* This should be removed after a suitable time has passed */
    check_script_args_for_stray_gjs_args(script_argc, script_argv);

    if (interactive_mode && enable_profiler) {
        g_message("Profiler disabled in interactive mode.");
        enable_profiler = false;
        g_unsetenv("GJS_ENABLE_PROFILER");  /* ignore env var in eval() */
    }

    js_context = (GjsContext*) g_object_new(GJS_TYPE_CONTEXT,
                                            "search-path", include_path,
                                            "program-name", program_name,
                                            "profiler-enabled", enable_profiler,
                                            NULL);

    env_coverage_output_path = g_getenv("GJS_COVERAGE_OUTPUT");
    if (env_coverage_output_path != NULL) {
        g_free(coverage_output_path);
        coverage_output_path = g_strdup(env_coverage_output_path);
    }

    env_coverage_prefixes = g_getenv("GJS_COVERAGE_PREFIXES");
    if (env_coverage_prefixes != NULL) {
        if (coverage_prefixes != NULL)
            g_strfreev(coverage_prefixes);
        coverage_prefixes = g_strsplit(env_coverage_prefixes, ":", -1);
    }

    if (coverage_prefixes) {
        if (!coverage_output_path)
            g_error("--coverage-output is required when taking coverage statistics");

        GFile *output = g_file_new_for_commandline_arg(coverage_output_path);
        coverage = gjs_coverage_new(coverage_prefixes, js_context, output);
        g_object_unref(output);
    }

    if (enable_profiler && profile_output_path) {
        GjsProfiler *profiler = gjs_context_get_profiler(js_context);
        gjs_profiler_set_filename(profiler, profile_output_path);
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
        if (!g_error_matches(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT))
            g_printerr("%s\n", error->message);
        g_clear_error(&error);
        goto out;
    }

 out:
    g_strfreev(gjs_argv_addr);

    /* Probably doesn't make sense to write statistics on failure */
    if (coverage && code == 0)
        gjs_coverage_write_statistics(coverage);

    g_free(coverage_output_path);
    g_free(profile_output_path);
    g_strfreev(coverage_prefixes);
    if (coverage)
        g_object_unref(coverage);
    g_object_unref(js_context);
    g_free(script);
    exit(code);
}
