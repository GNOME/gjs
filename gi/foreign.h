/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#ifndef GI_FOREIGN_H_
#define GI_FOREIGN_H_

#include <config.h>

#include <girepository.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>

#include "gi/arg.h"
#include "gjs/macros.h"

typedef bool (*GjsArgOverrideToGArgumentFunc)(
    JSContext* context, JS::Value value, const char* arg_name,
    GjsArgumentType argument_type, GITransfer transfer, GjsArgumentFlags flags,
    GArgument* arg);

typedef bool (*GjsArgOverrideFromGArgumentFunc) (JSContext             *context,
                                                 JS::MutableHandleValue value_p,
                                                 GIArgument            *arg);

typedef bool (*GjsArgOverrideReleaseGArgumentFunc) (JSContext *context,
                                                    GITransfer transfer,
                                                    GArgument *arg);

typedef struct {
    GjsArgOverrideToGArgumentFunc to_func;
    GjsArgOverrideFromGArgumentFunc from_func;
    GjsArgOverrideReleaseGArgumentFunc release_func;
} GjsForeignInfo;

void gjs_struct_foreign_register(const char* gi_namespace,
                                 const char* type_name, GjsForeignInfo* info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_to_g_argument(
    JSContext* context, JS::Value value, GIBaseInfo* interface_info,
    const char* arg_name, GjsArgumentType argument_type, GITransfer transfer,
    GjsArgumentFlags flags, GArgument* arg);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_from_g_argument(JSContext             *context,
                                                JS::MutableHandleValue value_p,
                                                GIBaseInfo            *interface_info,
                                                GIArgument            *arg);

GJS_JSAPI_RETURN_CONVENTION
bool  gjs_struct_foreign_release_g_argument      (JSContext      *context,
                                                  GITransfer      transfer,
                                                  GIBaseInfo     *interface_info,
                                                  GArgument      *arg);

#endif  // GI_FOREIGN_H_
