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

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wstrict-prototypes\"")
_Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")
#endif
#include <jsapi.h>
#include <jsdbgapi.h> // Needed by some bits
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
_Pragma("GCC diagnostic pop")
#endif
#include <glib.h>

#include <gjs/jsapi-util.h>

G_BEGIN_DECLS

/* This file inspects jsapi.h and attempts to provide a compatibility shim.
 * See https://bugzilla.gnome.org/show_bug.cgi?id=622896 for some initial discussion.
 */

#define JSVAL_IS_OBJECT(obj) (JSVAL_IS_NULL(obj) || !JSVAL_IS_PRIMITIVE(obj))

#define JS_GetGlobalObject(cx) gjs_get_global_object(cx)

static JSBool G_GNUC_UNUSED JS_NewNumberValue(JSContext *cx, double d, jsval *rval)
    {
        *rval = JS_NumberValue(d);
        if (JSVAL_IS_NUMBER(*rval))
            return JS_TRUE;
        return JS_FALSE;
    }

/**
 * GJS_NATIVE_CONSTRUCTOR_DECLARE:
 * Prototype a constructor.
 */
#define GJS_NATIVE_CONSTRUCTOR_DECLARE(name)            \
static JSBool                                           \
gjs_##name##_constructor(JSContext  *context,           \
                         unsigned    argc,              \
                         jsval      *vp)

/**
 * GJS_NATIVE_CONSTRUCTOR_VARIABLES:
 * Declare variables necessary for the constructor; should
 * be at the very top.
 */
#define GJS_NATIVE_CONSTRUCTOR_VARIABLES(name)          \
    JSObject *object = NULL;                            \
    jsval *argv G_GNUC_UNUSED = JS_ARGV(context, vp);

/**
 * GJS_NATIVE_CONSTRUCTOR_PRELUDE:
 * Call after the initial variable declaration.
 */
#define GJS_NATIVE_CONSTRUCTOR_PRELUDE(name)                            \
    {                                                                   \
        if (!JS_IsConstructing(context, vp)) {                          \
            gjs_throw_constructor_error(context);                       \
            return JS_FALSE;                                            \
        }                                                               \
        object = gjs_new_object_for_constructor(context, &gjs_##name##_class, vp); \
        if (object == NULL)                                             \
            return JS_FALSE;                                            \
    }

/**
 * GJS_NATIVE_CONSTRUCTOR_FINISH:
 * Call this at the end of a constructor when it's completed
 * successfully.
 */
#define GJS_NATIVE_CONSTRUCTOR_FINISH(name)             \
    JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(object));

/**
 * GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT:
 * Defines a constructor whose only purpose is to throw an error
 * and fail. To be used with classes that require a constructor (because they have
 * instances), but whose constructor cannot be used from JS code.
 */
#define GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(name)            \
    GJS_NATIVE_CONSTRUCTOR_DECLARE(name)                        \
    {                                                           \
        gjs_throw_abstract_constructor_error(context, vp);      \
        return JS_FALSE;                                        \
    }

G_END_DECLS

#endif  /* __GJS_COMPAT_H__ */
