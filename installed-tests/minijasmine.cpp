/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Philip Chimento

#include <locale.h>  // for setlocale, LC_ALL
#include <stdint.h>
#include <stdlib.h>  // for exit
#include <string.h>

#include <gio/gio.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <gjs/gjs.h>

[[noreturn]] static void bail_out(GjsContext* gjs_context, const char* msg) {
    g_object_unref(gjs_context);
    g_print("Bail out! %s\n", msg);
    exit(1);
}

int
main(int argc, char **argv)
{
    if (argc < 2)
        g_error("Need a test file");

    /* The fact that this isn't the default is kind of lame... */
    g_setenv("GJS_DEBUG_OUTPUT", "stderr", false);

    setlocale(LC_ALL, "");

    if (g_getenv("GJS_USE_UNINSTALLED_FILES") != NULL) {
        g_irepository_prepend_search_path(g_getenv("TOP_BUILDDIR"));
    } else {
        g_irepository_prepend_search_path(INSTTESTDIR);
        g_irepository_prepend_library_path(INSTTESTDIR);
    }

    const char *coverage_prefix = g_getenv("GJS_UNIT_COVERAGE_PREFIX");
    const char *coverage_output_path = g_getenv("GJS_UNIT_COVERAGE_OUTPUT");
    const char *search_path[] = { "resource:///org/gjs/jsunit", NULL };

    if (coverage_prefix)
        gjs_coverage_enable();

    GjsContext *cx = gjs_context_new_with_search_path((char **)search_path);
    GjsCoverage *coverage = NULL;

    if (coverage_prefix) {
        const char *coverage_prefixes[2] = { coverage_prefix, NULL };

        if (!coverage_output_path) {
            bail_out(cx, "GJS_UNIT_COVERAGE_OUTPUT is required when using GJS_UNIT_COVERAGE_PREFIX");
        }

        GFile *output = g_file_new_for_commandline_arg(coverage_output_path);
        coverage = gjs_coverage_new(coverage_prefixes, cx, output);
        g_object_unref(output);
    }

    GError *error = NULL;
    bool success;
    int code;

    int exitcode_ignored;
    if (!gjs_context_eval(cx, "imports.minijasmine;", -1, "<jasmine>",
                          &exitcode_ignored, &error))
        bail_out(cx, error->message);

    bool eval_as_module = argc >= 3 && strcmp(argv[2], "-m") == 0;
    if (eval_as_module) {
        uint8_t u8_exitcode_ignored;
        success = gjs_context_eval_module_file(cx, argv[1],
                                               &u8_exitcode_ignored, &error);
    } else {
        success = gjs_context_eval_file(cx, argv[1], &exitcode_ignored, &error);
    }
    if (!success)
        bail_out(cx, error->message);

    /* jasmineEnv.execute() queues up all the tests and runs them
     * asynchronously. This should start after the main loop starts, otherwise
     * we will hit the main loop only after several tests have already run. For
     * consistency we should guarantee that there is a main loop running during
     * all tests. */
    const char *start_suite_script =
        "const GLib = imports.gi.GLib;\n"
        "GLib.idle_add(GLib.PRIORITY_DEFAULT, function () {\n"
        "    try {\n"
        "        window._jasmineEnv.execute();\n"
        "    } catch (e) {\n"
        "        print('Bail out! Exception occurred inside Jasmine:', e);\n"
        "        window._jasmineRetval = 1;\n"
        "        window._jasmineMain.quit();\n"
        "    }\n"
        "    return GLib.SOURCE_REMOVE;\n"
        "});\n"
        "window._jasmineMain.run();\n"
        "window._jasmineRetval;";
    success = gjs_context_eval(cx, start_suite_script, -1, "<jasmine-start>",
                               &code, &error);
    if (!success)
        bail_out(cx, error->message);

    if (code != 0) {
        success = gjs_context_eval(cx, R"js(
            printerr(globalThis._jasmineErrorsOutput.join('\n'));
        )js",
                                   -1, "<jasmine-error-logs>", &code, &error);

        if (!success)
            bail_out(cx, error->message);

        if (code != 0)
            g_print("# Test script failed; see test log for assertions\n");
    }

    if (coverage) {
        gjs_coverage_write_statistics(coverage);
        g_clear_object(&coverage);
    }

    gjs_memory_report("before destroying context", false);
    g_object_unref(cx);
    gjs_memory_report("after destroying context", true);

    /* For TAP, should actually be return 0; as a nonzero return code would
     * indicate an error in the test harness. But that would be quite silly
     * when running the tests outside of the TAP driver. */
    return code;
}
