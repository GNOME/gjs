/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <scampa.giovanni@gmail.com>

#ifndef GJS_ENGINE_H_
#define GJS_ENGINE_H_

#include <config.h>

#include <stddef.h>  // for size_t

class GjsContextPrivate;
struct JSContext;
struct JSPrincipals;

JSContext* gjs_create_js_context(GjsContextPrivate* uninitialized_gjs);

bool gjs_load_internal_source(JSContext* cx, const char* filename, char** src,
                              size_t* length);

JSPrincipals* get_internal_principals();

#endif  // GJS_ENGINE_H_
