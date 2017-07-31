/* sp-capture-writer.c
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
 * https://git.gnome.org/browse/sysprof/tree/lib/capture/sp-capture-writer.c
 * It has been modified to remove unneeded functionality.
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sp-capture-writer.h"

#define DEFAULT_BUFFER_SIZE (getpagesize() * 64L)
#define INVALID_ADDRESS     (G_GUINT64_CONSTANT(0))

typedef struct
{
  /* A pinter into the string buffer */
  const gchar *str;

  /* The unique address for the string */
  guint64 addr;
} SpCaptureJitmapBucket;

struct _SpCaptureWriter
{
  /*
   * This is our buffer location for incoming strings. This is used
   * similarly to GStringChunk except there is only one-page, and after
   * it fills, we flush to disk.
   *
   * This is paired with a closed hash table for deduplication.
   */
  gchar addr_buf[4096*4];

  /* Our hashtable for deduplication. */
  SpCaptureJitmapBucket addr_hash[512];

  /* We keep the large fields above so that our allocation will be page
   * alinged for the write buffer. This improves the performance of large
   * writes to the target file-descriptor.
   */
  volatile gint ref_count;

  /*
   * Our address sequence counter. The value that comes from
   * monotonically increasing this is OR'd with JITMAP_MARK to denote
   * the address name should come from the JIT map.
   */
  gsize addr_seq;

  /* Our position in addr_buf. */
  gsize addr_buf_pos;

  /*
   * The number of hash table items in @addr_hash. This is an
   * optimization so that we can avoid calculating the number of strings
   * when flushing out the jitmap.
   */
  guint addr_hash_size;

  /* Capture file handle */
  int fd;

  /* Our write buffer for fd */
  guint8 *buf;
  gsize pos;
  gsize len;

  /* counter id sequence */
  gint next_counter_id;

  /* Statistics while recording */
  SpCaptureStat stat;
};

G_DEFINE_BOXED_TYPE (SpCaptureWriter, sp_capture_writer,
                     sp_capture_writer_ref, sp_capture_writer_unref)

static inline void
sp_capture_writer_frame_init (SpCaptureFrame     *frame_,
                              gint                len,
                              gint                cpu,
                              GPid                pid,
                              gint64              time_,
                              SpCaptureFrameType  type)
{
  g_assert (frame_ != NULL);

  frame_->len = len;
  frame_->cpu = cpu;
  frame_->pid = pid;
  frame_->time = time_;
  frame_->type = type;
  frame_->padding = 0;
}

static void
sp_capture_writer_finalize (SpCaptureWriter *self)
{
  if (self != NULL)
    {
      sp_capture_writer_flush (self);
      close (self->fd);
      g_free (self->buf);
      g_free (self);
    }
}

SpCaptureWriter *
sp_capture_writer_ref (SpCaptureWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
sp_capture_writer_unref (SpCaptureWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sp_capture_writer_finalize (self);
}

static gboolean
sp_capture_writer_flush_data (SpCaptureWriter *self)
{
  const guint8 *buf;
  gssize written;
  gsize to_write;

  g_assert (self != NULL);
  g_assert (self->pos <= self->len);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  buf = self->buf;
  to_write = self->pos;

  while (to_write > 0)
    {
      written = write (self->fd, buf, to_write);
      if (written < 0)
        return FALSE;

      if (written == 0 && errno != EAGAIN)
        return FALSE;

      g_assert (written <= (gssize)to_write);

      buf += written;
      to_write -= written;
    }

  self->pos = 0;

  return TRUE;
}

static inline void
sp_capture_writer_realign (gsize *pos)
{
  *pos = (*pos + SP_CAPTURE_ALIGN - 1) & ~(SP_CAPTURE_ALIGN - 1);
}

static inline gboolean
sp_capture_writer_ensure_space_for (SpCaptureWriter *self,
                                    gsize            len)
{
  /* Check for max frame size */
  if (len > G_MAXUSHORT)
    return FALSE;

  if ((self->len - self->pos) < len)
    {
      if (!sp_capture_writer_flush_data (self))
        return FALSE;
    }

  return TRUE;
}

static inline gpointer
sp_capture_writer_allocate (SpCaptureWriter *self,
                            gsize           *len)
{
  gpointer p;

  g_assert (self != NULL);
  g_assert (len != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  sp_capture_writer_realign (len);

  if (!sp_capture_writer_ensure_space_for (self, *len))
    return NULL;

  p = (gpointer)&self->buf[self->pos];

  self->pos += *len;

  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  return p;
}

static gboolean
sp_capture_writer_flush_jitmap (SpCaptureWriter *self)
{
  SpCaptureJitmap jitmap;
  gssize r;
  gsize len;

  g_assert (self != NULL);

  if (self->addr_hash_size == 0)
    return TRUE;

  g_assert (self->addr_buf_pos > 0);

  len = sizeof jitmap + self->addr_buf_pos;

  sp_capture_writer_realign (&len);

  sp_capture_writer_frame_init (&jitmap.frame,
                                len,
                                -1,
                                getpid (),
                                SP_CAPTURE_CURRENT_TIME,
                                SP_CAPTURE_FRAME_JITMAP);
  jitmap.n_jitmaps = self->addr_hash_size;

  if (sizeof jitmap != write (self->fd, &jitmap, sizeof jitmap))
    return FALSE;

  r = write (self->fd, self->addr_buf, len - sizeof jitmap);
  if (r < 0 || (gsize)r != (len - sizeof jitmap))
    return FALSE;

  self->addr_buf_pos = 0;
  self->addr_hash_size = 0;
  memset (self->addr_hash, 0, sizeof self->addr_hash);

  self->stat.frame_count[SP_CAPTURE_FRAME_JITMAP]++;

  return TRUE;
}

static gboolean
sp_capture_writer_lookup_jitmap (SpCaptureWriter  *self,
                                 const gchar      *name,
                                 SpCaptureAddress *addr)
{
  guint hash;
  guint i;

  g_assert (self != NULL);
  g_assert (name != NULL);
  g_assert (addr != NULL);

  hash = g_str_hash (name) % G_N_ELEMENTS (self->addr_hash);

  for (i = hash; i < G_N_ELEMENTS (self->addr_hash); i++)
    {
      SpCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (bucket->str == NULL)
        return FALSE;

      if (strcmp (bucket->str, name) == 0)
        {
          *addr = bucket->addr;
          return TRUE;
        }
    }

  for (i = 0; i < hash; i++)
    {
      SpCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (bucket->str == NULL)
        return FALSE;

      if (strcmp (bucket->str, name) == 0)
        {
          *addr = bucket->addr;
          return TRUE;
        }
    }

  return FALSE;
}

static SpCaptureAddress
sp_capture_writer_insert_jitmap (SpCaptureWriter *self,
                                 const gchar     *str)
{
  SpCaptureAddress addr;
  gchar *dst;
  gsize len;
  guint hash;
  guint i;

  g_assert (self != NULL);
  g_assert (str != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  len = sizeof addr + strlen (str) + 1;

  if ((self->addr_hash_size == G_N_ELEMENTS (self->addr_hash)) ||
      ((sizeof self->addr_buf - self->addr_buf_pos) < len))
    {
      if (!sp_capture_writer_flush_jitmap (self))
        return INVALID_ADDRESS;

      g_assert (self->addr_hash_size == 0);
      g_assert (self->addr_buf_pos == 0);
    }

  g_assert (self->addr_hash_size < G_N_ELEMENTS (self->addr_hash));
  g_assert (len > sizeof addr);

  /* Allocate the next unique address */
  addr = SP_CAPTURE_JITMAP_MARK | ++self->addr_seq;

  /* Copy the address into the buffer */
  dst = (gchar *)&self->addr_buf[self->addr_buf_pos];
  memcpy (dst, &addr, sizeof addr);

  /*
   * Copy the string into the buffer, keeping dst around for
   * when we insert into the hashtable.
   */
  dst += sizeof addr;
  memcpy (dst, str, len - sizeof addr);

  /* Advance our string cache position */
  self->addr_buf_pos += len;
  g_assert (self->addr_buf_pos <= sizeof self->addr_buf);

  /* Now place the address into the hashtable */
  hash = g_str_hash (str) % G_N_ELEMENTS (self->addr_hash);

  /* Start from the current hash bucket and go forward */
  for (i = hash; i < G_N_ELEMENTS (self->addr_hash); i++)
    {
      SpCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (G_LIKELY (bucket->str == NULL))
        {
          bucket->str = dst;
          bucket->addr = addr;
          self->addr_hash_size++;
          return addr;
        }
    }

  /* Wrap around to the beginning */
  for (i = 0; i < hash; i++)
    {
      SpCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (G_LIKELY (bucket->str == NULL))
        {
          bucket->str = dst;
          bucket->addr = addr;
          self->addr_hash_size++;
          return addr;
        }
    }

  g_assert_not_reached ();

  return INVALID_ADDRESS;
}

SpCaptureWriter *
sp_capture_writer_new_from_fd (int   fd,
                               gsize buffer_size)
{
  g_autofree gchar *nowstr = NULL;
  SpCaptureWriter *self;
  SpCaptureFileHeader *header;
  GTimeVal tv;
  gsize header_len = sizeof(*header);

  if (buffer_size == 0)
    buffer_size = DEFAULT_BUFFER_SIZE;

  g_assert (fd != -1);
  g_assert (buffer_size % getpagesize() == 0);

  if (ftruncate (fd, 0) != 0)
    return NULL;

  self = g_new0 (SpCaptureWriter, 1);
  self->ref_count = 1;
  self->fd = fd;
  self->buf = (guint8 *)g_malloc (buffer_size);
  self->len = buffer_size;
  self->next_counter_id = 1;

  g_get_current_time (&tv);
  nowstr = g_time_val_to_iso8601 (&tv);

  header = sp_capture_writer_allocate (self, &header_len);

  if (header == NULL)
    {
      sp_capture_writer_finalize (self);
      return NULL;
    }

  header->magic = SP_CAPTURE_MAGIC;
  header->version = 1;
#ifdef G_LITTLE_ENDIAN
  header->little_endian = TRUE;
#else
  header->little_endian = FALSE;
#endif
  header->padding = 0;
  g_strlcpy (header->capture_time, nowstr, sizeof header->capture_time);
  header->time = SP_CAPTURE_CURRENT_TIME;
  header->end_time = 0;
  memset (header->suffix, 0, sizeof header->suffix);

  if (!sp_capture_writer_flush_data (self))
    {
      sp_capture_writer_finalize (self);
      return NULL;
    }

  g_assert (self->pos == 0);
  g_assert (self->len > 0);
  g_assert (self->len % getpagesize() == 0);
  g_assert (self->buf != NULL);
  g_assert (self->addr_hash_size == 0);
  g_assert (self->fd != -1);

  return self;
}

SpCaptureWriter *
sp_capture_writer_new (const gchar *filename,
                       gsize        buffer_size)
{
  SpCaptureWriter *self;
  int fd;

  g_assert (filename != NULL);
  g_assert (buffer_size % getpagesize() == 0);

  if ((-1 == (fd = open (filename, O_CREAT | O_RDWR, 0640))) ||
      (-1 == ftruncate (fd, 0L)))
    return NULL;

  self = sp_capture_writer_new_from_fd (fd, buffer_size);

  if (self == NULL)
    close (fd);

  return self;
}

gboolean
sp_capture_writer_add_map (SpCaptureWriter *self,
                           gint64           time,
                           gint             cpu,
                           GPid             pid,
                           guint64          start,
                           guint64          end,
                           guint64          offset,
                           guint64          inode,
                           const gchar     *filename)
{
  SpCaptureMap *ev;
  gsize len;

  if (filename == NULL)
    filename = "";

  g_assert (self != NULL);
  g_assert (filename != NULL);

  len = sizeof *ev + strlen (filename) + 1;

  ev = (SpCaptureMap *)sp_capture_writer_allocate (self, &len);
  if (!ev)
    return FALSE;

  sp_capture_writer_frame_init (&ev->frame,
                                len,
                                cpu,
                                pid,
                                time,
                                SP_CAPTURE_FRAME_MAP);
  ev->start = start;
  ev->end = end;
  ev->offset = offset;
  ev->inode = inode;

  g_strlcpy (ev->filename, filename, len - sizeof *ev);
  ev->filename[len - sizeof *ev - 1] = '\0';

  self->stat.frame_count[SP_CAPTURE_FRAME_MAP]++;

  return TRUE;
}

SpCaptureAddress
sp_capture_writer_add_jitmap (SpCaptureWriter *self,
                              const gchar     *name)
{
  SpCaptureAddress addr = INVALID_ADDRESS;

  if (name == NULL)
    name = "";

  g_assert (self != NULL);
  g_assert (name != NULL);

  if (!sp_capture_writer_lookup_jitmap (self, name, &addr))
    addr = sp_capture_writer_insert_jitmap (self, name);

  return addr;
}

gboolean
sp_capture_writer_add_sample (SpCaptureWriter        *self,
                              gint64                  time,
                              gint                    cpu,
                              GPid                    pid,
                              const SpCaptureAddress *addrs,
                              guint                   n_addrs)
{
  SpCaptureSample *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev + (n_addrs * sizeof (SpCaptureAddress));

  ev = (SpCaptureSample *)sp_capture_writer_allocate (self, &len);
  if (!ev)
    return FALSE;

  sp_capture_writer_frame_init (&ev->frame,
                                len,
                                cpu,
                                pid,
                                time,
                                SP_CAPTURE_FRAME_SAMPLE);
  ev->n_addrs = n_addrs;

  memcpy (ev->addrs, addrs, (n_addrs * sizeof (SpCaptureAddress)));

  self->stat.frame_count[SP_CAPTURE_FRAME_SAMPLE]++;

  return TRUE;
}

static gboolean
sp_capture_writer_flush_end_time (SpCaptureWriter *self)
{
  gint64 end_time = SP_CAPTURE_CURRENT_TIME;
  ssize_t ret;

  g_assert (self != NULL);

  /* This field is opportunistic, so a failure is okay. */

again:
  ret = pwrite (self->fd,
                &end_time,
                sizeof (end_time),
                G_STRUCT_OFFSET (SpCaptureFileHeader, end_time));

  if (ret < 0 && errno == EAGAIN)
    goto again;

  return TRUE;
}

gboolean
sp_capture_writer_flush (SpCaptureWriter *self)
{
  g_assert (self != NULL);

  return (sp_capture_writer_flush_jitmap (self) &&
          sp_capture_writer_flush_data (self) &&
          sp_capture_writer_flush_end_time (self));
}
