/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

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
