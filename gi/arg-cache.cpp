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

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <girepository.h>

#include "arg.h"
#include "arg-cache.h"
#include "boxed.h"
#include "closure.h"
#include "function.h"
#include "gerror.h"
#include "object.h"
#include "union.h"
#include "util/log.h"

bool
gjs_arg_cache_build_return(GjsArgumentCache *self,
                           GjsParamType     *param_types,
                           GICallableInfo   *info,
                           bool&             inc_counter)
{
    GITypeInfo return_type;
    g_callable_info_load_return_type(info, &return_type);

    inc_counter = g_type_info_get_tag(&return_type) != GI_TYPE_TAG_VOID;

    int length_pos = g_type_info_get_array_length(&return_type);
    if (length_pos >= 0)
        param_types[length_pos] = PARAM_SKIPPED;

    if (inc_counter)
        self->param_type = PARAM_NORMAL;
    else
        self->param_type = PARAM_SKIPPED;

    return true;
}

bool
gjs_arg_cache_build_in_arg(GjsArgumentCache *self,
                           GjsParamType     *param_types,
                           int               gi_index,
                           GIArgInfo        *arg_info,
                           bool&             inc_counter)
{
    GITypeInfo type_info;
    g_arg_info_load_type(arg_info, &type_info);

    self->param_type = PARAM_NORMAL;
    inc_counter = true;

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo *interface_info = g_type_info_get_interface(&type_info);
        GIInfoType interface_type = g_base_info_get_type(interface_info);
        if (interface_type == GI_INFO_TYPE_CALLBACK) {
            if (strcmp(g_base_info_get_name(interface_info), "DestroyNotify") == 0 &&
                strcmp(g_base_info_get_namespace(interface_info), "GLib") == 0) {
                /* Skip GDestroyNotify if they appear before the respective callback */
                self->param_type = PARAM_SKIPPED;
                inc_counter = false;
            } else {
                self->param_type = PARAM_CALLBACK;

                int destroy = g_arg_info_get_destroy(arg_info);
                int closure = g_arg_info_get_closure(arg_info);

                if (destroy >= 0)
                    param_types[destroy] = PARAM_SKIPPED;

                if (closure >= 0)
                    param_types[closure] = PARAM_SKIPPED;

                if (destroy >= 0 && closure < 0) {
                    /* Function has a GDestroyNotify but no user_data, not supported */
                    g_base_info_unref(interface_info);
                    return false;
                }
            }

            g_base_info_unref(interface_info);
        }
    } else if (type_tag == GI_TYPE_TAG_ARRAY) {
        if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
            int length_pos = g_type_info_get_array_length(&type_info);

            if (length_pos >= 0) {
                param_types[length_pos] = PARAM_SKIPPED;
                self->param_type = PARAM_ARRAY;

                if (length_pos < gi_index) {
                    /* we already collected length_pos, remove it */
                    inc_counter = false;
                }
            }
        }
    }

    return true;
}

bool
gjs_arg_cache_build_out_arg(GjsArgumentCache *self,
                            GjsParamType     *param_types,
                            int               gi_index,
                            GIArgInfo        *arg_info,
                            bool&             inc_counter)
{
    GITypeInfo type_info;
    g_arg_info_load_type(arg_info, &type_info);

    self->param_type = PARAM_NORMAL;
    inc_counter = true;

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_ARRAY) {
        if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
            int length_pos = g_type_info_get_array_length(&type_info);

            if (length_pos >= 0) {
                param_types[length_pos] = PARAM_SKIPPED;
                self->param_type = PARAM_ARRAY;

                if (length_pos < gi_index) {
                    /* we already collected length_pos, remove it */
                    inc_counter = false;
                }
            }
        }
    }

    return true;
}

bool
gjs_arg_cache_build_inout_arg(GjsArgumentCache *in_self,
                              GjsArgumentCache *out_self,
                              GjsParamType     *param_types,
                              int               gi_index,
                              GIArgInfo        *arg_info,
                              bool&             inc_counter)
{
    GITypeInfo type_info;
    g_arg_info_load_type(arg_info, &type_info);

    in_self->param_type = PARAM_NORMAL;
    out_self->param_type = PARAM_NORMAL;
    inc_counter = true;

    GITypeTag type_tag = g_type_info_get_tag(&type_info);
    if (type_tag == GI_TYPE_TAG_ARRAY) {
        if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
            int length_pos = g_type_info_get_array_length(&type_info);

            if (length_pos >= 0) {
                param_types[length_pos] = PARAM_SKIPPED;
                in_self->param_type = PARAM_ARRAY;
                out_self->param_type = PARAM_ARRAY;

                if (length_pos < gi_index) {
                    /* we already collected length_pos, remove it */
                    inc_counter = false;
                }
            }
        }
    }

    return true;
}
