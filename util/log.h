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

#ifndef __GJS_UTIL_LOG_H__
#define __GJS_UTIL_LOG_H__

#include <glib.h>

G_BEGIN_DECLS

/* The idea of this is to be able to have one big log file for the entire
 * environment, and grep out what you care about. So each module or app
 * should have its own entry in the enum. Be sure to add new enum entries
 * to the switch in log.c
 */
typedef enum {
    GJS_DEBUG_STRACE_TIMESTAMP,
    GJS_DEBUG_GI_USAGE,
    GJS_DEBUG_MEMORY,
    GJS_DEBUG_CONTEXT,
    GJS_DEBUG_IMPORTER,
    GJS_DEBUG_NATIVE,
    GJS_DEBUG_KEEP_ALIVE,
    GJS_DEBUG_GREPO,
    GJS_DEBUG_GNAMESPACE,
    GJS_DEBUG_GOBJECT,
    GJS_DEBUG_GFUNCTION,
    GJS_DEBUG_GCLOSURE,
    GJS_DEBUG_GBOXED,
    GJS_DEBUG_GENUM,
    GJS_DEBUG_GPARAM,
    GJS_DEBUG_DATABASE,
    GJS_DEBUG_RESULTSET,
    GJS_DEBUG_WEAK_HASH,
    GJS_DEBUG_MAINLOOP,
    GJS_DEBUG_PROPS,
    GJS_DEBUG_SCOPE,
    GJS_DEBUG_HTTP,
    GJS_DEBUG_BYTE_ARRAY,
    GJS_DEBUG_GERROR,
    GJS_DEBUG_GFUNDAMENTAL,
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
#define gjs_debug_jsprop(topic, format...) \
    do { gjs_debug(topic, format); } while(0)
#else
#define gjs_debug_jsprop(topic, format...)
#endif

#if GJS_VERBOSE_ENABLE_MARSHAL
#define gjs_debug_marshal(topic, format...) \
    do { gjs_debug(topic, format); } while(0)
#else
#define gjs_debug_marshal(topic, format...)
#endif

#if GJS_VERBOSE_ENABLE_LIFECYCLE
#define gjs_debug_lifecycle(topic, format...) \
    do { gjs_debug(topic, format); } while(0)
#else
#define gjs_debug_lifecycle(topic, format...)
#endif

#if GJS_VERBOSE_ENABLE_GI_USAGE
#define gjs_debug_gi_usage(format...) \
    do { gjs_debug(GJS_DEBUG_GI_USAGE, format); } while(0)
#else
#define gjs_debug_gi_usage(format...)
#endif

#if GJS_VERBOSE_ENABLE_GCLOSURE
#define gjs_debug_closure(format...) \
    do { gjs_debug(GJS_DEBUG_GCLOSURE, format); } while(0)
#else
#define gjs_debug_closure(format, ...)
#endif

#if GJS_VERBOSE_ENABLE_GSIGNAL
#define gjs_debug_gsignal(format...) \
    do { gjs_debug(GJS_DEBUG_GOBJECT, format); } while(0)
#else
#define gjs_debug_gsignal(format...)
#endif

void gjs_debug(GjsDebugTopic topic,
               const char   *format,
               ...) G_GNUC_PRINTF (2, 3);

G_END_DECLS

#endif  /* __GJS_UTIL_LOG_H__ */
