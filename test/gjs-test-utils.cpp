/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008 litl, LLC
 * Copyright (c) 2016 Endless Mobile, Inc.
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

#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <glib.h>

#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs-test-utils.h"

void
gjs_unit_test_fixture_setup(GjsUnitTestFixture *fx,
                            gconstpointer       unused)
{
    fx->gjs_context = gjs_context_new();
    fx->cx = (JSContext *) gjs_context_get_native_context(fx->gjs_context);

    JS_BeginRequest(fx->cx);

    JS::RootedObject global(fx->cx, gjs_get_import_global(fx->cx));
    fx->compartment = JS_EnterCompartment(fx->cx, global);
}

void
gjs_unit_test_destroy_context(GjsUnitTestFixture *fx)
{
    GjsAutoChar message = gjs_unit_test_exception_message(fx);
    if (message)
        g_printerr("**\n%s\n", message.get());

    JS_LeaveCompartment(fx->cx, fx->compartment);
    JS_EndRequest(fx->cx);

    g_object_unref(fx->gjs_context);
}

void
gjs_unit_test_fixture_teardown(GjsUnitTestFixture *fx,
                               gconstpointer      unused)
{
    gjs_unit_test_destroy_context(fx);
}

char *
gjs_unit_test_exception_message(GjsUnitTestFixture *fx)
{
    if (!JS_IsExceptionPending(fx->cx))
        return nullptr;

    JS::RootedValue v_exc(fx->cx);
    g_assert_true(JS_GetPendingException(fx->cx, &v_exc));
    g_assert_true(v_exc.isObject());

    JS::RootedObject exc(fx->cx, &v_exc.toObject());
    JSErrorReport *report = JS_ErrorFromException(fx->cx, exc);
    g_assert_nonnull(report);

    char *retval = g_strdup(report->message().c_str());
    g_assert_nonnull(retval);
    JS_ClearPendingException(fx->cx);
    return retval;
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

    while (true) {
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
