/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_ENUMERATION_H_
#define GI_ENUMERATION_H_

#include <config.h>

#include <girepository.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_enum_values(JSContext       *context,
                            JS::HandleObject in_object,
                            GIEnumInfo      *info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_enumeration(JSContext       *context,
                            JS::HandleObject in_object,
                            GIEnumInfo      *info);

#endif  // GI_ENUMERATION_H_
