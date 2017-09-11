/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2015 Endless Mobile, Inc.
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

#ifndef _GJS_COVERAGE_INTERNAL_H
#define _GJS_COVERAGE_INTERNAL_H

#include <gio/gio.h>

#include "jsapi-util.h"
#include "coverage.h"

G_BEGIN_DECLS

GjsCoverage *gjs_coverage_new_internal_with_cache(const char * const *coverage_prefixes,
                                                  GjsContext         *context,
                                                  GFile              *output_dir,
                                                  GFile              *cache_path);

GjsCoverage *gjs_coverage_new_internal_without_cache(const char * const *prefixes,
                                                     GjsContext         *cx,
                                                     GFile              *output_dir);

GBytes * gjs_serialize_statistics(GjsCoverage *coverage);

JSString * gjs_deserialize_cache_to_object(GjsCoverage *coverage,
                                           GBytes      *cache_bytes);

bool gjs_run_script_in_coverage_compartment(GjsCoverage *coverage,
                                            const char  *script);
bool gjs_inject_value_into_coverage_compartment(GjsCoverage     *coverage,
                                                JS::HandleValue  value,
                                                const char      *property);

bool gjs_get_file_mtime(GFile    *file,
                        GTimeVal *mtime);

char *gjs_get_file_checksum(GFile *file);

bool gjs_write_cache_file(GFile  *file,
                          GBytes *cache_bytes);

extern const char *GJS_COVERAGE_CACHE_FILE_NAME;

G_END_DECLS

#endif
