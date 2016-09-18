/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2009  litl, LLC
 * Copyright (c) 2010  Red Hat, Inc.
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

#if !defined (__GJS_GJS_MODULE_H__) && !defined (GJS_COMPILATION)
#error "Only <gjs/gjs-module.h> can be included directly."
#endif

#ifndef __GJS_COMPAT_H__
#define __GJS_COMPAT_H__

#if defined(__clang__)
_Pragma("clang diagnostic push")
_Pragma("clang diagnostic ignored \"-Wuninitialized\"")
_Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"")
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wstrict-prototypes\"")
_Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")
#endif
#include <jsapi.h>
#include <jsdbgapi.h> // Needed by some bits
#if defined(__clang__)
_Pragma("clang diagnostic pop")
#elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic pop")
#endif
#include <glib.h>

#include <gjs/jsapi-util.h>

G_BEGIN_DECLS

#define JSVAL_IS_OBJECT(obj) \
    _Pragma("GCC warning \"JSVAL_IS_OBJECT is deprecated. Use JS::Value::isObjectOrNull() instead.\"") \
    ((obj).isObjectOrNull())

#define JS_GetGlobalObject(cx) gjs_get_global_object(cx)

static JSBool G_GNUC_UNUSED
JS_NewNumberValue(JSContext *cx,
                  double     d,
                  JS::Value *rval)
    {
        *rval = JS_NumberValue(d);
        return rval->isNumber();
    }

G_END_DECLS

#endif  /* __GJS_COMPAT_H__ */
