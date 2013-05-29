/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Red Hat, Inc.
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

#include "hash-x32.h"

/* Note: Not actually tested on x32 */

#define HASH_GSIZE_FITS_POINTER (sizeof(gsize) == sizeof(gpointer))

GHashTable *
gjs_hash_table_new_for_gsize (GDestroyNotify value_destroy)
{
    if (HASH_GSIZE_FITS_POINTER) {
        return g_hash_table_new_full (NULL, NULL, NULL, value_destroy);
    } else {
        return g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, value_destroy);
    }
}

void
gjs_hash_table_for_gsize_insert (GHashTable *table, gsize key, gpointer value)
{
    if (HASH_GSIZE_FITS_POINTER) {
        g_hash_table_insert (table, (gpointer)key, value);
    } else {
        guint64 *keycopy = g_new (guint64, 1);
        *keycopy = (guint64) key;
        g_hash_table_insert (table, keycopy, value);
    }
}

void
gjs_hash_table_for_gsize_remove (GHashTable *table, gsize key)
{
    if (HASH_GSIZE_FITS_POINTER)
        g_hash_table_remove (table, (gpointer)key);
    else
        g_hash_table_remove (table, &key);
}

gpointer
gjs_hash_table_for_gsize_lookup (GHashTable *table, gsize key)
{
    if (HASH_GSIZE_FITS_POINTER)
        return g_hash_table_lookup (table, (gpointer)key);
    else
        return g_hash_table_lookup (table, &key);
}

