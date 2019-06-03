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
#include <errno.h>
#include <memory>
#include <signal.h>
#include <sys/types.h>

#include "jsapi-wrapper.h"
#include <js/ProfilingStack.h>

#include "context.h"
#include "jsapi-util.h"
#include "profiler-private.h"

#ifdef ENABLE_PROFILER
# include <alloca.h>
#    ifdef HAVE_SYS_SYSCALL_H
#        include <sys/syscall.h>
#    endif
#    ifdef HAVE_UNISTD_H
#        include <unistd.h>
#    endif
#    ifdef G_OS_UNIX
#        include <glib-unix.h>
#    endif
#    include <sysprof-capture.h>
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

G_DEFINE_POINTER_TYPE(GjsProfiler, gjs_profiler)

struct _GjsProfiler {
#ifdef ENABLE_PROFILER
    /* The stack for the JSContext profiler to use for current stack
     * information while executing. We will look into this during our
     * SIGPROF handler.
     */
    PseudoStack stack;

    /* The context being profiled */
    JSContext *cx;

    /* Buffers and writes our sampled stacks */
    SysprofCaptureWriter* capture;
#endif  /* ENABLE_PROFILER */

    /* The filename to write to */
    char *filename;

    /* An FD to capture to */
    int fd;

#ifdef ENABLE_PROFILER
    /* Our POSIX timer to wakeup SIGPROF */
    timer_t timer;

    /* Cached copy of our pid */
    GPid pid;

    /* GLib signal handler ID for SIGUSR2 */
    unsigned sigusr2_id;
#endif  /* ENABLE_PROFILER */

    /* If we are currently sampling */
    unsigned running : 1;
};

static GjsContext *profiling_context;

#ifdef ENABLE_PROFILER
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
GJS_USE
static bool
gjs_profiler_extract_maps(GjsProfiler *self)
{
    int64_t now = g_get_monotonic_time() * 1000L;

    g_assert(((void) "Profiler must be set up before extracting maps", self));

    GjsAutoChar path = g_strdup_printf("/proc/%jd/maps", intmax_t(self->pid));

    char *content_tmp;
    size_t len;
    if (!g_file_get_contents(path, &content_tmp, &len, nullptr))
      return false;
    GjsAutoChar content = content_tmp;

    GjsAutoStrv lines = g_strsplit(content, "\n", 0);

    for (size_t ix = 0; lines[ix]; ix++) {
        char file[256];
        unsigned long start;
        unsigned long end;
        unsigned long offset;
        unsigned long inode;

        file[sizeof file - 1] = '\0';

        int r = sscanf(lines[ix], "%lx-%lx %*15s %lx %*x:%*x %lu %255s",
                       &start, &end, &offset, &inode, file);
        if (r != 5)
            continue;

        if (strcmp("[vdso]", file) == 0) {
            offset = 0;
            inode = 0;
        }

        if (!sysprof_capture_writer_add_map(self->capture, now, -1, self->pid,
                                            start, end, offset, inode, file))
            return false;
    }

    return true;
}
#endif  /* ENABLE_PROFILER */

/*
 * _gjs_profiler_new:
 * @context: The #GjsContext to profile
 *
 * This creates a new profiler for the #JSContext. It is important that
 * this instance is freed with _gjs_profiler_free() before the context is
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
 * If another #GjsContext already has a profiler, or @context already has one,
 * then returns %NULL instead.
 *
 * Returns: (transfer full) (nullable): A newly allocated #GjsProfiler
 */
GjsProfiler *
_gjs_profiler_new(GjsContext *context)
{
    g_return_val_if_fail(context, nullptr);

    if (profiling_context == context) {
        g_critical("You can only create one profiler at a time.");
        return nullptr;
    }

    if (profiling_context) {
        g_message("Not going to profile GjsContext %p; you can only profile "
                  "one context at a time.", context);
        return nullptr;
    }

    GjsProfiler *self = g_new0(GjsProfiler, 1);

#ifdef ENABLE_PROFILER
    self->cx = static_cast<JSContext *>(gjs_context_get_native_context(context));
    self->pid = getpid();
#endif
    self->fd = -1;

    profiling_context = context;

    return self;
}

/*
 * _gjs_profiler_free:
 * @self: A #GjsProfiler
 *
 * Frees a profiler instance and cleans up any allocated data.
 *
 * If the profiler is running, it will be stopped. This may result in blocking
 * to write the contents of the buffer to the underlying file-descriptor.
 */
void
_gjs_profiler_free(GjsProfiler *self)
{
    if (!self)
        return;

    if (self->running)
        gjs_profiler_stop(self);

    profiling_context = nullptr;

    g_clear_pointer(&self->filename, g_free);
#ifdef ENABLE_PROFILER
    g_clear_pointer(&self->capture, sysprof_capture_writer_unref);

    if (self->fd != -1)
        close(self->fd);
#endif
    g_free(self);
}

/*
 * _gjs_profiler_is_running:
 * @self: A #GjsProfiler
 *
 * Checks if the profiler is currently running. This means that the JS
 * profiler is enabled and POSIX signal timers are registered.
 *
 * Returns: %TRUE if the profiler is active.
 */
bool
_gjs_profiler_is_running(GjsProfiler *self)
{
    g_return_val_if_fail(self, false);

    return self->running;
}

#ifdef ENABLE_PROFILER

static void
gjs_profiler_sigprof(int        signum,
                     siginfo_t *info,
                     void      *unused)
{
    GjsProfiler *self = gjs_context_get_profiler(profiling_context);

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

    uint32_t depth = self->stack.stackSize();
    if (depth == 0)
        return;

    int64_t now = g_get_monotonic_time() * 1000L;

    /* NOTE: cppcheck warns that alloca() is not recommended since it can
     * easily overflow the stack; however, dynamic allocation is not an option
     * here since we are in a signal handler.
     */
    // cppcheck-suppress allocaCalled
    SysprofCaptureAddress* addrs =
        static_cast<SysprofCaptureAddress*>(alloca(sizeof *addrs * depth));

    for (uint32_t ix = 0; ix < depth; ix++) {
        js::ProfileEntry& entry = self->stack.entries[ix];
        const char *label = entry.label();
        const char *dynamic_string = entry.dynamicString();
        uint32_t flipped = depth - 1 - ix;
        size_t label_length = strlen(label);

        /*
         * 512 is an arbitrarily large size, very likely to be enough to
         * hold the final string.
         */
        char final_string[512] = { 0, };
        char *position = final_string;
        size_t available_length = sizeof (final_string) - 1;

        if (label_length > 0) {
            label_length = MIN(label_length, available_length);

            /* Start copying the label to the final string */
            memcpy(position, label, label_length);
            available_length -= label_length;
            position += label_length;

            /*
             * Add a space in between the label and the dynamic string,
             * if there is one.
             */
            if (dynamic_string && available_length > 0) {
                *position++ = ' ';
                available_length--;
            }
        }

        /* Now append the dynamic string at the end of the final string.
         * The string is cut in case it doesn't fit the remaining space.
         */
        if (dynamic_string) {
            size_t dynamic_string_length = strlen(dynamic_string);

            if (dynamic_string_length > 0) {
                size_t remaining_length = MIN(available_length, dynamic_string_length);
                memcpy(position, dynamic_string, remaining_length);
            }
        }

        /*
         * GeckoProfiler will put "js::RunScript" on the stack, but it has
         * a stack address of "this", which is not terribly useful since
         * everything will show up as [stack] when building callgraphs.
         */
        if (final_string[0] != '\0')
            addrs[flipped] =
                sysprof_capture_writer_add_jitmap(self->capture, final_string);
        else
            addrs[flipped] = SysprofCaptureAddress(entry.stackAddress());
    }

    if (!sysprof_capture_writer_add_sample(self->capture, now, -1, self->pid,
                                           -1, addrs, depth))
        gjs_profiler_stop(self);
}

#endif  /* ENABLE_PROFILER */

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
 * gjs_profiler_stop() will result in that delayed work to be completed.
 *
 * You should call gjs_profiler_stop() when the profiler is no longer needed.
 */
void
gjs_profiler_start(GjsProfiler *self)
{
    g_return_if_fail(self);
    if (self->running)
        return;

#ifdef ENABLE_PROFILER

    g_return_if_fail(!self->capture);

    struct sigaction sa = { 0 };
    struct sigevent sev = { 0 };
    struct itimerspec its = { 0 };
    struct itimerspec old_its;

    if (self->fd != -1) {
        self->capture = sysprof_capture_writer_new_from_fd(self->fd, 0);
        self->fd = -1;
    } else {
        GjsAutoChar path = g_strdup(self->filename);
        if (!path)
            path = g_strdup_printf("gjs-%jd.syscap", intmax_t(self->pid));

        self->capture = sysprof_capture_writer_new(path, 0);
    }

    if (!self->capture) {
        g_warning("Failed to open profile capture");
        return;
    }

    if (!gjs_profiler_extract_maps(self)) {
        g_warning("Failed to extract proc maps");
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        return;
    }

    /* Setup our signal handler for SIGPROF delivery */
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = gjs_profiler_sigprof;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPROF, &sa, nullptr) == -1) {
        g_warning("Failed to register sigaction handler: %s", g_strerror(errno));
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
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
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        return;
    }

    /* Calculate sampling interval */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = NSEC_PER_SEC / SAMPLES_PER_SEC;

    /* Now start this timer */
    if (timer_settime(self->timer, 0, &its, &old_its) != 0) {
        g_warning("Failed to enable profiler timer: %s", g_strerror(errno));
        timer_delete(self->timer);
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        return;
    }

    self->running = true;

    /* Notify the JS runtime of where to put stack info */
    js::SetContextProfilingStack(self->cx, &self->stack);

    /* Start recording stack info */
    js::EnableContextProfilingStack(self->cx, true);

    g_message("Profiler started");

#else  /* !ENABLE_PROFILER */

    self->running = true;
    g_message("Profiler is disabled. Recompile with --enable-profiler to use.");

#endif  /* ENABLE_PROFILER */
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

    g_assert(self);

    if (!self->running)
        return;

#ifdef ENABLE_PROFILER

    struct itimerspec its = { 0 };
    timer_settime(self->timer, 0, &its, nullptr);
    timer_delete(self->timer);

    js::EnableContextProfilingStack(self->cx, false);
    js::SetContextProfilingStack(self->cx, nullptr);

    sysprof_capture_writer_flush(self->capture);

    g_clear_pointer(&self->capture, sysprof_capture_writer_unref);

    g_message("Profiler stopped");

#endif  /* ENABLE_PROFILER */

    self->running = false;
}

#ifdef ENABLE_PROFILER

static gboolean
gjs_profiler_sigusr2(void *data)
{
    auto context = static_cast<GjsContext *>(data);
    GjsProfiler *current_profiler = gjs_context_get_profiler(context);

    if (current_profiler) {
        if (_gjs_profiler_is_running(current_profiler))
            gjs_profiler_stop(current_profiler);
        else
            gjs_profiler_start(current_profiler);
    }

    return G_SOURCE_CONTINUE;
}

#endif  /* ENABLE_PROFILER */

/*
 * _gjs_profiler_setup_signals:
 * @context: a #GjsContext with a profiler attached
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
_gjs_profiler_setup_signals(GjsProfiler *self,
                            GjsContext  *context)
{
    g_return_if_fail(context == profiling_context);

#ifdef ENABLE_PROFILER

    if (self->sigusr2_id != 0)
        return;

    self->sigusr2_id = g_unix_signal_add(SIGUSR2, gjs_profiler_sigusr2, context);

#else  /* !ENABLE_PROFILER */

    g_message("Profiler is disabled. Not setting up signals.");
    (void)self;

#endif  /* ENABLE_PROFILER */
}

/**
 * gjs_profiler_chain_signal:
 * @context: a #GjsContext with a profiler attached
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
bool
gjs_profiler_chain_signal(GjsContext *context,
                          siginfo_t *info)
{
#ifdef ENABLE_PROFILER

    if (info) {
        if (info->si_signo == SIGPROF) {
            gjs_profiler_sigprof(SIGPROF, info, nullptr);
            return true;
        }

        if (info->si_signo == SIGUSR2) {
            gjs_profiler_sigusr2(context);
            return true;
        }
    }

#else  // !ENABLE_PROFILER

    (void)context;
    (void)info;

#endif  /* ENABLE_PROFILER */

    return false;
}

/**
 * gjs_profiler_set_filename:
 * @self: A #GjsProfiler
 * @filename: string containing a filename
 *
 * Set the file to which profiling data is written when the @self is stopped.
 * By default, this is `gjs-$PID.syscap` in the current directory.
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

void _gjs_profiler_add_mark(GjsProfiler* self, gint64 time_nsec,
                            gint64 duration_nsec, const char* group,
                            const char* name, const char* message) {
    g_return_if_fail(self);
    g_return_if_fail(group);
    g_return_if_fail(name);

#ifdef ENABLE_PROFILER
    if (self->running && self->capture != nullptr) {
        sysprof_capture_writer_add_mark(self->capture, time_nsec, -1, self->pid,
                                        duration_nsec, group, name, message);
    }
#endif
}

void gjs_profiler_set_fd(GjsProfiler* self, int fd) {
    g_return_if_fail(self);
    g_return_if_fail(!self->filename);
    g_return_if_fail(!self->running);

#ifdef ENABLE_PROFILER
    if (self->fd != fd) {
        if (self->fd != -1)
            close(self->fd);
        self->fd = fd;
    }
#endif
}
