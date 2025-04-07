/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Canonical, Ltd.
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>
#include <tuple>    // for tie
#include <utility>  // for pair

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCAPI.h>  // for JS_GC
#include <js/TypeDecls.h>

#include "gi/object.h"
#include "gi/toggle.h"
#include "gjs/auto.h"
#include "gjs/context.h"
#include "gjs/gerror-result.h"
#include "installed-tests/js/libgjstesttools/gjs-test-tools.h"
#include "test/gjs-test-utils.h"

namespace Gjs {
namespace Test {

static GMutex s_gc_lock;
static GCond s_gc_finished;
static std::atomic_int s_gc_counter;
static std::deque<std::pair<::ObjectInstance*, ::ToggleQueue::Direction>>
    s_toggle_history;

struct ObjectInstance : ::ObjectInstance {
    using ::ObjectInstance::ensure_uses_toggle_ref;
    using ::ObjectInstance::new_for_gobject;
    using ::ObjectInstance::wrapper_is_rooted;
};

struct ToggleQueue {
    static decltype(::ToggleQueue::get_default()) get_default() {
        return ::ToggleQueue::get_default();
    }
    static void reset_queue() {
        auto tq = get_default();
        tq->m_shutdown = false;
        g_clear_handle_id(&tq->m_idle_id, g_source_remove);
        tq->q.clear();
    }
    static decltype(::ToggleQueue::q) queue() { return get_default()->q; }
    static ::ToggleQueue::Handler handler() {
        return get_default()->m_toggle_handler;
    }
};

namespace TQ {

static void on_gc(JSContext*, JSGCStatus status, JS::GCReason, void*) {
    if (status != JSGC_END)
        return;

    g_mutex_lock(&s_gc_lock);
    s_gc_counter.fetch_add(1);
    g_cond_broadcast(&s_gc_finished);
    g_mutex_unlock(&s_gc_lock);
}

static void setup(GjsUnitTestFixture* fx, const void*) {
    gjs_test_tools_init();
    gjs_unit_test_fixture_setup(fx, nullptr);
    AutoUnref<GIRepository> repo = gi_repository_dup_default();
    gi_repository_prepend_search_path(repo, g_getenv("TOP_BUILDDIR"));
    JS_SetGCCallback(fx->cx, on_gc, fx);

    AutoError error;
    int code;

    const char* gi_initializer = "imports.gi;";
    g_assert_true(gjs_context_eval(fx->gjs_context, gi_initializer, -1,
                                   "<gjs-test-toggle>", &code, &error));
    g_assert_no_error(error);
}

static void wait_for_gc(GjsUnitTestFixture* fx) {
    int count = s_gc_counter.load();

    JS_GC(fx->cx);

    g_mutex_lock(&s_gc_lock);
    while (count == s_gc_counter.load()) {
        g_cond_wait(&s_gc_finished, &s_gc_lock);
    }
    g_mutex_unlock(&s_gc_lock);
}

static void teardown(GjsUnitTestFixture* fx, const void*) {
    for (auto pair : s_toggle_history)
        ToggleQueue::get_default()->cancel(pair.first);

    s_toggle_history.clear();
    gjs_unit_test_fixture_teardown(fx, nullptr);

    g_assert_true(ToggleQueue::queue().empty());
    ToggleQueue::reset_queue();
    gjs_test_tools_reset();
}

}  // namespace TQ

static ::ObjectInstance* new_test_gobject(GjsUnitTestFixture* fx) {
    AutoUnref<GObject> gobject{G_OBJECT(g_object_new(G_TYPE_OBJECT, nullptr))};
    auto* object = ObjectInstance::new_for_gobject(fx->cx, gobject);
    static_cast<ObjectInstance*>(object)->ensure_uses_toggle_ref(fx->cx);
    return object;
}

static void wait_for(int interval) {
    AutoPointer<GMainLoop, GMainLoop, g_main_loop_unref> loop{
        g_main_loop_new(nullptr, false)};
    g_timeout_add_full(
        G_PRIORITY_LOW, interval,
        [](void* data) {
            g_main_loop_quit(static_cast<GMainLoop*>(data));
            return G_SOURCE_REMOVE;
        },
        loop, nullptr);
    g_main_loop_run(loop);
}

static void toggles_handler(::ObjectInstance* object,
                            ::ToggleQueue::Direction direction) {
    s_toggle_history.emplace_back(object, direction);
}

static void test_toggle_queue_unlock_empty(GjsUnitTestFixture*, const void*) {
    assert_equal(ToggleQueue::get_default()->cancel(nullptr), false, false);
}

static void test_toggle_queue_unlock_same_thread(GjsUnitTestFixture*,
                                                 const void*) {
    auto tq = ToggleQueue::get_default();
    assert_equal(tq->cancel(nullptr), false, false);
    assert_equal(ToggleQueue::get_default()->cancel(nullptr), false, false);
}

static void test_toggle_blocks_other_thread(GjsUnitTestFixture*, const void*) {
    struct LockedQueue {
        decltype(ToggleQueue::get_default()) tq = ToggleQueue::get_default();
    };

    auto locked_queue = std::make_unique<LockedQueue>();
    assert_equal(locked_queue->tq->cancel(nullptr), false, false);

    std::atomic_bool other_thread_running(false);
    std::atomic_bool accessed_from_other_thread(false);
    auto th = std::thread([&accessed_from_other_thread, &other_thread_running] {
        other_thread_running.store(true);
        auto locked_queue = std::make_unique<LockedQueue>();
        accessed_from_other_thread.store(true);
        assert_equal(ToggleQueue::get_default()->cancel(nullptr), false, false);
        other_thread_running = false;
    });

    while (!other_thread_running.load())
        g_assert_false(accessed_from_other_thread.load());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    g_assert_true(other_thread_running);
    g_assert_false(accessed_from_other_thread);

    auto other_queue = std::make_unique<LockedQueue>();
    assert_equal(other_queue->tq->cancel(nullptr), false, false);

    other_queue.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    g_assert_true(other_thread_running);
    g_assert_false(accessed_from_other_thread);

    // Ok, now other thread may get the lock...
    locked_queue.reset();
    while (!accessed_from_other_thread.load()) {
    }
    g_assert_true(accessed_from_other_thread);

    // Can enter again from main thread!
    th.join();
    g_assert_false(other_thread_running);
    assert_equal(ToggleQueue::get_default()->cancel(nullptr), false, false);
}

static void test_toggle_queue_empty(GjsUnitTestFixture*, const void*) {
    auto tq = ToggleQueue::get_default();
    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_empty_cancel(GjsUnitTestFixture*, const void*) {
    auto tq = ToggleQueue::get_default();
    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(nullptr);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_enqueue_one(GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    tq->handle_all_toggles(toggles_handler);
    assert_equal(s_toggle_history.size(), 1LU);
    assert_equal(s_toggle_history.front(), instance,
                 ::ToggleQueue::Direction::UP);
}

static void test_toggle_queue_enqueue_one_cancel(GjsUnitTestFixture* fx,
                                                 const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_true(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_enqueue_many_equal(GjsUnitTestFixture* fx,
                                                 const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);

    tq->handle_all_toggles(toggles_handler);
    assert_equal(s_toggle_history.size(), 0LU);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_enqueue_many_equal_cancel(GjsUnitTestFixture* fx,
                                                        const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_enqueue_more_up(GjsUnitTestFixture* fx,
                                              const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    tq->handle_all_toggles(toggles_handler);
    assert_equal(s_toggle_history.size(), 2LU);
    assert_equal(s_toggle_history.at(0), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(1), instance,
                 ::ToggleQueue::Direction::UP);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_enqueue_only_up(GjsUnitTestFixture* fx,
                                              const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    tq->handle_all_toggles(toggles_handler);
    assert_equal(s_toggle_history.size(), 4LU);
    assert_equal(s_toggle_history.at(0), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(1), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(2), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(3), instance,
                 ::ToggleQueue::Direction::UP);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_handle_more_up(GjsUnitTestFixture* fx,
                                             const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::DOWN, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    wait_for(50);

    assert_equal(s_toggle_history.size(), 2LU);
    assert_equal(s_toggle_history.at(0), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(1), instance,
                 ::ToggleQueue::Direction::UP);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_handle_only_up(GjsUnitTestFixture* fx,
                                             const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    wait_for(50);

    assert_equal(s_toggle_history.size(), 4LU);
    assert_equal(s_toggle_history.at(0), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(1), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(2), instance,
                 ::ToggleQueue::Direction::UP);
    assert_equal(s_toggle_history.at(3), instance,
                 ::ToggleQueue::Direction::UP);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);
}

static void test_toggle_queue_enqueue_only_up_cancel(GjsUnitTestFixture* fx,
                                                     const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);
    tq->enqueue(instance, ::ToggleQueue::Direction::UP, toggles_handler);

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_true(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_from_main_thread(GjsUnitTestFixture* fx,
                                                      const void*) {
    auto* instance = new_test_gobject(fx);
    auto tq = ToggleQueue::get_default();

    AutoUnref<GObject> reffed{instance->ptr(), TakeOwnership{}};

    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_from_main_thread_already_enqueued(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);
    AutoUnref<GObject> reffed;
    AutoError error;

    reffed = instance->ptr();
    gjs_test_tools_ref_other_thread(reffed, &error);
    g_assert_no_error(error);

    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).object, instance);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    auto tq = ToggleQueue::get_default();
    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_true(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_from_main_thread_unref_already_enqueued(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);
    AutoUnref<GObject> reffed;
    AutoError error;

    reffed = instance->ptr();
    gjs_test_tools_ref_other_thread(reffed, &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    reffed.reset();
    g_assert_true(ToggleQueue::queue().empty());

    auto tq = ToggleQueue::get_default();
    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_from_other_thread_ref_unref(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    gjs_test_tools_unref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    g_assert_true(ToggleQueue::queue().empty());

    auto tq = ToggleQueue::get_default();
    bool toggle_down_queued, toggle_up_queued;
    std::tie(toggle_down_queued, toggle_up_queued) = tq->cancel(instance);
    g_assert_false(toggle_down_queued);
    g_assert_false(toggle_up_queued);

    tq->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_up(GjsUnitTestFixture* fx,
                                               const void*) {
    auto* instance = new_test_gobject(fx);
    auto* instance_test = reinterpret_cast<ObjectInstance*>(instance);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    AutoUnref<GObject> reffed{instance->ptr()};
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    wait_for(50);
    g_assert_true(instance_test->wrapper_is_rooted());
    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_up_down(GjsUnitTestFixture* fx,
                                                    const void*) {
    auto* instance = new_test_gobject(fx);
    auto* instance_test = reinterpret_cast<ObjectInstance*>(instance);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    gjs_test_tools_unref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    g_assert_true(ToggleQueue::queue().empty());

    wait_for(50);
    g_assert_false(instance_test->wrapper_is_rooted());
    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_up_down_delayed(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);
    auto* instance_test = reinterpret_cast<ObjectInstance*>(instance);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    wait_for(50);
    g_assert_true(instance_test->wrapper_is_rooted());
    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());

    gjs_test_tools_unref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::DOWN);

    wait_for(50);
    g_assert_false(instance_test->wrapper_is_rooted());
    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_up_down_on_gc(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    assert_equal(ToggleQueue::queue().size(), 1LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);

    gjs_test_tools_unref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    g_assert_true(ToggleQueue::queue().empty());

    GWeakRef weak_ref;
    g_weak_ref_init(&weak_ref, instance->ptr());

    TQ::wait_for_gc(fx);
    g_assert_null(g_weak_ref_get(&weak_ref));

    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_many_up(GjsUnitTestFixture* fx,
                                                    const void*) {
    auto* instance = new_test_gobject(fx);
    auto* instance_test = reinterpret_cast<ObjectInstance*>(instance);

    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    AutoUnref<GObject> reffed{instance->ptr()};
    // Simulating the case where late threads are causing this...
    ToggleQueue::get_default()->enqueue(instance, ::ToggleQueue::Direction::UP,
                                        ToggleQueue().handler());

    assert_equal(ToggleQueue::queue().size(), 2LU);
    assert_equal(ToggleQueue::queue().at(0).direction,
                 ::ToggleQueue::Direction::UP);
    assert_equal(ToggleQueue::queue().at(1).direction,
                 ::ToggleQueue::Direction::UP);

    wait_for(50);
    g_assert_true(instance_test->wrapper_is_rooted());
    ToggleQueue::get_default()->handle_all_toggles(toggles_handler);
    g_assert_true(s_toggle_history.empty());
}

static void test_toggle_queue_object_handle_many_up_and_down(
    GjsUnitTestFixture* fx, const void*) {
    auto* instance = new_test_gobject(fx);
    auto* instance_test = reinterpret_cast<ObjectInstance*>(instance);

    // This is something similar to what is happening on #297
    AutoError error;
    gjs_test_tools_ref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    ToggleQueue::get_default()->enqueue(instance, ::ToggleQueue::Direction::UP,
                                        ToggleQueue().handler());
    gjs_test_tools_unref_other_thread(instance->ptr(), &error);
    g_assert_no_error(error);
    ToggleQueue::get_default()->enqueue(
        instance, ::ToggleQueue::Direction::DOWN, ToggleQueue().handler());

    g_assert_true(ToggleQueue::queue().empty());

    wait_for(50);
    g_assert_false(instance_test->wrapper_is_rooted());
    g_assert_true(ToggleQueue::queue().empty());

    GWeakRef weak_ref;
    g_assert_true(G_IS_OBJECT(instance->ptr()));
    g_weak_ref_init(&weak_ref, instance->ptr());

    TQ::wait_for_gc(fx);
    g_assert_null(g_weak_ref_get(&weak_ref));
    g_assert_true(ToggleQueue::queue().empty());
}

void add_tests_for_toggle_queue() {
#define ADD_TOGGLE_QUEUE_TEST(path, f)                                        \
    g_test_add("/toggle-queue/" path, GjsUnitTestFixture, nullptr, TQ::setup, \
               f, TQ::teardown);

    ADD_TOGGLE_QUEUE_TEST("spin-lock/unlock-empty",
                          test_toggle_queue_unlock_empty);
    ADD_TOGGLE_QUEUE_TEST("spin-lock/unlock-same-thread",
                          test_toggle_queue_unlock_same_thread);
    ADD_TOGGLE_QUEUE_TEST("spin-lock/blocks-other-thread",
                          test_toggle_blocks_other_thread);

    ADD_TOGGLE_QUEUE_TEST("empty", test_toggle_queue_empty);
    ADD_TOGGLE_QUEUE_TEST("empty_cancel", test_toggle_queue_empty_cancel);
    ADD_TOGGLE_QUEUE_TEST("enqueue_one", test_toggle_queue_enqueue_one);
    ADD_TOGGLE_QUEUE_TEST("enqueue_one_cancel",
                          test_toggle_queue_enqueue_one_cancel);
    ADD_TOGGLE_QUEUE_TEST("enqueue_many_equal",
                          test_toggle_queue_enqueue_many_equal);
    ADD_TOGGLE_QUEUE_TEST("enqueue_many_equal_cancel",
                          test_toggle_queue_enqueue_many_equal_cancel);
    ADD_TOGGLE_QUEUE_TEST("enqueue_more_up", test_toggle_queue_enqueue_more_up);
    ADD_TOGGLE_QUEUE_TEST("enqueue_only_up", test_toggle_queue_enqueue_only_up);
    ADD_TOGGLE_QUEUE_TEST("enqueue_only_up_cancel",
                          test_toggle_queue_enqueue_only_up_cancel);
    ADD_TOGGLE_QUEUE_TEST("handle_more_up", test_toggle_queue_handle_more_up);
    ADD_TOGGLE_QUEUE_TEST("handle_only_up", test_toggle_queue_handle_only_up);

    ADD_TOGGLE_QUEUE_TEST("object/not-enqueued_main_thread",
                          test_toggle_queue_object_from_main_thread);
    ADD_TOGGLE_QUEUE_TEST(
        "object/already_enqueued_main_thread",
        test_toggle_queue_object_from_main_thread_already_enqueued);
    ADD_TOGGLE_QUEUE_TEST(
        "object/already_enqueued_unref_main_thread",
        test_toggle_queue_object_from_main_thread_unref_already_enqueued);
    ADD_TOGGLE_QUEUE_TEST("object/ref_unref_other_thread",
                          test_toggle_queue_object_from_other_thread_ref_unref);
    ADD_TOGGLE_QUEUE_TEST("object/handle_up",
                          test_toggle_queue_object_handle_up);
    ADD_TOGGLE_QUEUE_TEST("object/handle_up_down",
                          test_toggle_queue_object_handle_up_down);
    ADD_TOGGLE_QUEUE_TEST("object/handle_up_down_delayed",
                          test_toggle_queue_object_handle_up_down_delayed);
    ADD_TOGGLE_QUEUE_TEST("object/handle_up_down_on_gc",
                          test_toggle_queue_object_handle_up_down_on_gc);
    ADD_TOGGLE_QUEUE_TEST("object/handle_many_up",
                          test_toggle_queue_object_handle_many_up);
    ADD_TOGGLE_QUEUE_TEST("object/handle_many_up_and_down",
                          test_toggle_queue_object_handle_many_up_and_down);

#undef ADD_TOGGLE_QUEUE_TEST
}

}  // namespace Test
}  // namespace Gjs
