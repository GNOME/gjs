/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018 Endless Mobile, Inc.
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

#ifndef GJS_PROFILER_PRIVATE_H
#define GJS_PROFILER_PRIVATE_H

#include <config.h>

#include <sys/time.h>

#include <glib.h>
#include "jsapi-wrapper.h"
#include <js/ProfilingStack.h>

#include "context.h"
#include "profiler.h"
#ifdef ENABLE_PROFILER
# include "util/sp-capture-writer.h"
#endif

class GjsProfiler {
    /* The stack for the JSContext profiler to use for current stack
     * information while executing. We will look into this during our
     * SIGPROF handler.
     */
    js::ProfileEntry m_stack[1024];

    /* The context being profiled */
    JSContext *m_cx;

    /* The filename to write to */
    char *m_filename;

    /* The depth of @stack. This value may be larger than the
     * number of elements in stack, and so you MUST ensure you
     * don't walk past the end of stack[] when iterating.
     */
    uint32_t m_stack_depth;

    /* Cached copy of our pid */
    GPid m_pid;

    /* If we are currently sampling */
    unsigned m_running : 1;

    /*
     * Note that stack_depth could be larger than the number of
     * items we have in our stack space. We must protect ourselves
     * against overflowing by discarding anything after that depth
     * of the stack.
     *
     * Can be run from a signal handler.
     */
    unsigned stack_size(void) const {
        return std::min(m_stack_depth, uint32_t(G_N_ELEMENTS(m_stack)));
    }

public:
    GjsProfiler(GjsContext *context);
    ~GjsProfiler();

    void set_filename(const char *filename);
    void start(void);
    void stop(void);
    bool is_running(void) const { return m_running; }

#ifdef ENABLE_PROFILER
private:
    /* Buffers and writes our sampled stacks */
    SpCaptureWriter *m_capture;

    /* Our POSIX timer to wakeup SIGPROF */
    timer_t m_timer;

    bool extract_maps(void);

public:
    static void sigprof(int        signum,
                        siginfo_t *info,
                        void      *context);

#endif  /* ENABLE_PROFILER */
};

void _gjs_profiler_setup_signals(void);

#endif  /* GJS_PROFILER_PRIVATE_H */
