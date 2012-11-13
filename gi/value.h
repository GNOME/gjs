/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#ifndef __GJS_VALUE_H__
#define __GJS_VALUE_H__

#include <glib-object.h>
#include "gjs/jsapi-util.h"

G_BEGIN_DECLS

JSBool     gjs_value_to_g_value         (JSContext    *context,
                                         jsval         value,
                                         GValue       *gvalue);
JSBool     gjs_value_to_g_value_no_copy (JSContext    *context,
                                         jsval         value,
                                         GValue       *gvalue);
JSBool     gjs_value_from_g_value       (JSContext    *context,
                                         jsval        *value_p,
                                         const GValue *gvalue);
GClosure*  gjs_closure_new_marshaled    (JSContext    *context,
                                         JSObject     *callable,
                                         const char   *description);
GClosure*  gjs_closure_new_for_signal   (JSContext    *context,
                                         JSObject     *callable,
                                         const char   *description,
                                         guint         signal_id);

G_END_DECLS

#endif  /* __GJS_VALUE_H__ */
