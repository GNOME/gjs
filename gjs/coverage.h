/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2014 Endless Mobile, Inc.
 * SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>
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

GJS_EXPORT void gjs_coverage_enable(void);

GJS_EXPORT
void gjs_coverage_write_statistics(GjsCoverage *self);

GJS_EXPORT GJS_USE GjsCoverage* gjs_coverage_new(
    const char* const* coverage_prefixes, GjsContext* coverage_context,
    GFile* output_dir);

G_END_DECLS

#endif /* GJS_COVERAGE_H_ */
