/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>

#include <sstream>

#include <glib-object.h>

#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gi/function.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"

using mozilla::Maybe;

/* Default spidermonkey toString is worthless.  Replace it
 * with something that gives us both the introspection name
 * and a memory address.
 */
bool gjs_wrapper_to_string_func(JSContext* context, JSObject* this_obj,
                                const char* objtype,
                                Maybe<const GI::BaseInfo> info, GType gtype,
                                const void* native_address,
                                JS::MutableHandleValue rval) {
    std::ostringstream out;
    out << '[' << objtype;
    if (!native_address)
        out << " prototype of";
    else
        out << " instance wrapper";

    if (info) {
        out << " GIName:" << info->ns() << "." << info->name();
    } else {
        out << " GType:" << g_type_name(gtype);
    }

    out << " jsobj@" << this_obj;
    if (native_address)
        out << " native@" << native_address;

    out << ']';

    return gjs_string_from_utf8(context, out.str().c_str(), rval);
}

bool gjs_wrapper_throw_nonexistent_field(JSContext* cx, GType gtype,
                                         const char* field_name) {
    gjs_throw(cx, "No property %s on %s", field_name, g_type_name(gtype));
    return false;
}

bool gjs_wrapper_throw_readonly_field(JSContext* cx, GType gtype,
                                      const char* field_name) {
    gjs_throw(cx, "Property %s.%s is not writable", g_type_name(gtype),
              field_name);
    return false;
}

template <GI::InfoTag TAG>
bool gjs_define_static_methods(JSContext* cx, JS::HandleObject constructor,
                               GType gtype, const GI::UnownedInfo<TAG> info) {
    for (GI::AutoFunctionInfo meth_info : info.methods()) {
        // Anything that isn't a method we put on the constructor. This
        // includes <constructor> introspection methods, as well as static
        // methods. We may want to change this to use
        // GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the like
        // in the future.
        if (!meth_info.is_method()) {
            if (!gjs_define_function(cx, constructor, gtype, meth_info))
                return false;
        }
    }

    // Also define class/interface methods if there is a gtype struct

    Maybe<GI::AutoStructInfo> type_struct;
    if constexpr (TAG == GI::InfoTag::OBJECT)
        type_struct = info.class_struct();
    else if constexpr (TAG == GI::InfoTag::INTERFACE)
        type_struct = info.iface_struct();
    if (!type_struct)
        return true;

    for (GI::AutoFunctionInfo meth_info : type_struct->methods()) {
        if (!gjs_define_function(cx, constructor, gtype, meth_info))
            return false;
    }

    return true;
}

// All possible instantiations are needed
template bool gjs_define_static_methods<GI::InfoTag::ENUM>(
    JSContext*, JS::HandleObject constructor, GType, const GI::EnumInfo);
template bool gjs_define_static_methods<GI::InfoTag::INTERFACE>(
    JSContext*, JS::HandleObject constructor, GType, const GI::InterfaceInfo);
template bool gjs_define_static_methods<GI::InfoTag::OBJECT>(
    JSContext*, JS::HandleObject constructor, GType, const GI::ObjectInfo);
template bool gjs_define_static_methods<GI::InfoTag::STRUCT>(
    JSContext*, JS::HandleObject constructor, GType, const GI::StructInfo);
template bool gjs_define_static_methods<GI::InfoTag::UNION>(
    JSContext*, JS::HandleObject constructor, GType, const GI::UnionInfo);
