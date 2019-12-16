/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018  Philip Chimento <philip.chimento@gmail.com>
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

#ifndef GJS_DEPRECATION_H_
#define GJS_DEPRECATION_H_

struct JSContext;

enum GjsDeprecationMessageId {
    None,
    ByteArrayInstanceToString,
    DeprecatedGObjectProperty,
};

void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            GjsDeprecationMessageId message);

#endif  // GJS_DEPRECATION_H_
