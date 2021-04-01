/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>
 */

#ifndef GJS_PROMISE_H_
#define GJS_PROMISE_H_

#include <config.h>

#include <gio/gio.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gjs/context-private.h"

GSource* gjs_promise_job_queue_source_new(GjsContextPrivate* cx,
                                          GCancellable* cancellable);

void gjs_promise_job_queue_source_attach(GSource* source);

void gjs_promise_job_queue_source_remove(GSource* source);

void gjs_promise_job_queue_source_wakeup(GSource* source);

bool gjs_define_native_promise_stuff(JSContext* cx,
                                     JS::MutableHandleObject module);

#endif  // GJS_PROMISE_H_
