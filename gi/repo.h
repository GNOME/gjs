/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_REPO_H_
#define GI_REPO_H_

#include <config.h>

#include <js/TypeDecls.h>

#include "gi/info.h"
#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_repo(JSContext              *cx,
                     JS::MutableHandleObject repo);
GJS_JSAPI_RETURN_CONVENTION
JSObject*   gjs_lookup_private_namespace        (JSContext      *context);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_namespace_object(JSContext*, const GI::BaseInfo);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_lookup_namespace_object_by_name(JSContext   *context,
                                              JS::HandleId name);

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

#endif  // GI_REPO_H_
