/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#ifndef _GJS_DEBUG_EXECUTABLE_LINES_UTIL_H
#define _GJS_DEBUG_EXECUTABLE_LINES_UTIL_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gjs/gjs.h>

G_BEGIN_DECLS

guint *
gjs_context_get_executable_lines_for_file(GjsContext *context,
                                          GFile      *file,
                                          guint      begin_line,
                                          guint      *n_executable_lines);

guint *
gjs_context_get_executable_lines_for_filename(GjsContext *context,
                                              const gchar *filename,
                                              guint       begin_line,
                                              guint       *n_executable_lines);

guint *
gjs_context_get_executable_lines_for_string(GjsContext  *context,
                                            const gchar *filename,
                                            const gchar *str,
                                            guint       begin_line,
                                            guint       *n_executable_lines);

guint *
gjs_context_get_executable_lines_for_native_script(GjsContext  *context,
                                                   gpointer    native_script,
                                                   const gchar *lines,
                                                   guint       begin_line,
                                                   guint       *n_executable_lines);

G_END_DECLS

#endif
