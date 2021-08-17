/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>
 */

#ifndef UTIL_CONSOLE_H_
#define UTIL_CONSOLE_H_

/* This file has to be valid C, because it's used in libgjs-private */

#include <stdbool.h> /* IWYU pragma: keep */

#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

extern const int stdout_fd;
extern const int stdin_fd;
extern const int stderr_fd;

GJS_USE
bool gjs_console_is_tty(int fd);

bool gjs_console_clear(void);

G_END_DECLS

#endif  // UTIL_CONSOLE_H_
