/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_ARG_H_
#define GI_ARG_H_

#include <config.h>

#include <stddef.h>  // for size_t
#include <stdint.h>

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>  // for GHashTable

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/info.h"
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
                                   const GI::ArgInfo arg_info, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_explicit_array(JSContext*, JS::HandleValue,
                                 const GI::TypeInfo, const char* arg_name,
                                 GjsArgumentType, GITransfer, GjsArgumentFlags,
                                 void** contents, size_t* length_p);

size_t gjs_type_get_element_size(GITypeTag element_tag, const GI::TypeInfo);
// FIXME: GI::TypeInfo&?

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_gi_argument(JSContext*, JS::HandleValue, const GI::TypeInfo,
                              const char* arg_name, GjsArgumentType, GITransfer,
                              GjsArgumentFlags, GIArgument*);

GJS_JSAPI_RETURN_CONVENTION
bool inline gjs_value_to_gi_argument(JSContext* cx, JS::HandleValue value,
                                     const GI::TypeInfo type_info,
                                     GjsArgumentType argument_type,
                                     GITransfer transfer, GIArgument* arg) {
    return gjs_value_to_gi_argument(cx, value, type_info,
                                    nullptr /* arg_name */, argument_type,
                                    transfer, GjsArgumentFlags::NONE, arg);
}
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_basic_gi_argument(JSContext*, JS::HandleValue, GITypeTag,
                                    GIArgument*, const char* arg_name,
                                    GjsArgumentType, GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_gerror_gi_argument(JSContext*, JS::HandleValue, GITransfer,
                                     GIArgument*, const char* arg_name,
                                     GjsArgumentType, GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_basic_glist_gi_argument(JSContext*, JS::HandleValue,
                                          GITypeTag element_tag, GIArgument*,
                                          const char* arg_name,
                                          GjsArgumentType);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_basic_gslist_gi_argument(JSContext*, JS::HandleValue,
                                           GITypeTag element_tag, GIArgument*,
                                           const char* arg_name,
                                           GjsArgumentType);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_basic_ghash_gi_argument(JSContext*, JS::HandleValue,
                                          GITypeTag key_tag,
                                          GITypeTag value_tag, GITransfer,
                                          GIArgument*, const char* arg_name,
                                          GjsArgumentType, GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_basic_array_gi_argument(JSContext*, JS::HandleValue,
                                          GITypeTag element_tag, GIArrayType,
                                          GIArgument*, const char* arg_name,
                                          GjsArgumentType, GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_byte_array_gi_argument(JSContext*, JS::HandleValue,
                                         GIArgument*, const char* arg_name,
                                         GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_basic_explicit_array(JSContext*, JS::HandleValue,
                                       GITypeTag element_tag,
                                       const char* arg_name, GjsArgumentType,
                                       GjsArgumentFlags, void** contents_out,
                                       size_t* length_out);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_gdk_atom_gi_argument(JSContext*, JS::HandleValue, GIArgument*,
                                       const char* arg_name, GjsArgumentType);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_interface_gi_argument(JSContext*, JS::HandleValue,
                                        const GI::BaseInfo interface_info,
                                        GITransfer, GIArgument*,
                                        const char* arg_name, GjsArgumentType,
                                        GjsArgumentFlags);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_gi_argument(JSContext*, JS::MutableHandleValue,
                                      GITypeTag, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_gi_argument(JSContext*, JS::MutableHandleValue,
                                const GI::TypeInfo, GjsArgumentType, GITransfer,
                                GIArgument*);

GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_value_from_gi_argument(JSContext* cx,
                                       JS::MutableHandleValue value_p,
                                       const GI::TypeInfo type_info,
                                       GIArgument* arg, bool copy_structs) {
    return gjs_value_from_gi_argument(
        cx, value_p, type_info, GJS_ARGUMENT_ARGUMENT,
        copy_structs ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_ghash(JSContext*, JS::MutableHandleValue,
                                GITypeTag key_tag, GITypeTag value_tag,
                                GHashTable*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_basic_glist_gi_argument(JSContext*, JS::MutableHandleValue,
                                            GITypeTag element_tag, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_basic_gslist_gi_argument(JSContext*, JS::MutableHandleValue,
                                             GITypeTag element_tag,
                                             GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_basic_zero_terminated_array(JSContext*,
                                                JS::MutableHandleValue,
                                                GITypeTag element_tag,
                                                void* c_array);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_fixed_size_array_gi_argument(JSContext*,
                                                       JS::MutableHandleValue,
                                                       GITypeTag element_tag,
                                                       size_t fixed_size,
                                                       GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_explicit_array(JSContext*, JS::MutableHandleValue,
                                         GITypeTag element_tag, GIArgument*,
                                         size_t length);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_explicit_array(JSContext*, JS::MutableHandleValue,
                                   const GI::TypeInfo, GITransfer, GIArgument*,
                                   size_t length);

GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_value_from_explicit_array(JSContext* context,
                                          JS::MutableHandleValue value_p,
                                          const GI::TypeInfo type_info,
                                          GIArgument* arg, size_t length) {
    return gjs_value_from_explicit_array(context, value_p, type_info,
                                         GI_TRANSFER_EVERYTHING, arg, length);
}
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_byte_array_gi_argument(JSContext*, JS::MutableHandleValue,
                                           GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_garray_gi_argument(JSContext*, JS::MutableHandleValue,
                                             GITypeTag element_tag,
                                             GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_from_basic_gptrarray_gi_argument(JSContext*,
                                                JS::MutableHandleValue,
                                                GITypeTag element_tag,
                                                GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release(JSContext*, GITransfer, const GI::TypeInfo,
                             GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_gi_argument_release(JSContext* cx, GITransfer transfer,
                                    const GI::TypeInfo type_info,
                                    GIArgument* arg) {
    return gjs_gi_argument_release(cx, transfer, type_info,
                                   GjsArgumentFlags::NONE, arg);
}
void gjs_gi_argument_release_basic(GITransfer, GITypeTag, GjsArgumentFlags,
                                   GIArgument*);
void gjs_gi_argument_release_basic_glist(GITransfer, GITypeTag element_tag,
                                         GIArgument*);
void gjs_gi_argument_release_basic_gslist(GITransfer, GITypeTag element_tag,
                                          GIArgument*);
void gjs_gi_argument_release_basic_ghash(GITransfer, GITypeTag key_tag,
                                         GITypeTag value_tag, GIArgument*);
void gjs_gi_argument_release_basic_garray(GITransfer transfer,
                                          GITypeTag element_tag,
                                          GIArgument* arg);
void gjs_gi_argument_release_basic_gptrarray(GITransfer transfer,
                                             GITypeTag element_tag,
                                             GIArgument* arg);
void gjs_gi_argument_release_basic_c_array(GITransfer, GITypeTag element_tag,
                                           GIArgument*);
void gjs_gi_argument_release_basic_c_array(GITransfer, GITypeTag element_tag,
                                           size_t length, GIArgument*);
void gjs_gi_argument_release_basic_in_array(GITransfer, GITypeTag element_tag,
                                            GIArgument*);
void gjs_gi_argument_release_basic_in_array(GITransfer, GITypeTag element_tag,
                                            size_t length, GIArgument*);
void gjs_gi_argument_release_basic_out_array(GITransfer, GITypeTag element_tag,
                                             GIArgument*);
void gjs_gi_argument_release_basic_out_array(GITransfer, GITypeTag element_tag,
                                             size_t length, GIArgument*);
void gjs_gi_argument_release_byte_array(GIArgument* arg);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_out_array(JSContext*, GITransfer,
                                       const GI::TypeInfo, size_t length,
                                       GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_out_array(JSContext*, GITransfer,
                                       const GI::TypeInfo, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_array(JSContext*, GITransfer,
                                      const GI::TypeInfo, size_t length,
                                      GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_array(JSContext*, GITransfer,
                                      const GI::TypeInfo, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
bool gjs_gi_argument_release_in_arg(JSContext*, GITransfer, const GI::TypeInfo,
                                    GjsArgumentFlags, GIArgument*);
GJS_JSAPI_RETURN_CONVENTION
inline bool gjs_gi_argument_release_in_arg(JSContext* cx, GITransfer transfer,
                                           const GI::TypeInfo type_info,
                                           GIArgument* arg) {
    return gjs_gi_argument_release_in_arg(cx, transfer, type_info,
                                          GjsArgumentFlags::ARG_IN, arg);
}

GJS_JSAPI_RETURN_CONVENTION
bool _gjs_flags_value_is_valid(JSContext* cx, GType gtype, int64_t value);

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
                                  const GI::TypeInfo param_info, GITransfer,
                                  const GValue* gvalue);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_object_from_g_hash(JSContext* cx, JS::MutableHandleValue,
                            const GI::TypeInfo key_param_info,
                            const GI::TypeInfo val_param_info,
                            GITransfer transfer, GHashTable* hash);

#endif  // GI_ARG_H_
