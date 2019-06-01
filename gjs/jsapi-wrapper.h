/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#ifndef GJS_JSAPI_WRAPPER_H_
#define GJS_JSAPI_WRAPPER_H_

#include <config.h>

/* COMPAT: SpiderMonkey headers in some places use DEBUG instead of JS_DEBUG */
/* https://bugzilla.mozilla.org/show_bug.cgi?id=1261161 */
#ifdef HAVE_DEBUG_SPIDERMONKEY
#define DEBUG 1
#endif

#include <js-config.h>  /* SpiderMonkey's #defines that affect public API */

#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC system_header
#endif
// COMPAT: These are the headers that cause compiler warnings and need to be
// marked as system headers.
// IWYU pragma: begin_exports
#include <js/Conversions.h>
#include <jsapi.h>
#include <jsfriendapi.h>
// IWYU pragma: end_exports

#endif  // GJS_JSAPI_WRAPPER_H_
