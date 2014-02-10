/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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
 */

#ifndef __GJS_KEEP_ALIVE_H__
#define __GJS_KEEP_ALIVE_H__

#include <glib.h>
#include "gjs/jsapi-util.h"

G_BEGIN_DECLS

/* This is an alternative to JS_AddRoot().
 *
 * This "keep alive" object holds a collection of child objects and
 * traces them when GC occurs. If the keep alive object is collected,
 * it calls a notification function on all the child objects.
 *
 * The "global keep alive" is stuck on the global object as a property,
 * so its children only get notified when the entire JSContext is
 * blown away (or its global object replaced, I suppose, but that
 * won't happen afaik).
 *
 * The problem with JS_AddRoot() is that it has no notification when the
 * JSContext is destroyed. Also, it can be annoying to wrap a C type purely
 * to put a finalizer on it, this lets you avoid that.
 *
 * All three fields (notify, child, and data) are optional, so you can have
 * no JSObject - just notification+data - and you can have no notifier,
 * only the keep-alive capability.
 */

typedef void (* GjsUnrootedFunc) (JSObject *obj,
                                  void     *data);


JSObject* gjs_keep_alive_new                       (JSContext         *context);
void      gjs_keep_alive_add_child                 (JSObject          *keep_alive,
                                                    GjsUnrootedFunc    notify,
                                                    JSObject          *child,
                                                    void              *data);
void      gjs_keep_alive_remove_child              (JSObject          *keep_alive,
                                                    GjsUnrootedFunc    notify,
                                                    JSObject          *child,
                                                    void              *data);
JSObject* gjs_keep_alive_get_global                (JSContext         *context);
JSObject* gjs_keep_alive_get_global_if_exists      (JSContext         *context);
void      gjs_keep_alive_add_global_child          (JSContext         *context,
                                                    GjsUnrootedFunc  notify,
                                                    JSObject          *child,
                                                    void              *data);
void      gjs_keep_alive_remove_global_child       (JSContext         *context,
                                                    GjsUnrootedFunc  notify,
                                                    JSObject          *child,
                                                    void              *data);

typedef struct GjsKeepAliveIter GjsKeepAliveIter;
struct GjsKeepAliveIter {
    gpointer dummy[4];
    guint v;
    GHashTableIter dummyhiter;
};

void gjs_keep_alive_iterator_init (GjsKeepAliveIter *iter, JSObject *keep_alive);

gboolean gjs_keep_alive_iterator_next (GjsKeepAliveIter  *iter,
                                       GjsUnrootedFunc    notify_func,
                                       JSObject         **out_child,
                                       void             **out_data);

G_END_DECLS

#endif  /* __GJS_KEEP_ALIVE_H__ */
