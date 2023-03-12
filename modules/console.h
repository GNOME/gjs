/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef MODULES_CONSOLE_H_
#define MODULES_CONSOLE_H_

#include <config.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_console_private_stuff(JSContext* context,
                                      JS::MutableHandleObject module);

#endif  // MODULES_CONSOLE_H_
