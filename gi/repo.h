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

#ifndef __GJS_REPO_H__
#define __GJS_REPO_H__

#include <glib.h>

#include <girepository.h>

#include <gjs/gjs-module.h>
#include <util/log.h>

G_BEGIN_DECLS

JSBool      gjs_define_repo                     (JSContext      *context,
                                                 JSObject      **module_out,
                                                 const char     *name);
const char* gjs_info_type_name                  (GIInfoType      type);
JSObject*   gjs_lookup_private_namespace        (JSContext      *context);
JSObject*   gjs_lookup_namespace_object         (JSContext      *context,
                                                 GIBaseInfo     *info);
JSObject*   gjs_lookup_namespace_object_by_name (JSContext      *context,
                                                 jsid            name);
JSObject*   gjs_lookup_function_object          (JSContext      *context,
                                                 GIFunctionInfo *info);
JSObject *  gjs_lookup_generic_constructor      (JSContext      *context,
                                                 GIBaseInfo     *info);
JSObject *  gjs_lookup_generic_prototype        (JSContext      *context,
                                                 GIBaseInfo     *info);
JSBool      gjs_define_info                     (JSContext      *context,
                                                 JSObject       *in_object,
                                                 GIBaseInfo     *info,
                                                 gboolean       *defined);
char*       gjs_camel_from_hyphen               (const char     *hyphen_name);
char*       gjs_hyphen_from_camel               (const char     *camel_name);


#if GJS_VERBOSE_ENABLE_GI_USAGE
void _gjs_log_info_usage(GIBaseInfo *info);
#endif

G_END_DECLS

#endif  /* __GJS_REPO_H__ */
