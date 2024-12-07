/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

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

#include <array>
#include <atomic>  // for atomic_bool
#include <memory>  // for unique_ptr
#include <string>  // for string

#include <glib.h>

#include "gjs/auto.h"
#include "util/log.h"
#include "util/misc.h"

static std::atomic_bool s_initialized = ATOMIC_VAR_INIT(false);
static bool s_debug_log_enabled = false;
static bool s_print_thread = false;
static std::unique_ptr<LogFile> s_log_file;
static Gjs::AutoPointer<GTimer, GTimer, g_timer_destroy> s_timer;
static std::array<bool, GJS_DEBUG_LAST> s_enabled_topics;

static const char* topic_to_prefix(GjsDebugTopic topic) {
    switch (topic) {
        case GJS_DEBUG_GI_USAGE:
            return "JS GI USE";
        case GJS_DEBUG_MEMORY:
            return "JS MEMORY";
        case GJS_DEBUG_CONTEXT:
            return "JS CTX";
        case GJS_DEBUG_IMPORTER:
            return "JS IMPORT";
        case GJS_DEBUG_NATIVE:
            return "JS NATIVE";
        case GJS_DEBUG_CAIRO:
            return "JS CAIRO";
        case GJS_DEBUG_KEEP_ALIVE:
            return "JS KP ALV";
        case GJS_DEBUG_MAINLOOP:
            return "JS MAINLOOP";
        case GJS_DEBUG_GREPO:
            return "JS G REPO";
        case GJS_DEBUG_GNAMESPACE:
            return "JS G NS";
        case GJS_DEBUG_GOBJECT:
            return "JS G OBJ";
        case GJS_DEBUG_GFUNCTION:
            return "JS G FUNC";
        case GJS_DEBUG_GFUNDAMENTAL:
            return "JS G FNDMTL";
        case GJS_DEBUG_GCLOSURE:
            return "JS G CLSR";
        case GJS_DEBUG_GBOXED:
            return "JS G BXD";
        case GJS_DEBUG_GENUM:
            return "JS G ENUM";
        case GJS_DEBUG_GPARAM:
            return "JS G PRM";
        case GJS_DEBUG_GERROR:
            return "JS G ERR";
        case GJS_DEBUG_GINTERFACE:
            return "JS G IFACE";
        case GJS_DEBUG_GTYPE:
            return "JS GTYPE";
        default:
            return "???";
    }
}

static GjsDebugTopic prefix_to_topic(const char* prefix) {
    for (unsigned i = 0; i < GJS_DEBUG_LAST; i++) {
        auto topic = static_cast<GjsDebugTopic>(i);
        if (g_str_equal(topic_to_prefix(topic), prefix))
            return topic;
    }

    return GJS_DEBUG_LAST;
}

void gjs_log_init() {
    bool expected = false;
    if (!s_initialized.compare_exchange_strong(expected, true))
        return;

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
            Gjs::AutoChar file_name;
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
        s_log_file = std::make_unique<LogFile>(log_file.c_str());
        if (s_log_file->has_error()) {
            fprintf(stderr, "Failed to open log file `%s': %s\n",
                    log_file.c_str(), g_strerror(errno));
        }

        s_debug_log_enabled = true;
    }

    if (!s_log_file)
        s_log_file = std::make_unique<LogFile>(nullptr, stderr);

    if (s_debug_log_enabled) {
        auto* topics = g_getenv("GJS_DEBUG_TOPICS");
        s_enabled_topics.fill(topics == nullptr);
        if (topics) {
            Gjs::AutoStrv prefixes{g_strsplit(topics, ";", -1)};
            for (unsigned i = 0; prefixes[i] != NULL; i++) {
                GjsDebugTopic topic = prefix_to_topic(prefixes[i]);
                if (topic != GJS_DEBUG_LAST)
                    s_enabled_topics[topic] = true;
            }
        }
    }
}

void gjs_log_cleanup() {
    bool expected = true;
    if (!s_initialized.compare_exchange_strong(expected, false))
        return;

    s_timer = nullptr;
    s_enabled_topics.fill(false);
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
    va_list args;
    char *s;

    if (!s_debug_log_enabled || !s_enabled_topics[topic])
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

    write_to_stream(s_log_file->fp(), topic_to_prefix(topic), s);

    g_free(s);
}
