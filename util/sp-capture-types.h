/* sp-capture-types.h
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
 * https://git.gnome.org/browse/sysprof/tree/lib/capture/sp-capture-types.c
 * It has been modified to remove unneeded functionality.
 */

#ifndef SP_CAPTURE_FORMAT_H
#define SP_CAPTURE_FORMAT_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define SP_CAPTURE_MAGIC (GUINT32_TO_LE(0xFDCA975E))
#define SP_CAPTURE_ALIGN (sizeof(SpCaptureAddress))

#if __WORDSIZE == 64
# define SP_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE000000000000000)
# define SP_CAPTURE_ADDRESS_FORMAT "0x%016lx"
#else
# define SP_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE0000000)
# define SP_CAPTURE_ADDRESS_FORMAT "0x%016llx"
#endif

#define SP_CAPTURE_CURRENT_TIME (g_get_monotonic_time() * 1000L)

typedef struct _SpCaptureWriter SpCaptureWriter;

typedef guint64 SpCaptureAddress;

typedef enum
{
  SP_CAPTURE_FRAME_TIMESTAMP = 1,
  SP_CAPTURE_FRAME_SAMPLE    = 2,
  SP_CAPTURE_FRAME_MAP       = 3,
  SP_CAPTURE_FRAME_PROCESS   = 4,
  SP_CAPTURE_FRAME_FORK      = 5,
  SP_CAPTURE_FRAME_EXIT      = 6,
  SP_CAPTURE_FRAME_JITMAP    = 7,
  SP_CAPTURE_FRAME_CTRDEF    = 8,
  SP_CAPTURE_FRAME_CTRSET    = 9,
} SpCaptureFrameType;

#pragma pack(push, 1)

typedef struct
{
  guint32 magic;
  guint8  version;
  guint32 little_endian : 1;
  guint32 padding : 23;
  gchar   capture_time[64];
  gint64  time;
  gint64  end_time;
  gchar   suffix[168];
} SpCaptureFileHeader;

typedef struct
{
  guint16 len;
  gint16  cpu;
  gint32  pid;
  gint64  time;
  guint8  type;
  guint64 padding : 56;
  guint8  data[0];
} SpCaptureFrame;

typedef struct
{
  SpCaptureFrame frame;
  guint64        start;
  guint64        end;
  guint64        offset;
  guint64        inode;
  gchar          filename[0];
} SpCaptureMap;

typedef struct
{
  SpCaptureFrame frame;
  guint32        n_jitmaps;
  guint8         data[0];
} SpCaptureJitmap;

typedef struct
{
  SpCaptureFrame   frame;
  guint16          n_addrs;
  guint64          padding : 48;
  SpCaptureAddress addrs[0];
} SpCaptureSample;

#pragma pack(pop)

G_STATIC_ASSERT (sizeof (SpCaptureFileHeader) == 256);
G_STATIC_ASSERT (sizeof (SpCaptureFrame) == 24);
G_STATIC_ASSERT (sizeof (SpCaptureMap) == 56);
G_STATIC_ASSERT (sizeof (SpCaptureJitmap) == 28);
G_STATIC_ASSERT (sizeof (SpCaptureSample) == 32);

G_END_DECLS

#endif /* SP_CAPTURE_FORMAT_H */
