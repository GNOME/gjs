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

#include <config.h>

#include "mem.h"
#include "compat.h"
#include <util/log.h>

#define GJS_DEFINE_COUNTER(name)             \
    GjsMemCounter gjs_counter_ ## name = { \
        0, #name                                \
    };


GJS_DEFINE_COUNTER(everything)

GJS_DEFINE_COUNTER(boxed)
GJS_DEFINE_COUNTER(gerror)
GJS_DEFINE_COUNTER(closure)
GJS_DEFINE_COUNTER(database)
GJS_DEFINE_COUNTER(function)
GJS_DEFINE_COUNTER(fundamental)
GJS_DEFINE_COUNTER(importer)
GJS_DEFINE_COUNTER(ns)
GJS_DEFINE_COUNTER(object)
GJS_DEFINE_COUNTER(param)
GJS_DEFINE_COUNTER(repo)
GJS_DEFINE_COUNTER(resultset)
GJS_DEFINE_COUNTER(weakhash)
GJS_DEFINE_COUNTER(interface)

#define GJS_LIST_COUNTER(name) \
    & gjs_counter_ ## name

static GjsMemCounter* counters[] = {
    GJS_LIST_COUNTER(boxed),
    GJS_LIST_COUNTER(gerror),
    GJS_LIST_COUNTER(closure),
    GJS_LIST_COUNTER(database),
    GJS_LIST_COUNTER(function),
    GJS_LIST_COUNTER(fundamental),
    GJS_LIST_COUNTER(importer),
    GJS_LIST_COUNTER(ns),
    GJS_LIST_COUNTER(object),
    GJS_LIST_COUNTER(param),
    GJS_LIST_COUNTER(repo),
    GJS_LIST_COUNTER(resultset),
    GJS_LIST_COUNTER(weakhash),
    GJS_LIST_COUNTER(interface)
};

void
gjs_memory_report(const char *where,
                  gboolean    die_if_leaks)
{
    int i;
    int n_counters;
    int total_objects;

    gjs_debug(GJS_DEBUG_MEMORY,
              "Memory report: %s",
              where);

    n_counters = G_N_ELEMENTS(counters);

    total_objects = 0;
    for (i = 0; i < n_counters; ++i) {
        total_objects += counters[i]->value;
    }

    if (total_objects != GJS_GET_COUNTER(everything)) {
        gjs_debug(GJS_DEBUG_MEMORY,
                  "Object counts don't add up!");
    }

    gjs_debug(GJS_DEBUG_MEMORY,
              "  %d objects currently alive",
              GJS_GET_COUNTER(everything));

    for (i = 0; i < n_counters; ++i) {
        gjs_debug(GJS_DEBUG_MEMORY,
                  "    %12s = %d",
                  counters[i]->name,
                  counters[i]->value);
    }

    if (die_if_leaks && GJS_GET_COUNTER(everything) > 0) {
        g_error("%s: JavaScript objects were leaked.", where);
    }
}
