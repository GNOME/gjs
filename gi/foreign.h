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

typedef bool (*GjsArgOverrideToGIArgumentFunc)(JSContext*, JS::Value,
                                               const char* arg_name,
                                               GjsArgumentType, GITransfer,
                                               GjsArgumentFlags, GIArgument*);

typedef bool (*GjsArgOverrideFromGIArgumentFunc)(JSContext*,
                                                 JS::MutableHandleValue,
                                                 GIArgument*);

typedef bool (*GjsArgOverrideReleaseGIArgumentFunc)(JSContext*, GITransfer,
                                                    GIArgument*);

typedef struct {
    GjsArgOverrideToGIArgumentFunc to_func;
    GjsArgOverrideFromGIArgumentFunc from_func;
    GjsArgOverrideReleaseGIArgumentFunc release_func;
} GjsForeignInfo;

void gjs_struct_foreign_register(const char* gi_namespace,
                                 const char* type_name, GjsForeignInfo* info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_to_g_argument(
    JSContext* context, JS::Value value, GIBaseInfo* interface_info,
    const char* arg_name, GjsArgumentType argument_type, GITransfer transfer,
    GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_from_g_argument(JSContext             *context,
                                                JS::MutableHandleValue value_p,
                                                GIBaseInfo            *interface_info,
                                                GIArgument            *arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_release_g_argument(JSContext*, GITransfer,
                                           GIBaseInfo* interface_info,
                                           GIArgument*);

#endif  // GI_FOREIGN_H_
