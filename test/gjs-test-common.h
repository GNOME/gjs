/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento

#pragma once

#include <config.h>

struct JSContext;

char* gjs_test_get_exception_message(JSContext*);
