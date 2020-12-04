/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

#ifndef GJS_MODULE_H_
#define GJS_MODULE_H_

#include <config.h>

#include <gio/gio.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
JSObject *
gjs_module_import(JSContext       *cx,
                  JS::HandleObject importer,
                  JS::HandleId     id,
                  const char      *name,
                  GFile           *file);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_get_native_registry(JSObject* global);

#endif  // GJS_MODULE_H_
