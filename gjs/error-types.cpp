/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <glib-object.h>

#include "gjs/error-types.h"

// clang-format off
G_DEFINE_QUARK(gjs-error-quark, gjs_error)
G_DEFINE_QUARK(gjs-js-error-quark, gjs_js_error)
// clang-format on

GType gjs_js_error_get_type() {
    static const GEnumValue errors[] = {{.value = GJS_JS_ERROR_ERROR,
                                         .value_name = "Error",
                                         .value_nick = "error"},
                                        {.value = GJS_JS_ERROR_EVAL_ERROR,
                                         .value_name = "EvalError",
                                         .value_nick = "eval-error"},
                                        {.value = GJS_JS_ERROR_INTERNAL_ERROR,
                                         .value_name = "InternalError",
                                         .value_nick = "internal-error"},
                                        {.value = GJS_JS_ERROR_RANGE_ERROR,
                                         .value_name = "RangeError",
                                         .value_nick = "range-error"},
                                        {.value = GJS_JS_ERROR_REFERENCE_ERROR,
                                         .value_name = "ReferenceError",
                                         .value_nick = "reference-error"},
                                        {.value = GJS_JS_ERROR_STOP_ITERATION,
                                         .value_name = "StopIteration",
                                         .value_nick = "stop-iteration"},
                                        {.value = GJS_JS_ERROR_SYNTAX_ERROR,
                                         .value_name = "SyntaxError",
                                         .value_nick = "syntax-error"},
                                        {.value = GJS_JS_ERROR_TYPE_ERROR,
                                         .value_name = "TypeError",
                                         .value_nick = "type-error"},
                                        {.value = GJS_JS_ERROR_URI_ERROR,
                                         .value_name = "URIError",
                                         .value_nick = "uri-error"},
                                        {}};
    // Initialization of static local variable guaranteed only once in C++11
    static GType g_type_id = g_enum_register_static("GjsJSError", errors);
    return g_type_id;
}
