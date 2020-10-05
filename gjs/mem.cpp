/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <glib.h>

#include "gjs/mem-private.h"
#include "gjs/mem.h"
#include "util/log.h"

#define GJS_DEFINE_COUNTER(name)             \
    GjsMemCounter gjs_counter_ ## name = { \
        0, #name                                \
    };


GJS_DEFINE_COUNTER(everything)
GJS_FOR_EACH_COUNTER(GJS_DEFINE_COUNTER)

#define GJS_LIST_COUNTER(name) &gjs_counter_##name,

static GjsMemCounter* counters[] = {GJS_FOR_EACH_COUNTER(GJS_LIST_COUNTER)};

void
gjs_memory_report(const char *where,
                  bool        die_if_leaks)
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

    if (GJS_GET_COUNTER(everything) != 0) {
        for (i = 0; i < n_counters; ++i) {
            gjs_debug(GJS_DEBUG_MEMORY, "    %24s = %d", counters[i]->name,
                      counters[i]->value);
        }

        if (die_if_leaks)
            g_error("%s: JavaScript objects were leaked.", where);
    }
}
