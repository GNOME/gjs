/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#ifndef GI_TOGGLE_H_
#define GI_TOGGLE_H_

#include <config.h>

#include <atomic>
#include <deque>
#include <thread>
#include <utility>  // for pair

#include <glib.h>  // for gboolean

class ObjectInstance;
namespace Gjs {
namespace Test {
struct ToggleQueue;
}
}

/* Thread-safe queue for enqueueing toggle-up or toggle-down events on GObjects
 * from any thread. For more information, see object.cpp, comments near
 * wrapped_gobj_toggle_notify(). */
class ToggleQueue {
public:
    enum Direction {
        DOWN,
        UP
    };

    using Handler = void (*)(ObjectInstance*, Direction);

 private:
    friend Gjs::Test::ToggleQueue;
    struct Item {
        Item() {}
        Item(ObjectInstance* o, Direction d) : object(o), direction(d) {}
        ObjectInstance* object;
        ToggleQueue::Direction direction;
    };

    struct Locked {
        explicit Locked(ToggleQueue* queue) { queue->lock(); }
        ~Locked() { get_default_unlocked().maybe_unlock(); }
        ToggleQueue* operator->() { return &get_default_unlocked(); }
    };

    std::deque<Item> q;
    std::atomic_bool m_shutdown = ATOMIC_VAR_INIT(false);

    unsigned m_idle_id = 0;
    Handler m_toggle_handler = nullptr;
    std::atomic<std::thread::id> m_holder = std::thread::id();
    unsigned m_holder_ref_count = 0;

    void lock();
    void maybe_unlock();
    [[nodiscard]] bool is_locked() const {
        return m_holder != std::thread::id();
    }
    [[nodiscard]] bool owns_lock() const {
        return m_holder == std::this_thread::get_id();
    }

    [[nodiscard]] std::deque<Item>::iterator find_operation_locked(
        const ObjectInstance* obj, Direction direction);

    [[nodiscard]] std::deque<Item>::const_iterator find_operation_locked(
        const ObjectInstance* obj, Direction direction) const;

    static gboolean idle_handle_toggle(void *data);
    static void idle_destroy_notify(void *data);

    [[nodiscard]] static ToggleQueue& get_default_unlocked() {
        static ToggleQueue the_singleton;
        return the_singleton;
    }

 public:
    /* These two functions return a pair DOWN, UP signifying whether toggles
     * are / were queued. is_queued() just checks and does not modify. */
    [[nodiscard]] std::pair<bool, bool> is_queued(ObjectInstance* obj) const;
    /* Cancels pending toggles and returns whether any were queued. */
    std::pair<bool, bool> cancel(ObjectInstance* obj);

    /* Pops a toggle from the queue and processes it. Call this if you don't
     * want to wait for it to be processed in idle time. Returns false if queue
     * is empty. */
    bool handle_toggle(Handler handler);
    void handle_all_toggles(Handler handler);

    /* After calling this, the toggle queue won't accept any more toggles. Only
     * intended for use when destroying the JSContext and breaking the
     * associations between C and JS objects. */
    void shutdown(void);

    /* Queues a toggle to be processed in idle time. */
    void enqueue(ObjectInstance* obj, Direction direction, Handler handler);

    [[nodiscard]] static Locked get_default() {
        return Locked(&get_default_unlocked());
    }
};

#endif  // GI_TOGGLE_H_
