/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Endless Mobile, Inc.

#ifndef GJS_PROFILER_PRIVATE_H_
#define GJS_PROFILER_PRIVATE_H_

#include <stdint.h>

#include "gjs/context.h"
#include "gjs/macros.h"
#include "gjs/profiler.h"

GjsProfiler *_gjs_profiler_new(GjsContext *context);
void _gjs_profiler_free(GjsProfiler *self);

void _gjs_profiler_add_mark(GjsProfiler* self, int64_t time, int64_t duration,
                            const char* group, const char* name,
                            const char* message);

[[nodiscard]] bool _gjs_profiler_is_running(GjsProfiler* self);

void _gjs_profiler_setup_signals(GjsProfiler *self, GjsContext *context);

#endif  // GJS_PROFILER_PRIVATE_H_
