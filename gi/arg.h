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

#ifndef __GJS_ARG_H__
#define __GJS_ARG_H__

#include <glib.h>

#include <jsapi.h>

#include <girepository.h>

G_BEGIN_DECLS

/* Different roles for a GArgument */
typedef enum {
    GJS_ARGUMENT_ARGUMENT,
    GJS_ARGUMENT_RETURN_VALUE,
    GJS_ARGUMENT_FIELD,
    GJS_ARGUMENT_LIST_ELEMENT,
    GJS_ARGUMENT_HASH_ELEMENT
} GjsArgumentType;

JSBool gjs_value_to_arg   (JSContext  *context,
                           jsval       value,
                           GIArgInfo  *arg_info,
                           GArgument  *arg);

JSBool gjs_value_to_g_argument (JSContext      *context,
                                jsval           value,
                                GITypeInfo     *type_info,
                                const char     *arg_name,
                                GjsArgumentType argument_type,
                                GITransfer      transfer,
                                gboolean        may_be_null,
                                GArgument      *arg);

JSBool gjs_value_from_g_argument (JSContext  *context,
                                  jsval      *value_p,
                                  GITypeInfo *type_info,
                                  GArgument  *arg);
JSBool gjs_g_argument_release    (JSContext  *context,
                                  GITransfer  transfer,
                                  GITypeInfo *type_info,
                                  GArgument  *arg);
JSBool gjs_g_argument_release_in_arg (JSContext  *context,
                                      GITransfer  transfer,
                                      GITypeInfo *type_info,
                                      GArgument  *arg);

JSBool _gjs_flags_value_is_valid (JSContext   *context,
                                  GType        gtype,
                                  guint        value);


JSBool gjs_array_to_strv (JSContext   *context,
                          jsval        array_value,
                          unsigned int length,
                          void       **arr_p);

G_END_DECLS

#endif  /* __GJS_ARG_H__ */
