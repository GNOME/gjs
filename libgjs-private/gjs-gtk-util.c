/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2014 Endless Mobile, Inc.
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

#include <stddef.h>  // for NULL

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include "libgjs-private/gjs-gtk-util.h"

static GParamSpec* gjs_gtk_container_class_find_child_property(
    GIObjectInfo* container_info, GObject* container, const char* property) {
    GIBaseInfo* class_info = NULL;
    GIBaseInfo* find_child_property_fun = NULL;

    GIArgument ret;
    GIArgument find_child_property_args[2];

    class_info = g_object_info_get_class_struct(container_info);
    find_child_property_fun =
        g_struct_info_find_method(class_info, "find_child_property");

    find_child_property_args[0].v_pointer = G_OBJECT_GET_CLASS(container);
    find_child_property_args[1].v_string = (char*)property;

    g_function_info_invoke(find_child_property_fun, find_child_property_args, 2,
                           NULL, 0, &ret, NULL);

    g_clear_pointer(&class_info, g_base_info_unref);
    g_clear_pointer(&find_child_property_fun, g_base_info_unref);

    return (GParamSpec*)ret.v_pointer;
}

void gjs_gtk_container_child_set_property(GObject* container, GObject* child,
                                          const char* property,
                                          const GValue* value) {
    GParamSpec* pspec = NULL;
    GIBaseInfo* base_info = NULL;
    GIBaseInfo* child_set_property_fun = NULL;
    GIObjectInfo* container_info;
    GValue value_arg = G_VALUE_INIT;
    GIArgument ret;

    GIArgument child_set_property_args[4];

    base_info = g_irepository_find_by_name(NULL, "Gtk", "Container");
    container_info = (GIObjectInfo*)base_info;

    pspec = gjs_gtk_container_class_find_child_property(container_info,
                                                        container, property);
    if (pspec == NULL) {
        g_warning("%s does not have a property called %s",
                  g_type_name(G_OBJECT_TYPE(container)), property);
        goto out;
    }

    if ((G_VALUE_TYPE(value) == G_TYPE_POINTER) &&
        (g_value_get_pointer(value) == NULL) &&
        !g_value_type_transformable(G_VALUE_TYPE(value), pspec->value_type)) {
        /* Set an empty value. This will happen when we set a NULL value from
         * JS. Since GJS doesn't know the GParamSpec for this property, it will
         * just put NULL into a G_TYPE_POINTER GValue, which will later fail
         * when trying to transform it to the GParamSpec's GType.
         */
        g_value_init(&value_arg, pspec->value_type);
    } else {
        g_value_init(&value_arg, G_VALUE_TYPE(value));
        g_value_copy(value, &value_arg);
    }

    child_set_property_fun =
        g_object_info_find_method(container_info, "child_set_property");

    child_set_property_args[0].v_pointer = container;
    child_set_property_args[1].v_pointer = child;
    child_set_property_args[2].v_string = (char*)property;
    child_set_property_args[3].v_pointer = &value_arg;

    g_function_info_invoke(child_set_property_fun, child_set_property_args, 4,
                           NULL, 0, &ret, NULL);

    g_value_unset(&value_arg);

out:
    g_clear_pointer(&pspec, g_param_spec_unref);
    g_clear_pointer(&base_info, g_base_info_unref);
    g_clear_pointer(&child_set_property_fun, g_base_info_unref);
}
