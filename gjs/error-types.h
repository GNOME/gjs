/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef GJS_ERROR_TYPES_H_
#define GJS_ERROR_TYPES_H_

#if !defined(INSIDE_GJS_H) && !defined(GJS_COMPILATION)
#    error "Only <gjs/gjs.h> can be included directly."
#endif

#include <glib-object.h>
#include <glib.h>

#include <gjs/macros.h>

G_BEGIN_DECLS

GJS_EXPORT
GQuark gjs_error_quark(void);
#define GJS_ERROR gjs_error_quark()

typedef enum {
    GJS_ERROR_FAILED,
    GJS_ERROR_SYSTEM_EXIT,
} GjsError;

GJS_EXPORT
GQuark gjs_js_error_quark(void);
#define GJS_JS_ERROR gjs_js_error_quark()

GJS_EXPORT
GType gjs_js_error_get_type(void);
#define GJS_TYPE_JS_ERROR gjs_js_error_get_type()

typedef enum {
    GJS_JS_ERROR_ERROR,
    GJS_JS_ERROR_EVAL_ERROR,
    GJS_JS_ERROR_INTERNAL_ERROR,
    GJS_JS_ERROR_RANGE_ERROR,
    GJS_JS_ERROR_REFERENCE_ERROR,
    GJS_JS_ERROR_STOP_ITERATION,
    GJS_JS_ERROR_SYNTAX_ERROR,
    GJS_JS_ERROR_TYPE_ERROR,
    GJS_JS_ERROR_URI_ERROR,
} GjsJSError;

G_END_DECLS

#endif /* GJS_ERROR_TYPES_H_ */
