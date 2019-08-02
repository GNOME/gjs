/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017 Chun-wei Fan
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

#ifndef GJS_MACROS_H_
#define GJS_MACROS_H_

#include <glib.h> /* IWYU pragma: keep */

#ifdef G_OS_WIN32
# ifdef GJS_COMPILATION
#  define GJS_EXPORT __declspec(dllexport)
# else
#  define GJS_EXPORT __declspec(dllimport)
# endif
#    define siginfo_t void
#else
# define GJS_EXPORT
#endif

/**
 * GJS_USE:
 *
 * Indicates a return value must be used, or the compiler should log a warning.
 * If it is really okay to ignore the return value, use mozilla::Unused to
 * bypass this warning.
 */
#if defined(__GNUC__) || defined(__clang__)
#    define GJS_USE __attribute__((warn_unused_result))
#else
#    define GJS_USE
#endif

/**
 * GJS_JSAPI_RETURN_CONVENTION:
 *
 * Same as %GJS_USE, but indicates that a return value of true or non-null means
 * that no exception must be pending on the passed-in #JSContext. Conversely, a
 * return value of false or nullptr means that an exception must be pending, or
 * else an uncatchable exception has been thrown.
 *
 * It's intended for use by static analysis tools to do better consistency
 * checks. If not using them, then it has the same effect as %GJS_USE above.
 * It's also intended as documentation for the programmer.
 */
#ifdef __clang_analyzer__
#    define GJS_JSAPI_RETURN_CONVENTION \
        GJS_USE                         \
        __attribute__((annotate("jsapi_return_convention")))
#else
#    define GJS_JSAPI_RETURN_CONVENTION GJS_USE
#endif

#ifdef __GNUC__
#    define GJS_ALWAYS_INLINE __attribute__((always_inline))
#else
#    define GJS_ALWAYS_INLINE
#endif

#endif /* GJS_MACROS_H_ */
