/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Igalia, S.L.

#pragma once

#include <config.h>

#ifdef CAIRO_HAS_XLIB_SURFACE
#    include <cairo-xlib.h>  // IWYU pragma: export
#    undef None
// X11 defines a global None macro. Rude! This conflicts with None used as an
// enum member in SpiderMonkey headers, e.g. JS::ExceptionStatus::None.
#endif
