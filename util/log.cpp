/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include "config.h"

#include "log.h"
#include "misc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/* prefix is allowed if it's in the ;-delimited environment variable
 * GJS_DEBUG_TOPICS or if that variable is not set.
 */
static gboolean
is_allowed_prefix (const char *prefix)
{
    static const char *topics = NULL;
    static char **prefixes = NULL;
    gboolean found = FALSE;
    int i;

    if (topics == NULL) {
        topics = g_getenv("GJS_DEBUG_TOPICS");

        if (!topics)
            return TRUE;

        /* We never really free this, should be gone when the process exits */
        prefixes = g_strsplit(topics, ";", -1);
    }

    if (!prefixes)
        return TRUE;

    for (i = 0; prefixes[i] != NULL; i++) {
        if (!strcmp(prefixes[i], prefix)) {
            found = TRUE;
            break;
        }
    }

    return found;
}

#define PREFIX_LENGTH 12

static void
write_to_stream(FILE       *logfp,
                const char *prefix,
                const char *s)
{
    /* seek to end to avoid truncating in case we're using shared logfile */
    (void)fseek(logfp, 0, SEEK_END);

    fprintf(logfp, "%*s: %s", PREFIX_LENGTH, prefix, s);
    if (!g_str_has_suffix(s, "\n"))
        fputs("\n", logfp);
    fflush(logfp);
}

void
gjs_debug(GjsDebugTopic topic,
          const char   *format,
          ...)
{
    static FILE *logfp = NULL;
    static gboolean debug_log_enabled = FALSE;
    static gboolean strace_timestamps = FALSE;
    static gboolean checked_for_timestamp = FALSE;
    static gboolean print_timestamp = FALSE;
    static GTimer *timer = NULL;
    const char *prefix;
    va_list args;
    char *s;

    if (!checked_for_timestamp) {
        print_timestamp = gjs_environment_variable_is_set("GJS_DEBUG_TIMESTAMP");
        checked_for_timestamp = TRUE;
    }

    if (print_timestamp && !timer) {
        timer = g_timer_new();
    }

    if (logfp == NULL) {
        const char *debug_output = g_getenv("GJS_DEBUG_OUTPUT");
        if (debug_output != NULL &&
            strcmp(debug_output, "stderr") == 0) {
            debug_log_enabled = TRUE;
        } else if (debug_output != NULL) {
            const char *log_file;
            char *free_me;
            char *c;

            /* Allow debug-%u.log for per-pid logfiles as otherwise log
             * messages from multiple processes can overwrite each other.
             *
             * (printf below should be safe as we check '%u' is the only format
             * string)
             */
            c = strchr((char *) debug_output, '%');
            if (c && c[1] == 'u' && !strchr(c+1, '%')) {
                free_me = g_strdup_printf(debug_output, (guint)getpid());
                log_file = free_me;
            } else {
                log_file = debug_output;
                free_me = NULL;
            }

            /* avoid truncating in case we're using shared logfile */
            logfp = fopen(log_file, "a");
            if (!logfp)
                fprintf(stderr, "Failed to open log file `%s': %s\n",
                        log_file, g_strerror(errno));

            g_free(free_me);

            debug_log_enabled = TRUE;
        }

        if (logfp == NULL)
            logfp = stderr;

        strace_timestamps = gjs_environment_variable_is_set("GJS_STRACE_TIMESTAMPS");
    }

    /* only strace timestamps if debug
     * log wasn't specifically switched on
     */
    if (!debug_log_enabled &&
        topic != GJS_DEBUG_STRACE_TIMESTAMP)
        return;

    switch (topic) {
    case GJS_DEBUG_STRACE_TIMESTAMP:
        /* return early if strace timestamps are disabled, avoiding
         * printf format overhead and so forth.
         */
        if (!strace_timestamps)
            return;
        /* this is a special magic topic for use with
         * git clone http://www.gnome.org/~federico/git/performance-scripts.git
         * http://www.gnome.org/~federico/news-2006-03.html#timeline-tools
         */
        prefix = "MARK";
        break;
    case GJS_DEBUG_GI_USAGE:
        prefix = "JS GI USE";
        break;
    case GJS_DEBUG_MEMORY:
        prefix = "JS MEMORY";
        break;
    case GJS_DEBUG_CONTEXT:
        prefix = "JS CTX";
        break;
    case GJS_DEBUG_IMPORTER:
        prefix = "JS IMPORT";
        break;
    case GJS_DEBUG_NATIVE:
        prefix = "JS NATIVE";
        break;
    case GJS_DEBUG_KEEP_ALIVE:
        prefix = "JS KP ALV";
        break;
    case GJS_DEBUG_GREPO:
        prefix = "JS G REPO";
        break;
    case GJS_DEBUG_GNAMESPACE:
        prefix = "JS G NS";
        break;
    case GJS_DEBUG_GOBJECT:
        prefix = "JS G OBJ";
        break;
    case GJS_DEBUG_GFUNCTION:
        prefix = "JS G FUNC";
        break;
    case GJS_DEBUG_GFUNDAMENTAL:
        prefix = "JS G FNDMTL";
        break;
    case GJS_DEBUG_GCLOSURE:
        prefix = "JS G CLSR";
        break;
    case GJS_DEBUG_GBOXED:
        prefix = "JS G BXD";
        break;
    case GJS_DEBUG_GENUM:
        prefix = "JS G ENUM";
        break;
    case GJS_DEBUG_GPARAM:
        prefix = "JS G PRM";
        break;
    case GJS_DEBUG_DATABASE:
        prefix = "JS DB";
        break;
    case GJS_DEBUG_RESULTSET:
        prefix = "JS RS";
        break;
    case GJS_DEBUG_WEAK_HASH:
        prefix = "JS WEAK";
        break;
    case GJS_DEBUG_MAINLOOP:
        prefix = "JS MAINLOOP";
        break;
    case GJS_DEBUG_PROPS:
        prefix = "JS PROPS";
        break;
    case GJS_DEBUG_SCOPE:
        prefix = "JS SCOPE";
        break;
    case GJS_DEBUG_HTTP:
        prefix = "JS HTTP";
        break;
    case GJS_DEBUG_BYTE_ARRAY:
        prefix = "JS BYTE ARRAY";
        break;
    case GJS_DEBUG_GERROR:
        prefix = "JS G ERR";
        break;
    default:
        prefix = "???";
        break;
    }

    if (!is_allowed_prefix(prefix))
        return;

    va_start (args, format);
    s = g_strdup_vprintf (format, args);
    va_end (args);

    if (topic == GJS_DEBUG_STRACE_TIMESTAMP) {
        /* Put a magic string in strace output */
        char *s2;
        s2 = g_strdup_printf("%s: gjs: %s",
                             prefix, s);
        access(s2, F_OK);
        g_free(s2);
    } else {
        if (print_timestamp) {
            static gdouble previous = 0.0;
            gdouble total = g_timer_elapsed(timer, NULL) * 1000.0;
            gdouble since = total - previous;
            const char *ts_suffix;
            char *s2;

            if (since > 50.0) {
                ts_suffix = "!!  ";
            } else if (since > 100.0) {
                ts_suffix = "!!! ";
            } else if (since > 200.0) {
                ts_suffix = "!!!!";
            } else {
                ts_suffix = "    ";
            }

            s2 = g_strdup_printf("%g %s%s",
                                 total, ts_suffix, s);
            g_free(s);
            s = s2;

            previous = total;
        }

        write_to_stream(logfp, prefix, s);
    }

    g_free(s);
}
