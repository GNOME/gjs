/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <string.h>

#include "gi/function.h"
#include "gi/wrapperutils.h"
#include "gjs/context-private.h"

/* Default spidermonkey toString is worthless.  Replace it
 * with something that gives us both the introspection name
 * and a memory address.
 */
bool gjs_wrapper_to_string_func(JSContext* context, JSObject* this_obj,
                                const char* objtype, GIBaseInfo* info,
                                GType gtype, const void* native_address,
                                JS::MutableHandleValue rval) {
    GString *buf;
    bool ret = false;

    buf = g_string_new("");
    g_string_append_c(buf, '[');
    g_string_append(buf, objtype);
    if (!native_address)
        g_string_append(buf, " prototype of");
    else
        g_string_append(buf, " instance wrapper");

    if (info) {
        g_string_append_printf(buf, " GIName:%s.%s",
                               g_base_info_get_namespace(info),
                               g_base_info_get_name(info));
    } else {
        g_string_append(buf, " GType:");
        g_string_append(buf, g_type_name(gtype));
    }

    g_string_append_printf(buf, " jsobj@%p", this_obj);
    if (native_address)
        g_string_append_printf(buf, " native@%p", native_address);

    g_string_append_c(buf, ']');

    if (!gjs_string_from_utf8(context, buf->str, rval))
        goto out;

    ret = true;
 out:
    g_string_free(buf, true);
    return ret;
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

bool gjs_wrapper_define_gtype_prop(JSContext* cx, JS::HandleObject constructor,
                                   GType gtype) {
    JS::RootedObject gtype_obj(cx, gjs_gtype_create_gtype_wrapper(cx, gtype));
    if (!gtype_obj)
        return false;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    return JS_DefinePropertyById(cx, constructor, atoms.gtype(), gtype_obj,
                                 JSPROP_PERMANENT);
}

// These policies work around having separate g_foo_info_get_n_methods() and
// g_foo_info_get_method() functions for different GIInfoTypes. It's not
// possible to use GIFooInfo* as the template parameter, because the GIFooInfo
// structs are all typedefs of GIBaseInfo. It's also not possible to use the
// GIInfoType enum value as the template parameter, because GI_INFO_TYPE_BOXED
// could be either a GIStructInfo or GIUnionInfo.
template <InfoType::Tag TAG>
struct InfoMethodsPolicy {};

static GIStructInfo* no_type_struct(GIBaseInfo*) { return nullptr; }

#define DECLARE_POLICY(tag, type, type_struct_func)                            \
    template <>                                                                \
    struct InfoMethodsPolicy<InfoType::tag> {                                  \
        using T = GI##tag##Info;                                               \
        static constexpr int (*n_methods)(T*) = g_##type##_info_get_n_methods; \
        static constexpr GIFunctionInfo* (*method)(T*, int) = g_##type         \
            ##_info_get_method;                                                \
        static constexpr GIStructInfo* (*type_struct)(T*) = type_struct_func;  \
    };

DECLARE_POLICY(Enum, enum, no_type_struct)
DECLARE_POLICY(Interface, interface, g_interface_info_get_iface_struct)
DECLARE_POLICY(Object, object, g_object_info_get_class_struct)
DECLARE_POLICY(Struct, struct, no_type_struct)
DECLARE_POLICY(Union, union, no_type_struct)

#undef DECLARE_POLICY

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
