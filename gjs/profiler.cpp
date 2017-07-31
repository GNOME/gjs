/* profiler.cpp
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "jsapi-wrapper.h"
#include <js/ProfilingStack.h>

#include "jsapi-util.h"
#include "profiler.h"
#include "util/sp-capture-writer.h"

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

G_DEFINE_POINTER_TYPE(GjsProfiler, gjs_profiler)

struct _GjsProfiler {
    /* The stack for the JSContext profiler to use for current stack
     * information while executing. We will look into this during our
     * SIGPROF handler.
     */
    js::ProfileEntry stack[1024];

    /* The context being profiled */
    JSContext *cx;

    /* Buffers and writes our sampled stacks */
    SpCaptureWriter *capture;

    /* The filename to write to */
    char *filename;

    /* Our POSIX timer to wakeup SIGPROF */
    timer_t timer;

    /* The depth of @stack. This value may be larger than the
     * number of elements in stack, and so you MUST ensure you
     * don't walk past the end of stack[] when iterating.
     */
    uint32_t stack_depth;

    /* Cached copy of our pid */
    GPid pid;

    /* If we are currently sampling */
    unsigned running : 1;

    /* If we should shutdown */
    unsigned shutdown : 1;
};

static GjsProfiler *current_profiler;

/*
 * gjs_profiler_extract_maps:
 *
 * This function will write the mapped section information to the
 * capture file so that the callgraph builder can generate symbols
 * from the stack addresses provided.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and the profile
 *   should abort.
 */
static bool
gjs_profiler_extract_maps(GjsProfiler *self)
{
    using AutoStrv = std::unique_ptr<char *, decltype(&g_strfreev)>;

    int64_t now = g_get_monotonic_time() * 1000L;

    g_assert(((void) "Profiler must be set up before extracting maps", self));

    GjsAutoChar path = g_strdup_printf("/proc/%jd/maps", intmax_t(getpid()));

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

        if (!sp_capture_writer_add_map(self->capture, now, -1, self->pid, start,
                                       end, offset, inode, file))
            return false;
    }

    return true;
}

/**
 * gjs_profiler_new:
 * @context: The #GjsContext to profile
 *
 * This creates a new profiler for the #JSRuntime. It is important that
 * this structure is freed with gjs_profiler_free() before runtime is
 * destroyed.
 *
 * Call gjs_profiler_start() to enable the profiler, and gjs_profiler_stop()
 * when you have finished.
 *
 * The profiler works by enabling the JS profiler in spidermonkey so that
 * sample information is available. A POSIX timer is used to signal SIGPROF
 * to the process on a regular interval to collect the most recent profile
 * sample and stash it away. It is a programming error to mask SIGPROF from
 * the thread controlling the JS context.
 *
 * Returns: (transfer full): A newly allocated #GjsProfiler
 */
GjsProfiler *
gjs_profiler_new(GjsContext *context)
{
    g_return_val_if_fail(context, nullptr);

    auto cx = static_cast<JSContext *>(gjs_context_get_native_context(context));

    GjsProfiler *self = g_new0(GjsProfiler, 1);
    self->cx = cx;

    self->pid = getpid();

    current_profiler = self;

    return self;
}

/**
 * gjs_profiler_free:
 * @self: A #GjsProfiler
 *
 * Frees a profiler instance and cleans up any allocated data.
 *
 * If the profiler is running, it will be stopped. This may result in blocking
 * to write the contents of the buffer to the underlying file-descriptor.
 */
void
gjs_profiler_free(GjsProfiler *self)
{
    if (!self)
        return;

    if (self->running)
        gjs_profiler_stop (self);

    g_clear_pointer(&self->filename, g_free);
    g_clear_pointer(&self->capture, sp_capture_writer_unref);
    g_free(self);
}

/**
 * gjs_profiler_is_running:
 * @self: A #GjsProfiler
 *
 * Checks if the profiler is currently running. This means that the JS
 * profiler is enabled and POSIX signal timers are registered.
 *
 * Returns: %TRUE if the profiler is active.
 */
gboolean
gjs_profiler_is_running(GjsProfiler *self)
{
    g_return_val_if_fail(self, false);

    return self->running;
}

/* Run from a signal handler */
static inline unsigned
gjs_profiler_get_stack_size(GjsProfiler *self)
{
    g_assert(((void) "Profiler must be set up before getting stack size", self));

    /*
     * Note that stack_depth could be larger than the number of
     * items we have in our stack space. We must protect ourselves
     * against overflowing by discarding anything after that depth
     * of the stack.
     */
    return std::min(self->stack_depth, uint32_t(G_N_ELEMENTS(self->stack)));
}

static void
gjs_profiler_sigprof(int        signum,
                     siginfo_t *info,
                     void      *context)
{
    GjsProfiler *self = current_profiler;

    g_assert(((void) "Profiler must be set up before sending SIGPROF", info));
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

    size_t depth = gjs_profiler_get_stack_size(self);
    if (depth == 0)
        return;

    static_assert(G_N_ELEMENTS(self->stack) < G_MAXUSHORT,
                  "Number of elements in profiler stack should be expressible"
                  "in an unsigned short");

    int64_t now = g_get_monotonic_time() * 1000L;
    SpCaptureAddress *addrs = static_cast<SpCaptureAddress *>(alloca(sizeof *addrs * depth));

    for (size_t ix = 0; ix < depth; ix++) {
        js::ProfileEntry *entry = &self->stack[ix];
        const char *label = entry->label();
        size_t flipped = depth - 1 - ix;

        /*
         * SPSProfiler will put "js::RunScript" on the stack, but it has
         * a stack address of "this", which is not terribly useful since
         * everything will show up as [stack] when building callgraphs.
         */
        if (label)
            addrs[flipped] = sp_capture_writer_add_jitmap(self->capture, label);
        else
            addrs[flipped] = SpCaptureAddress(entry->stackAddress());
    }

    if (!sp_capture_writer_add_sample(self->capture, now, -1, self->pid, addrs, depth))
        gjs_profiler_stop (self);
}

/**
 * gjs_profiler_start:
 * @self: A #GjsProfiler
 *
 * As expected, this starts the GjsProfiler.
 *
 * This will enable the underlying JS profiler and register a POSIX timer to
 * deliver SIGPROF on the configured sampling frequency.
 *
 * To reduce sampling overhead, #GjsProfiler stashes information about the
 * profile to be calculated once the profiler has been disabled. Calling
 * gjs_profile_stop() will result in that delayed work to be completed.
 *
 * You should call gjs_profiler_stop() when the profiler is no longer needed.
 */
void
gjs_profiler_start (GjsProfiler *self)
{
    g_return_if_fail(self);
    g_return_if_fail(!self->capture);

    struct sigaction sa = {{ 0 }};
    struct sigevent sev = {{ 0 }};
    struct itimerspec its = {{ 0 }};
    struct itimerspec old_its;

    if (self->running)
        return;

    GjsAutoChar path = g_strdup(self->filename);
    if (!path) {
        GjsAutoChar filename = g_strdup_printf ("gjs-profile-%jd", intmax_t(getpid()));
        path = g_build_filename(g_get_tmp_dir(), filename.get(), nullptr);
    }

    self->capture = sp_capture_writer_new(path, 0);

    if (!self->capture) {
        g_warning ("Failed to open profile capture");
        return;
    }

    if (!gjs_profiler_extract_maps(self)) {
        g_warning ("Failed to extract proc maps");
        return;
    }

    self->stack_depth = 0;

    /* Setup our signal handler for SIGPROF delivery */
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = gjs_profiler_sigprof;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPROF, &sa, nullptr) == -1) {
        g_warning("Failed to register sigaction handler: %s", g_strerror(errno));
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

    if (timer_create(CLOCK_MONOTONIC, &sev, &self->timer) == -1) {
        g_warning("Failed to create profiler timer: %s", g_strerror(errno));
        g_clear_pointer(&self->capture, sp_capture_writer_unref);
        return;
    }

    /* Calculate sampling interval */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;

    /* Now start this timer */
    if (0 != timer_settime(self->timer, 0, &its, &old_its)) {
        g_warning("Failed to enable profiler timer: %s", g_strerror(errno));
        timer_delete(self->timer);
        g_clear_pointer(&self->capture, sp_capture_writer_unref);
        return;
    }

    self->running = true;

    /* Notify the JS runtime of where to put stack info */
    js::SetContextProfilingStack(self->cx, self->stack, &self->stack_depth,
                                 G_N_ELEMENTS(self->stack));

    /* Start recording stack info */
    js::EnableContextProfilingStack(self->cx, true);

    g_message("Profiler started");
}

/**
 * gjs_profiler_stop:
 * @self: A #GjsProfiler
 *
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
gjs_profiler_stop(GjsProfiler *self)
{
    /* Note: can be called from a signal handler */

    struct itimerspec its = {{ 0 }};

    g_assert(self);

    if (!self->running)
        return;

    if (self == current_profiler)
        current_profiler = nullptr;

    timer_settime(self->timer, 0, &its, nullptr);
    timer_delete(self->timer);

    js::EnableContextProfilingStack(self->cx, false);

    sp_capture_writer_flush(self->capture);

    g_clear_pointer(&self->capture, sp_capture_writer_unref);

    self->stack_depth = 0;
    self->running = false;

    g_message("Profiler stopped");
}

static gboolean
gjs_profiler_sigusr2 (void *unused)
{
    if (current_profiler) {
        if (gjs_profiler_is_running(current_profiler))
          gjs_profiler_stop(current_profiler);
        else
          gjs_profiler_start(current_profiler);
    }

    return G_SOURCE_CONTINUE;
}

/**
 * gjs_profiler_setup_signals:
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
gjs_profiler_setup_signals(void)
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
 * Use this to pass a signal handler caught by another signal handler
 * to a GjsProfiler. This might be needed if you have your own complex
 * signal handling system for which GjsProfiler cannot simply add a
 * SIGUSR2 handler.
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
            gjs_profiler_sigprof(SIGPROF, info, nullptr);
            return TRUE;
        }

        if (info->si_signo == SIGUSR2) {
            gjs_profiler_sigusr2(nullptr);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * gjs_profiler_set_filename:
 * @self: A #GjsProfiler
 * @filename: string containing a filename
 *
 * Set the file to which profiling data is written when the @self is stopped.
 * By default, this is $TMPDIR/gjs-profile-$PID.
 */
void
gjs_profiler_set_filename(GjsProfiler *self,
                          const char  *filename)
{
    g_return_if_fail(self);
    g_return_if_fail(!self->running);

    g_free(self->filename);
    self->filename = g_strdup(filename);
}
