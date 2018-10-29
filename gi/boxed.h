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

#ifndef __GJS_BOXED_H__
#define __GJS_BOXED_H__

#include <stdbool.h>
#include <glib.h>

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

#include <girepository.h>

G_BEGIN_DECLS

typedef enum {
    GJS_BOXED_CREATION_NONE    =  0,
    GJS_BOXED_CREATION_NO_COPY = (1 << 0)
} GjsBoxedCreationFlags;

/* Hack for now... why doesn't gobject-introspection have this? */
typedef GIStructInfo GIBoxedInfo;

void      gjs_define_boxed_class       (JSContext             *context,
                                        JS::HandleObject       in_object,
                                        GIBoxedInfo           *info);

GJS_JSAPI_RETURN_CONVENTION
void*     gjs_c_struct_from_boxed      (JSContext             *context,
                                        JS::HandleObject       obj);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_boxed_from_c_struct      (JSContext             *context,
                                        GIStructInfo          *info,
                                        void                  *gboxed,
                                        GjsBoxedCreationFlags  flags);
GJS_USE
bool      gjs_typecheck_boxed          (JSContext             *context,
                                        JS::HandleObject       obj,
                                        GIStructInfo          *expected_info,
                                        GType                  expected_type,
                                        bool                   throw_error);

G_END_DECLS

#endif  /* __GJS_BOXED_H__ */
