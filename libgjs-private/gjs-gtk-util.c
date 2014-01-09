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

#include <config.h>
#include <gtk/gtk.h>

#include "gjs-gtk-util.h"

void
gjs_gtk_container_child_set_property (GtkContainer *container,
                                      GtkWidget    *child,
                                      const gchar  *property,
                                      const GValue *value)
{
    GParamSpec *pspec;

    pspec = gtk_container_class_find_child_property (G_OBJECT_GET_CLASS (container),
                                                     property);
    if (pspec == NULL) {
      g_warning ("%s does not have a property called %s",
                 g_type_name (G_OBJECT_TYPE (container)), property);
      return;
    }

    if ((G_VALUE_TYPE (value) == G_TYPE_POINTER) &&
        (g_value_get_pointer (value) == NULL) &&
        !g_value_type_transformable (G_VALUE_TYPE (value), pspec->value_type)) {
        /* Set an empty value. This will happen when we set a NULL value from JS.
         * Since GJS doesn't know the GParamSpec for this property, it
         * will just put NULL into a G_TYPE_POINTER GValue, which will later
         * fail when trying to transform it to the GParamSpec's GType.
         */
        GValue null_value = G_VALUE_INIT;
        g_value_init (&null_value, pspec->value_type);
        gtk_container_child_set_property (container, child,
                                          property, &null_value);
        g_value_unset (&null_value);
    } else {
        gtk_container_child_set_property (container, child,
                                          property, value);
    }
}
