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
#include <sys/select.h>
#include <sys/time.h>
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

/* Fork a process that waits the given time then
 * sends us ABRT
 */
void
gjs_crash_after_timeout(int seconds)
{
    pid_t parent_pid;
    int pipe_fds[2];
    fd_set read_fds;
    struct timeval term_time;
    struct timeval remaining;
    struct timeval now;
    int old_flags;

    /* We use a pipe to know in the child when the parent exited */
    if (pipe(pipe_fds) != 0) {
        fprintf(stderr, "Failed to create pipe to crash-in-timeout process: %s\n",
                strerror(errno));
        return;
    }

    /* We want pipe_fds[1] to only be open in the parent process; when it closes
     * the child will see an EOF. Setting FD_CLOEXEC is protection in case the
     * parent spawns off some process without properly closing fds.
     */
    old_flags = fcntl(pipe_fds[1], F_GETFD);
    if (old_flags == -1 ||
        fcntl(pipe_fds[1], F_SETFD, old_flags | FD_CLOEXEC) != 0) {
        fprintf(stderr, "Couldn't make crash-timeout pipe FD_CLOEXEC: %s\n",
                strerror(errno));
        return;
    }

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
        close(pipe_fds[0]);
        return;
    }

    close (pipe_fds[1]);

    gettimeofday (&now, NULL);

    term_time = now;
    term_time.tv_sec += seconds;

    FD_ZERO(&read_fds);
    FD_SET(pipe_fds[0], &read_fds);

    while (TRUE) {
        remaining.tv_sec = term_time.tv_sec - now.tv_sec;
        remaining.tv_usec = term_time.tv_usec - now.tv_usec;
        if (remaining.tv_usec < 0) {
            remaining.tv_usec += 1000;
            remaining.tv_sec -= 1;
        }

        if (remaining.tv_sec < 0) /* expired */
            break;

        select(pipe_fds[0] + 1, &read_fds, NULL, NULL, &remaining);
        if (FD_ISSET(pipe_fds[0], &read_fds)) {
            /* The parent exited */
            _exit(0);
        }

        gettimeofday(&now, NULL);
    }

    if (kill(parent_pid, 0) == 0) {
        fprintf(stderr, "Timeout of %d seconds expired; aborting process %d\n",
                seconds, (int) parent_pid);
        kill(parent_pid, SIGABRT);
    }

    _exit(1);
}

