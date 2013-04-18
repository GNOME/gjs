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
#include <gjs/gjs-module.h>
#include <locale.h>

#include <string.h>

typedef struct {
    GjsContext *context;
} GjsTestJSFixture;

static void
setup(GjsTestJSFixture *fix,
      gconstpointer     test_data)
{
    const char *test_filename = test_data;
    const char *js_version;

    js_version = gjs_context_scan_file_for_js_version(test_filename);

    fix->context = g_object_new (GJS_TYPE_CONTEXT,
                                 "js-version", js_version,
                                 NULL);
}

static void
teardown(GjsTestJSFixture *fix,
         gconstpointer     test_data)
{
    gjs_memory_report("before destroying context", FALSE);
    g_object_unref(fix->context);
    gjs_memory_report("after destroying context", TRUE);
}

static void
test(GjsTestJSFixture *fix,
     gconstpointer     test_data)
{
    GError *error = NULL;
    gboolean success;
    int code;

    success = gjs_context_eval_file(fix->context, (const char*)test_data, &code, &error);
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
    GSList *test_filenames = NULL;
    int retval;

    /* The tests are known to fail in the presence of the JIT;
     * we leak objects.
     * https://bugzilla.gnome.org/show_bug.cgi?id=616193
     */
    g_setenv("GJS_DISABLE_JIT", "1", FALSE);

    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);

    g_irepository_prepend_search_path(PKGLIBDIR);
    js_test_dir = g_build_filename(PKGLIBDIR, "js", NULL);

    all_tests = read_all_dir_sorted(js_test_dir);
    for (iter = all_tests; iter; iter = iter->next) {
        char *name = iter->data;
        char *test_name;
        char *file_name;

        if (!(g_str_has_prefix(name, "test") &&
              g_str_has_suffix(name, ".js"))) {
            g_free(name);
            continue;
        }
        if (g_str_has_prefix (name, "testCairo") && g_getenv ("GJS_TEST_SKIP_CAIRO"))
            continue;

        /* pretty print, drop 'test' prefix and '.js' suffix from test name */
        test_name = g_strconcat("/js/", name + 4, NULL);
        test_name[strlen(test_name)-3] = '\0';

        file_name = g_build_filename(js_test_dir, name, NULL);
        g_test_add(test_name, GjsTestJSFixture, file_name, setup, test, teardown);
        g_free(name);
        g_free(test_name);
        test_filenames = g_slist_prepend(test_filenames, file_name);
        /* not freeing file_name yet as it's needed while running the test */
    }
    g_free(js_test_dir);
    g_slist_free(all_tests);

    retval =  g_test_run ();
    g_slist_foreach(test_filenames, (GFunc)g_free, test_filenames);
    g_slist_free(test_filenames);
    return retval;
}
