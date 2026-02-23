/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2008 litl, LLC
 */

#ifndef GJS_MEM_H_
#define GJS_MEM_H_

#if !defined(INSIDE_GJS_H) && !defined(GJS_COMPILATION)
#    error "Only <gjs/gjs.h> can be included directly."
#endif

#include <stdbool.h> /* IWYU pragma: keep */
#include <stdint.h>

#include <glib-object.h>
#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

GJS_EXPORT
void gjs_memory_report(const char* where, bool die_if_leaks);

int32_t estimate_size_of_gdkpixbuf(GObject* wrapped);
G_END_DECLS

#endif  // GJS_MEM_H_
