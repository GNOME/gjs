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

#ifndef GJS_TOGGLE_H
#define GJS_TOGGLE_H

#include <deque>
#include <mutex>
#include <glib-object.h>

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
        GObject *gobj;
        ToggleQueue::Direction direction;
        unsigned needs_unref : 1;
    };

    std::mutex lock;
    std::deque<Item> q;
    unsigned m_idle_id;
    Handler m_toggle_handler;

    std::deque<Item>::iterator find_operation_locked(GObject  *gobj,
                                                     Direction direction);
    bool find_and_erase_operation_locked(GObject *gobj, Direction direction);

    static gboolean idle_handle_toggle(void *data);
    static void idle_destroy_notify(void *data);

public:
    /* These two functions return a pair DOWN, UP signifying whether toggles
     * are / were queued. is_queued() just checks and does not modify. */
    std::pair<bool, bool> is_queued(GObject *gobj);
    /* Cancels pending toggles and returns whether any were queued. */
    std::pair<bool, bool> cancel(GObject *gobj);

    /* Pops a toggle from the queue and processes it. Call this if you don't
     * want to wait for it to be processed in idle time. Returns false if queue
     * is empty. */
    bool handle_toggle(Handler handler);
    
    /* Queues a toggle to be processed in idle time. */
    void enqueue(GObject  *gobj,
                 Direction direction,
                 Handler   handler);

    static ToggleQueue&
    get_default(void) {
        static ToggleQueue the_singleton;
        return the_singleton;
    }
};

#endif  /* GJS_TOGGLE_H */
