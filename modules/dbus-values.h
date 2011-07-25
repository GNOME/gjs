/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC.
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
#ifndef __GJS_JS_DBUS_VALUES_H__
#define __GJS_JS_DBUS_VALUES_H__

#include <dbus/dbus.h>
#include <gjs/gjs-module.h>

G_BEGIN_DECLS

JSBool gjs_js_values_from_dbus    (JSContext          *context,
                                   DBusMessageIter    *iter,
                                   GjsRootedArray    **array_p);
JSBool gjs_js_one_value_from_dbus (JSContext          *context,
                                   DBusMessageIter    *iter,
                                   jsval              *value_p);
JSBool gjs_js_values_to_dbus      (JSContext          *context,
                                   int                 index,
                                   jsval               values,
                                   DBusMessageIter    *iter,
                                   DBusSignatureIter  *sig_iter);
JSBool gjs_js_one_value_to_dbus   (JSContext          *context,
                                   jsval               value,
                                   DBusMessageIter    *iter,
                                   DBusSignatureIter  *sig_iter);

void gjs_js_push_current_message  (DBusMessage        *message);
void gjs_js_pop_current_message   (void);


G_END_DECLS

#endif  /* __GJS_JS_DBUS_VALUES_H__ */
