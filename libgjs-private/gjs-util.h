/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#ifndef __GJS_PRIVATE_UTIL_H__
#define __GJS_PRIVATE_UTIL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* For imports.format */
char * gjs_format_int_alternative_output (int n);

/* For imports.gettext */
void gjs_textdomain     (const char *domain);
void gjs_bindtextdomain (const char *domain,
                         const char *location);

/* For imports.overrides.GObject */
GParamFlags gjs_param_spec_get_flags (GParamSpec *pspec);
GType       gjs_param_spec_get_value_type (GParamSpec *pspec);
GType       gjs_param_spec_get_owner_type (GParamSpec *pspec);

G_END_DECLS

#endif
