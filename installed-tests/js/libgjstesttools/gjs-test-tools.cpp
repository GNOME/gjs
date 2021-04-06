/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#include "installed-tests/js/libgjstesttools/gjs-test-tools.h"

#include <mutex>
#include <unordered_set>

#include "gjs/jsapi-util.h"

#ifdef G_OS_UNIX
#    include <errno.h>
#    include <fcntl.h> /* for FD_CLOEXEC */
#    include <stdarg.h>
#    include <unistd.h> /* for close, write */

#    include <glib-unix.h> /* for g_unix_open_pipe */
#endif

static GObject* m_tmp_object = NULL;
static GWeakRef m_tmp_weak;
static std::unordered_set<GObject*> m_finalized_objects;
static std::mutex m_finalized_objects_lock;

struct FinalizedObjectsLocked {
    FinalizedObjectsLocked() : hold(m_finalized_objects_lock) {}

    std::unordered_set<GObject*>* operator->() { return &m_finalized_objects; }
    std::lock_guard<std::mutex> hold;
};

void gjs_test_tools_init() {}

void gjs_test_tools_reset() {
    if (!FinalizedObjectsLocked()->count(m_tmp_object))
        g_clear_object(&m_tmp_object);
    else
        m_tmp_object = nullptr;

    g_weak_ref_set(&m_tmp_weak, nullptr);

    FinalizedObjectsLocked()->clear();
}

// clang-format off
static G_DEFINE_QUARK(gjs-test-utils::finalize, finalize);
// clang-format on

static void monitor_object_finalization(GObject* object) {
    g_object_steal_qdata(object, finalize_quark());
    g_object_set_qdata_full(object, finalize_quark(), object, [](void* data) {
        FinalizedObjectsLocked()->insert(static_cast<GObject*>(data));
    });
}

void gjs_test_tools_delayed_ref(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_ref(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}

void gjs_test_tools_delayed_unref(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_unref(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}

void gjs_test_tools_delayed_dispose(GObject* object, int interval) {
    g_timeout_add(
        interval,
        [](void *data) {
            g_object_run_dispose(G_OBJECT(data));
            return G_SOURCE_REMOVE;
        },
        object);
}

void gjs_test_tools_save_object(GObject* object) {
    g_assert(!m_tmp_object);
    g_set_object(&m_tmp_object, object);
    monitor_object_finalization(object);
}

void gjs_test_tools_save_object_unreffed(GObject* object) {
    g_assert(!m_tmp_object);
    m_tmp_object = object;
    monitor_object_finalization(object);
}

void gjs_test_tools_ref_other_thread(GObject* object) {
    // cppcheck-suppress leakNoVarFunctionCall
    g_thread_join(g_thread_new("ref_object", g_object_ref, object));
}

typedef enum {
    REF = 1 << 0,
    UNREF = 1 << 1,
} RefType;

typedef struct {
    GObject* object;
    RefType ref_type;
    int delay;
} RefThreadData;

static RefThreadData* ref_thread_data_new(GObject* object, int interval,
                                          RefType ref_type) {
    auto* ref_data = g_new(RefThreadData, 1);

    ref_data->object = object;
    ref_data->delay = interval;
    ref_data->ref_type = ref_type;

    monitor_object_finalization(object);

    return ref_data;
}

static void* ref_thread_func(void* data) {
    GjsAutoPointer<RefThreadData, void, g_free> ref_data =
        static_cast<RefThreadData*>(data);

    if (FinalizedObjectsLocked()->count(ref_data->object))
        return nullptr;

    if (ref_data->delay > 0)
        g_usleep(ref_data->delay);

    if (FinalizedObjectsLocked()->count(ref_data->object))
        return nullptr;

    if (ref_data->ref_type & REF)
        g_object_ref(ref_data->object);

    if (!(ref_data->ref_type & UNREF)) {
        return ref_data->object;
    } else if (ref_data->ref_type & REF) {
        g_usleep(ref_data->delay);

        if (FinalizedObjectsLocked()->count(ref_data->object))
            return nullptr;
    }

    if (ref_data->object != m_tmp_object)
        g_object_steal_qdata(ref_data->object, finalize_quark());
    g_object_unref(ref_data->object);
    return nullptr;
}

void gjs_test_tools_unref_other_thread(GObject* object) {
    // cppcheck-suppress leakNoVarFunctionCall
    g_thread_join(g_thread_new("unref_object", ref_thread_func,
                               ref_thread_data_new(object, -1, UNREF)));
}

void gjs_test_tools_delayed_ref_other_thread(GObject* object, int interval) {
    g_thread_unref(g_thread_new("ref_object", ref_thread_func,
                                ref_thread_data_new(object, interval, REF)));
}

void gjs_test_tools_delayed_unref_other_thread(GObject* object, int interval) {
    g_thread_unref(g_thread_new("unref_object", ref_thread_func,
                                ref_thread_data_new(object, interval, UNREF)));
}

void gjs_test_tools_delayed_ref_unref_other_thread(GObject* object,
                                                   int interval) {
    g_thread_unref(
        g_thread_new("ref_unref_object", ref_thread_func,
                     ref_thread_data_new(object, interval,
                                         static_cast<RefType>(REF | UNREF))));
}

void gjs_test_tools_run_dispose_other_thread(GObject* object) {
    // cppcheck-suppress leakNoVarFunctionCall
    g_thread_join(g_thread_new(
        "run_dispose",
        [](void* object) -> void* {
            g_object_run_dispose(G_OBJECT(object));
            return nullptr;
        },
        object));
}

/**
 * gjs_test_tools_get_saved:
 * Returns: (transfer full)
 */
GObject* gjs_test_tools_get_saved() {
    if (FinalizedObjectsLocked()->count(m_tmp_object))
        m_tmp_object = nullptr;

    return static_cast<GObject*>(g_steal_pointer(&m_tmp_object));
}

/**
 * gjs_test_tools_steal_saved:
 * Returns: (transfer none)
 */
GObject* gjs_test_tools_steal_saved() { return gjs_test_tools_get_saved(); }

void gjs_test_tools_save_weak(GObject* object) {
    g_weak_ref_set(&m_tmp_weak, object);
}

/**
 * gjs_test_tools_get_weak:
 * Returns: (transfer full)
 */
GObject* gjs_test_tools_get_weak() {
    return static_cast<GObject*>(g_weak_ref_get(&m_tmp_weak));
}

/**
 * gjs_test_tools_get_weak_other_thread:
 * Returns: (transfer full)
 */
GObject* gjs_test_tools_get_weak_other_thread() {
    return static_cast<GObject*>(
        // cppcheck-suppress leakNoVarFunctionCall
        g_thread_join(g_thread_new(
            "weak_get",
            [](void*) -> void* { return gjs_test_tools_get_weak(); }, NULL)));
}

/**
 * gjs_test_tools_get_disposed:
 * Returns: (transfer none)
 */
GObject* gjs_test_tools_get_disposed(GObject* object) {
    g_object_run_dispose(G_OBJECT(object));
    return object;
}

#ifdef G_OS_UNIX

// Adapted from glnx_throw_errno_prefix()
G_GNUC_PRINTF(2, 3)
static gboolean throw_errno_prefix(GError** error, const char* fmt, ...) {
    int errsv = errno;
    char* old_msg;
    GString* buf;

    va_list args;

    if (!error)
        return FALSE;

    va_start(args, fmt);

    g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(errsv),
                        g_strerror(errsv));

    old_msg = g_steal_pointer(&(*error)->message);
    buf = g_string_new("");
    g_string_append_vprintf(buf, fmt, args);
    g_string_append(buf, ": ");
    g_string_append(buf, old_msg);
    g_free(old_msg);
    (*error)->message = g_string_free(g_steal_pointer(&buf), FALSE);

    va_end(args);

    errno = errsv;
    return FALSE;
}

#endif /* G_OS_UNIX */

/**
 * gjs_open_bytes:
 * @bytes: bytes to send to the pipe
 * @error: Return location for a #GError, or %NULL
 *
 * Creates a pipe and sends @bytes to it, such that it is suitable for passing
 * to g_subprocess_launcher_take_fd().
 *
 * Returns: file descriptor, or -1 on error
 */
int gjs_test_tools_open_bytes(GBytes* bytes, GError** error) {
    int pipefd[2], result;
    size_t count;
    const void* buf;
    ssize_t bytes_written;

    g_return_val_if_fail(bytes, -1);
    g_return_val_if_fail(error == NULL || *error == NULL, -1);

#ifdef G_OS_UNIX
    if (!g_unix_open_pipe(pipefd, FD_CLOEXEC, error))
        return -1;

    buf = g_bytes_get_data(bytes, &count);

    bytes_written = write(pipefd[1], buf, count);
    if (bytes_written < 0) {
        throw_errno_prefix(error, "write");
        return -1;
    }

    if ((size_t)bytes_written != count)
        g_warning("%s: %zu bytes sent, only %zd bytes written", __func__, count,
                  bytes_written);

    result = close(pipefd[1]);
    if (result == -1) {
        throw_errno_prefix(error, "close");
        return -1;
    }

    return pipefd[0];
#else
    g_error("%s is currently supported on UNIX only", __func__);
#endif
}
