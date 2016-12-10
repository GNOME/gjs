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
#ifndef _GJS_COVERAGE_H
#define _GJS_COVERAGE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "context.h"

G_BEGIN_DECLS

#define GJS_TYPE_COVERAGE gjs_coverage_get_type()

G_DECLARE_FINAL_TYPE(GjsCoverage, gjs_coverage, GJS, COVERAGE, GObject);

void gjs_coverage_write_statistics(GjsCoverage *self);

GjsCoverage * gjs_coverage_new(const char * const *coverage_prefixes,
                               GjsContext         *coverage_context,
                               GFile              *output_dir);

G_END_DECLS

#endif
