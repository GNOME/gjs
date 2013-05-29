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
#ifndef __GJS_UTIL_HASH_X32_H__
#define __GJS_UTIL_HASH_X32_H__

#include <glib.h>

G_BEGIN_DECLS

/* Hash table that operates on gsize; on every architecture except x32,
 * sizeof(gsize) == sizeof(gpointer), and so we can just use it as a
 * hash key directly.  But on x32, we have to fall back to malloc().
 */

GHashTable *gjs_hash_table_new_for_gsize (GDestroyNotify value_destroy);
void gjs_hash_table_for_gsize_insert (GHashTable *table, gsize key, gpointer value);
void gjs_hash_table_for_gsize_remove (GHashTable *table, gsize key);
gpointer gjs_hash_table_for_gsize_lookup (GHashTable *table, gsize key);

G_END_DECLS

#endif
