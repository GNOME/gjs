/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Endless Mobile, Inc.

#ifndef GJS_PROFILER_PRIVATE_H_
#define GJS_PROFILER_PRIVATE_H_

#include <stdint.h>

#include <js/ProfilingCategory.h>
#include <js/ProfilingStack.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/context.h"
#include "gjs/profiler.h"

class AutoProfilerLabel {
 public:
    explicit inline AutoProfilerLabel(JSContext* cx, const char* label,
                                      const char* dynamicString,
                                      JS::ProfilingCategoryPair categoryPair =
                                          JS::ProfilingCategoryPair::OTHER,
                                      uint32_t flags = 0)
        : m_stack(js::GetContextProfilingStackIfEnabled(cx)) {
        if (m_stack)
            m_stack->pushLabelFrame(label, dynamicString, this, categoryPair,
                                    flags);
    }

    inline ~AutoProfilerLabel() {
        if (m_stack)
            m_stack->pop();
    }

 private:
    ProfilingStack* m_stack;
};

namespace Gjs {
enum GCCounters { GC_HEAP_BYTES, MALLOC_HEAP_BYTES, N_COUNTERS };
}  // namespace Gjs

GjsProfiler *_gjs_profiler_new(GjsContext *context);
void _gjs_profiler_free(GjsProfiler *self);

void _gjs_profiler_add_mark(GjsProfiler* self, int64_t time, int64_t duration,
                            const char* group, const char* name,
                            const char* message);

[[nodiscard]] bool _gjs_profiler_sample_gc_memory_info(
    GjsProfiler* self, int64_t gc_counters[Gjs::GCCounters::N_COUNTERS]);

[[nodiscard]] bool _gjs_profiler_is_running(GjsProfiler* self);

void _gjs_profiler_setup_signals(GjsProfiler *self, GjsContext *context);

#endif  // GJS_PROFILER_PRIVATE_H_
