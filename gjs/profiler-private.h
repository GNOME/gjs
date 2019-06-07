/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018 Endless Mobile, Inc.
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

#ifndef GJS_PROFILER_PRIVATE_H
#define GJS_PROFILER_PRIVATE_H

#include "context.h"
#include "gjs/macros.h"
#include "profiler.h"

G_BEGIN_DECLS

GjsProfiler *_gjs_profiler_new(GjsContext *context);
void _gjs_profiler_free(GjsProfiler *self);

void _gjs_profiler_add_mark(GjsProfiler* self, gint64 time, gint64 duration,
                            const char* group, const char* name,
                            const char* message);

GJS_USE
bool _gjs_profiler_is_running(GjsProfiler *self);

void _gjs_profiler_setup_signals(GjsProfiler *self, GjsContext *context);

G_END_DECLS

#endif  /* GJS_PROFILER_PRIVATE_H */
