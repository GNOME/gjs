/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento

#ifndef GJS_JSAPI_CLASS_H_
#define GJS_JSAPI_CLASS_H_

#include <config.h>

#include <js/CallArgs.h>  // for JSNative
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>

#include "gjs/macros.h"

struct JSFunctionSpec;
struct JSPropertySpec;

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
bool gjs_define_property_dynamic(JSContext*, JS::HandleObject proto,
                                 const char* prop_name, JS::HandleId,
                                 const char* func_namespace, JSNative getter,
                                 JS::HandleValue getter_slot, JSNative setter,
                                 JS::HandleValue setter_slot, unsigned flags);

GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_define_property_dynamic(JSContext* cx, JS::HandleObject proto,
                                        const char* prop_name, JS::HandleId id,
                                        const char* func_namespace,
                                        JSNative getter, JSNative setter,
                                        JS::HandleValue private_slot,
                                        unsigned flags) {
    return gjs_define_property_dynamic(cx, proto, prop_name, id, func_namespace,
                                       getter, private_slot, setter,
                                       private_slot, flags);
}

[[nodiscard]] JS::Value gjs_dynamic_property_private_slot(
    JSObject* accessor_obj);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_in_prototype_chain(JSContext* cx, JS::HandleObject proto,
                                   JS::HandleObject check_obj,
                                   bool* is_in_chain);

#endif  // GJS_JSAPI_CLASS_H_
