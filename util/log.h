/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef UTIL_LOG_H_
#define UTIL_LOG_H_

#include <config.h>

/* The idea of this is to be able to have one big log file for the entire
 * environment, and grep out what you care about. So each module or app
 * should have its own entry in the enum. Be sure to add new enum entries
 * to the switch in log.c
 */
typedef enum {
    GJS_DEBUG_GI_USAGE,
    GJS_DEBUG_MEMORY,
    GJS_DEBUG_CONTEXT,
    GJS_DEBUG_IMPORTER,
    GJS_DEBUG_NATIVE,
    GJS_DEBUG_CAIRO,
    GJS_DEBUG_KEEP_ALIVE,
    GJS_DEBUG_MAINLOOP,
    GJS_DEBUG_GREPO,
    GJS_DEBUG_GNAMESPACE,
    GJS_DEBUG_GOBJECT,
    GJS_DEBUG_GFUNCTION,
    GJS_DEBUG_GCLOSURE,
    GJS_DEBUG_GBOXED,
    GJS_DEBUG_GENUM,
    GJS_DEBUG_GPARAM,
    GJS_DEBUG_GERROR,
    GJS_DEBUG_GFUNDAMENTAL,
    GJS_DEBUG_GINTERFACE,
    GJS_DEBUG_GTYPE,
    GJS_DEBUG_LAST,
} GjsDebugTopic;

/* These defines are because we have some pretty expensive and
 * extremely verbose debug output in certain areas, that's useful
 * sometimes, but just too much to compile in by default. The areas
 * tend to be broader and less focused than the ones represented by
 * GjsDebugTopic.
 *
 * Don't use these special "disabled by default" log macros to print
 * anything that's an abnormal or error situation.
 *
 * Don't use them for one-time events, either. They are for routine
 * stuff that happens over and over and would deluge the logs, so
 * should be off by default.
 */

/* Whether to be verbose about JavaScript property access and resolution */
#ifndef GJS_VERBOSE_ENABLE_PROPS
#define GJS_VERBOSE_ENABLE_PROPS 0
#endif

/* Whether to be verbose about JavaScript function arg and closure marshaling */
#ifndef GJS_VERBOSE_ENABLE_MARSHAL
#define GJS_VERBOSE_ENABLE_MARSHAL 0
#endif

/* Whether to be verbose about constructing, destroying, and gc-rooting
 * various kinds of JavaScript thingy
 */
#ifndef GJS_VERBOSE_ENABLE_LIFECYCLE
#define GJS_VERBOSE_ENABLE_LIFECYCLE 0
#endif

/* Whether to log all gobject-introspection types and methods we use
 */
#ifndef GJS_VERBOSE_ENABLE_GI_USAGE
#define GJS_VERBOSE_ENABLE_GI_USAGE 0
#endif

/* Whether to log all callback GClosure debugging (finalizing, invalidating etc)
 */
#ifndef GJS_VERBOSE_ENABLE_GCLOSURE
#define GJS_VERBOSE_ENABLE_GCLOSURE 0
#endif

/* Whether to log all GObject signal debugging
 */
#ifndef GJS_VERBOSE_ENABLE_GSIGNAL
#define GJS_VERBOSE_ENABLE_GSIGNAL 0
#endif

#if GJS_VERBOSE_ENABLE_PROPS
#    define GJS_USED_VERBOSE_PROPS
#    define gjs_debug_jsprop(topic, ...)   \
        do {                               \
            gjs_debug(topic, __VA_ARGS__); \
        } while (0)
#else
#    define GJS_USED_VERBOSE_PROPS [[maybe_unused]]
#    define gjs_debug_jsprop(topic, ...) ((void)0)
#endif

#if GJS_VERBOSE_ENABLE_MARSHAL
#    define GJS_USED_VERBOSE_MARSHAL
#    define gjs_debug_marshal(topic, ...)  \
        do {                               \
            gjs_debug(topic, __VA_ARGS__); \
        } while (0)
#else
#    define GJS_USED_VERBOSE_MARSHAL [[maybe_unused]]
#    define gjs_debug_marshal(topic, ...) ((void)0)
#endif

#if GJS_VERBOSE_ENABLE_LIFECYCLE
#    define GJS_USED_VERBOSE_LIFECYCLE
#    define gjs_debug_lifecycle(topic, ...) \
        do {                                \
            gjs_debug(topic, __VA_ARGS__);  \
        } while (0)
#else
#    define GJS_USED_VERBOSE_LIFECYCLE [[maybe_unused]]
#    define gjs_debug_lifecycle(topic, ...) ((void)0)
#endif

#if GJS_VERBOSE_ENABLE_GI_USAGE
#    define GJS_USED_VERBOSE_GI_USAGE
#    define gjs_debug_gi_usage(...)                     \
        do {                                            \
            gjs_debug(GJS_DEBUG_GI_USAGE, __VA_ARGS__); \
        } while (0)
#else
#    define GJS_USED_VERBOSE_GI_USAGE [[maybe_unused]]
#    define gjs_debug_gi_usage(...) ((void)0)
#endif

#if GJS_VERBOSE_ENABLE_GCLOSURE
#    define GJS_USED_VERBOSE_GCLOSURE
#    define gjs_debug_closure(...)                      \
        do {                                            \
            gjs_debug(GJS_DEBUG_GCLOSURE, __VA_ARGS__); \
        } while (0)
#else
#    define GJS_USED_VERBOSE_GCLOSURE [[maybe_unused]]
#    define gjs_debug_closure(...) ((void)0)
#endif

#if GJS_VERBOSE_ENABLE_GSIGNAL
#    define GJS_USED_VERBOSE_GSIGNAL
#    define gjs_debug_gsignal(...)                     \
        do {                                           \
            gjs_debug(GJS_DEBUG_GOBJECT, __VA_ARGS__); \
        } while (0)
#else
#    define GJS_USED_VERBOSE_GSIGNAL [[maybe_unused]]
#    define gjs_debug_gsignal(...) ((void)0)
#endif

void gjs_log_init();
void gjs_log_cleanup();

[[gnu::format(printf, 2, 3)]] void gjs_debug(GjsDebugTopic topic,
                                             const char* format, ...);

#endif  // UTIL_LOG_H_
