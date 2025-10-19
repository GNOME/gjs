/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#pragma once

#include <config.h>

#include <js/TypeDecls.h>

#include "gi/info.h"
#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_repo(JSContext*, JS::MutableHandleObject repo);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_private_namespace(JSContext*);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_namespace_object(JSContext*, const GI::BaseInfo);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_namespace_object_by_name(JSContext*, JS::HandleId name);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_generic_constructor(JSContext*, const GI::BaseInfo);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_generic_prototype(JSContext*, const GI::BaseInfo);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_new_object_with_generic_prototype(JSContext*, const GI::BaseInfo);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_info(JSContext*, JS::HandleObject in_object, const GI::BaseInfo,
                     bool* defined);

[[nodiscard]] char* gjs_hyphen_from_camel(const char* camel_name);
