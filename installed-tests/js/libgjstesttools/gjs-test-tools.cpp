/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

#include "installed-tests/js/libgjstesttools/gjs-test-tools.h"

#include <mutex>
#include <unordered_set>

#include "gjs/jsapi-util.h"

static std::atomic<GObject*> m_tmp_object = nullptr;
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
    gjs_test_tools_clear_saved();
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
    g_object_ref(object);
    gjs_test_tools_save_object_unreffed(object);
}

void gjs_test_tools_save_object_unreffed(GObject* object) {
    GObject* expected = nullptr;
    g_assert(m_tmp_object.compare_exchange_strong(expected, object));
}

void gjs_test_tools_clear_saved() {
    if (!FinalizedObjectsLocked()->count(m_tmp_object)) {
        auto* object = m_tmp_object.exchange(nullptr);
        g_clear_object(&object);
    } else {
        m_tmp_object = nullptr;
    }
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

    return m_tmp_object.exchange(nullptr);
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
