/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2009  litl, LLC
 * Copyright (c) 2010  Red Hat, Inc.
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

#if !defined (__GJS_GJS_MODULE_H__) && !defined (GJS_COMPILATION)
#error "Only <gjs/gjs-module.h> can be included directly."
#endif

#ifndef __GJS_COMPAT_H__
#define __GJS_COMPAT_H__

#include <jsapi.h>

G_BEGIN_DECLS

/* This file inspects jsapi.h and attempts to provide a compatibility shim.
 * See https://bugzilla.gnome.org/show_bug.cgi?id=622896 for some initial discussion.
 */

/* The old JS_AddRoot accepted anything via void *, new
 * api is stricter.
 * Upstream commit 2fc2a12a4565, Spidermonkey >= Jun 07 2010
 */
#ifndef JS_TYPED_ROOTING_API
#define JS_AddValueRoot JS_AddRoot
#define JS_AddObjectRoot JS_AddRoot
#define JS_AddStringRoot JS_AddRoot
#define JS_AddGCThingRoot JS_AddRoot
#define JS_RemoveValueRoot JS_RemoveRoot
#define JS_RemoveObjectRoot JS_RemoveRoot
#define JS_RemoveStringRoot JS_RemoveRoot
#define JS_RemoveGCThingRoot JS_RemoveRoot
#endif

/* commit 5ad4532aa996, Spidermonkey > Jun 17 2010
 * This one is complex; jsid appears to be explicitly
 * different from JSVAL now.  If we're on an old xulrunner,
 * define JSID_IS_VOID in a compatible way.
 */
#ifndef JSID_VOID
#define JSID_VOID JSVAL_VOID
#define JSID_IS_VOID(id) (id == JSVAL_VOID)
#define INT_TO_JSID(i) ((jsid) INT_TO_JSVAL(i))
#endif

/* commit 66c8ad02543b, Spidermonkey > Aug 16 2010
 * "Slow natives" */
#ifdef JSFUN_CONSTRUCTOR
/* All functions are "fast", so define this to a no-op */
#define JSFUN_FAST_NATIVE 0
#endif

G_END_DECLS

#endif  /* __GJS_COMPAT_H__ */
