/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>

#include <glib.h>

#include "gjs/mem-private.h"  // IWYU pragma: associated
#include "gjs/mem.h"
#include "util/log.h"

namespace Gjs {
namespace Memory {
namespace Counters {
#define GJS_DEFINE_COUNTER(name, ix) Counter name(#name);

GJS_DEFINE_COUNTER(everything, -1)
GJS_FOR_EACH_COUNTER(GJS_DEFINE_COUNTER)
}  // namespace Counters
}  // namespace Memory
}  // namespace Gjs

#define GJS_LIST_COUNTER(name, ix) &Gjs::Memory::Counters::name,

static Gjs::Memory::Counter* counters[] = {
    GJS_FOR_EACH_COUNTER(GJS_LIST_COUNTER)};

void
gjs_memory_report(const char *where,
                  bool        die_if_leaks)
{
    int i;
    int n_counters;
    int64_t total_objects;

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
              "  %" G_GINT64_FORMAT " objects currently alive",
              GJS_GET_COUNTER(everything));

    if (GJS_GET_COUNTER(everything) != 0) {
        for (i = 0; i < n_counters; ++i) {
            gjs_debug(GJS_DEBUG_MEMORY, "    %24s = %" G_GINT64_FORMAT,
                      counters[i]->name, counters[i]->value.load());
        }

        if (die_if_leaks)
            g_error("%s: JavaScript objects were leaked.", where);
    }
}
