/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef UTIL_MISC_H_
#define UTIL_MISC_H_

bool    gjs_environment_variable_is_set   (const char *env_variable_name);

char** gjs_g_strv_concat(char*** strv_array, int len);

#endif  // UTIL_MISC_H_
