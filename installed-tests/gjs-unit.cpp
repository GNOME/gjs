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

#include <glib.h>
#include <glib/gstdio.h>
#include <girepository.h>
#include <locale.h>

#include <string.h>

#include <gio/gio.h>

#include <gjs/gjs.h>

#include "gjs/coverage.h"
#include "gjs/mem.h"

static const char *skip;

typedef struct {
    const char *coverage_prefix;
    const char *coverage_output_path;
    char       *filename;
    char       *test_name;
} GjsTestData;

static GjsTestData *
gjs_unit_test_data_new(const char *coverage_prefix,
                       const char *coverage_output_path,
                       char       *filename,
                       char       *test_name)
{
    GjsTestData *data = (GjsTestData *) g_new0(GjsTestData, 1);
    data->coverage_prefix = coverage_prefix;
    data->coverage_output_path = coverage_output_path;
    data->filename = filename;
    data->test_name = test_name;
    return data;
}

static void
gjs_unit_test_data_free(gpointer test_data, gpointer user_data)
{
    GjsTestData *data = (GjsTestData *) test_data;
    g_free(data->filename);
    g_free(data->test_name);
    g_free(data);
}

typedef struct {
    GjsContext  *context;
    GjsCoverage *coverage;
} GjsTestJSFixture;

static void
setup(GjsTestJSFixture *fix,
      gconstpointer     test_data)
{
    GjsTestData *data = (GjsTestData *) test_data;
    fix->context = gjs_context_new ();

    if (data->coverage_prefix) {
        const char *coverage_prefixes[2] = { data->coverage_prefix, NULL };

        if (!data->coverage_output_path) {
            g_error("GJS_UNIT_COVERAGE_OUTPUT is required when using GJS_UNIT_COVERAGE_PREFIX");
        }

        GFile *output = g_file_new_for_commandline_arg(data->coverage_output_path);
        fix->coverage = gjs_coverage_new(coverage_prefixes, fix->context, output);
        g_object_unref(output);
    }
}

static void
teardown(GjsTestJSFixture *fix,
         gconstpointer     unused)
{
    if (fix->coverage) {
        gjs_coverage_write_statistics(fix->coverage);

        g_clear_object(&fix->coverage);
    }

    gjs_memory_report("before destroying context", false);
    g_object_unref(fix->context);
    gjs_memory_report("after destroying context", true);
}

static void
test(GjsTestJSFixture *fix,
     gconstpointer     test_data)
{
    GError *error = NULL;
    bool success;
    int code;

    GjsTestData *data = (GjsTestData *) test_data;

    if (skip && strstr(skip, data->test_name)) {
        g_test_message("Skipping %s", data->test_name);
        g_test_skip("due to GJS_TEST_SKIP environment variable");
        return;
    }

    success = gjs_context_eval_file(fix->context, data->filename, &code, &error);
    if (!success)
        g_error("%s", error->message);
    g_assert(error == NULL);
    if (code != 0)
        g_error("Test script returned code %d; assertions will be in gjs.log", code);
}

static GSList *
read_all_dir_sorted (const char *dirpath)
{
    GSList *result = NULL;
    GDir *dir;
    const char *name;

    dir = g_dir_open(dirpath, 0, NULL);
    g_assert(dir != NULL);

    while ((name = g_dir_read_name(dir)) != NULL)
        result = g_slist_prepend (result, g_strdup (name));
    result = g_slist_sort(result, (GCompareFunc) strcmp);

    g_dir_close(dir);
    return result;
}

int
main(int argc, char **argv)
{
    char *js_test_dir;
    GSList *all_tests, *iter;
    GSList *all_registered_test_data = NULL;
    gpointer context_class;
    int retval;

    /* The tests are known to fail in the presence of the JIT;
     * we leak objects.
     * https://bugzilla.gnome.org/show_bug.cgi?id=616193
     */
    g_setenv("GJS_DISABLE_JIT", "1", false);
    /* The fact that this isn't the default is kind of lame... */
    g_setenv("GJS_DEBUG_OUTPUT", "stderr", false);

    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);

    /* Make sure to create the GjsContext class first, so we
     * can override the GjsPrivate lookup path.
     */
    context_class = g_type_class_ref (gjs_context_get_type ());

    if (g_getenv ("GJS_USE_UNINSTALLED_FILES") != NULL) {
        g_irepository_prepend_search_path(g_getenv ("TOP_BUILDDIR"));
        js_test_dir = g_build_filename(g_getenv ("TOP_SRCDIR"), "installed-tests", "js", NULL);
    } else {
        g_irepository_prepend_search_path(INSTTESTDIR);
        js_test_dir = g_build_filename(INSTTESTDIR, "js", NULL);
    }

    const char *coverage_prefix = g_getenv("GJS_UNIT_COVERAGE_PREFIX");
    const char *coverage_output_directory = g_getenv("GJS_UNIT_COVERAGE_OUTPUT");
    skip = g_getenv("GJS_TEST_SKIP");

    all_tests = read_all_dir_sorted(js_test_dir);
    for (iter = all_tests; iter; iter = iter->next) {
        char *name = (char*) iter->data;
        char *test_name;
        char *file_name;
        GjsTestData *test_data;

        if (!(g_str_has_prefix(name, "test") &&
              g_str_has_suffix(name, ".js"))) {
            g_free(name);
            continue;
        }

        /* pretty print, drop 'test' prefix and '.js' suffix from test name */
        test_name = g_strconcat("/js/", name + 4, NULL);
        test_name[strlen(test_name)-3] = '\0';

        file_name = g_build_filename(js_test_dir, name, NULL);
        test_data = gjs_unit_test_data_new(coverage_prefix,
                                           coverage_output_directory, file_name,
                                           test_name);
        g_test_add(test_name, GjsTestJSFixture, test_data, setup, test, teardown);
        g_free(name);
        all_registered_test_data = g_slist_prepend(all_registered_test_data, test_data);
        /* not freeing file_name, test_name, or test_data yet as it's needed
         * while running the test */
    }
    g_free(js_test_dir);
    g_slist_free(all_tests);

    retval =  g_test_run ();
    g_slist_foreach(all_registered_test_data,
                    (GFunc)gjs_unit_test_data_free,
                    all_registered_test_data);
    g_slist_free(all_registered_test_data);

    g_type_class_unref (context_class);

    return retval;
}
