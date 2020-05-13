/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento

#ifndef GJS_JSAPI_CLASS_H_
#define GJS_JSAPI_CLASS_H_

#include <config.h>

#include <glib-object.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gi/wrapperutils.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_init_class_dynamic(
    JSContext* cx, JS::HandleObject in_object, JS::HandleObject parent_proto,
    const char* ns_name, const char* class_name, const JSClass* clasp,
    JSNative constructor_native, unsigned nargs, JSPropertySpec* ps,
    JSFunctionSpec* fs, JSPropertySpec* static_ps, JSFunctionSpec* static_fs,
    JS::MutableHandleObject prototype, JS::MutableHandleObject constructor);

[[nodiscard]] bool gjs_typecheck_instance(JSContext* cx, JS::HandleObject obj,
                                          const JSClass* static_clasp,
                                          bool throw_error);

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_construct_object_dynamic(JSContext                  *cx,
                                       JS::HandleObject            proto,
                                       const JS::HandleValueArray& args);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_property_dynamic(JSContext       *cx,
                                 JS::HandleObject proto,
                                 const char      *prop_name,
                                 const char      *func_namespace,
                                 JSNative         getter,
                                 JSNative         setter,
                                 JS::HandleValue  private_slot,
                                 unsigned         flags);

[[nodiscard]] JS::Value gjs_dynamic_property_private_slot(
    JSObject* accessor_obj);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_in_prototype_chain(JSContext* cx, JS::HandleObject proto,
                                   JS::HandleObject check_obj,
                                   bool* is_in_chain);

#endif  // GJS_JSAPI_CLASS_H_
