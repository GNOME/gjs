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

#include <config.h>

#include <string.h>

#include "gjs/jsapi-wrapper.h"
#include "repo.h"
#include "gtype.h"
#include "function.h"

#include <util/log.h>

#include <girepository.h>

#include "enumeration.h"

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_define_enum_value(JSContext       *context,
                      JS::HandleObject in_object,
                      GIValueInfo     *info)
{
    const char *value_name;
    char *fixed_name;
    gsize i;
    gint64 value_val;

    value_name = g_base_info_get_name( (GIBaseInfo*) info);
    value_val = g_value_info_get_value(info);

    /* g-i converts enum members such as GDK_GRAVITY_SOUTH_WEST to
     * Gdk.GravityType.south-west (where 'south-west' is value_name)
     * Convert back to all SOUTH_WEST.
     */
    fixed_name = g_ascii_strup(value_name, -1);
    for (i = 0; fixed_name[i]; ++i) {
        char c = fixed_name[i];
        if (!(('A' <= c && c <= 'Z') ||
              ('0' <= c && c <= '9')))
            fixed_name[i] = '_';
    }

    gjs_debug(GJS_DEBUG_GENUM,
              "Defining enum value %s (fixed from %s) %" G_GINT64_MODIFIER "d",
              fixed_name, value_name, value_val);

    if (!JS_DefineProperty(context, in_object,
                           fixed_name, (double) value_val,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_throw(context, "Unable to define enumeration value %s %" G_GINT64_FORMAT " (no memory most likely)",
                  fixed_name, value_val);
        g_free(fixed_name);
        return false;
    }
    g_free(fixed_name);

    return true;
}

bool
gjs_define_enum_values(JSContext       *context,
                       JS::HandleObject in_object,
                       GIEnumInfo      *info)
{
    GType gtype;
    int i, n_values;

    /* Fill in enum values first, so we don't define the enum itself until we're
     * sure we can finish successfully.
     */
    n_values = g_enum_info_get_n_values(info);
    for (i = 0; i < n_values; ++i) {
        GIValueInfo *value_info = g_enum_info_get_value(info, i);
        bool failed;

        failed = !gjs_define_enum_value(context, in_object, value_info);

        g_base_info_unref( (GIBaseInfo*) value_info);

        if (failed) {
            return false;
        }
    }

    gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);
    JS::RootedObject gtype_obj(context,
        gjs_gtype_create_gtype_wrapper(context, gtype));
    if (!gtype_obj)
        return false;

    return JS_DefineProperty(context, in_object, "$gtype", gtype_obj,
                             JSPROP_PERMANENT);
}

bool
gjs_define_enum_static_methods(JSContext       *context,
                               JS::HandleObject constructor,
                               GIEnumInfo      *enum_info)
{
    int i, n_methods;

    n_methods = g_enum_info_get_n_methods(enum_info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfoFlags flags;

        GjsAutoFunctionInfo meth_info = g_enum_info_get_method(enum_info, i);
        flags = g_function_info_get_flags(meth_info);

        g_warn_if_fail(!(flags & GI_FUNCTION_IS_METHOD));
        /* Anything that isn't a method we put on the prototype of the
         * constructor.  This includes <constructor> introspection
         * methods, as well as the forthcoming "static methods"
         * support.  We may want to change this to use
         * GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the
         * like in the near future.
         */
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            if (!gjs_define_function(context, constructor, G_TYPE_NONE,
                                     meth_info))
                return false;
        }
    }

    return true;
}

bool
gjs_define_enumeration(JSContext       *context,
                       JS::HandleObject in_object,
                       GIEnumInfo      *info)
{
    const char *enum_name;

    /* An enumeration is simply an object containing integer attributes for
     * each enum value. It does not have a special JSClass.
     *
     * We could make this more typesafe and also print enum values as strings
     * if we created a class for each enum and made the enum values instances
     * of that class. However, it would have a lot more overhead and just
     * be more complicated in general. I think this is fine.
     */

    enum_name = g_base_info_get_name( (GIBaseInfo*) info);

    JS::RootedObject enum_obj(context, JS_NewPlainObject(context));
    if (!enum_obj) {
        gjs_throw(context, "Could not create enumeration %s.%s",
                  g_base_info_get_namespace(info), enum_name);
        return false;
    }

    if (!gjs_define_enum_values(context, enum_obj, info) ||
        !gjs_define_enum_static_methods(context, enum_obj, info))
        return false;

    gjs_debug(GJS_DEBUG_GENUM,
              "Defining %s.%s as %p",
              g_base_info_get_namespace( (GIBaseInfo*) info),
              enum_name, enum_obj.get());

    if (!JS_DefineProperty(context, in_object, enum_name, enum_obj,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_throw(context, "Unable to define enumeration property (no memory most likely)");
        return false;
    }

    return true;
}
