/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2017 Chun-wei Fan
 */

#ifndef GJS_MACROS_H_
#define GJS_MACROS_H_

#include <glib.h>

#ifdef G_OS_WIN32
# ifdef GJS_COMPILATION
#  define GJS_EXPORT __declspec(dllexport)
# else
#  define GJS_EXPORT __declspec(dllimport)
# endif
#    define siginfo_t void
#else
#    define GJS_EXPORT __attribute__((visibility("default")))
#endif

/**
 * GJS_USE:
 *
 * Indicates a return value must be used, or the compiler should log a warning.
 * Equivalent to [[nodiscard]], but this macro is for use in external headers
 * which are not necessarily compiled with a C++ compiler.
 */
#if defined(__GNUC__) || defined(__clang__)
#    define GJS_USE __attribute__((warn_unused_result))
#else
#    define GJS_USE
#endif

/**
 * GJS_JSAPI_RETURN_CONVENTION:
 *
 * Same as [[nodiscard]], but indicates that a return value of true or non-null
 * means that no exception must be pending on the passed-in #JSContext.
 * Conversely, a return value of false or nullptr means that an exception must
 * be pending, or else an uncatchable exception has been thrown.
 *
 * It's intended for use by static analysis tools to do better consistency
 * checks. If not using them, then it has the same effect as [[nodiscard]].
 * It's also intended as documentation for the programmer.
 */
#ifdef __clang_analyzer__
#    define GJS_JSAPI_RETURN_CONVENTION \
        [[nodiscard]] __attribute__((annotate("jsapi_return_convention")))
#else
#    define GJS_JSAPI_RETURN_CONVENTION [[nodiscard]]
#endif

#ifdef __GNUC__
#    define GJS_ALWAYS_INLINE __attribute__((always_inline))
#else
#    define GJS_ALWAYS_INLINE
#endif

#endif /* GJS_MACROS_H_ */
