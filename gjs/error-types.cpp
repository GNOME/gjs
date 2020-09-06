/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <glib-object.h>

#include "gjs/error-types.h"

// clang-format off
G_DEFINE_QUARK(gjs-error-quark, gjs_error)
G_DEFINE_QUARK(gjs-js-error-quark, gjs_js_error)
// clang-format on

GType gjs_js_error_get_type(void) {
    static volatile GType g_type_id;

    if (g_once_init_enter(&g_type_id)) {
        static GEnumValue errors[] = {
            { GJS_JS_ERROR_ERROR, "Error", "error" },
            { GJS_JS_ERROR_EVAL_ERROR, "EvalError", "eval-error" },
            { GJS_JS_ERROR_INTERNAL_ERROR, "InternalError", "internal-error" },
            { GJS_JS_ERROR_RANGE_ERROR, "RangeError", "range-error" },
            { GJS_JS_ERROR_REFERENCE_ERROR, "ReferenceError", "reference-error" },
            { GJS_JS_ERROR_STOP_ITERATION, "StopIteration", "stop-iteration" },
            { GJS_JS_ERROR_SYNTAX_ERROR, "SyntaxError", "syntax-error" },
            { GJS_JS_ERROR_TYPE_ERROR, "TypeError", "type-error" },
            { GJS_JS_ERROR_URI_ERROR, "URIError", "uri-error" },
            { 0, nullptr, nullptr }
        };

        g_type_id = g_enum_register_static("GjsJSError", errors);
    }

    return g_type_id;
}
