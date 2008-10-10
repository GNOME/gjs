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

#include "dirs.h"

static gboolean initialized = FALSE;
static gboolean installed = FALSE;
static char ** cached[6];

static void
init_search_paths(void)
{
    guint i;

    if (initialized)
        return;

    initialized = TRUE;

    installed = ! gjs_environment_variable_is_set("GJS_USE_UNINSTALLED_FILES");

    for (i = 0; i < G_N_ELEMENTS(cached); ++i) {
        cached[i] = NULL;
    }
}

static void
ensure_search_path_in_cache(GjsDirectoryType dir_type)
{
    GPtrArray *path;
    G_CONST_RETURN gchar* G_CONST_RETURN * system_data_dirs;

    g_assert(G_N_ELEMENTS(cached) > (unsigned int) dir_type);

    init_search_paths();

    if (cached[dir_type] != NULL) {
        return;
    }

    path = g_ptr_array_new();
    system_data_dirs = g_get_system_data_dirs();

    switch (dir_type) {
    case GJS_DIRECTORY_SHARED_JAVASCRIPT: {
        int i;

        if (installed) {
            g_ptr_array_add(path, g_strdup(GJS_JS_DIR));
        } else {
            char *s;
            s = g_build_filename(GJS_TOP_SRCDIR, "modules", NULL);
            g_ptr_array_add(path, s);
        }

        for (i = 0; system_data_dirs[i] != NULL; ++i) {
            char *s;
            s = g_build_filename(system_data_dirs[i], "gjs-1.0", NULL);
            g_ptr_array_add(path, s);
        }
    }
        break;

    case GJS_DIRECTORY_SHARED_JAVASCRIPT_NATIVE: {
        if (installed) {
            g_ptr_array_add(path, g_strdup(GJS_NATIVE_DIR));
        } else {
            char *s;
            s = g_build_filename(GJS_BUILDDIR, ".libs", NULL);
            g_ptr_array_add(path, s);
        }
    }
        break;

    }

    g_ptr_array_add(path, NULL);

    cached[dir_type] = (char**) g_ptr_array_free(path, FALSE);
}

char**
gjs_get_search_path(GjsDirectoryType dir_type)
{
    ensure_search_path_in_cache(dir_type);

    g_assert(cached[dir_type] != NULL);

    return g_strdupv(cached[dir_type]);
}

char*
gjs_find_file_on_path (GjsDirectoryType  dir_type,
                       const char       *filename)
{
    int i;
    char **path;

    ensure_search_path_in_cache(dir_type);

    path = cached[dir_type];

    for (i = 0; path[i] != NULL; ++i) {
        char *s;
        gboolean exists;

        s = g_build_filename(path[i], filename, NULL);

        exists = g_file_test(s, G_FILE_TEST_EXISTS);

        if (exists) {
            return s;
        }

        g_free(s);
    }

    return NULL;
}

#if GJS_BUILD_TESTS
#include <stdlib.h>

void
gjstest_test_func_util_dirs(void)
{
    char *filename;

    putenv("GJS_USE_UNINSTALLED_FILES=1");

    g_assert(gjs_find_file_on_path(GJS_DIRECTORY_SHARED_JAVASCRIPT, "no-such-file-as-this.js") == NULL);
    g_assert(gjs_find_file_on_path(GJS_DIRECTORY_SHARED_JAVASCRIPT_NATIVE, "no-such-file-as-this.so") == NULL);

    filename = gjs_find_file_on_path(GJS_DIRECTORY_SHARED_JAVASCRIPT, "lang.js");
    g_assert(filename != NULL);
    g_free(filename);

    filename = gjs_find_file_on_path(GJS_DIRECTORY_SHARED_JAVASCRIPT_NATIVE, "gi.so");
    g_assert(filename != NULL);
    g_free(filename);
}

#endif /* GJS_BUILD_TESTS */
