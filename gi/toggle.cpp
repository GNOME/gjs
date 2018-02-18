/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017 Endless Mobile, Inc.
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
 *
 * Authored by: Philip Chimento <philip@endlessm.com>, <philip.chimento@gmail.com>
 */

#include <algorithm>
#include <deque>
#include <mutex>
#include <glib-object.h>

#include "toggle.h"

std::deque<ToggleQueue::Item>::iterator
ToggleQueue::find_operation_locked(GObject               *gobj,
                                   ToggleQueue::Direction direction)
{
    return std::find_if(q.begin(), q.end(),
        [gobj, direction](const Item& item)->bool {
            return item.gobj == gobj && item.direction == direction;
        });
}

bool
ToggleQueue::find_and_erase_operation_locked(GObject               *gobj,
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
ToggleQueue::is_queued(GObject *gobj)
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
