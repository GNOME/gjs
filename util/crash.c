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
#include <glib.h>

#include "crash.h"

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_BACKTRACE
static void
unbuffered_write_stderr(const char *s)
{
    size_t len;

    len = strlen(s);
    write(STDERR_FILENO, s, len);
}

static void
gjs_print_maps(void)
{
    int fd;

    fd = open("/proc/self/maps", O_RDONLY);
    if (fd != -1) {
        char buf[128];
        size_t n;

        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(STDERR_FILENO, buf, n);
        }
        (void)close(fd);

        unbuffered_write_stderr("\n");
    }
}
#endif

/* this only works if we build with -rdynamic */
void
gjs_print_backtrace(void)
{
#ifdef HAVE_BACKTRACE
    void *bt[500];
    int bt_size;
    char buf[128];

    bt_size = backtrace(bt, 500);

    /* Avoid dynamic allocations since we may in SIGSEGV signal handler, so use
     * backtrace_symbols_fd */

    unbuffered_write_stderr("\n");
    backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
    unbuffered_write_stderr("\n");

    sprintf(buf, "backtrace pid %lu\n\n", (gulong) getpid());
    unbuffered_write_stderr(buf);

    /* best effort attempt to extract shared library relocations so that
     * mapping backtrace addresses to symbols is possible after the fact */
    gjs_print_maps();
#endif
}

static void
signal_handler(int num)
{
    const char *sleep_on_crash;

    switch (num) {
    case SIGSEGV:
    case SIGABRT:
        gjs_print_backtrace();

        sleep_on_crash = g_getenv("GJS_SLEEP_ON_CRASH");

        if (sleep_on_crash && !strcmp(sleep_on_crash, "1")) {
            fprintf(stderr, "\n");
            fprintf(stderr, "=== sleeping; attach debugger to PID %u\n", getpid());
            fprintf(stderr, "\n");

            sleep(1000);
        }

        exit(1);
        break;
    }
}

void
gjs_init_sleep_on_crash(void)
{
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
}

/* Fork a process that waits the given time then
 * sends us ABRT
 */
void
gjs_crash_after_timeout(int seconds)
{
    pid_t parent_pid;
    guint remaining;

    parent_pid = getpid();

    switch (fork()) {
    case -1:
        fprintf(stderr, "Failed to fork crash-in-timeout process: %s\n",
                strerror(errno));
        return;
    case 0:
        /* child */
        break;
    default:
        /* parent */
        return;
    }

    remaining = seconds;
    while ((remaining = sleep(remaining)) > 0) {
        /* empty */
    }

    if (kill(parent_pid, 0) == 0) {
        fprintf(stderr, "Timeout of %d seconds expired; aborting process %d\n",
                seconds, (int) parent_pid);
        kill(parent_pid, SIGABRT);
    }
}

