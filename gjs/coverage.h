/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 Endless Mobile, Inc.
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
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */

#ifndef GJS_COVERAGE_H_
#define GJS_COVERAGE_H_

#if !defined(INSIDE_GJS_H) && !defined(GJS_COMPILATION)
#    error "Only <gjs/gjs.h> can be included directly."
#endif

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h> /* for G_BEGIN_DECLS, G_END_DECLS */

#include <gjs/context.h>
#include <gjs/macros.h>

G_BEGIN_DECLS

#define GJS_TYPE_COVERAGE gjs_coverage_get_type()

G_DECLARE_FINAL_TYPE(GjsCoverage, gjs_coverage, GJS, COVERAGE, GObject);

GJS_EXPORT
void gjs_coverage_write_statistics(GjsCoverage *self);

GJS_EXPORT GJS_USE GjsCoverage* gjs_coverage_new(
    const char* const* coverage_prefixes, GjsContext* coverage_context,
    GFile* output_dir);

G_END_DECLS

#endif /* GJS_COVERAGE_H_ */
