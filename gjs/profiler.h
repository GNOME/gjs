/* profiler.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GJS_PROFILER_H
#define GJS_PROFILER_H

#include <gjs/context.h>
#include <signal.h>

G_BEGIN_DECLS

#define GJS_TYPE_PROFILER (gjs_profiler_get_type())

typedef struct _GjsProfiler GjsProfiler;

GType        gjs_profiler_get_type      (void);
GjsProfiler *gjs_profiler_new           (GjsContext  *context);
void         gjs_profiler_free          (GjsProfiler *self);
void         gjs_profiler_set_filename  (GjsProfiler *self,
                                         const gchar *filename);
void         gjs_profiler_start         (GjsProfiler *self);
void         gjs_profiler_stop          (GjsProfiler *self);
gboolean     gjs_profiler_is_running    (GjsProfiler *self);
void         gjs_profiler_setup_signals (void);
gboolean     gjs_profiler_chain_signal  (siginfo_t   *info);

G_END_DECLS

#endif /* GJS_PROFILER_H */
