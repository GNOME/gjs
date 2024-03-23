/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <algorithm>  // for find_if
#include <atomic>
#include <deque>
#include <utility>  // for pair

#include "gi/object.h"
#include "gi/toggle.h"
#include "util/log.h"

/* No-op unless GJS_VERBOSE_ENABLE_LIFECYCLE is defined to 1. */
inline void debug(const char* did GJS_USED_VERBOSE_LIFECYCLE,
                  const ObjectInstance* object GJS_USED_VERBOSE_LIFECYCLE) {
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "ToggleQueue %s %p (%s @ %p)", did,
                        object, object ? g_type_name(object->gtype()) : "",
                        object ? object->ptr() : nullptr);
}

void ToggleQueue::lock() {
    auto holding_thread = std::thread::id();
    auto current_thread = std::this_thread::get_id();

    while (!m_holder.compare_exchange_weak(holding_thread, current_thread,
                                           std::memory_order_acquire)) {
        // In case the current thread is holding the lock, we can just try
        // again, checking if this is still true and in case continue
        if (holding_thread != current_thread)
            holding_thread = std::thread::id();
    }

    m_holder_ref_count++;
}

void ToggleQueue::maybe_unlock() {
    g_assert(owns_lock() && "Nothing to unlock here");

    if (!(--m_holder_ref_count))
        m_holder.store(std::thread::id(), std::memory_order_release);
}

std::deque<ToggleQueue::Item>::iterator ToggleQueue::find_operation_locked(
    const ObjectInstance* obj, ToggleQueue::Direction direction) {
    return std::find_if(
        q.begin(), q.end(), [obj, direction](const Item& item) -> bool {
            return item.object == obj && item.direction == direction;
        });
}

std::deque<ToggleQueue::Item>::const_iterator
ToggleQueue::find_operation_locked(const ObjectInstance* obj,
                                   ToggleQueue::Direction direction) const {
    return std::find_if(
        q.begin(), q.end(), [obj, direction](const Item& item) -> bool {
            return item.object == obj && item.direction == direction;
        });
}

void ToggleQueue::handle_all_toggles(Handler handler) {
    g_assert(owns_lock() && "Unsafe access to queue");
    while (handle_toggle(handler))
        ;
}

gboolean
ToggleQueue::idle_handle_toggle(void *data)
{
    auto self = Locked(static_cast<ToggleQueue*>(data));
    self->handle_all_toggles(self->m_toggle_handler);

    return G_SOURCE_REMOVE;
}

void
ToggleQueue::idle_destroy_notify(void *data)
{
    auto self = Locked(static_cast<ToggleQueue*>(data));
    self->m_idle_id = 0;
    self->m_toggle_handler = nullptr;
}

std::pair<bool, bool> ToggleQueue::is_queued(ObjectInstance* obj) const {
    g_assert(owns_lock() && "Unsafe access to queue");
    bool has_toggle_down = find_operation_locked(obj, DOWN) != q.end();
    bool has_toggle_up = find_operation_locked(obj, UP) != q.end();
    return {has_toggle_down, has_toggle_up};
}

std::pair<bool, bool> ToggleQueue::cancel(ObjectInstance* obj) {
    debug("cancel", obj);
    g_assert(owns_lock() && "Unsafe access to queue");
    bool had_toggle_down = false;
    bool had_toggle_up = false;

    for (auto it = q.begin(); it != q.end();) {
        if (it->object == obj) {
            had_toggle_down |= (it->direction == Direction::DOWN);
            had_toggle_up |= (it->direction == Direction::UP);
            it = q.erase(it);
            continue;
        }
        it++;
    }

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "ToggleQueue: %p (%p) was %s", obj,
                        obj ? obj->ptr() : nullptr,
                        had_toggle_down && had_toggle_up
                            ? "queued to toggle BOTH"
                        : had_toggle_down ? "queued to toggle DOWN"
                        : had_toggle_up   ? "queued to toggle UP"
                                          : "not queued");
    return {had_toggle_down, had_toggle_up};
}

bool ToggleQueue::handle_toggle(Handler handler) {
    g_assert(owns_lock() && "Unsafe access to queue");

    if (q.empty())
        return false;

    auto const& item = q.front();
    if (item.direction == UP)
        debug("handle UP", item.object);
    else
        debug("handle DOWN", item.object);

    handler(item.object, item.direction);
    q.pop_front();

    return true;
}

void
ToggleQueue::shutdown(void)
{
    debug("shutdown", nullptr);
    g_assert(((void)"Queue should have been emptied before shutting down",
              q.empty()));
    m_shutdown = true;
}

void ToggleQueue::enqueue(ObjectInstance* obj, ToggleQueue::Direction direction,
                          // https://trac.cppcheck.net/ticket/10733
                          // cppcheck-suppress passedByValue
                          ToggleQueue::Handler handler) {
    g_assert(owns_lock() && "Unsafe access to queue");

    if (G_UNLIKELY (m_shutdown)) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Enqueuing GObject %p to toggle %s after "
                  "shutdown, probably from another thread (%p).",
                  obj->ptr(), direction == UP ? "UP" : "DOWN", g_thread_self());
        return;
    }

    auto other_item = find_operation_locked(obj, direction == UP ? DOWN : UP);
    if (other_item != q.end()) {
        if (direction == UP) {
            debug("enqueue UP, dequeuing already DOWN object", obj);
        } else {
            debug("enqueue DOWN, dequeuing already UP object", obj);
        }
        q.erase(other_item);
        return;
    }

    /* Only keep an unowned reference on the object here, as if we're here, the
     * JSObject wrapper has already a reference and we don't want to cause
     * any weak notify in case it has lost one already in the main thread.
     * So let's just save the pointer to keep track of the object till we
     * don't handle this toggle.
     * We rely on object's cancelling the queue in case an object gets
     * finalized earlier than we've processed it.
     */
    q.emplace_back(obj, direction);

    if (direction == UP) {
        debug("enqueue UP", obj);
    } else {
        debug("enqueue DOWN", obj);
    }

    if (m_idle_id) {
        g_assert(((void) "Should always enqueue with the same handler",
                  m_toggle_handler == handler));
        return;
    }

    m_toggle_handler = handler;
    m_idle_id = g_idle_add_full(G_PRIORITY_HIGH, idle_handle_toggle, this,
                                idle_destroy_notify);
}
