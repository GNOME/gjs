/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
 */

#include <config.h>

#include <locale.h> /* for setlocale */
#include <stdbool.h>
#include <stddef.h> /* for size_t */
#include <string.h>

#include <glib-object.h>
#include <girepository.h>
#include <glib.h>
#include <glib/gi18n.h> /* for bindtextdomain, bind_textdomain_codeset, textdomain */

#include "libgjs-private/gjs-util.h"
#include "util/console.h"

char *
gjs_format_int_alternative_output(int n)
{
#ifdef HAVE_PRINTF_ALTERNATIVE_INT
    return g_strdup_printf("%Id", n);
#else
    return g_strdup_printf("%d", n);
#endif
}

GType gjs_locale_category_get_type(void) {
    static size_t gjs_locale_category_get_type = 0;
    if (g_once_init_enter(&gjs_locale_category_get_type)) {
        static const GEnumValue v[] = {
            {GJS_LOCALE_CATEGORY_ALL, "GJS_LOCALE_CATEGORY_ALL", "all"},
            {GJS_LOCALE_CATEGORY_COLLATE, "GJS_LOCALE_CATEGORY_COLLATE",
             "collate"},
            {GJS_LOCALE_CATEGORY_CTYPE, "GJS_LOCALE_CATEGORY_CTYPE", "ctype"},
            {GJS_LOCALE_CATEGORY_MESSAGES, "GJS_LOCALE_CATEGORY_MESSAGES",
             "messages"},
            {GJS_LOCALE_CATEGORY_MONETARY, "GJS_LOCALE_CATEGORY_MONETARY",
             "monetary"},
            {GJS_LOCALE_CATEGORY_NUMERIC, "GJS_LOCALE_CATEGORY_NUMERIC",
             "numeric"},
            {GJS_LOCALE_CATEGORY_TIME, "GJS_LOCALE_CATEGORY_TIME", "time"},
            {0, NULL, NULL}};
        GType g_define_type_id = g_enum_register_static(
            g_intern_static_string("GjsLocaleCategory"), v);

        g_once_init_leave(&gjs_locale_category_get_type, g_define_type_id);
    }
    return gjs_locale_category_get_type;
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

#define G_CLOSURE_NOTIFY(func) ((GClosureNotify)(void (*)(void))func)

GBinding* gjs_g_object_bind_property_full(
    GObject* source, const char* source_property, GObject* target,
    const char* target_property, GBindingFlags flags,
    GjsBindingTransformFunc to_callback, void* to_data,
    GDestroyNotify to_notify, GjsBindingTransformFunc from_callback,
    void* from_data, GDestroyNotify from_notify) {
    GClosure* to_closure = NULL;
    GClosure* from_closure = NULL;

    if (to_callback)
        to_closure = g_cclosure_new(G_CALLBACK(to_callback), to_data,
                                    G_CLOSURE_NOTIFY(to_notify));

    if (from_callback)
        from_closure = g_cclosure_new(G_CALLBACK(from_callback), from_data,
                                      G_CLOSURE_NOTIFY(from_notify));

    return g_object_bind_property_with_closures(source, source_property, target,
                                                target_property, flags,
                                                to_closure, from_closure);
}

#if GLIB_CHECK_VERSION(2, 72, 0)
void gjs_g_binding_group_bind_full(
    GBindingGroup* source, const char* source_property, GObject* target,
    const char* target_property, GBindingFlags flags,
    GjsBindingTransformFunc to_callback, void* to_data,
    GDestroyNotify to_notify, GjsBindingTransformFunc from_callback,
    void* from_data, GDestroyNotify from_notify) {
    GClosure* to_closure = NULL;
    GClosure* from_closure = NULL;

    if (to_callback)
        to_closure = g_cclosure_new(G_CALLBACK(to_callback), to_data,
                                    G_CLOSURE_NOTIFY(to_notify));

    if (from_callback)
        from_closure = g_cclosure_new(G_CALLBACK(from_callback), from_data,
                                      G_CLOSURE_NOTIFY(from_notify));

    g_binding_group_bind_with_closures(source, source_property, target,
                                       target_property, flags,
                                       to_closure, from_closure);
}
#endif

#undef G_CLOSURE_NOTIFY

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
    g_clear_pointer(&base_info, g_base_info_unref);
    g_clear_pointer(&child_set_property_fun, g_base_info_unref);
}

/**
 * gjs_list_store_insert_sorted:
 * @store: a #GListStore
 * @item: the new item
 * @compare_func: (scope call): pairwise comparison function for sorting
 * @user_data: (closure): user data for @compare_func
 *
 * Inserts @item into @store at a position to be determined by the
 * @compare_func.
 *
 * The list must already be sorted before calling this function or the
 * result is undefined.  Usually you would approach this by only ever
 * inserting items by way of this function.
 *
 * This function takes a ref on @item.
 *
 * Returns: the position at which @item was inserted
 */
unsigned int gjs_list_store_insert_sorted(GListStore *store, GObject *item,
                                          GjsCompareDataFunc compare_func,
                                          void *user_data) {
  return g_list_store_insert_sorted(store, item, (GCompareDataFunc)compare_func, user_data);
}

/**
 * gjs_list_store_sort:
 * @store: a #GListStore
 * @compare_func: (scope call): pairwise comparison function for sorting
 * @user_data: (closure): user data for @compare_func
 *
 * Sort the items in @store according to @compare_func.
 */
void gjs_list_store_sort(GListStore *store, GjsCompareDataFunc compare_func,
                         void *user_data) {
  g_list_store_sort(store, (GCompareDataFunc)compare_func, user_data);
}

/**
 * gjs_gtk_custom_sorter_new:
 * @sort_func: (nullable) (scope call): function to sort items
 * @user_data: (closure): user data for @compare_func
 * @destroy: destroy notify for @user_data
 *
 * Creates a new `GtkSorter` that works by calling @sort_func to compare items.
 *
 * If @sort_func is %NULL, all items are considered equal.
 *
 * Returns: (transfer full): a new `GtkCustomSorter`
 */
GObject* gjs_gtk_custom_sorter_new(GjsCompareDataFunc sort_func,
                                   void* user_data, GDestroyNotify destroy) {
    GIObjectInfo* container_info =
        g_irepository_find_by_name(NULL, "Gtk", "CustomSorter");
    GIBaseInfo* custom_sorter_new_fun =
        g_object_info_find_method(container_info, "new");

    GIArgument ret;
    GIArgument custom_sorter_new_args[3];
    custom_sorter_new_args[0].v_pointer = sort_func;
    custom_sorter_new_args[1].v_pointer = user_data;
    custom_sorter_new_args[2].v_pointer = destroy;

    g_function_info_invoke(custom_sorter_new_fun, custom_sorter_new_args, 3,
                           NULL, 0, &ret, NULL);

    g_clear_pointer(&container_info, g_base_info_unref);
    g_clear_pointer(&custom_sorter_new_fun, g_base_info_unref);

    return (GObject*)ret.v_pointer;
}

/**
 * gjs_gtk_custom_sorter_set_sort_func:
 * @sorter: a `GtkCustomSorter`
 * @sort_func: (nullable) (scope call): function to sort items
 * @user_data: (closure): user data to pass to @sort_func
 * @destroy: destroy notify for @user_data
 *
 * Sets (or unsets) the function used for sorting items.
 *
 * If @sort_func is %NULL, all items are considered equal.
 *
 * If the sort func changes its sorting behavior, gtk_sorter_changed() needs to
 * be called.
 *
 * If a previous function was set, its @user_destroy will be called now.
 */
void gjs_gtk_custom_sorter_set_sort_func(GObject* sorter,
                                         GjsCompareDataFunc sort_func,
                                         void* user_data,
                                         GDestroyNotify destroy) {
    GIObjectInfo* container_info =
        g_irepository_find_by_name(NULL, "Gtk", "CustomSorter");
    GIBaseInfo* set_sort_func_fun =
        g_object_info_find_method(container_info, "set_sort_func");

    GIArgument unused_ret;
    GIArgument set_sort_func_args[4];
    set_sort_func_args[0].v_pointer = sorter;
    set_sort_func_args[1].v_pointer = sort_func;
    set_sort_func_args[2].v_pointer = user_data;
    set_sort_func_args[3].v_pointer = destroy;

    g_function_info_invoke(set_sort_func_fun, set_sort_func_args, 4, NULL, 0,
                           &unused_ret, NULL);

    g_clear_pointer(&container_info, g_base_info_unref);
    g_clear_pointer(&set_sort_func_fun, g_base_info_unref);
}

static void* log_writer_user_data = NULL;
static GDestroyNotify log_writer_user_data_free = NULL;
static GThread* log_writer_thread = NULL;

static GLogWriterOutput gjs_log_writer_func_wrapper(GLogLevelFlags log_level,
                                                    const GLogField* fields,
                                                    size_t n_fields,
                                                    void* user_data) {
    g_assert(log_writer_thread);

    // If the wrapper is called from a thread other than the one that set it,
    // return unhandled so the fallback logger is used.
    if (g_thread_self() != log_writer_thread)
        return g_log_writer_default(log_level, fields, n_fields, NULL);

    GjsGLogWriterFunc func = (GjsGLogWriterFunc)user_data;
    GVariantDict dict;
    g_variant_dict_init(&dict, NULL);

    size_t f;
    for (f = 0; f < n_fields; f++) {
        const GLogField* field = &fields[f];

        GVariant* value;
        if (field->length < 0) {
            size_t bytes_len = strlen(field->value);
            GBytes* bytes = g_bytes_new(field->value, bytes_len);

            value = g_variant_new_maybe(
                G_VARIANT_TYPE_BYTESTRING,
                g_variant_new_from_bytes(G_VARIANT_TYPE_BYTESTRING, bytes,
                                         true));
            g_bytes_unref(bytes);
        } else if (field->length > 0) {
            GBytes* bytes = g_bytes_new(field->value, field->length);

            value = g_variant_new_maybe(
                G_VARIANT_TYPE_BYTESTRING,
                g_variant_new_from_bytes(G_VARIANT_TYPE_BYTESTRING, bytes,
                                         true));
            g_bytes_unref(bytes);
        } else {
            value = g_variant_new_maybe(G_VARIANT_TYPE_STRING, NULL);
        }

        g_variant_dict_insert_value(&dict, field->key, value);
    }

    GVariant* string_fields = g_variant_dict_end(&dict);
    g_variant_ref(string_fields);

    GLogWriterOutput output =
        func(log_level, string_fields, log_writer_user_data);

    g_variant_unref(string_fields);

    // If the function did not handle the log, fallback to the default
    // handler.
    if (output == G_LOG_WRITER_UNHANDLED)
        return g_log_writer_default(log_level, fields, n_fields, NULL);

    return output;
}

/**
 * gjs_log_set_writer_default:
 *
 * Sets the structured logging writer function back to the platform default.
 */
void gjs_log_set_writer_default() {
    if (log_writer_user_data_free) {
        log_writer_user_data_free(log_writer_user_data);
    }

    g_log_set_writer_func(g_log_writer_default, NULL, NULL);
    log_writer_user_data_free = NULL;
    log_writer_user_data = NULL;
    log_writer_thread = NULL;
}

/**
 * gjs_log_set_writer_func:
 * @func: (scope notified): callback with log data
 * @user_data: (closure): user data for @func
 * @user_data_free: (destroy user_data_free): destroy for @user_data
 *
 * Sets a given function as the writer function for structured logging,
 * passing log fields as a variant. If called from JavaScript the application
 * must call gjs_log_set_writer_default prior to exiting.
 */
void gjs_log_set_writer_func(GjsGLogWriterFunc func, void* user_data,
                             GDestroyNotify user_data_free) {
    log_writer_user_data = user_data;
    log_writer_user_data_free = user_data_free;
    log_writer_thread = g_thread_self();

    g_log_set_writer_func(gjs_log_writer_func_wrapper, func, NULL);
}

/**
 * gjs_clear_terminal:
 *
 * Clears the terminal, if possible.
 */
void gjs_clear_terminal(void) {
    if (!gjs_console_is_tty(stdout_fd))
        return;

    gjs_console_clear();
}
