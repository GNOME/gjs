/* profiler.h
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2016 Christian Hergert <christian@hergert.me>
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
void gjs_profiler_set_capture_writer(GjsProfiler* self, void* capture);

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
