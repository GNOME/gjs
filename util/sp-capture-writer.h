/* sp-capture-writer.h
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

/* The original source of this file is:
 * https://git.gnome.org/browse/sysprof/tree/lib/capture/sp-capture-writer.h
 * It has been modified to remove unneeded functionality.
 */

#ifndef SP_CAPTURE_WRITER_H
#define SP_CAPTURE_WRITER_H

#include "sp-capture-types.h"

G_BEGIN_DECLS

typedef struct _SpCaptureWriter SpCaptureWriter;

typedef struct
{
  /*
   * The number of frames indexed by SpCaptureFrameType
   */
  gsize frame_count[16];

  /*
   * Padding for future expansion.
   */
  gsize padding[48];
} SpCaptureStat;

SpCaptureWriter    *sp_capture_writer_new             (const gchar             *filename,
                                                       gsize                    buffer_size);
SpCaptureWriter    *sp_capture_writer_new_from_fd     (int                      fd,
                                                       gsize                    buffer_size);
SpCaptureWriter    *sp_capture_writer_ref             (SpCaptureWriter         *self);
void                sp_capture_writer_unref           (SpCaptureWriter         *self);
gboolean            sp_capture_writer_add_map         (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       GPid                     pid,
                                                       guint64                  start,
                                                       guint64                  end,
                                                       guint64                  offset,
                                                       guint64                  inode,
                                                       const gchar             *filename);
guint64             sp_capture_writer_add_jitmap      (SpCaptureWriter         *self,
                                                       const gchar             *name);
gboolean            sp_capture_writer_add_sample      (SpCaptureWriter         *self,
                                                       gint64                   time,
                                                       gint                     cpu,
                                                       GPid                     pid,
                                                       const SpCaptureAddress  *addrs,
                                                       guint                    n_addrs);
gboolean            sp_capture_writer_flush           (SpCaptureWriter         *self);

#define SP_TYPE_CAPTURE_WRITER (sp_capture_writer_get_type())
GType sp_capture_writer_get_type (void);

G_END_DECLS

#endif /* SP_CAPTURE_WRITER_H */
