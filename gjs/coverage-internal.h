/*
 * Copyright © 2015 Endless Mobile, Inc.
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

#ifndef _GJS_COVERAGE_INTERNAL_H
#define _GJS_COVERAGE_INTERNAL_H

#ifndef GJS_COMPILATION
#error This file is for internal use and use in the tests only
#endif

#include "jsapi-util.h"
#include "coverage.h"

gboolean gjs_run_script_in_coverage_compartment(GjsCoverage *coverage,
                                                const char  *script);
gboolean gjs_inject_value_into_coverage_compartment(GjsCoverage     *coverage,
                                                    JS::HandleValue value,
                                                    const char      *property);

extern const char *GJS_COVERAGE_CACHE_FILE_NAME;

#endif
