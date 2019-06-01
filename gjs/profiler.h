/* profiler.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef GJS_PROFILER_H_
#define GJS_PROFILER_H_

#if !defined(INSIDE_GJS_H) && !defined(GJS_COMPILATION)
#    error "Only <gjs/gjs.h> can be included directly."
#endif

#include <glib-object.h>
#include <glib.h>

#include <gjs/macros.h>

G_BEGIN_DECLS

#define GJS_TYPE_PROFILER (gjs_profiler_get_type())

typedef struct _GjsProfiler GjsProfiler;

GJS_EXPORT
GType gjs_profiler_get_type(void);

GJS_EXPORT
void gjs_profiler_set_filename(GjsProfiler *self,
                               const char  *filename);
GJS_EXPORT
void gjs_profiler_set_fd(GjsProfiler* self, int fd);

GJS_EXPORT
void gjs_profiler_start(GjsProfiler *self);

GJS_EXPORT
void gjs_profiler_stop(GjsProfiler *self);

G_END_DECLS

#endif  // GJS_PROFILER_H_
