/*
 * Copyright © 2014 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#ifndef _GJS_DEBUG_COVERAGE_H
#define _GJS_DEBUG_COVERAGE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GJS_TYPE_DEBUG_COVERAGE gjs_coverage_get_type()

#define GJS_DEBUG_COVERAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), \
     GJS_TYPE_DEBUG_COVERAGE, GjsCoverage))

#define GJS_DEBUG_COVERAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), \
     GJS_TYPE_DEBUG_COVERAGE, GjsCoverageClass))

#define GJS_IS_DEBUG_COVERAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
     GJS_TYPE_DEBUG_COVERAGE))

#define GJS_IS_DEBUG_COVERAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
     GJS_TYPE_DEBUG_COVERAGE))

#define GJS_DEBUG_COVERAGE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
     GJS_TYPE_DEBUG_COVERAGE, GjsCoverageClass))

typedef struct _GFile GFile;
typedef struct _GjsDebugHooks GjsDebugHooks;
typedef struct _GjsContext GjsContext;

typedef struct _GjsCoverage GjsCoverage;
typedef struct _GjsCoverageClass GjsCoverageClass;
typedef struct _GjsCoveragePrivate GjsCoveragePrivate;

struct _GjsCoverage {
    GObject parent;
};

struct _GjsCoverageClass {
    GObjectClass parent_class;
};

GType gjs_debug_coverage_get_type(void);

/**
 * gjs_debug_coverage_write_statistics:
 * @coverage: A #GjsDebugCoverage
 * @output_file (allow-none): A #GFile to write statistics to. If NULL is provided then coverage data
 * will be written to files in the form of (filename).info in the same directory as the input file
 *
 * This function takes all available statistics and writes them out to either the file provided
 * or to files of the pattern (filename).info in the same directory as the scanned files. It will
 * provide coverage data for all files ending with ".js" in the coverage directories, even if they
 * were never actually executed.
 */
void gjs_coverage_write_statistics(GjsCoverage *coverage,
                                   const char  *output_directory);

GjsCoverage * gjs_coverage_new(const char    **covered_directories,
                               GjsContext    *coverage_context);

G_END_DECLS

#endif
