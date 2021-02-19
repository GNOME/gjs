/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Endless Mobile, Inc.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>

#include <algorithm>  // for find_if
#include <deque>
#include <mutex>
#include <utility>  // for pair

#include <glib-object.h>
#include <glib.h>

#include "gi/toggle.h"

std::deque<ToggleQueue::Item>::iterator
ToggleQueue::find_operation_locked(const GObject               *gobj,
                                   ToggleQueue::Direction direction) {
    return std::find_if(q.begin(), q.end(),
        [gobj, direction](const Item& item)->bool {
            return item.gobj == gobj && item.direction == direction;
        });
}

std::deque<ToggleQueue::Item>::const_iterator
ToggleQueue::find_operation_locked(const GObject *gobj,
                                   ToggleQueue::Direction direction) const {
    return std::find_if(q.begin(), q.end(),
        [gobj, direction](const Item& item)->bool {
            return item.gobj == gobj && item.direction == direction;
        });
}

bool
ToggleQueue::find_and_erase_operation_locked(const GObject               *gobj,
                                             ToggleQueue::Direction direction)
{
    auto pos = find_operation_locked(gobj, direction);
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

std::pair<bool, bool>
ToggleQueue::is_queued(GObject *gobj) const
{
    std::lock_guard<std::mutex> hold(lock);
    bool has_toggle_down = find_operation_locked(gobj, DOWN) != q.end();
    bool has_toggle_up = find_operation_locked(gobj, UP) != q.end();
    return {has_toggle_down, has_toggle_up};
}

std::pair<bool, bool>
ToggleQueue::cancel(GObject *gobj)
{
    debug("cancel", gobj);
    std::lock_guard<std::mutex> hold(lock);
    bool had_toggle_down = find_and_erase_operation_locked(gobj, DOWN);
    bool had_toggle_up = find_and_erase_operation_locked(gobj, UP);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "ToggleQueue: %p (%s) was %s", gobj,
                        G_OBJECT_TYPE_NAME(gobj),
                        had_toggle_down && had_toggle_up ? "queued to toggle BOTH"
                            : had_toggle_down ? "queued to toggle DOWN"
                            : had_toggle_up ? "queued to toggle UP"
                            : "not queued");
    return {had_toggle_down, had_toggle_up};
}

bool
ToggleQueue::handle_toggle(Handler handler)
{
    Item item;
    {
        std::lock_guard<std::mutex> hold(lock);
        if (q.empty())
            return false;

        item = q.front();
        handler(item.gobj, item.direction);
        q.pop_front();
    }

    debug("handle", item.gobj);
    if (item.needs_unref)
        g_object_unref(item.gobj);

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

void
ToggleQueue::enqueue(GObject               *gobj,
                     ToggleQueue::Direction direction,
                     ToggleQueue::Handler   handler)
{
    if (G_UNLIKELY (m_shutdown)) {
        gjs_debug(GJS_DEBUG_GOBJECT, "Enqueuing GObject %p to toggle %s after "
                  "shutdown, probably from another thread (%p).", gobj,
                  direction == UP ? "UP" : "DOWN",
                  g_thread_self());
        return;
    }

    Item item{gobj, direction};
    /* If we're toggling up we take a reference to the object now,
     * so it won't toggle down before we process it. This ensures we
     * only ever have at most two toggle notifications queued.
     * (either only up, or down-up)
     */
    if (direction == UP) {
        debug("enqueue UP", gobj);
        g_object_ref(gobj);
        item.needs_unref = true;
    } else {
        debug("enqueue DOWN", gobj);
    }
    /* If we're toggling down, we don't need to take a reference since
     * the associated JSObject already has one, and that JSObject won't
     * get finalized until we've completed toggling (since it's rooted,
     * until we unroot it when we dispatch the toggle down idle).
     *
     * Taking a reference now would be bad anyway, since it would force
     * the object to toggle back up again.
     */

    std::lock_guard<std::mutex> hold(lock);
    q.push_back(item);

    if (m_idle_id) {
        g_assert(((void) "Should always enqueue with the same handler",
                  m_toggle_handler == handler));
        return;
    }

    m_toggle_handler = handler;
    m_idle_id = g_idle_add_full(G_PRIORITY_HIGH, idle_handle_toggle, this,
                                idle_destroy_notify);
}
