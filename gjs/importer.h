/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GJS_IMPORTER_H_
#define GJS_IMPORTER_H_

#include <config.h>

#include <string>
#include <vector>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_root_importer(JSContext* cx,
                                   const std::vector<std::string>& search_path);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_import_native_module(JSContext       *cx,
                              JS::HandleObject importer,
                              const char      *name);

#endif  // GJS_IMPORTER_H_
