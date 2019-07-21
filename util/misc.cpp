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

#include <glib.h>

#include "util/misc.h"

bool
gjs_environment_variable_is_set(const char *env_variable_name)
{
    const char *s;

    s = g_getenv(env_variable_name);
    if (!s)
        return false;

    if (*s == '\0')
        return false;

    return true;
}

/** gjs_g_strv_concat:
 *
 * Concate an array of string arrays to one string array. The strings in each
 * array is copied to the resulting array.
 *
 * @strv_array: array of 0-terminated arrays of strings. Null elements are
 * allowed.
 * @len: number of arrays in @strv_array
 *
 * Returns: (transfer full): a newly allocated 0-terminated array of strings.
 */
char** gjs_g_strv_concat(char*** strv_array, int len) {
    GPtrArray* array = g_ptr_array_sized_new(16);

    for (int i = 0; i < len; i++) {
        char** strv = strv_array[i];
        if (!strv)
            continue;

        for (int j = 0; strv[j]; ++j)
            g_ptr_array_add(array, g_strdup(strv[j]));
    }

    g_ptr_array_add(array, nullptr);

    return reinterpret_cast<char**>(g_ptr_array_free(array, false));
}
