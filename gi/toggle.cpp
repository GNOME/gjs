/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

#include <algorithm>  // for find_if
#include <deque>
#include <mutex>
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

bool ToggleQueue::find_and_erase_operation_locked(
    const ObjectInstance* obj, ToggleQueue::Direction direction) {
    auto pos = find_operation_locked(obj, direction);
    bool had_toggle = (pos != q.end());
    if (had_toggle)
        q.erase(pos);
    return had_toggle;
}

gboolean
ToggleQueue::idle_handle_toggle(void *data)
{
    auto self = static_cast<ToggleQueue *>(data);
    while (self->handle_toggle(self->m_toggle_handler))
        ;

    return G_SOURCE_REMOVE;
}

void
ToggleQueue::idle_destroy_notify(void *data)
{
    auto self = static_cast<ToggleQueue *>(data);
    std::lock_guard<std::mutex> hold(self->lock);
    self->m_idle_id = 0;
    self->m_toggle_handler = nullptr;
}

std::pair<bool, bool> ToggleQueue::is_queued(ObjectInstance* obj) const {
    std::lock_guard<std::mutex> hold(lock);
    bool has_toggle_down = find_operation_locked(obj, DOWN) != q.end();
    bool has_toggle_up = find_operation_locked(obj, UP) != q.end();
    return {has_toggle_down, has_toggle_up};
}

std::pair<bool, bool> ToggleQueue::cancel(ObjectInstance* obj) {
    debug("cancel", obj);
    std::lock_guard<std::mutex> hold(lock);
    bool had_toggle_down = find_and_erase_operation_locked(obj, DOWN);
    bool had_toggle_up = find_and_erase_operation_locked(obj, UP);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "ToggleQueue: %p was %s", obj->ptr(),
                        had_toggle_down && had_toggle_up
                            ? "queued to toggle BOTH"
                        : had_toggle_down ? "queued to toggle DOWN"
                        : had_toggle_up   ? "queued to toggle UP"
                                          : "not queued");
    return {had_toggle_down, had_toggle_up};
}

bool ToggleQueue::handle_toggle(Handler handler) {
    Item item;
    {
        std::lock_guard<std::mutex> hold(lock);
        if (q.empty())
            return false;

        item = q.front();
        q.pop_front();
    }

    if (item.direction == UP)
        debug("handle UP", item.object);
    else
        debug("handle DOWN", item.object);
    handler(item.object, item.direction);

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
                          ToggleQueue::Handler handler) {
    if (G_UNLIKELY (m_shutdown)) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Enqueuing GObject %p to toggle %s after "
                  "shutdown, probably from another thread (%p).",
                  obj->ptr(), direction == UP ? "UP" : "DOWN", g_thread_self());
        return;
    }

    std::lock_guard<std::mutex> hold(lock);
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
