/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2008 litl, LLC
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
