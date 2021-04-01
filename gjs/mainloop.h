/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>
 */

#ifndef GJS_MAINLOOP_H_
#define GJS_MAINLOOP_H_

#include <config.h>
#include <js/TypeDecls.h>

#include "gjs/context.h"

class GjsEventLoop;

GjsEventLoop* gjs_event_loop_new();

void gjs_event_loop_free(GjsEventLoop* event_loop);

void gjs_event_loop_spin(GjsEventLoop* event_loop, GjsContext* context);

void gjs_event_loop_ref(GjsEventLoop* event_loop);

bool gjs_event_loop_unref(GjsEventLoop* event_loop);

#endif  // GJS_MAINLOOP_H_
