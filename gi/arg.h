/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_ARG_H_
#define GI_ARG_H_

#include <config.h>

#include <stddef.h>  // for size_t
#include <stdint.h>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>  // for GHashTable

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

// Different roles for a GIArgument; currently used only in exception and debug
// messages.
typedef enum {
    GJS_ARGUMENT_ARGUMENT,
    GJS_ARGUMENT_RETURN_VALUE,
    GJS_ARGUMENT_FIELD,
    GJS_ARGUMENT_LIST_ELEMENT,
    GJS_ARGUMENT_HASH_ELEMENT,
    GJS_ARGUMENT_ARRAY_ELEMENT
} GjsArgumentType;

enum class GjsArgumentFlags : uint8_t {
    NONE = 0,
    MAY_BE_NULL = 1 << 0,
    CALLER_ALLOCATES = 1 << 1,
    SKIP_IN = 1 << 2,
    SKIP_OUT = 1 << 3,
    SKIP_ALL = SKIP_IN | SKIP_OUT,
    ARG_IN = 1 << 4,
    ARG_OUT = 1 << 5,
    ARG_INOUT = ARG_IN | ARG_OUT,
};

// Overload operator| so that Visual Studio won't complain
// when converting unsigned char to GjsArgumentFlags
GjsArgumentFlags operator|(GjsArgumentFlags const& v1, GjsArgumentFlags const& v2);

[[nodiscard]] char* gjs_argument_display_name(const char* arg_name,
                                              GjsArgumentType arg_type);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_callback_out_arg(JSContext* context, JS::HandleValue value,
                                   GIArgInfo* arg_info, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_explicit_array(JSContext* cx, JS::HandleValue value,
                                 GITypeInfo* type_info, const char* arg_name,
                                 GjsArgumentType arg_type, GITransfer transfer,
                                 GjsArgumentFlags flags, void** contents,
                                 size_t* length_p);

size_t gjs_type_get_element_size(GITypeTag element_type, GITypeInfo* type_info);

void gjs_gi_argument_init_default(GITypeInfo* type_info, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_gi_argument(JSContext*, JS::HandleValue, GITypeInfo*,
                              const char* arg_name, GjsArgumentType, GITransfer,
                              GjsArgumentFlags, GIArgument*);

GJS_JSAPI_RETURN_CONVENTION
bool inline gjs_value_to_gi_argument(JSContext* cx, JS::HandleValue value,
                                     GITypeInfo* type_info,
                                     GjsArgumentType argument_type,
                                     GITransfer transfer, GIArgument* arg) {
    return gjs_value_to_gi_argument(cx, value, type_info,
                                    nullptr /* arg_name */, argument_type,
                                    transfer, GjsArgumentFlags::NONE, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_gi_argument(JSContext*, JS::MutableHandleValue, GITypeInfo*,
                                GjsArgumentType, GITransfer, GIArgument*);

GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_value_from_gi_argument(JSContext* cx,
                                       JS::MutableHandleValue value_p,
                                       GITypeInfo* type_info, GIArgument* arg,
                                       bool copy_structs) {
    return gjs_value_from_gi_argument(
        cx, value_p, type_info, GJS_ARGUMENT_ARGUMENT,
        copy_structs ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_explicit_array(JSContext* context,
                                   JS::MutableHandleValue value_p,
                                   GITypeInfo* type_info, GITransfer transfer,
                                   GIArgument* arg, int length);

GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_value_from_explicit_array(JSContext* context,
                                          JS::MutableHandleValue value_p,
                                          GITypeInfo* type_info,
                                          GIArgument* arg, int length) {
    return gjs_value_from_explicit_array(context, value_p, type_info,
                                         GI_TRANSFER_EVERYTHING, arg, length);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release(JSContext*, GITransfer, GITypeInfo*,
                             GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_gi_argument_release(JSContext* cx, GITransfer transfer,
                                    GITypeInfo* type_info, GIArgument* arg) {
    return gjs_gi_argument_release(cx, transfer, type_info,
                                   GjsArgumentFlags::NONE, arg);
}
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_out_array(JSContext*, GITransfer, GITypeInfo*,
                                       unsigned length, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_out_array(JSContext*, GITransfer, GITypeInfo*,
                                       GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_array(JSContext*, GITransfer, GITypeInfo*,
                                      unsigned length, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_array(JSContext*, GITransfer, GITypeInfo*,
                                      GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_arg(JSContext*, GITransfer, GITypeInfo*,
                                    GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_gi_argument_release_in_arg(JSContext* cx, GITransfer transfer,
                                           GITypeInfo* type_info,
                                           GIArgument* arg) {
    return gjs_gi_argument_release_in_arg(cx, transfer, type_info,
                                          GjsArgumentFlags::ARG_IN, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool _gjs_flags_value_is_valid(JSContext* cx, GType gtype, int64_t value);

[[nodiscard]] int64_t _gjs_enum_from_int(GIEnumInfo* enum_info, int int_value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_strv(JSContext             *context,
                         JS::MutableHandleValue value_p,
                         const char           **strv);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_strv (JSContext   *context,
                        JS::Value    array_value,
                        unsigned int length,
                        void       **arr_p);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_g_value_array(JSContext* cx, JS::MutableHandleValue value_p,
                                  GITypeInfo* param_info, GITransfer,
                                  const GValue* gvalue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_from_g_hash(JSContext* cx, JS::MutableHandleValue,
                            GITypeInfo* key_param_info,
                            GITypeInfo* val_param_info, GITransfer transfer,
                            GHashTable* hash);

#endif  // GI_ARG_H_
