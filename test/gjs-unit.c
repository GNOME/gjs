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
#include <gjs/gjs.h>
#include <util/crash.h>
#include <locale.h>

#include <string.h>

typedef struct {
    GjsContext *context;
} GjsTestJSFixture;

static const char *top_srcdir;


static void
setup(GjsTestJSFixture *fix,
      gconstpointer     test_data)
{
    gboolean success;
    GError *error = NULL;
    int code;
    char *filename;
    char *search_path[2];

    search_path[0] = g_build_filename(top_srcdir, "test", "modules", NULL);
    search_path[1] = NULL;

    fix->context = gjs_context_new_with_search_path(search_path);
    g_free(search_path[0]);

    /* Load jsUnit.js directly into global scope, rather than
     * requiring each test to import it as a module, among other
     * things this lets us test importing modules without relying on
     * importing a module, but also it's just less typing to have
     * "assert*" without a prefix.
     */
    filename = g_build_filename(top_srcdir, "test", "js", "modules", "jsUnit.js", NULL);
    success = gjs_context_eval_file(fix->context, filename, &code, &error);
    g_free(filename);

    if (!success)
        g_error("%s", error->message);
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
    g_assert_cmpint(code, ==, 0);
}

int
main(int argc, char **argv)
{
    char *js_test_dir;
    const char *name;
    GDir *dir;

    /* we're always going to use uninstalled files, set up necessary
     * environment variables, but don't overwrite if already set */
    g_setenv("TOP_SRCDIR", GJS_TOP_SRCDIR, FALSE);
    g_setenv("BUILDDIR", GJS_BUILDDIR, FALSE);
    g_setenv("XDG_DATA_HOME", GJS_BUILDDIR "/test_user_data", FALSE);
    g_setenv("GJS_PATH", GJS_TOP_SRCDIR"/modules:"GJS_TOP_SRCDIR"/test/js/modules:"GJS_BUILDDIR"/.libs:", FALSE);

    gjs_crash_after_timeout(60*7); /* give the unit tests 7 minutes to complete */
    gjs_init_sleep_on_crash();

    /* need ${top_srcdir} later */
    top_srcdir = g_getenv ("TOP_SRCDIR");

    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);

    g_type_init();

    /* iterate through all 'test*.js' files in ${top_srcdir}/test/js */
    js_test_dir = g_build_filename(top_srcdir, "test", "js", NULL);
    dir = g_dir_open(js_test_dir, 0, NULL);
    g_assert(dir != NULL);

    while ((name = g_dir_read_name(dir)) != NULL) {
        char *test_name;
        char *file_name;

        if (!(g_str_has_prefix(name, "test") &&
              g_str_has_suffix(name, ".js")))
            continue;

        /* pretty print, drop 'test' prefix and '.js' suffix from test name */
        test_name = g_strconcat("/js/", name + 4, NULL);
        test_name[strlen(test_name)-3] = '\0';

        file_name = g_build_filename(js_test_dir, name, NULL);
        g_test_add(test_name, GjsTestJSFixture, file_name, setup, test, teardown);
        g_free(test_name);
        /* not freeing file_name as it's needed while running the test */
    }
    g_dir_close(dir);

    return g_test_run ();
}
