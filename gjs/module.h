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
JSObject* gjs_script_module_build_private(JSContext* cx, const char* uri);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_get_native_registry(JSObject* global);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_get_module_registry(JSObject* global);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_get_source_map_registry(JSObject* global);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_module_load(JSContext* cx, const char* identifier,
                          const char* uri);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_module_resolve(JSContext* cx,
                             JS::HandleValue importing_module_priv,
                             JS::HandleObject module_request);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_populate_module_meta(JSContext* cx, JS::HandleValue private_ref,
                              JS::HandleObject meta_object);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_dynamic_module_resolve(JSContext* cx,
                                JS::HandleValue importing_module_priv,
                                JS::HandleObject module_request,
                                JS::HandleObject internal_promise);

#endif  // GJS_MODULE_H_
