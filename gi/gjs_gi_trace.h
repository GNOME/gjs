/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 Red Hat, Inc.
// SPDX-FileContributor: Author: Colin Walters <walters@verbum.org>

#ifndef GI_GJS_GI_TRACE_H_
#define GI_GJS_GI_TRACE_H_

#include <config.h>

#ifdef HAVE_DTRACE

/* include the generated probes header and put markers in code */
#include "gjs_gi_probes.h"
#define TRACE(probe) probe

#else

/* Wrap the probe to allow it to be removed when no systemtap available */
#define TRACE(probe)

#endif

#endif  // GI_GJS_GI_TRACE_H_
