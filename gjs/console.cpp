/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>  // for PACKAGE_STRING

#include <locale.h>  // for setlocale, LC_ALL
#include <stdint.h>
#include <stdlib.h>  // for EXIT_SUCCESS / EXIT_FAILURE
#include <string.h>  // for strcmp, strlen

#ifdef HAVE_UNISTD_H
#    include <unistd.h>  // for close
#elif defined (_WIN32)
#    include <io.h>
#endif

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/auto.h"
#include "gjs/gerror-result.h"
#include "gjs/gjs.h"
#include "util/console.h"

static Gjs::AutoStrv include_path;
static Gjs::AutoStrv coverage_prefixes;
static Gjs::AutoChar coverage_output_path;
static Gjs::AutoChar profile_output_path;
static Gjs::AutoChar command;
static gboolean print_version = false;
static gboolean print_js_version = false;
static gboolean debugging = false;
static gboolean exec_as_module = false;
static bool enable_profiler = false;

static gboolean parse_profile_arg(const char *, const char *, void *, GError **);

using GjsAutoGOptionContext =
    Gjs::AutoPointer<GOptionContext, GOptionContext, g_option_context_free>;

// clang-format off
static GOptionEntry entries[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &print_version, "Print GJS version and exit" },
    { "jsversion", 0, 0, G_OPTION_ARG_NONE, &print_js_version,
        "Print version of the JS engine and exit" },
    { "command", 'c', 0, G_OPTION_ARG_STRING, command.out(), "Program passed in as a string", "COMMAND" },
    { "coverage-prefix", 'C', 0, G_OPTION_ARG_STRING_ARRAY, coverage_prefixes.out(),
      "Add the prefix PREFIX to the list of files to generate coverage info for", "PREFIX" },
    { "coverage-output", 0, 0, G_OPTION_ARG_STRING, coverage_output_path.out(),
      "Write coverage output to a directory DIR. This option is mandatory when using --coverage-prefix", "DIR", },
    { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, include_path.out(),
        "Add DIR to the list of paths to search for JS files", "DIR" },
    { "module", 'm', 0, G_OPTION_ARG_NONE, &exec_as_module,
        "Execute the file as a module" },
    { "profile", 0, G_OPTION_FLAG_OPTIONAL_ARG | G_OPTION_FLAG_FILENAME,
        G_OPTION_ARG_CALLBACK, reinterpret_cast<void *>(&parse_profile_arg),
        "Enable the profiler and write output to FILE (default: gjs-$PID.syscap)",
        "FILE" },
    { "debugger", 'd', 0, G_OPTION_ARG_NONE, &debugging, "Start in debug mode" },
    { NULL }
};
// clang-format on

[[nodiscard]] static Gjs::AutoStrv strndupv(int n, char* const* strv) {
    Gjs::AutoPointer<GStrvBuilder, GStrvBuilder, g_strv_builder_unref> builder{
        g_strv_builder_new()};

    for (int i = 0; i < n; ++i)
        g_strv_builder_add(builder, strv[i]);

    return g_strv_builder_end(builder);
}

[[nodiscard]] static Gjs::AutoStrv strcatv(char** strv1, char** strv2) {
    if (strv1 == NULL && strv2 == NULL)
        return NULL;
    if (strv1 == NULL)
        return g_strdupv(strv2);
    if (strv2 == NULL)
        return g_strdupv(strv1);

    Gjs::AutoPointer<GStrvBuilder, GStrvBuilder, g_strv_builder_unref> builder{
        g_strv_builder_new()};

    g_strv_builder_addv(builder, const_cast<const char**>(strv1));
    g_strv_builder_addv(builder, const_cast<const char**>(strv2));

    return g_strv_builder_end(builder);
}

static gboolean parse_profile_arg(const char* option_name [[maybe_unused]],
                                  const char* value, void*, GError**) {
    enable_profiler = true;
    profile_output_path = Gjs::AutoChar{value, Gjs::TakeOwnership{}};
    return true;
}

static void
check_script_args_for_stray_gjs_args(int           argc,
                                     char * const *argv)
{
    Gjs::AutoError error;
    Gjs::AutoStrv new_coverage_prefixes;
    Gjs::AutoChar new_coverage_output_path;
    Gjs::AutoStrv new_include_paths;
    // Don't add new entries here. This is only for arguments that were
    // previously accepted after the script name on the command line, for
    // backwards compatibility.
    // clang-format off
    GOptionEntry script_check_entries[] = {
        { "coverage-prefix", 'C', 0, G_OPTION_ARG_STRING_ARRAY, new_coverage_prefixes.out() },
        { "coverage-output", 0, 0, G_OPTION_ARG_STRING, new_coverage_output_path.out() },
        { "include-path", 'I', 0, G_OPTION_ARG_STRING_ARRAY, new_include_paths.out() },
        { NULL }
    };
    // clang-format on

    Gjs::AutoStrv argv_copy{g_new(char*, argc + 2)};
    int ix;

    argv_copy[0] = g_strdup("dummy"); /* Fake argv[0] for GOptionContext */
    for (ix = 0; ix < argc; ix++)
        argv_copy[ix + 1] = g_strdup(argv[ix]);
    argv_copy[argc + 1] = NULL;

    GjsAutoGOptionContext script_options = g_option_context_new(NULL);
    g_option_context_set_ignore_unknown_options(script_options, true);
    g_option_context_set_help_enabled(script_options, false);
    g_option_context_add_main_entries(script_options, script_check_entries, NULL);
    if (!g_option_context_parse_strv(script_options, argv_copy.out(), &error)) {
        g_warning("Scanning script arguments failed: %s", error->message);
        return;
    }

    if (new_coverage_prefixes) {
        g_warning("You used the --coverage-prefix option after the script on "
                  "the GJS command line. Support for this will be removed in a "
                  "future version. Place the option before the script or use "
                  "the GJS_COVERAGE_PREFIXES environment variable.");
        coverage_prefixes = strcatv(coverage_prefixes, new_coverage_prefixes);
    }
    if (new_include_paths) {
        g_warning("You used the --include-path option after the script on the "
                  "GJS command line. Support for this will be removed in a "
                  "future version. Place the option before the script or use "
                  "the GJS_PATH environment variable.");
        include_path = strcatv(include_path, new_include_paths);
    }
    if (new_coverage_output_path) {
        g_warning(
            "You used the --coverage-output option after the script on "
            "the GJS command line. Support for this will be removed in a "
            "future version. Place the option before the script or use "
            "the GJS_COVERAGE_OUTPUT environment variable.");
        coverage_output_path = new_coverage_output_path;
    }
}

int define_argv_and_eval_script(GjsContext* js_context, int argc,
                                char* const* argv, const char* script,
                                size_t len, const char* filename) {
    gjs_context_set_argv(js_context, argc, const_cast<const char**>(argv));

    Gjs::AutoError error;
    /* evaluate the script */
    int code = 0;
    if (exec_as_module) {
        Gjs::AutoUnref<GFile> output{g_file_new_for_commandline_arg(filename)};
        Gjs::AutoChar uri{g_file_get_uri(output)};
        if (!gjs_context_register_module(js_context, uri, uri, &error)) {
            g_critical("%s", error->message);
            code = 1;
        }

        uint8_t code_u8 = 0;
        if (!code &&
            !gjs_context_eval_module(js_context, uri, &code_u8, &error)) {
            code = code_u8;

            if (!g_error_matches(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT))
                g_critical("%s", error->message);
        }
    } else if (!gjs_context_eval(js_context, script, len, filename, &code,
                                 &error)) {
        if (!g_error_matches(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT))
            g_critical("%s", error->message);
    }
    return code;
}

int main(int argc, char** argv) {
    Gjs::AutoError error;
    const char *filename;
    const char *program_name;
    gsize len;
    int gjs_argc = argc, script_argc, ix;
    char * const *script_argv;
    const char *env_coverage_output_path;
    bool interactive_mode = false;

    setlocale(LC_ALL, "");

    GjsAutoGOptionContext context = g_option_context_new(NULL);
    g_option_context_set_ignore_unknown_options(context, true);
    g_option_context_set_help_enabled(context, false);

    Gjs::AutoStrv argv_copy_addr{g_strdupv(argv)};
    char** argv_copy = argv_copy_addr;

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
        if (command && (strcmp(argv[ix], "-c") == 0 ||
                        strcmp(argv[ix], "--command") == 0)) {
            gjs_argc = ix + 2;
            break;
        }
    }
    Gjs::AutoStrv gjs_argv_addr{strndupv(gjs_argc, argv)};
    char** gjs_argv = gjs_argv_addr;
    script_argc = argc - gjs_argc;
    script_argv = argv + gjs_argc;

    /* Parse again, only the GJS options this time */
    include_path.release();
    coverage_prefixes.release();
    coverage_output_path.release();
    command.release();
    print_version = false;
    print_js_version = false;
    debugging = false;
    exec_as_module = false;
    g_option_context_set_ignore_unknown_options(context, false);
    g_option_context_set_help_enabled(context, true);
    if (!g_option_context_parse_strv(context, &gjs_argv, &error)) {
        Gjs::AutoChar help_text{
            g_option_context_get_help(context, true, nullptr)};
        g_printerr("%s\n\n%s\n", error->message, help_text.get());
        return EXIT_FAILURE;
    }

    if (print_version) {
        g_print("%s\n", PACKAGE_STRING);
        return EXIT_SUCCESS;
    }

    if (print_js_version) {
        g_print("%s\n", gjs_get_js_version());
        return EXIT_SUCCESS;
    }

    Gjs::AutoChar program_path;
    gjs_argc = g_strv_length(gjs_argv);
    Gjs::AutoChar script;
    if (command) {
        script = command;
        len = strlen(script);
        filename = "<command line>";
        program_name = gjs_argv[0];
    } else if (gjs_argc == 1) {
        if (exec_as_module) {
            g_warning(
                "'-m' requires a file argument.\nExample: gjs -m main.js");
            return EXIT_FAILURE;
        }

        script = g_strdup("const Console = imports.console; Console.interact();");
        len = strlen(script);
        filename = "<stdin>";
        program_name = gjs_argv[0];
        interactive_mode = true;
    } else {
        /* All unprocessed options should be in script_argv */
        g_assert(gjs_argc == 2);
        Gjs::AutoUnref<GFile> input{
            g_file_new_for_commandline_arg(gjs_argv[1])};
        if (!g_file_load_contents(input, nullptr, script.out(), &len, nullptr,
                                  &error)) {
            g_printerr("%s\n", error->message);
            return EXIT_FAILURE;
        }
        program_path = g_file_get_path(input);
        filename = gjs_argv[1];
        program_name = gjs_argv[1];
    }

    /* This should be removed after a suitable time has passed */
    check_script_args_for_stray_gjs_args(script_argc, script_argv);

    /* Check for GJS_TRACE_FD for sysprof profiling */
    const char* env_tracefd = g_getenv("GJS_TRACE_FD");
    int tracefd = -1;
    if (env_tracefd) {
        tracefd = g_ascii_strtoll(env_tracefd, nullptr, 10);
        g_setenv("GJS_TRACE_FD", "", true);
        if (tracefd > 0)
            enable_profiler = true;
    }

    if (interactive_mode && enable_profiler) {
        g_message("Profiler disabled in interactive mode.");
        enable_profiler = false;
        g_unsetenv("GJS_ENABLE_PROFILER");  /* ignore env var in eval() */
        g_unsetenv("GJS_TRACE_FD");         /* ignore env var in eval() */
    }

    const char* env_coverage_prefixes = g_getenv("GJS_COVERAGE_PREFIXES");
    if (env_coverage_prefixes)
        coverage_prefixes = g_strsplit(env_coverage_prefixes, ":", -1);

    if (coverage_prefixes)
        gjs_coverage_enable();

#ifdef HAVE_READLINE_READLINE_H
    Gjs::AutoChar repl_history_path = gjs_console_get_repl_history_path();
#else
    Gjs::AutoChar repl_history_path = nullptr;
#endif

    Gjs::AutoUnref<GjsContext> js_context{GJS_CONTEXT(g_object_new(
        GJS_TYPE_CONTEXT,
        // clang-format off
        "search-path", include_path.get(),
        "program-name", program_name,
        "program-path", program_path.get(),
        "profiler-enabled", enable_profiler,
        "exec-as-module", exec_as_module,
        "repl-history-path", repl_history_path.get(),
        // clang-format on
        nullptr))};

    env_coverage_output_path = g_getenv("GJS_COVERAGE_OUTPUT");
    if (env_coverage_output_path != NULL) {
        g_free(coverage_output_path);
        coverage_output_path = g_strdup(env_coverage_output_path);
    }

    Gjs::AutoUnref<GjsCoverage> coverage;
    if (coverage_prefixes) {
        if (!coverage_output_path)
            g_error("--coverage-output is required when taking coverage statistics");

        Gjs::AutoUnref<GFile> output{
            g_file_new_for_commandline_arg(coverage_output_path)};
        coverage = gjs_coverage_new(coverage_prefixes, js_context, output);
    }

    if (enable_profiler && profile_output_path) {
        GjsProfiler *profiler = gjs_context_get_profiler(js_context);
        gjs_profiler_set_filename(profiler, profile_output_path);
    } else if (enable_profiler && tracefd > -1) {
        GjsProfiler* profiler = gjs_context_get_profiler(js_context);
        gjs_profiler_set_fd(profiler, tracefd);
        tracefd = -1;
    }

    if (tracefd != -1) {
        close(tracefd);
        tracefd = -1;
    }

    /* If we're debugging, set up the debugger. It will break on the first
     * frame. */
    if (debugging)
        gjs_context_setup_debugger_console(js_context);

    int code = define_argv_and_eval_script(js_context, script_argc, script_argv,
                                           script, len, filename);

    /* Probably doesn't make sense to write statistics on failure */
    if (coverage && code == 0)
        gjs_coverage_write_statistics(coverage);

    if (debugging)
        g_print("Program exited with code %d\n", code);

    return code;
}
