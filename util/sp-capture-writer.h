/* sp-capture-writer.h
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
