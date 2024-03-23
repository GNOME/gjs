/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_NS_H_
#define GI_NS_H_

#include <config.h>

#include "gjs/macros.h"

class JSObject;
struct JSContext;

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_ns(JSContext    *context,
                        const char   *ns_name);

#endif  // GI_NS_H_
