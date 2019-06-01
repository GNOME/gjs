/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef GI_ARG_H_
#define GI_ARG_H_

#include <stdbool.h>
#include <glib.h>

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

#include <girepository.h>

G_BEGIN_DECLS

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

GJS_USE
char* gjs_argument_display_name(const char* arg_name, GjsArgumentType arg_type);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_arg(JSContext      *context,
                      JS::HandleValue value,
                      GIArgInfo      *arg_info,
                      GIArgument     *arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_explicit_array(JSContext       *context,
                                 JS::HandleValue  value,
                                 GIArgInfo       *arg_info,
                                 GIArgument      *arg,
                                 size_t          *length_p);

void gjs_gi_argument_init_default(GITypeInfo* type_info, GIArgument* arg);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_value_to_g_argument (JSContext      *context,
                              JS::HandleValue value,
                              GITypeInfo     *type_info,
                              const char     *arg_name,
                              GjsArgumentType argument_type,
                              GITransfer      transfer,
                              bool            may_be_null,
                              GArgument      *arg);

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

GJS_USE
int64_t _gjs_enum_from_int(GIEnumInfo* enum_info, int int_value);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_from_strv(JSContext             *context,
                         JS::MutableHandleValue value_p,
                         const char           **strv);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_array_to_strv (JSContext   *context,
                        JS::Value    array_value,
                        unsigned int length,
                        void       **arr_p);

G_END_DECLS

#endif  // GI_ARG_H_
