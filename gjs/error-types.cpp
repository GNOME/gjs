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

#include <config.h>

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
