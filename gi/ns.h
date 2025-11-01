/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#pragma once

#include <config.h>

#include "gjs/macros.h"

class JSObject;
struct JSContext;

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_ns(JSContext*, const char* ns_name);
