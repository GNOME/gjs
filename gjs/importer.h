/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#pragma once

#include <config.h>

#include <string>
#include <vector>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_create_root_importer(JSContext*,
                                   const std::vector<std::string>& search_path);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_import_native_module(JSContext*, JS::HandleObject importer,
                              const char* id_str);
