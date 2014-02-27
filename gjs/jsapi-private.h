/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010 litl, LLC
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

/* This file wraps C++ stuff from the spidermonkey private API so we
 * can use it from our other C files. This file should be included by
 * jsapi-util.c only. "Public" API from this jsapi-private.c should be
 * declared in jsapi-util.h
 */

#ifndef __GJS_JSAPI_PRIVATE_H__
#define __GJS_JSAPI_PRIVATE_H__

#include <glib-object.h>
#include "gjs/jsapi-util.h"

G_BEGIN_DECLS

void gjs_schedule_gc_if_needed (JSContext *context);
void gjs_gc_if_needed          (JSContext *context);

G_END_DECLS

#endif  /* __GJS_JSAPI_PRIVATE_H__ */
