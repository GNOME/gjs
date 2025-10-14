// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Christian Hergert <christian@hergert.me>

#include <config.h>  // for ENABLE_PROFILER, HAVE_SYS_SYSCALL_H, HAVE_UNISTD_H

#ifdef HAVE_SIGNAL_H
#    include <signal.h>  // for siginfo_t, sigevent, sigaction, SIGPROF, ...
#endif

#ifdef ENABLE_PROFILER
// IWYU has a weird loop where if this is present, it asks for it to be removed,
// and if absent, asks for it to be added
#    include <alloca.h>  // IWYU pragma: keep
#    include <errno.h>
#    include <stdint.h>
#    include <stdio.h>        // for sscanf
#    include <string.h>       // for memcpy, strlen
#    include <sys/syscall.h>  // for __NR_gettid
#    include <sys/types.h>    // for timer_t
#    include <time.h>         // for size_t, CLOCK_MONOTONIC, itimerspec, ...
#    ifdef HAVE_UNISTD_H
#        include <unistd.h>  // for getpid, syscall
#    endif
#    include <array>
#endif

#include <glib-object.h>
#include <glib.h>

#ifdef ENABLE_PROFILER
#    ifdef G_OS_UNIX
#        include <glib-unix.h>
#    endif
#    include <sysprof-capture.h>
#endif

#include <js/GCAPI.h>           // for JSFinalizeStatus, JSGCStatus, GCReason
#include <js/ProfilingStack.h>  // for EnableContextProfilingStack, ...
#include <js/TypeDecls.h>
#include <mozilla/Atomics.h>  // for ProfilingStack operators

#include "gjs/auto.h"
#include "gjs/context.h"
#include "gjs/jsapi-util.h"  // for gjs_explain_gc_reason
#include "gjs/mem-private.h"
#include "gjs/profiler-private.h"  // IWYU pragma: associated
#include "gjs/profiler.h"

#define FLUSH_DELAY_SECONDS 3

/*
 * This is mostly non-exciting code wrapping the builtin Profiler in
 * mozjs. In particular, the profiler consumer is required to "bring your
 * own sampler".  We do the very non-surprising thing of using POSIX
 * timers to deliver SIGPROF to the thread containing the JSContext.
 *
 * However, we do use a Linux'ism that allows us to deliver the signal
 * to only a single thread. Doing this in a generic fashion would
 * require thread-registration so that we can mask SIGPROF from all
 * threads except the JS thread. The gecko engine uses tgkill() to do
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
    ProfilingStack stack;

    /* The context being profiled */
    JSContext *cx;

    /* Buffers and writes our sampled stacks */
    SysprofCaptureWriter* capture;
    GSource* periodic_flush;

    SysprofCaptureWriter* target_capture;

    // Cache previous values of counters so that we don't overrun the output
    // with counters that don't change very often
    uint64_t last_counter_values[GJS_N_COUNTERS];
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

    /* Timing information */
    int64_t gc_begin_time;
    int64_t sweep_begin_time;
    int64_t group_sweep_begin_time;
    const char* gc_reason;  // statically allocated

    /* GLib signal handler ID for SIGUSR2 */
    unsigned sigusr2_id;
    unsigned counter_base;  // index of first GObject memory counter
    unsigned gc_counter_base;  // index of first GC stats counter
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
[[nodiscard]] static bool gjs_profiler_extract_maps(GjsProfiler* self) {
    int64_t now = g_get_monotonic_time() * 1000L;

    g_assert(((void) "Profiler must be set up before extracting maps", self));

    Gjs::AutoChar path{g_strdup_printf("/proc/%jd/maps", intmax_t(self->pid))};

    Gjs::AutoChar content;
    size_t len;
    if (!g_file_get_contents(path, content.out(), &len, nullptr))
        return false;

    Gjs::AutoStrv lines{g_strsplit(content, "\n", 0)};

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

static void setup_counter_helper(SysprofCaptureCounter* counter,
                                 const char* counter_name,
                                 unsigned counter_base, size_t ix) {
    g_snprintf(counter->category, sizeof counter->category, "GJS");
    g_snprintf(counter->name, sizeof counter->name, "%s", counter_name);
    g_snprintf(counter->description, sizeof counter->description, "%s",
               GJS_COUNTER_DESCRIPTIONS[ix]);
    counter->id = uint32_t(counter_base + ix);
    counter->type = SYSPROF_CAPTURE_COUNTER_INT64;
    counter->value.v64 = 0;
}

[[nodiscard]] static bool gjs_profiler_define_counters(GjsProfiler* self) {
    int64_t now = g_get_monotonic_time() * 1000L;

    g_assert(self && "Profiler must be set up before defining counters");

    std::array<SysprofCaptureCounter, GJS_N_COUNTERS> counters;
    self->counter_base =
        sysprof_capture_writer_request_counter(self->capture, GJS_N_COUNTERS);

#    define SETUP_COUNTER(counter_name, ix)                                    \
        setup_counter_helper(&counters[ix], #counter_name, self->counter_base, \
                             ix);
    GJS_FOR_EACH_COUNTER(SETUP_COUNTER);
#    undef SETUP_COUNTER

    if (!sysprof_capture_writer_define_counters(
            self->capture, now, -1, self->pid, counters.data(), GJS_N_COUNTERS))
        return false;

    std::array<SysprofCaptureCounter, Gjs::GCCounters::N_COUNTERS> gc_counters;
    self->gc_counter_base = sysprof_capture_writer_request_counter(
        self->capture, Gjs::GCCounters::N_COUNTERS);

    constexpr size_t category_size = sizeof gc_counters[0].category;
    constexpr size_t name_size = sizeof gc_counters[0].name;
    constexpr size_t description_size = sizeof gc_counters[0].description;

    for (size_t ix = 0; ix < Gjs::GCCounters::N_COUNTERS; ix++) {
        g_snprintf(gc_counters[ix].category, category_size, "GJS");
        gc_counters[ix].id = uint32_t(self->gc_counter_base + ix);
        gc_counters[ix].type = SYSPROF_CAPTURE_COUNTER_INT64;
        gc_counters[ix].value.v64 = 0;
    }
    g_snprintf(gc_counters[Gjs::GCCounters::GC_HEAP_BYTES].name, name_size,
               "GC bytes");
    g_snprintf(gc_counters[Gjs::GCCounters::GC_HEAP_BYTES].description,
               description_size, "Bytes used in GC heap");
    g_snprintf(gc_counters[Gjs::GCCounters::MALLOC_HEAP_BYTES].name, name_size,
               "Malloc bytes");
    g_snprintf(gc_counters[Gjs::GCCounters::MALLOC_HEAP_BYTES].description,
               description_size, "Malloc bytes owned by tenured GC things");

    return sysprof_capture_writer_define_counters(self->capture, now, -1,
                                                  self->pid, gc_counters.data(),
                                                  Gjs::GCCounters::N_COUNTERS);
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
    g_clear_pointer(&self->periodic_flush, g_source_destroy);
    g_clear_pointer(&self->target_capture, sysprof_capture_writer_unref);

    if (self->fd != -1)
        close(self->fd);

    self->stack.~ProfilingStack();
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

static void gjs_profiler_sigprof(int signum [[maybe_unused]], siginfo_t* info,
                                 void*) {
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
    SysprofCaptureAddress* addrs =
        // cppcheck-suppress allocaCalled
        static_cast<SysprofCaptureAddress*>(alloca(sizeof *addrs * depth));

    for (uint32_t ix = 0; ix < depth; ix++) {
        js::ProfilingStackFrame& entry = self->stack.frames[ix];
        const char *label = entry.label();
        const char *dynamic_string = entry.dynamicString();
        uint32_t flipped = depth - 1 - ix;
        size_t label_length = strlen(label);

        /*
         * 512 is an arbitrarily large size, very likely to be enough to
         * hold the final string.
         */
        char final_string[512];
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
                position += remaining_length;
            }
        }

        *position = 0;

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
                                           -1, addrs, depth)) {
        gjs_profiler_stop(self);
        return;
    }

    unsigned ids[GJS_N_COUNTERS];
    SysprofCaptureCounterValue values[GJS_N_COUNTERS];
    size_t new_counts = 0;

#    define FETCH_COUNTERS(name, ix)                       \
        {                                                  \
            uint64_t count = GJS_GET_COUNTER(name);        \
            if (count != self->last_counter_values[ix]) {  \
                ids[new_counts] = self->counter_base + ix; \
                values[new_counts].v64 = count;            \
                new_counts++;                              \
            }                                              \
            self->last_counter_values[ix] = count;         \
        }
    GJS_FOR_EACH_COUNTER(FETCH_COUNTERS);
#    undef FETCH_COUNTERS

    if (new_counts > 0 &&
        !sysprof_capture_writer_set_counters(self->capture, now, -1, self->pid,
                                             ids, values, new_counts))
        gjs_profiler_stop(self);
}

static gboolean profiler_auto_flush_cb(void* user_data) {
    auto* self = static_cast<GjsProfiler*>(user_data);

    if (!self->running)
        return G_SOURCE_REMOVE;

    sysprof_capture_writer_flush(self->capture);

    return G_SOURCE_CONTINUE;
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

    struct sigaction sa = {{0}};
    struct sigevent sev = {{0}};
    struct itimerspec its = {{0}};
    struct itimerspec old_its;

    if (self->target_capture) {
        self->capture = sysprof_capture_writer_ref(self->target_capture);
    } else if (self->fd != -1) {
        self->capture = sysprof_capture_writer_new_from_fd(self->fd, 0);
        self->fd = -1;
    } else {
        Gjs::AutoChar path{g_strdup(self->filename)};
        if (!path)
            path = g_strdup_printf("gjs-%jd.syscap", intmax_t(self->pid));

        self->capture = sysprof_capture_writer_new(path, 0);
    }

    if (!self->capture) {
        g_warning("Failed to open profile capture");
        return;
    }

    /* Automatically flush to be resilient against SIGINT, etc */
    if (!self->periodic_flush) {
        self->periodic_flush =
            g_timeout_source_new_seconds(FLUSH_DELAY_SECONDS);
        g_source_set_name(self->periodic_flush,
                          "[sysprof-capture-writer-flush]");
        g_source_set_priority(self->periodic_flush, G_PRIORITY_LOW + 100);
        g_source_set_callback(self->periodic_flush,
                              (GSourceFunc)profiler_auto_flush_cb, self,
                              nullptr);
        g_source_attach(self->periodic_flush,
                        g_main_context_get_thread_default());
    }

    if (!gjs_profiler_extract_maps(self)) {
        g_warning("Failed to extract proc maps");
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        g_clear_pointer(&self->periodic_flush, g_source_destroy);
        return;
    }

    if (!gjs_profiler_define_counters(self)) {
        g_warning("Failed to define sysprof counters");
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        g_clear_pointer(&self->periodic_flush, g_source_destroy);
        return;
    }

    /* Setup our signal handler for SIGPROF delivery */
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = gjs_profiler_sigprof;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGPROF, &sa, nullptr) == -1) {
        g_warning("Failed to register sigaction handler: %s", g_strerror(errno));
        g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
        g_clear_pointer(&self->periodic_flush, g_source_destroy);
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
        g_clear_pointer(&self->periodic_flush, g_source_destroy);
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
        g_clear_pointer(&self->periodic_flush, g_source_destroy);
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
    g_message("Profiler is disabled. Recompile with it enabled to use.");

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

    struct itimerspec its = {{0}};
    timer_settime(self->timer, 0, &its, nullptr);
    timer_delete(self->timer);

    js::EnableContextProfilingStack(self->cx, false);
    js::SetContextProfilingStack(self->cx, nullptr);

    sysprof_capture_writer_flush(self->capture);

    g_clear_pointer(&self->capture, sysprof_capture_writer_unref);
    g_clear_pointer(&self->periodic_flush, g_source_destroy);

    g_message("Profiler stopped");

#endif  /* ENABLE_PROFILER */

    self->running = false;
}

#ifdef ENABLE_PROFILER

static gboolean
gjs_profiler_sigusr2(void *data)
{
    GjsContext* context = GJS_CONTEXT(data);
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
 * gjs_profiler_set_capture_writer:
 * @self: A #GjsProfiler
 * @capture: (nullable): A #SysprofCaptureWriter
 *
 * Set the capture writer to which profiling data is written when the @self
 * is stopped.
 */
void gjs_profiler_set_capture_writer(GjsProfiler* self, gpointer capture) {
    g_return_if_fail(self);
    g_return_if_fail(!self->running);

#ifdef ENABLE_PROFILER
    g_clear_pointer(&self->target_capture, sysprof_capture_writer_unref);
    self->target_capture =
        capture ? sysprof_capture_writer_ref(
                      reinterpret_cast<SysprofCaptureWriter*>(capture))
                : NULL;
#else
    // Unused in the no-profiler case
    (void)capture;
#endif
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

void _gjs_profiler_add_mark(GjsProfiler* self, int64_t time_nsec,
                            int64_t duration_nsec, const char* group,
                            const char* name, const char* message) {
    g_return_if_fail(self);
    g_return_if_fail(group);
    g_return_if_fail(name);

#ifdef ENABLE_PROFILER
    if (self->running && self->capture != nullptr) {
        sysprof_capture_writer_add_mark(self->capture, time_nsec, -1, self->pid,
                                        duration_nsec, group, name, message);
    }
#else
    // Unused in the no-profiler case
    (void)time_nsec;
    (void)duration_nsec;
    (void)message;
#endif
}

bool _gjs_profiler_sample_gc_memory_info(
    GjsProfiler* self, int64_t gc_counters[Gjs::GCCounters::N_COUNTERS]) {
    g_return_val_if_fail(self, false);

#ifdef ENABLE_PROFILER
    if (self->running && self->capture) {
        unsigned ids[Gjs::GCCounters::N_COUNTERS];
        SysprofCaptureCounterValue values[Gjs::GCCounters::N_COUNTERS];

        for (size_t ix = 0; ix < Gjs::GCCounters::N_COUNTERS; ix++) {
            ids[ix] = self->gc_counter_base + ix;
            values[ix].v64 = gc_counters[ix];
        }

        int64_t now = g_get_monotonic_time() * 1000L;
        if (!sysprof_capture_writer_set_counters(self->capture, now, -1,
                                                 self->pid, ids, values,
                                                 Gjs::GCCounters::N_COUNTERS))
            return false;
    }
#else
    // Unused in the no-profiler case
    (void)gc_counters;
#endif
    return true;
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
#else
    (void)fd;  // Unused in the no-profiler case
#endif
}

void _gjs_profiler_set_finalize_status(GjsProfiler* self,
                                       JSFinalizeStatus status) {
#ifdef ENABLE_PROFILER
    // Implementation note for mozjs-140:
    //
    // Sweeping happens in three phases:
    // 1st phase (JSFINALIZE_GROUP_PREPARE): the collector prepares to sweep a
    // group of zones. 2nd phase (JSFINALIZE_GROUP_START): weak references to
    // unmarked things have been removed, but no GC thing has been swept. 3rd
    // Phase (JSFINALIZE_GROUP_END): all dead GC things for a group of zones
    // have been swept. The above repeats for each sweep group.
    // JSFINALIZE_COLLECTION_END occurs at the end of all GC. (see
    // js/src/gc/GC.cpp, GCRuntime::beginSweepPhase, beginSweepingSweepGroup,
    // and endSweepPhase, all called from incrementalSlice).
    //
    // Incremental GC muddies the waters, because BeginSweepPhase is always run
    // to entirety, but SweepPhase can be run incrementally and mixed with JS
    // code runs or even native code, when MaybeGC/IncrementalGC return.
    // After GROUP_START, the collector may yield to the mutator meaning JS code
    // can run between the callback for GROUP_START and GROUP_END.

    int64_t now = g_get_monotonic_time() * 1000L;

    switch (status) {
        case JSFINALIZE_GROUP_PREPARE:
            self->sweep_begin_time = now;
            break;
        case JSFINALIZE_GROUP_START:
            self->group_sweep_begin_time = now;
            break;
        case JSFINALIZE_GROUP_END:
            if (self->group_sweep_begin_time != 0) {
                _gjs_profiler_add_mark(self, self->group_sweep_begin_time,
                                       now - self->group_sweep_begin_time,
                                       "GJS", "Group sweep", nullptr);
            }
            self->group_sweep_begin_time = 0;
            break;
        case JSFINALIZE_COLLECTION_END:
            if (self->sweep_begin_time != 0) {
                _gjs_profiler_add_mark(self, self->sweep_begin_time,
                                       now - self->sweep_begin_time, "GJS",
                                       "Sweep", nullptr);
            }
            self->sweep_begin_time = 0;
            break;
        default:
            g_assert_not_reached();
    }
#else
    (void)self;
    (void)status;
#endif
}

void _gjs_profiler_set_gc_status(GjsProfiler* self, JSGCStatus status,
                                 JS::GCReason reason) {
#ifdef ENABLE_PROFILER
    int64_t now = g_get_monotonic_time() * 1000L;

    switch (status) {
        case JSGC_BEGIN:
            self->gc_begin_time = now;
            self->gc_reason = gjs_explain_gc_reason(reason);
            break;
        case JSGC_END:
            if (self->gc_begin_time != 0) {
                _gjs_profiler_add_mark(self, self->gc_begin_time,
                                       now - self->gc_begin_time, "GJS",
                                       "Garbage collection", self->gc_reason);
            }
            self->gc_begin_time = 0;
            self->gc_reason = nullptr;
            break;
        default:
            g_assert_not_reached();
    }
#else
    (void)self;
    (void)status;
    (void)reason;
#endif
}
