/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>   // for FILE, fprintf, fflush, fopen, fputs, fseek
#include <string.h>  // for strchr, strcmp

#ifdef _WIN32
# include <io.h>
# include <process.h>
# ifndef F_OK
#  define F_OK 0
# endif
#else
#    include <unistd.h>  // for getpid
#endif

#include "util/log.h"
#include "util/misc.h"

/* prefix is allowed if it's in the ;-delimited environment variable
 * GJS_DEBUG_TOPICS or if that variable is not set.
 */
static bool
is_allowed_prefix (const char *prefix)
{
    static const char *topics = NULL;
    static char **prefixes = NULL;
    bool found = false;
    int i;

    if (topics == NULL) {
        topics = g_getenv("GJS_DEBUG_TOPICS");

        if (!topics)
            return true;

        /* We never really free this, should be gone when the process exits */
        prefixes = g_strsplit(topics, ";", -1);
    }

    if (!prefixes)
        return true;

    for (i = 0; prefixes[i] != NULL; i++) {
        if (!strcmp(prefixes[i], prefix)) {
            found = true;
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
    static bool debug_log_enabled = false;
    static bool checked_for_timestamp = false;
    static bool print_timestamp = false;
    static bool checked_for_thread = false;
    static bool print_thread = false;
    static GTimer *timer = NULL;
    const char *prefix;
    va_list args;
    char *s;

    if (!checked_for_timestamp) {
        print_timestamp = gjs_environment_variable_is_set("GJS_DEBUG_TIMESTAMP");
        checked_for_timestamp = true;
    }

    if (!checked_for_thread) {
        print_thread = gjs_environment_variable_is_set("GJS_DEBUG_THREAD");
        checked_for_thread = true;
    }

    if (print_timestamp && !timer) {
        timer = g_timer_new();
    }

    if (logfp == NULL) {
        const char *debug_output = g_getenv("GJS_DEBUG_OUTPUT");
        if (debug_output != NULL &&
            strcmp(debug_output, "stderr") == 0) {
            debug_log_enabled = true;
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
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wformat-nonliteral\"")
#endif
                free_me = g_strdup_printf(debug_output, (guint)getpid());
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic pop")
#endif
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

            debug_log_enabled = true;
        }

        if (logfp == NULL)
            logfp = stderr;
    }

    if (!debug_log_enabled)
        return;

    switch (topic) {
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
    case GJS_DEBUG_GERROR:
        prefix = "JS G ERR";
        break;
    case GJS_DEBUG_GINTERFACE:
        prefix = "JS G IFACE";
        break;
    case GJS_DEBUG_GTYPE:
        prefix = "JS GTYPE";
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

    if (print_thread) {
        char *s2 = g_strdup_printf("(thread %p) %s", g_thread_self(), s);
        g_free(s);
        s = s2;
    }

    write_to_stream(logfp, prefix, s);

    g_free(s);
}
