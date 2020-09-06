/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_REPO_H_
#define GI_REPO_H_

#include <config.h>

#include <girepository.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"
#include "util/log.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_repo(JSContext              *cx,
                     JS::MutableHandleObject repo);

[[nodiscard]] const char* gjs_info_type_name(GIInfoType type);
GJS_JSAPI_RETURN_CONVENTION
JSObject*   gjs_lookup_private_namespace        (JSContext      *context);
GJS_JSAPI_RETURN_CONVENTION
JSObject*   gjs_lookup_namespace_object         (JSContext      *context,
                                                 GIBaseInfo     *info);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_lookup_namespace_object_by_name(JSContext   *context,
                                              JS::HandleId name);

GJS_JSAPI_RETURN_CONVENTION
JSObject *  gjs_lookup_generic_constructor      (JSContext      *context,
                                                 GIBaseInfo     *info);
GJS_JSAPI_RETURN_CONVENTION
JSObject *  gjs_lookup_generic_prototype        (JSContext      *context,
                                                 GIBaseInfo     *info);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_new_object_with_generic_prototype(JSContext* cx,
                                                GIBaseInfo* info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_info(JSContext       *context,
                     JS::HandleObject in_object,
                     GIBaseInfo      *info,
                     bool            *defined);

[[nodiscard]] char* gjs_hyphen_from_camel(const char* camel_name);

#if GJS_VERBOSE_ENABLE_GI_USAGE
void _gjs_log_info_usage(GIBaseInfo *info);
#endif

#endif  // GI_REPO_H_
