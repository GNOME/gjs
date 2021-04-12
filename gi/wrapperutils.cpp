/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

#include <config.h>

#include <sstream>

#include <girepository.h>
#include <glib-object.h>

#include <js/TypeDecls.h>

#include "gi/function.h"
#include "gi/wrapperutils.h"
#include "gjs/jsapi-util.h"

/* Default spidermonkey toString is worthless.  Replace it
 * with something that gives us both the introspection name
 * and a memory address.
 */
bool gjs_wrapper_to_string_func(JSContext* context, JSObject* this_obj,
                                const char* objtype, GIBaseInfo* info,
                                GType gtype, const void* native_address,
                                JS::MutableHandleValue rval) {
    std::ostringstream out;
    out << '[' << objtype;
    if (!native_address)
        out << " prototype of";
    else
        out << " instance wrapper";

    if (info) {
        out << " GIName:" << g_base_info_get_namespace(info) << "."
            << g_base_info_get_name(info);
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

// These policies work around having separate g_foo_info_get_n_methods() and
// g_foo_info_get_method() functions for different GIInfoTypes. It's not
// possible to use GIFooInfo* as the template parameter, because the GIFooInfo
// structs are all typedefs of GIBaseInfo. It's also not possible to use the
// GIInfoType enum value as the template parameter, because GI_INFO_TYPE_BOXED
// could be either a GIStructInfo or GIUnionInfo.
template <typename InfoT>
static inline GIStructInfo* no_type_struct(InfoT*) {
    return nullptr;
}

template <InfoType::Tag TAG, typename InfoT = void,
          int (*NMethods)(InfoT*) = nullptr,
          GIFunctionInfo* (*Method)(InfoT*, int) = nullptr,
          GIStructInfo* (*TypeStruct)(InfoT*) = &no_type_struct<InfoT>>
struct InfoMethodsPolicy {
    static constexpr decltype(NMethods) n_methods = NMethods;
    static constexpr decltype(Method) method = Method;
    static constexpr decltype(TypeStruct) type_struct = TypeStruct;
};

template <>
struct InfoMethodsPolicy<InfoType::Enum>
    : InfoMethodsPolicy<InfoType::Enum, GIEnumInfo, &g_enum_info_get_n_methods,
                        &g_enum_info_get_method> {};
template <>
struct InfoMethodsPolicy<InfoType::Interface>
    : InfoMethodsPolicy<
          InfoType::Interface, GIInterfaceInfo, &g_interface_info_get_n_methods,
          &g_interface_info_get_method, &g_interface_info_get_iface_struct> {};
template <>
struct InfoMethodsPolicy<InfoType::Object>
    : InfoMethodsPolicy<InfoType::Object, GIObjectInfo,
                        &g_object_info_get_n_methods, &g_object_info_get_method,
                        &g_object_info_get_class_struct> {};
template <>
struct InfoMethodsPolicy<InfoType::Struct>
    : InfoMethodsPolicy<InfoType::Struct, GIStructInfo,
                        &g_struct_info_get_n_methods,
                        &g_struct_info_get_method> {};
template <>
struct InfoMethodsPolicy<InfoType::Union>
    : InfoMethodsPolicy<InfoType::Union, GIUnionInfo,
                        &g_union_info_get_n_methods, &g_union_info_get_method> {
};

template <InfoType::Tag TAG>
bool gjs_define_static_methods(JSContext* cx, JS::HandleObject constructor,
                               GType gtype, GIBaseInfo* info) {
    int n_methods = InfoMethodsPolicy<TAG>::n_methods(info);

    for (int ix = 0; ix < n_methods; ix++) {
        GjsAutoFunctionInfo meth_info =
            InfoMethodsPolicy<TAG>::method(info, ix);
        GIFunctionInfoFlags flags = g_function_info_get_flags(meth_info);

        // Anything that isn't a method we put on the constructor. This
        // includes <constructor> introspection methods, as well as static
        // methods. We may want to change this to use
        // GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the like
        // in the future.
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            if (!gjs_define_function(cx, constructor, gtype, meth_info))
                return false;
        }
    }

    // Also define class/interface methods if there is a gtype struct

    GjsAutoStructInfo type_struct = InfoMethodsPolicy<TAG>::type_struct(info);
    // Not an error for it to be null even in the case of Object and Interface;
    // documentation says g_object_info_get_class_struct() and
    // g_interface_info_get_iface_struct() can validly return a null pointer.
    if (!type_struct)
        return true;

    n_methods = g_struct_info_get_n_methods(type_struct);

    for (int ix = 0; ix < n_methods; ix++) {
        GjsAutoFunctionInfo meth_info =
            g_struct_info_get_method(type_struct, ix);

        if (!gjs_define_function(cx, constructor, gtype, meth_info))
            return false;
    }

    return true;
}

// All possible instantiations are needed
template bool gjs_define_static_methods<InfoType::Enum>(
    JSContext* cx, JS::HandleObject constructor, GType gtype, GIBaseInfo* info);
template bool gjs_define_static_methods<InfoType::Interface>(
    JSContext* cx, JS::HandleObject constructor, GType gtype, GIBaseInfo* info);
template bool gjs_define_static_methods<InfoType::Object>(
    JSContext* cx, JS::HandleObject constructor, GType gtype, GIBaseInfo* info);
template bool gjs_define_static_methods<InfoType::Struct>(
    JSContext* cx, JS::HandleObject constructor, GType gtype, GIBaseInfo* info);
template bool gjs_define_static_methods<InfoType::Union>(
    JSContext* cx, JS::HandleObject constructor, GType gtype, GIBaseInfo* info);
