/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include <girepository.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gi/arg-cache.h"
#include "gi/function.h"
#include "gjs/jsapi-util.h"

bool gjs_arg_cache_build_return(JSContext*, GjsArgumentCache* self,
                                GjsParamType* param_types,
                                GICallableInfo* callable,
                                bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    GITypeInfo return_type;
    g_callable_info_load_return_type(callable, &return_type);

    if (g_type_info_get_tag(&return_type) == GI_TYPE_TAG_VOID) {
        *inc_counter_out = false;
        self->param_type = PARAM_SKIPPED;
        return true;
    }

    *inc_counter_out = true;
    self->param_type = PARAM_NORMAL;

    if (g_type_info_get_tag(&return_type) == GI_TYPE_TAG_ARRAY) {
        int length_pos = g_type_info_get_array_length(&return_type);
        if (length_pos >= 0) {
            param_types[length_pos] = PARAM_SKIPPED;
            return true;
        }
    }

    return true;
}

bool gjs_arg_cache_build_arg(JSContext* cx, GjsArgumentCache* self,
                             GjsParamType* param_types, int gi_index,
                             GIDirection direction, GIArgInfo* arg,
                             GICallableInfo* callable, bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    GITypeInfo type_info;
    g_arg_info_load_type(arg, &type_info);

    self->param_type = PARAM_NORMAL;
    *inc_counter_out = true;

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GjsAutoBaseInfo interface_info = g_type_info_get_interface(&type_info);
        if (interface_info.type() == GI_INFO_TYPE_CALLBACK) {
            if (direction != GI_DIRECTION_IN) {
                // Can't do callbacks for out or inout
                gjs_throw(cx,
                          "Function %s.%s has a callback out-argument %s, not "
                          "supported",
                          g_base_info_get_namespace(callable),
                          g_base_info_get_name(callable),
                          g_base_info_get_name(arg));
                return false;
            }

            if (strcmp(interface_info.name(), "DestroyNotify") == 0 &&
                strcmp(interface_info.ns(), "GLib") == 0) {
                // We don't know (yet) what to do with GDestroyNotify appearing
                // before a callback. If the callback comes later in the
                // argument list, then PARAM_UNKNOWN will be overwritten with
                // PARAM_SKIPPED. If no callback follows, then this is probably
                // an unsupported function, so the value will remain
                // PARAM_UNKNOWN.
                self->param_type = PARAM_UNKNOWN;
                *inc_counter_out = false;
            } else {
                self->param_type = PARAM_CALLBACK;

                int destroy_pos = g_arg_info_get_destroy(arg);
                int closure_pos = g_arg_info_get_closure(arg);

                if (destroy_pos >= 0)
                    param_types[destroy_pos] = PARAM_SKIPPED;

                if (closure_pos >= 0)
                    param_types[closure_pos] = PARAM_SKIPPED;

                if (destroy_pos >= 0 && closure_pos < 0) {
                    gjs_throw(cx,
                              "Function %s.%s has a GDestroyNotify but no "
                              "user_data, not supported",
                              g_base_info_get_namespace(callable),
                              g_base_info_get_name(callable));
                    return false;
                }
            }
        }
    } else if (type_tag == GI_TYPE_TAG_ARRAY) {
        if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
            int length_pos = g_type_info_get_array_length(&type_info);

            if (length_pos >= 0) {
                self->param_type = PARAM_ARRAY;
                if (param_types[length_pos] != PARAM_SKIPPED) {
                    param_types[length_pos] = PARAM_SKIPPED;
                    if (length_pos < gi_index) {
                        // we already collected length_pos, remove it
                        *inc_counter_out = false;
                    }
                }
            }
        }
    }

    return true;
}

bool gjs_arg_cache_build_inout_arg(JSContext*, GjsArgumentCache* in_self,
                                   GjsArgumentCache* out_self,
                                   GjsParamType* param_types, int gi_index,
                                   GIArgInfo* arg, bool* inc_counter_out) {
    g_assert(inc_counter_out && "forgot out parameter");

    GITypeInfo type_info;
    g_arg_info_load_type(arg, &type_info);

    in_self->param_type = PARAM_NORMAL;
    out_self->param_type = PARAM_NORMAL;
    *inc_counter_out = true;

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_ARRAY) {
        if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
            int length_pos = g_type_info_get_array_length(&type_info);

            if (length_pos >= 0) {
                param_types[length_pos] = PARAM_SKIPPED;
                in_self->param_type = PARAM_ARRAY;
                out_self->param_type = PARAM_ARRAY;

                if (length_pos < gi_index) {
                    // we already collected length_pos, remove it
                    *inc_counter_out = false;
                }
            }
        }
    }

    return true;
}
