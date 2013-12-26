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
#include <jsprvtd.h>
#include <jsdbgapi.h>
#include <gjs/compat.h>
#include <gjs/jsapi-util.h>
#include <gjs/executable-linesutil.h>

typedef void(*LineForeachFunc)(const gchar *str,
                               gpointer    user_data);

static void
for_each_line_in_string(const gchar *data,
                        gpointer    user_data,
                        LineForeachFunc func)
{
    const gchar *str = data;

    while (str)
    {
        (*func)(str + 1, user_data);
        str = (gchar *) (strstr(str + 1, "\n"));
    }
}

static void
increment_line_counter(const gchar *str,
                       gpointer    user_data)
{
    guint *lineCount = (guint *) user_data;
    ++(*lineCount);
}

static guint
count_lines_in_string (const gchar *data)
{
    guint lineCount = 0;

    for_each_line_in_string(data,
                            &lineCount,
                            increment_line_counter);

    return lineCount;
}

typedef struct _ExecutableDeterminationData
{
    guint currentLine;
    const gchar *filename;
    GArray *statistics;
} ExecutableDeterminationData;

static gboolean
is_nonexecutable_character (gchar character)
{
    return character == ' ' ||
           character == ';' ||
           character == ']' ||
           character == '}' ||
           character == ')';
}

const gchar *
advance_past_leading_nonexecutable_characters (const gchar *str)
{
    while (is_nonexecutable_character (str[0]))
        ++str;

    return str;
}

static gboolean
is_only_newline (const gchar *str)
{
    if (str[0] == '\n')
        return TRUE;

    return FALSE;
}

static gboolean
is_only_function_definition (const gchar *str)
{
    if (strncmp(str, "function", 8) == 0)
        return TRUE;

    return FALSE;
}

static gboolean
is_single_line_comment (const gchar *str)
{
    if (strncmp(str, "//", 2) == 0)
        return TRUE;

    return FALSE;
}

static const gchar *
search_backwards_for_substr (const gchar *haystack,
                             const gchar *needle,
                             const gchar *position)
{
    guint needle_length = strlen(needle);

    /* Search backwards for the first character, then try and
     * strcmp if there's a match. Removes needless jumping around
     * into strcmp */
    while (position != haystack)
    {
        if (position[0] == needle[0] &&
            strncmp(position, needle, needle_length) == 0)
            return position;

      --position;
    }

    return NULL;
}

static gboolean
is_within_comment_block (const gchar *str, const gchar *begin)
{
    static const gchar *previousCommentBeginIdentifier = "/*";
    static const gchar *previousCommentEndIdentifier = "*/";
    const gchar *previousCommentBeginToken = search_backwards_for_substr(begin,
                                                                         previousCommentBeginIdentifier,
                                                                         str);
    const gchar *previousCommentEndToken = search_backwards_for_substr(begin,
                                                                       previousCommentEndIdentifier,
                                                                       str);

    /* We are in a comment block if previousCommentBegin > previousCommentEnd or
    * if there is no previous comment end */
    const gboolean withinCommentBlock =
        (previousCommentBeginToken > previousCommentEndToken) ||
        (previousCommentBeginToken && !previousCommentEndToken);

    return withinCommentBlock;
}

static gboolean
is_nonexecutable_line(const gchar *data,
                      guint       lineNumber)
{
    const gchar *str = data;
    if (lineNumber)
    {
        guint lineNo = lineNumber;

        /* The lines provided to us by JS_GetLinePCs are always going to
         * be 1-indexed and not 0-indexed, so we should only advance for
         * n - 1 lines */
        while (--lineNo)
        {
            str = strstr(str, "\n");
            g_assert(str);
            str += 1;
        }
    }

    str = advance_past_leading_nonexecutable_characters(str);

    /* Line zero is not executable */
    return lineNumber == 0 ||
           is_only_newline(str) ||
           is_single_line_comment(str) ||
           is_only_function_definition(str) ||
           is_within_comment_block(str, data);
}

static unsigned int *
determine_executable_lines(JSContext   *context,
                           JSScript    *script,
                           guint       begin,
                           const gchar *data,
                           guint       *n_executable_lines)
{
    jsbytecode **program_counters;
    unsigned int *lines;
    unsigned int count;

    if (!JS_GetLinePCs(context, script, begin, UINT_MAX,
                       &count,
                       &lines,
                       &program_counters))
    {
        exit (1);
    }

    /* Unfortunately, JS_GetLinePC's doesn't return a completely accurate
     * picture and can sometimes include lines which can never be executed
     * (such as comments). Strip those out */
    if (data)
    {
        unsigned int i = 0;
        for (; i < count; ++i)
        {
            /* Remove nonexecutable lines */
            if (is_nonexecutable_line(data, lines[i]))
            {
                /* Not the last line, we should move everything else
                 * backwards by one */
                if (i < count - 1)
                    memmove(&(lines[i]),
                            &(lines[i + 1]),
                            sizeof(guint) * ((count - 1) - i));

                /* We have fewer lines now, and we need to reset the line counter
                 * by -1 as well since we should consider whether or not the
                 * next line (now moved into our position is nonexecutable too */
                --count;
                i > 0 ? --i : i = 0;
            }
        }
    }

    /* We don't want to leak the details of JS_free to clients,
     * so make a copy of it here so that we can just free with
     * g_free. Note that count here might be less than what we
     * started with, but that's because we stripped obviously
     * nonexecutable lines */
    unsigned int *malloc_lines = g_new0(unsigned int, count);
    memcpy(malloc_lines, lines, sizeof(unsigned int) * count);

    *n_executable_lines = count;

    JS_free(context, lines);
    JS_free(context, program_counters);

  return malloc_lines;
}

guint *
gjs_context_get_executable_lines_for_native_script(GjsContext  *context,
                                                   gpointer    native_script,
                                                   const gchar *lines,
                                                   guint       begin_line,
                                                   guint       *n_executable_lines)
{
    return determine_executable_lines((JSContext *) gjs_context_get_native_context(context),
                                      (JSScript *) native_script,
                                      begin_line,
                                      lines,
                                      n_executable_lines);
}

guint *
gjs_context_get_executable_lines_for_string(GjsContext  *context,
                                            const gchar *filename,
                                            const gchar *str,
                                            guint       begin_line,
                                            guint       *n_executable_lines)
{
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);

    JS_BeginRequest(js_context);
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));
    JS::CompileOptions options(js_context);
    options.setUTF8(true)
         .setFileAndLine(filename, 0)
         .setSourcePolicy(JS::CompileOptions::SAVE_SOURCE);
    js::RootedObject rootedObj(js_context,
                               JS_GetGlobalObject(js_context));

    JSScript *script = JS::Compile(js_context,
                                   rootedObj,
                                   options,
                                   filename);
    JS_EndRequest(js_context);

    if (!script)
        return NULL;

    /* XXX: It seems like there is no function available to free the returned script.
     * As far as I can tell, it gets added to the GC-roots and then cleaned up
     * on the next garbage-collector pass. */
    return gjs_context_get_executable_lines_for_native_script(context,
                                                              script,
                                                              str,
                                                              begin_line,
                                                              n_executable_lines);
}

guint *
gjs_context_get_executable_lines_for_file(GjsContext *context,
                                          GFile      *file,
                                          guint      begin_line,
                                          guint      *n_executable_lines)
{
    GFileInputStream *stream = g_file_read(file, NULL, NULL);

    /* It isn't a file, so we can't get its executable lines */
    if (!stream)
    {
        g_object_unref(file);
        return NULL;
    }

    g_seekable_seek((GSeekable *) stream, 0, G_SEEK_END, NULL, NULL);
    goffset data_count = g_seekable_tell((GSeekable *) stream);
    g_object_unref(stream);

    stream = g_file_read(file, NULL, NULL);

    char data[data_count];
    gsize bytes_read;
    g_input_stream_read_all((GInputStream *) stream,
                            (void *) data,
                            data_count,
                            &bytes_read,
                            NULL,
                            NULL);

    g_assert(bytes_read == data_count);

    gchar *path = g_file_get_path(file);
    guint *retval = gjs_context_get_executable_lines_for_string(context,
                                                                path,
                                                                data,
                                                                begin_line,
                                                                n_executable_lines);

    g_free(path);
    g_object_unref(stream);

    return retval;
}

guint *
gjs_context_get_executable_lines_for_filename(GjsContext  *context,
                                              const gchar *filename,
                                              guint       begin_line,
                                              guint       *n_executable_lines)
{
    GFile *file = g_file_new_for_path(filename);

    g_return_val_if_fail(file, NULL);

    guint *retval = gjs_context_get_executable_lines_for_file(context,
                                                              file,
                                                              begin_line,
                                                              n_executable_lines);

    g_object_unref (file);

    return retval;
}
