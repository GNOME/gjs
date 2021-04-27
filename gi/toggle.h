/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>

#ifndef GI_TOGGLE_H_
#define GI_TOGGLE_H_

#include <atomic>
#include <deque>
#include <mutex>
#include <utility>  // for pair

#include <glib-object.h>
#include <glib.h>

#include "util/log.h"

/* Thread-safe queue for enqueueing toggle-up or toggle-down events on GObjects
 * from any thread. For more information, see object.cpp, comments near
 * wrapped_gobj_toggle_notify(). */
class ToggleQueue {
public:
    enum Direction {
        DOWN,
        UP
    };

    typedef void (*Handler)(GObject *, Direction);

private:
    struct Item {
        Item() {}
        Item(GObject* o, Direction d) : gobj(o), direction(d) {}
        GObject *gobj;
        ToggleQueue::Direction direction;
    };

    mutable std::mutex lock;
    std::deque<Item> q;
    std::atomic_bool m_shutdown = ATOMIC_VAR_INIT(false);

    unsigned m_idle_id = 0;
    Handler m_toggle_handler = nullptr;

    /* No-op unless GJS_VERBOSE_ENABLE_LIFECYCLE is defined to 1. */
    inline void debug(const char* did GJS_USED_VERBOSE_LIFECYCLE,
                      const void* what GJS_USED_VERBOSE_LIFECYCLE) {
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "ToggleQueue %s %p", did, what);
    }

    [[nodiscard]] std::deque<Item>::iterator find_operation_locked(
        const GObject* gobj, Direction direction);

    [[nodiscard]] std::deque<Item>::const_iterator find_operation_locked(
        const GObject* gobj, Direction direction) const;

    [[nodiscard]] bool find_and_erase_operation_locked(const GObject* gobj,
                                                       Direction direction);

    static gboolean idle_handle_toggle(void *data);
    static void idle_destroy_notify(void *data);

 public:
    /* These two functions return a pair DOWN, UP signifying whether toggles
     * are / were queued. is_queued() just checks and does not modify. */
    [[nodiscard]] std::pair<bool, bool> is_queued(GObject* gobj) const;
    /* Cancels pending toggles and returns whether any were queued. */
    std::pair<bool, bool> cancel(GObject* gobj);

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
    void enqueue(GObject  *gobj,
                 Direction direction,
                 Handler   handler);

    [[nodiscard]] static ToggleQueue& get_default() {
        static ToggleQueue the_singleton;
        return the_singleton;
    }
};

#endif  // GI_TOGGLE_H_
