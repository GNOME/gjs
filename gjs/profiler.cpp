/* profiler.cpp
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#include <algorithm>
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <memory>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "jsapi-wrapper.h"
#include <js/ProfilingStack.h>

#include "jsapi-util.h"
#include "profiler-private.h"
#ifdef ENABLE_PROFILER
# include "util/sp-capture-writer.h"
#endif

/*
 * This is mostly non-exciting code wrapping the builtin Profiler in
 * mozjs. In particular, the profiler consumer is required to "bring your
 * own sampler".  We do the very non-surprising thing of using POSIX
 * timers to deliver SIGPROF to the thread containing the JSContext.
 *
 * However, we do use a Linux'ism that allows us to deliver the signal
 * to only a single thread. Doing this in a generic fashion would
 * require thread-registration so that we can mask SIGPROF from all
 * threads execpt the JS thread. The gecko engine uses tgkill() to do
 * this with a secondary thread instead of using POSIX timers. We could
 * do this too, but it would still be Linux-only.
 *
 * Another option might be to use pthread_kill() and a secondary thread
 * to perform the notification.
 *
 * From within the signal handler, we process the current stack as
 * delivered to us from the JSContext. Any pointer data that comes from
 * the runtime has to be copied, so we keep our own dedup'd string
 * pointers for JavaScript file/line information. Non-JS instruction
 * pointers are just fine, as they can be resolved by parsing the ELF for
 * the file mapped on disk containing that address.
 *
 * As much of this code has to run from signal handlers, it is very
 * important that we don't use anything that can malloc() or lock, or
 * deadlocks are very likely. Most of GjsProfilerCapture is signal-safe.
 */

#define SAMPLES_PER_SEC G_GUINT64_CONSTANT(1000)
#define NSEC_PER_SEC G_GUINT64_CONSTANT(1000000000)

static GjsProfiler *current_profiler;

#ifdef ENABLE_PROFILER
/*
 * This function will write the mapped section information to the
 * capture file so that the callgraph builder can generate symbols
 * from the stack addresses provided.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and the profile
 *   should abort.
 */
bool
GjsProfiler::extract_maps(void)
{
    using AutoStrv = std::unique_ptr<char *, decltype(&g_strfreev)>;

    int64_t now = g_get_monotonic_time() * 1000L;

    GjsAutoChar path = g_strdup_printf("/proc/%jd/maps", intmax_t(m_pid));

    char *content_tmp;
    size_t len;
    if (!g_file_get_contents(path, &content_tmp, &len, nullptr))
      return false;
    GjsAutoChar content = content_tmp;

    AutoStrv lines(g_strsplit(content, "\n", 0), g_strfreev);

    for (size_t ix = 0; lines.get()[ix]; ix++) {
        char file[256];
        unsigned long start;
        unsigned long end;
        unsigned long offset;
        unsigned long inode;

        file[sizeof file - 1] = '\0';

        int r = sscanf(lines.get()[ix], "%lx-%lx %*15s %lx %*x:%*x %lu %255s",
                       &start, &end, &offset, &inode, file);
        if (r != 5)
            continue;

        if (strcmp("[vdso]", file) == 0) {
            offset = 0;
            inode = 0;
        }

        if (!sp_capture_writer_add_map(m_capture, now, -1, m_pid, start,
                                       end, offset, inode, file))
            return false;
    }

    return true;
}
#endif  /* ENABLE_PROFILER */

/*
 * @context: The #GjsContext to profile
 *
 * This creates a new profiler for the #JSContext. It is important that
 * this instance is freed before the context is destroyed.
 *
 * Call start() to enable the profiler, and stop() when you have finished.
 *
 * The profiler works by enabling the JS profiler in spidermonkey so that
 * sample information is available. A POSIX timer is used to signal SIGPROF
 * to the process on a regular interval to collect the most recent profile
 * sample and stash it away. It is a programming error to mask SIGPROF from
 * the thread controlling the JS context.
 */
GjsProfiler::GjsProfiler(GjsContext *context)
  : m_filename(nullptr), m_stack_depth(0), m_running(false)
#ifdef ENABLE_PROFILER
  , m_capture(nullptr)
#endif
{
    g_assert(context);
    g_assert(((void)"You can ony create one profiler at a time.",
              !current_profiler));

    m_cx = static_cast<JSContext *>(gjs_context_get_native_context(context));
    m_pid = getpid();

    current_profiler = this;
}

/*
 * Frees a profiler instance and cleans up any allocated data.
 *
 * If the profiler is running, it will be stopped. This may result in blocking
 * to write the contents of the buffer to the underlying file-descriptor.
 */
GjsProfiler::~GjsProfiler()
{
    if (m_running)
        stop();

    current_profiler = nullptr;

    g_clear_pointer(&m_filename, g_free);
#ifdef ENABLE_PROFILER
    g_clear_pointer(&m_capture, sp_capture_writer_unref);
#endif
}

#ifdef ENABLE_PROFILER

void
GjsProfiler::sigprof(int        signum,
                     siginfo_t *info,
                     void      *context)
{
    GjsProfiler *self = current_profiler;

    g_assert(((void) "SIGPROF handler called with invalid signal info", info));
    g_assert(((void) "SIGPROF handler called with other signal",
              info->si_signo == SIGPROF));

    /*
     * NOTE:
     *
     * This is the SIGPROF signal handler. Everything done in this thread
     * needs to be things that are safe to do in a signal handler. One thing
     * that is not okay to do, is *malloc*.
     */

    if (!self || info->si_code != SI_TIMER)
        return;

    size_t depth = self->stack_size();
    if (depth == 0)
        return;

    static_assert(G_N_ELEMENTS(self->m_stack) < G_MAXUSHORT,
                  "Number of elements in profiler stack should be expressible"
                  "in an unsigned short");

    int64_t now = g_get_monotonic_time() * 1000L;

    /* NOTE: cppcheck warns that alloca() is not recommended since it can
     * easily overflow the stack; however, dynamic allocation is not an option
     * here since we are in a signal handler.
     * Another option would be to always allocate G_N_ELEMENTS(self->stack),
     * but that is by definition at least as large of an allocation and
     * therefore is more likely to overflow.
     */
    // cppcheck-suppress allocaCalled
    SpCaptureAddress *addrs = static_cast<SpCaptureAddress *>(alloca(sizeof *addrs * depth));

    for (size_t ix = 0; ix < depth; ix++) {
        js::ProfileEntry& entry = self->m_stack[ix];
        const char *label = entry.label();
        size_t flipped = depth - 1 - ix;

        /*
         * SPSProfiler will put "js::RunScript" on the stack, but it has
         * a stack address of "this", which is not terribly useful since
         * everything will show up as [stack] when building callgraphs.
         */
        if (label)
            addrs[flipped] = sp_capture_writer_add_jitmap(self->m_capture, label);
        else
            addrs[flipped] = SpCaptureAddress(entry.stackAddress());
    }

    if (!sp_capture_writer_add_sample(self->m_capture, now, -1, self->m_pid,
                                      addrs, depth))
        self->stop();
}

/*
 * As expected, this starts the GjsProfiler.
 *
 * This will enable the underlying JS profiler and register a POSIX timer to
 * deliver SIGPROF on the configured sampling frequency.
 *
 * To reduce sampling overhead, #GjsProfiler stashes information about the
 * profile to be calculated once the profiler has been disabled. Calling
 * stop() will result in that delayed work to be completed.
 *
 * You should call stop() when the profiler is no longer needed.
 */
void
GjsProfiler::start(void)
{
    g_return_if_fail(!m_capture);

    struct sigaction sa = {{ 0 }};
    struct sigevent sev = {{ 0 }};
    struct itimerspec its = {{ 0 }};
    struct itimerspec old_its;

    if (m_running)
        return;

    GjsAutoChar path = g_strdup(m_filename);
    if (!path)
        path = g_strdup_printf("gjs-%jd.syscap", intmax_t(m_pid));

    m_capture = sp_capture_writer_new(path, 0);

    if (!m_capture) {
        g_warning("Failed to open profile capture");
        return;
    }

    if (!extract_maps()) {
        g_warning("Failed to extract proc maps");
        g_clear_pointer(&m_capture, sp_capture_writer_unref);
        return;
    }

    m_stack_depth = 0;

    /* Setup our signal handler for SIGPROF delivery */
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = &GjsProfiler::sigprof;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPROF, &sa, nullptr) == -1) {
        g_warning("Failed to register sigaction handler: %s", g_strerror(errno));
        g_clear_pointer(&m_capture, sp_capture_writer_unref);
        return;
    }

    /*
     * Create our SIGPROF timer
     *
     * We want to receive a SIGPROF signal on the JS thread using our
     * configured sampling frequency. Instead of allowing any thread to be
     * notified, we set the _tid value to ensure that only our thread gets
     * delivery of the signal. This feature is generally just for
     * threading implementations, but it works for us as well and ensures
     * that the thread is blocked while we capture the stack.
     */
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
    sev._sigev_un._tid = syscall(__NR_gettid);

    if (timer_create(CLOCK_MONOTONIC, &sev, &m_timer) == -1) {
        g_warning("Failed to create profiler timer: %s", g_strerror(errno));
        g_clear_pointer(&m_capture, sp_capture_writer_unref);
        return;
    }

    /* Calculate sampling interval */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;

    /* Now start this timer */
    if (timer_settime(m_timer, 0, &its, &old_its) != 0) {
        g_warning("Failed to enable profiler timer: %s", g_strerror(errno));
        timer_delete(m_timer);
        g_clear_pointer(&m_capture, sp_capture_writer_unref);
        return;
    }

    m_running = true;

    /* Notify the JS runtime of where to put stack info */
    js::SetContextProfilingStack(m_cx, m_stack, &m_stack_depth,
                                 G_N_ELEMENTS(m_stack));

    /* Start recording stack info */
    js::EnableContextProfilingStack(m_cx, true);

    g_message("Profiler started");
}

/*
 * Stops a currently running #GjsProfiler. If the profiler is not running,
 * this function will do nothing.
 *
 * Some work may be delayed until the end of the capture. Such delayed work
 * includes flushing the resulting samples and file location information to
 * disk.
 *
 * This may block while writing to disk. Generally, the writes are delivered
 * to a tmpfs device, and are therefore negligible.
 */
void
GjsProfiler::stop(void)
{
    /* Note: can be called from a signal handler */

    struct itimerspec its = {{ 0 }};

    if (!m_running)
        return;

    timer_settime(m_timer, 0, &its, nullptr);
    timer_delete(m_timer);

    js::EnableContextProfilingStack(m_cx, false);
    js::SetContextProfilingStack(m_cx, nullptr, nullptr, 0);

    sp_capture_writer_flush(m_capture);

    g_clear_pointer(&m_capture, sp_capture_writer_unref);

    m_stack_depth = 0;
    m_running = false;

    g_message("Profiler stopped");
}

static gboolean
gjs_profiler_sigusr2(void *unused)
{
    if (current_profiler) {
        if (current_profiler->is_running())
            current_profiler->stop();
        else
            current_profiler->start();
    }

    return G_SOURCE_CONTINUE;
}

/*
 * _gjs_profiler_setup_signals:
 *
 * If you want to simply allow profiling of your process with minimal
 * fuss, simply call gjs_profiler_setup_signals(). This will allow
 * enabling and disabling the profiler with SIGUSR2. You must call
 * this from main() immediately when your program starts and must not
 * block SIGUSR2 from your signal mask.
 *
 * If this is not sufficient, use gjs_profiler_chain_signal() from your
 * own signal handler to pass the signal to a GjsProfiler.
 */
void
_gjs_profiler_setup_signals(void)
{
    static bool initialized = false;

    if (!initialized) {
        initialized = true;
        g_unix_signal_add(SIGUSR2, gjs_profiler_sigusr2, nullptr);
    }
}

/**
 * gjs_profiler_chain_signal:
 * @info: #siginfo_t passed in to signal handler
 *
 * Use this to pass a signal info caught by another signal handler to a
 * GjsProfiler. This might be needed if you have your own complex signal
 * handling system for which GjsProfiler cannot simply add a SIGUSR2 handler.
 *
 * This function should only be called from the JS thread.
 *
 * Returns: %TRUE if the signal was handled.
 */
gboolean
gjs_profiler_chain_signal(siginfo_t *info)
{
    if (info) {
        if (info->si_signo == SIGPROF) {
            GjsProfiler::sigprof(SIGPROF, info, nullptr);
            return true;
        }

        if (info->si_signo == SIGUSR2) {
            gjs_profiler_sigusr2(nullptr);
            return true;
        }
    }

    return false;
}

#else  /* ENABLE_PROFILER */

void
GjsProfiler::start(void)
{
    if (m_running)
        return;

    m_running = true;

    g_message("Profiler is disabled. Recompile with --enable-profiler to use.");
}

void
GjsProfiler::stop(void)
{
    if (!m_running)
        return;

    m_running = false;
}

void
_gjs_profiler_setup_signals(void)
{
    g_message("Profiler is disabled. Not setting up signals.");
}

gboolean
gjs_profiler_chain_signal(siginfo_t *info)
{
    return false;
}

#endif  /* ENABLE_PROFILER */

/*
 * @filename: string containing a filename
 *
 * Set the file to which profiling data is written when the profiler is stopped.
 * By default, this is `gjs-$PID.syscap` in the current directory.
 */
void
GjsProfiler::set_filename(const char *filename)
{
    g_return_if_fail(!m_running);

    g_free(m_filename);
    m_filename = g_strdup(filename);
}
