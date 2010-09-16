/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2009  litl, LLC
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

#ifndef __GJS_COMPAT_H__
#define __GJS_COMPAT_H__

#include <glib.h>

G_BEGIN_DECLS

#include "config.h"

#if !GLIB_CHECK_VERSION(2, 18, 0)

#define g_set_error_literal(error, domain, code, message) g_set_error(error, domain, code, "%s", message)
#define g_dpgettext2(domain, ctx, msgid) dgettext(domain, msgid)

#endif

/* See https://bugzilla.gnome.org/show_bug.cgi?id=622896 */
#ifndef HAVE_JS_ADDVALUEROOT

/* The old JS_AddRoot accepted anything via void *, new
 * api is stricter.
 */
#define JS_AddValueRoot JS_AddRoot
#define JS_AddObjectRoot JS_AddRoot
#define JS_AddStringRoot JS_AddRoot
#define JS_AddGCThingRoot JS_AddRoot
#define JS_RemoveValueRoot JS_RemoveRoot
#define JS_RemoveObjectRoot JS_RemoveRoot
#define JS_RemoveStringRoot JS_RemoveRoot
#define JS_RemoveGCThingRoot JS_RemoveRoot

/* This one is complex; jsid appears to be explicitly
 * different from JSVAL now.  If we're on an old xulrunner,
 * define JSID_IS_VOID in a simple way.
 */
#define JSID_VOID JSVAL_VOID
#define JSID_IS_VOID(id) (id == JSVAL_VOID)

#endif

G_END_DECLS

#endif  /* __GJS_MEM_H__ */
