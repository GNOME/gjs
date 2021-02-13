/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef UTIL_MISC_H_
#define UTIL_MISC_H_

#include <stdlib.h>  // for size_t
#include <string.h>  // for memcpy

#include <glib.h>  // for g_malloc

bool    gjs_environment_variable_is_set   (const char *env_variable_name);

char** gjs_g_strv_concat(char*** strv_array, int len);

/*
 * _gjs_memdup2:
 * @mem: (nullable): the memory to copy.
 * @byte_size: the number of bytes to copy.
 *
 * Allocates @byte_size bytes of memory, and copies @byte_size bytes into it
 * from @mem. If @mem is null or @byte_size is 0 it returns null.
 *
 * This replaces g_memdup(), which was prone to integer overflows when
 * converting the argument from a gsize to a guint.
 *
 * This static inline version is a backport of the new public g_memdup2() API
 * from GLib 2.68.
 * See https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1927.
 * It should be replaced when GLib 2.68 becomes the stable branch.
 *
 * Returns: (nullable): a pointer to the newly-allocated copy of the memory,
 *    or null if @mem is null.
 */
static inline void* _gjs_memdup2(const void* mem, size_t byte_size) {
    if (!mem || byte_size == 0)
        return nullptr;

    void* new_mem = g_malloc(byte_size);
    memcpy(new_mem, mem, byte_size);
    return new_mem;
}

#endif  // UTIL_MISC_H_
