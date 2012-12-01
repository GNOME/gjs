/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2012 Red Hat, Inc.
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

#include <string.h>

#include "proxyutils.h"

/* Default spidermonkey toString is worthless.  Replace it
 * with something that gives us both the introspection name
 * and a memory address.
 */
JSBool
_gjs_proxy_to_string_func(JSContext  *context,
                          JSObject   *this_obj,
                          const char *objtype,
                          GIBaseInfo *info,
                          GType       gtype,
                          gpointer    native_address,
                          jsval      *rval)
{
    GString *buf;
    JSBool ret = JS_FALSE;

    buf = g_string_new("");
    g_string_append_c(buf, '[');
    g_string_append(buf, objtype);
    if (native_address == NULL)
        g_string_append(buf, " prototype of");
    else
        g_string_append(buf, " instance proxy");

    if (info != NULL) {
        g_string_append_printf(buf, " GIName:%s.%s",
                               g_base_info_get_namespace(info),
                               g_base_info_get_name(info));
    } else {
        g_string_append(buf, " GType:");
        g_string_append(buf, g_type_name(gtype));
    }

    g_string_append_printf(buf, " jsobj@%p", this_obj);
    if (native_address != NULL)
        g_string_append_printf(buf, " native@%p", native_address);
    
    g_string_append_c(buf, ']');

    if (!gjs_string_from_utf8 (context, buf->str, -1, rval))
        goto out;

    ret = JS_TRUE;
 out:
    g_string_free (buf, TRUE);
    return ret;
}
