/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento

#ifndef TEST_GJS_TEST_COMMON_H_
#define TEST_GJS_TEST_COMMON_H_

#include <config.h>

struct JSContext;

char* gjs_test_get_exception_message(JSContext* cx);

#endif  // TEST_GJS_TEST_COMMON_H_
