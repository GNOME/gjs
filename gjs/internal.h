// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#pragma once

#include <config.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_load_internal_module(JSContext*, const char* identifier);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_compile_module(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_compile_internal_module(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_get_registry(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_get_source_map_registry(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_set_global_module_loader(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_set_module_private(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_parse_uri(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_resolve_relative_resource_or_file(JSContext*, unsigned,
                                                    JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_load_resource_or_file(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_load_resource_or_file_async(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_uri_exists(JSContext*, unsigned, JS::Value*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_internal_atob(JSContext*, unsigned, JS::Value*);
