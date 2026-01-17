/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Endless Mobile, Inc.

#pragma once

#include <config.h>

#include <stdint.h>

#include <chrono>
#include <ratio>  // for nano
#include <string>

#include <js/GCAPI.h>  // for JSFinalizeStatus, JSGCStatus, GCReason
#include <js/ProfilingCategory.h>
#include <js/ProfilingStack.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/context.h"
#include "gjs/profiler.h"
#include "util/misc.h"

#define GJS_PROFILER_DYNAMIC_STRING(cx, str) \
    js::GetContextProfilingStackIfEnabled(cx) ? (str) : ""

class AutoProfilerLabel {
 public:
    explicit AutoProfilerLabel(JSContext* cx, const char* label,
                               const std::string& dynamicString,
                               JS::ProfilingCategoryPair categoryPair =
                                   JS::ProfilingCategoryPair::OTHER,
                               uint32_t flags = 0)
        : m_stack(js::GetContextProfilingStackIfEnabled(cx)) {
        if (m_stack)
            m_stack->pushLabelFrame(label, dynamicString.c_str(), this,
                                    categoryPair, flags);
    }

    ~AutoProfilerLabel() {
        if (m_stack)
            m_stack->pop();
    }

 private:
    ProfilingStack* m_stack;
};

namespace Gjs {
enum GCCounters { GC_HEAP_BYTES, MALLOC_HEAP_BYTES, N_COUNTERS };
}  // namespace Gjs

GjsProfiler* gjs_profiler_new(GjsContext*);
void gjs_profiler_free(GjsProfiler*);

using ProfilerTimePoint =
    std::chrono::time_point<GLib::MonotonicClock, std::chrono::nanoseconds>;
using ProfilerDuration = std::chrono::duration<uint64_t, std::nano>;

void gjs_profiler_add_mark(GjsProfiler*, ProfilerTimePoint, ProfilerDuration,
                           const char* group, const char* name,
                           const char* message);

[[nodiscard]]
bool gjs_profiler_sample_gc_memory_info(
    GjsProfiler*, int64_t gc_counters[Gjs::GCCounters::N_COUNTERS]);

[[nodiscard]] bool gjs_profiler_is_running(GjsProfiler*);

void gjs_profiler_setup_signals(GjsProfiler*, GjsContext*);

void gjs_profiler_set_finalize_status(GjsProfiler*, JSFinalizeStatus);
void gjs_profiler_set_gc_status(GjsProfiler*, JSGCStatus, JS::GCReason);
