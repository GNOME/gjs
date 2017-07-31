/* sp-capture-reader.h
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

#ifndef SP_CAPTURE_READER_H
#define SP_CAPTURE_READER_H

#include "sp-capture-types.h"

G_BEGIN_DECLS

typedef struct _SpCaptureReader SpCaptureReader;

SpCaptureReader          *sp_capture_reader_new            (const gchar         *filename,
                                                            GError             **error);
SpCaptureReader          *sp_capture_reader_new_from_fd    (int                  fd,
                                                            GError             **error);
SpCaptureReader          *sp_capture_reader_ref            (SpCaptureReader     *self);
void                      sp_capture_reader_unref          (SpCaptureReader     *self);
const gchar              *sp_capture_reader_get_filename   (SpCaptureReader     *self);
const gchar              *sp_capture_reader_get_time       (SpCaptureReader     *self);
gboolean                  sp_capture_reader_skip           (SpCaptureReader     *self);
gboolean                  sp_capture_reader_peek_type      (SpCaptureReader     *self,
                                                            SpCaptureFrameType  *type);
const SpCaptureMap       *sp_capture_reader_read_map       (SpCaptureReader     *self);
const SpCaptureExit      *sp_capture_reader_read_exit      (SpCaptureReader     *self);
const SpCaptureFork      *sp_capture_reader_read_fork      (SpCaptureReader     *self);
const SpCaptureTimestamp *sp_capture_reader_read_timestamp (SpCaptureReader     *self);
const SpCaptureProcess   *sp_capture_reader_read_process   (SpCaptureReader     *self);
const SpCaptureSample    *sp_capture_reader_read_sample    (SpCaptureReader     *self);
GHashTable               *sp_capture_reader_read_jitmap    (SpCaptureReader     *self);
gboolean                  sp_capture_reader_reset          (SpCaptureReader     *self);
gboolean                  sp_capture_reader_splice         (SpCaptureReader     *self,
                                                            SpCaptureWriter     *dest,
                                                            GError             **error);
gboolean                  sp_capture_reader_save_as        (SpCaptureReader     *self,
                                                            const gchar         *filename,
                                                            GError             **error);

#ifndef SP_DISABLE_GOBJECT
# define SP_TYPE_CAPTURE_READER (sp_capture_reader_get_type())
  GType sp_capture_reader_get_type (void);
#endif

#if GLIB_CHECK_VERSION(2, 44, 0)
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpCaptureReader, sp_capture_reader_unref)
#endif

G_END_DECLS

#endif /* SP_CAPTURE_READER_H */
