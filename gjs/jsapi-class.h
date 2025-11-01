/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento

#pragma once

#include <config.h>

#include <js/CallArgs.h>  // for JSNative
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>

#include "gjs/macros.h"

struct JSFunctionSpec;
struct JSPropertySpec;

GJS_JSAPI_RETURN_CONVENTION
bool gjs_init_class_dynamic(
    JSContext*, JS::HandleObject in_object, JS::HandleObject parent_proto,
    const char* ns_name, const char* class_name, const JSClass*,
    JSNative constructor_native, unsigned nargs, JSPropertySpec* ps,
    JSFunctionSpec* fs, JSPropertySpec* static_ps, JSFunctionSpec* static_fs,
    JS::MutableHandleObject prototype, JS::MutableHandleObject constructor);

[[nodiscard]]
bool gjs_typecheck_instance(JSContext*, JS::HandleObject,
                            const JSClass* static_clasp, bool throw_error);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_construct_object_dynamic(JSContext*, JS::HandleObject proto,
                                       const JS::HandleValueArray& args);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_property_dynamic(JSContext*, JS::HandleObject proto,
                                 const char* prop_name, JS::HandleId,
                                 const char* func_namespace, JSNative getter,
                                 JS::HandleValue getter_slot, JSNative setter,
                                 JS::HandleValue setter_slot, unsigned flags);

[[nodiscard]]
JS::Value gjs_dynamic_property_private_slot(JSObject* accessor_obj);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_in_prototype_chain(JSContext*, JS::HandleObject proto,
                                   JS::HandleObject check_obj,
                                   bool* is_in_chain);
