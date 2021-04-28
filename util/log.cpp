/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <atomic>  // for atomic_bool
#include <string>  // for string
#include <type_traits>  // for remove_reference<>::type

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>   // for FILE, fprintf, fflush, fopen, fputs, fseek
#include <string.h>  // for strchr, strcmp
#include "gjs/jsapi-util.h"

#ifdef _WIN32
# include <io.h>
# include <process.h>
# ifndef F_OK
#  define F_OK 0
# endif
#else
#    include <unistd.h>  // for getpid
#endif

#include <glib.h>

#include "util/log.h"
#include "util/misc.h"

static std::atomic_bool s_initialized = ATOMIC_VAR_INIT(false);
static bool s_debug_log_enabled = false;
static bool s_print_thread = false;
static const char* s_topics = nullptr;
static FILE* s_logfp = nullptr;
static GjsAutoStrv s_prefixes;
static GjsAutoPointer<GTimer, GTimer, g_timer_destroy> s_timer;

void gjs_log_init() {
    bool expected = false;
    if (!s_initialized.compare_exchange_strong(expected, true))
        return;

    s_topics = g_getenv("GJS_DEBUG_TOPICS");
    if (s_topics)
        s_prefixes = g_strsplit(s_topics, ";", -1);

    if (gjs_environment_variable_is_set("GJS_DEBUG_TIMESTAMP"))
        s_timer = g_timer_new();

    s_print_thread = gjs_environment_variable_is_set("GJS_DEBUG_THREAD");

    const char* debug_output = g_getenv("GJS_DEBUG_OUTPUT");
    if (debug_output && g_str_equal(debug_output, "stderr")) {
        s_debug_log_enabled = true;
    } else if (debug_output) {
        std::string log_file;
        char* c;

        /* Allow debug-%u.log for per-pid logfiles as otherwise log
         * messages from multiple processes can overwrite each other.
         *
         * (printf below should be safe as we check '%u' is the only format
         * string)
         */
        c = strchr(const_cast<char*>(debug_output), '%');
        if (c && c[1] == 'u' && !strchr(c + 1, '%')) {
            GjsAutoChar file_name;
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
            _Pragma("GCC diagnostic push")
                _Pragma("GCC diagnostic ignored \"-Wformat-nonliteral\"")
#endif
                    file_name = g_strdup_printf(debug_output, getpid());
#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
            _Pragma("GCC diagnostic pop")
#endif
                log_file = file_name.get();
        } else {
            log_file = debug_output;
        }

        /* avoid truncating in case we're using shared logfile */
        s_logfp = fopen(log_file.c_str(), "a");
        if (!s_logfp)
            fprintf(stderr, "Failed to open log file `%s': %s\n",
                    log_file.c_str(), g_strerror(errno));

        s_debug_log_enabled = true;
    }

    if (!s_logfp)
        s_logfp = stderr;
}

void gjs_log_cleanup() {
    bool expected = true;
    if (!s_initialized.compare_exchange_strong(expected, false))
        return;

    if (s_logfp && s_logfp != stderr) {
        fclose(s_logfp);
        s_logfp = nullptr;
    }

    s_timer = nullptr;
    s_prefixes = nullptr;
}

/* prefix is allowed if it's in the ;-delimited environment variable
 * GJS_DEBUG_TOPICS or if that variable is not set.
 */
static bool
is_allowed_prefix (const char *prefix)
{
    bool found = false;
    int i;

    if (!s_prefixes)
        return true;

    for (i = 0; s_prefixes[i] != NULL; i++) {
        if (!strcmp(s_prefixes[i], prefix)) {
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
    const char *prefix;
    va_list args;
    char *s;

    if (!s_debug_log_enabled)
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
    case GJS_DEBUG_CAIRO:
        prefix = "JS CAIRO";
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

    if (s_timer) {
        static gdouble previous = 0.0;
        gdouble total = g_timer_elapsed(s_timer, NULL) * 1000.0;
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

    if (s_print_thread) {
        char *s2 = g_strdup_printf("(thread %p) %s", g_thread_self(), s);
        g_free(s);
        s = s2;
    }

    write_to_stream(s_logfp, prefix, s);

    g_free(s);
}
