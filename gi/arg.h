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
    FILENAME = 1 << 4,  //  Sharing the bit with UNSIGNED, used only for strings
    UNSIGNED = 1 << 4,  //  Sharing the bit with FILENAME, used only for enums
};

[[nodiscard]] char* gjs_argument_display_name(const char* arg_name,
                                              GjsArgumentType arg_type);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_arg(JSContext      *context,
                      JS::HandleValue value,
                      GIArgInfo      *arg_info,
                      GIArgument     *arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_explicit_array(JSContext* cx, JS::HandleValue value,
                                 GITypeInfo* type_info, const char* arg_name,
                                 GjsArgumentType arg_type, GITransfer transfer,
                                 GjsArgumentFlags flags, void** contents,
                                 size_t* length_p);

void gjs_gi_argument_init_default(GITypeInfo* type_info, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_g_argument(JSContext* cx, JS::HandleValue value,
                             GITypeInfo* type_info, const char* arg_name,
                             GjsArgumentType argument_type, GITransfer transfer,
                             GjsArgumentFlags flags, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool inline gjs_value_to_g_argument(JSContext* cx, JS::HandleValue value,
                                    GITypeInfo* type_info,
                                    GjsArgumentType argument_type,
                                    GITransfer transfer, GIArgument* arg) {
    return gjs_value_to_g_argument(cx, value, type_info, nullptr /* arg_name */,
                                   argument_type, transfer,
                                   GjsArgumentFlags::NONE, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_g_argument(JSContext             *context,
                               JS::MutableHandleValue value_p,
                               GITypeInfo            *type_info,
                               GIArgument            *arg,
                               bool                   copy_structs);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_explicit_array(JSContext             *context,
                                   JS::MutableHandleValue value_p,
                                   GITypeInfo            *type_info,
                                   GIArgument            *arg,
                                   int                    length);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_g_argument_release    (JSContext  *context,
                                GITransfer  transfer,
                                GITypeInfo *type_info,
                                GArgument  *arg);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_g_argument_release_out_array(JSContext* cx, GITransfer transfer,
                                      GITypeInfo* type_info, unsigned length,
                                      GIArgument* arg);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_g_argument_release_in_array(JSContext* cx, GITransfer transfer,
                                     GITypeInfo* type_info, unsigned length,
                                     GIArgument* arg);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_g_argument_release_in_arg (JSContext  *context,
                                    GITransfer  transfer,
                                    GITypeInfo *type_info,
                                    GArgument  *arg);

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
                                  GITypeInfo* param_info, const GValue* gvalue);

#endif  // GI_ARG_H_
