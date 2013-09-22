/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2012  Red Hat, Inc.
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

#ifndef __GJS_GTYPE_H__
#define __GJS_GTYPE_H__

#include <glib.h>
#include <girepository.h>
#include "gjs/jsapi-util.h"

G_BEGIN_DECLS

jsval      gjs_gtype_create_proto         (JSContext       *context,
                                           JSObject        *module,
                                           const char      *proto_name,
                                           JSObject        *parent);

JSObject * gjs_gtype_create_gtype_wrapper (JSContext *context,
                                           GType      gtype);

GType      gjs_gtype_get_actual_gtype (JSContext *context,
                                       JSObject  *object);

JSBool    gjs_typecheck_gtype         (JSContext             *context,
                                       JSObject              *obj,
                                       JSBool                 throw_error);

G_END_DECLS

#endif  /* __GJS_INTERFACE_H__ */
