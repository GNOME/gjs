/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2014 Colin Walters <walters@verbum.org>
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

#ifndef __GJS_CONTEXT_PRIVATE_H__
#define __GJS_CONTEXT_PRIVATE_H__

#include <inttypes.h>

#include "context.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"

G_BEGIN_DECLS

GJS_USE
bool         _gjs_context_destroying                  (GjsContext *js_context);

void         _gjs_context_schedule_gc_if_needed       (GjsContext *js_context);

void _gjs_context_schedule_gc(GjsContext *js_context);

void _gjs_context_exit(GjsContext *js_context,
                       uint8_t     exit_code);

GJS_USE
bool _gjs_context_get_is_owner_thread(GjsContext *js_context);

GJS_USE
bool _gjs_context_should_exit(GjsContext *js_context,
                              uint8_t    *exit_code_p);

void _gjs_context_set_sweeping(GjsContext *js_context,
                               bool        sweeping);

GJS_USE
bool _gjs_context_is_sweeping(JSContext *cx);

GJS_JSAPI_RETURN_CONVENTION
bool _gjs_context_enqueue_job(GjsContext      *gjs_context,
                              JS::HandleObject job);

GJS_USE
bool _gjs_context_run_jobs(GjsContext *gjs_context);

void _gjs_context_unregister_unhandled_promise_rejection(GjsContext *gjs_context,
                                                         uint64_t    promise_id);

G_END_DECLS

void _gjs_context_register_unhandled_promise_rejection(GjsContext   *gjs_context,
                                                       uint64_t      promise_id,
                                                       GjsAutoChar&& stack);

#endif  /* __GJS_CONTEXT_PRIVATE_H__ */
