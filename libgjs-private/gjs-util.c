/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include <config.h> /* IWYU pragma: keep */

#include <locale.h>    /* for setlocale */
#include <stddef.h>    /* for size_t */
#include <sys/types.h> /* IWYU pragma: keep */

#include <gio/gio.h>
#include <glib-object.h>
#include <girepository.h>
#include <glib.h>
#include <glib/gi18n.h> /* for bindtextdomain, bind_textdomain_codeset, textdomain */

#ifdef G_OS_UNIX
#    include <errno.h>
#    include <fcntl.h> /* for FD_CLOEXEC */
#    include <stdarg.h>
#    include <unistd.h> /* for close, write */

#    include <glib-unix.h> /* for g_unix_open_pipe */
#endif

#include "libgjs-private/gjs-util.h"

char *
gjs_format_int_alternative_output(int n)
{
#ifdef HAVE_PRINTF_ALTERNATIVE_INT
    return g_strdup_printf("%Id", n);
#else
    return g_strdup_printf("%d", n);
#endif
}

GType
gjs_locale_category_get_type(void)
{
  static volatile size_t g_define_type_id__volatile = 0;
  if (g_once_init_enter(&g_define_type_id__volatile)) {
      static const GEnumValue v[] = {
          { GJS_LOCALE_CATEGORY_ALL, "GJS_LOCALE_CATEGORY_ALL", "all" },
          { GJS_LOCALE_CATEGORY_COLLATE, "GJS_LOCALE_CATEGORY_COLLATE", "collate" },
          { GJS_LOCALE_CATEGORY_CTYPE, "GJS_LOCALE_CATEGORY_CTYPE", "ctype" },
          { GJS_LOCALE_CATEGORY_MESSAGES, "GJS_LOCALE_CATEGORY_MESSAGES", "messages" },
          { GJS_LOCALE_CATEGORY_MONETARY, "GJS_LOCALE_CATEGORY_MONETARY", "monetary" },
          { GJS_LOCALE_CATEGORY_NUMERIC, "GJS_LOCALE_CATEGORY_NUMERIC", "numeric" },
          { GJS_LOCALE_CATEGORY_TIME, "GJS_LOCALE_CATEGORY_TIME", "time" },
          { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static(g_intern_static_string("GjsLocaleCategory"), v);

      g_once_init_leave(&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

/**
 * gjs_setlocale:
 * @category:
 * @locale: (allow-none):
 *
 * Returns:
 */
const char *
gjs_setlocale(GjsLocaleCategory category, const char *locale)
{
    /* According to man setlocale(3), the return value may be allocated in
     * static storage. */
    return (const char *) setlocale(category, locale);
}

void
gjs_textdomain(const char *domain)
{
    textdomain(domain);
}

void
gjs_bindtextdomain(const char *domain,
                   const char *location)
{
    bindtextdomain(domain, location);
    /* Always use UTF-8; we assume it internally here */
    bind_textdomain_codeset(domain, "UTF-8");
}

GParamFlags
gjs_param_spec_get_flags(GParamSpec *pspec)
{
    return pspec->flags;
}

GType
gjs_param_spec_get_value_type(GParamSpec *pspec)
{
    return pspec->value_type;
}

GType
gjs_param_spec_get_owner_type(GParamSpec *pspec)
{
    return pspec->owner_type;
}

#ifdef G_OS_UNIX

// Adapted from glnx_throw_errno_prefix()
G_GNUC_PRINTF(2, 3)
static gboolean throw_errno_prefix(GError** error, const char* fmt, ...) {
    int errsv = errno;
    char* old_msg;
    GString* buf;

    va_list args;

    if (!error)
        return FALSE;

    va_start(args, fmt);

    g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(errsv),
                        g_strerror(errsv));

    old_msg = g_steal_pointer(&(*error)->message);
    buf = g_string_new("");
    g_string_append_vprintf(buf, fmt, args);
    g_string_append(buf, ": ");
    g_string_append(buf, old_msg);
    g_free(old_msg);
    (*error)->message = g_string_free(g_steal_pointer(&buf), FALSE);

    va_end(args);

    errno = errsv;
    return FALSE;
}

#endif /* G_OS_UNIX */

/**
 * gjs_open_bytes:
 * @bytes: bytes to send to the pipe
 * @error: Return location for a #GError, or %NULL
 *
 * Creates a pipe and sends @bytes to it, such that it is suitable for passing
 * to g_subprocess_launcher_take_fd().
 *
 * Returns: file descriptor, or -1 on error
 */
int gjs_open_bytes(GBytes* bytes, GError** error) {
    int pipefd[2], result;
    size_t count;
    const void* buf;
    ssize_t bytes_written;

    g_return_val_if_fail(bytes, -1);
    g_return_val_if_fail(error == NULL || *error == NULL, -1);

#ifdef G_OS_UNIX
    if (!g_unix_open_pipe(pipefd, FD_CLOEXEC, error))
        return -1;

    buf = g_bytes_get_data(bytes, &count);

    bytes_written = write(pipefd[1], buf, count);
    if (bytes_written < 0) {
        throw_errno_prefix(error, "write");
        return -1;
    }

    if ((size_t)bytes_written != count)
        g_warning("%s: %zd bytes sent, only %zu bytes written", __func__, count,
                  bytes_written);

    result = close(pipefd[1]);
    if (result == -1) {
        throw_errno_prefix(error, "close");
        return -1;
    }

    return pipefd[0];
#else
    g_error("%s is currently supported on UNIX only", __func__);
#endif
}

static GIBaseInfo* find_method_fallback(GIStructInfo* class_info,
                                        const char* method_name) {
    GIBaseInfo* method;
    guint n_methods, i;

    n_methods = g_struct_info_get_n_methods(class_info);

    for (i = 0; i < n_methods; i++) {
        method = g_struct_info_get_method(class_info, i);

        if (strcmp(g_base_info_get_name(method), method_name) == 0)
            return method;
        g_base_info_unref(method);
    }

    return NULL;
}

static GParamSpec* gjs_gtk_container_class_find_child_property(
    GIObjectInfo* container_info, GObject* container, const char* property) {
    GIBaseInfo* class_info = NULL;
    GIBaseInfo* find_child_property_fun = NULL;

    GIArgument ret;
    GIArgument find_child_property_args[2];

    class_info = g_object_info_get_class_struct(container_info);
    find_child_property_fun =
        g_struct_info_find_method(class_info, "find_child_property");

    /* Workaround for
       https://gitlab.gnome.org/GNOME/gobject-introspection/merge_requests/171
     */
    if (find_child_property_fun == NULL)
        find_child_property_fun =
            find_method_fallback(class_info, "find_child_property");

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
