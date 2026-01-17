/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#pragma once

#include <config.h>

#include <girepository/girepository.h>

#include <js/TypeDecls.h>
#include <js/Value.h>

#include "gi/arg.h"
#include "gi/info.h"
#include "gjs/macros.h"

using GjsArgOverrideToGIArgumentFunc = bool (*)(JSContext*, JS::Value,
                                                const char* arg_name,
                                                GjsArgumentType, GITransfer,
                                                GjsArgumentFlags, GIArgument*);

using GjsArgOverrideFromGIArgumentFunc = bool (*)(JSContext*,
                                                  JS::MutableHandleValue,
                                                  GIArgument*);

using GjsArgOverrideReleaseGIArgumentFunc = bool (*)(JSContext*, GITransfer,
                                                     GIArgument*);

struct GjsForeignInfo {
    GjsArgOverrideToGIArgumentFunc to_func;
    GjsArgOverrideFromGIArgumentFunc from_func;
    GjsArgOverrideReleaseGIArgumentFunc release_func;
};

void gjs_struct_foreign_register(const char* gi_namespace,
                                 const char* type_name, GjsForeignInfo*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_to_gi_argument(JSContext*, JS::Value,
                                               const GI::StructInfo&,
                                               const char* arg_name,
                                               GjsArgumentType, GITransfer,
                                               GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_convert_from_gi_argument(JSContext*,
                                                 JS::MutableHandleValue,
                                                 const GI::StructInfo&,
                                                 GIArgument*);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_struct_foreign_release_gi_argument(JSContext*, GITransfer,
                                            const GI::StructInfo&, GIArgument*);
