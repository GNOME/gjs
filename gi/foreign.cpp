/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

#include <config.h>

#include <string.h>  // for strcmp

#include <girepository.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gi/foreign.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"

static struct {
    const char* gi_namespace;
    bool loaded;
} foreign_modules[] = {
    // clang-format off
    {"cairo", false},
    {nullptr}
    // clang-format on
};

static GHashTable* foreign_structs_table = NULL;

[[nodiscard]] static GHashTable* get_foreign_structs() {
    // FIXME: look into hasing on GITypeInfo instead.
    if (!foreign_structs_table) {
        foreign_structs_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                     (GDestroyNotify)g_free,
                                     NULL);
    }

    return foreign_structs_table;
}

[[nodiscard]] static bool gjs_foreign_load_foreign_module(
    JSContext* context, const char* gi_namespace) {
    int i;

    for (i = 0; foreign_modules[i].gi_namespace; ++i) {
        char *script;

        if (strcmp(gi_namespace, foreign_modules[i].gi_namespace) != 0)
            continue;

        if (foreign_modules[i].loaded)
            return true;

        // FIXME: Find a way to check if a module is imported
        //        and only execute this statement if isn't
        script = g_strdup_printf("imports.%s;", gi_namespace);
        JS::RootedValue retval(context);
        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
        if (!gjs->eval_with_scope(nullptr, script, strlen(script), "<internal>",
                                  &retval)) {
            g_critical("ERROR importing foreign module %s\n", gi_namespace);
            g_free(script);
            return false;
        }
        g_free(script);
        foreign_modules[i].loaded = true;
        return true;
    }

    return false;
}

void gjs_struct_foreign_register(const char* gi_namespace,
                                 const char* type_name, GjsForeignInfo* info) {
    char *canonical_name;

    g_return_if_fail(info);
    g_return_if_fail(info->to_func);
    g_return_if_fail(info->from_func);

    canonical_name = g_strdup_printf("%s.%s", gi_namespace, type_name);
    g_hash_table_insert(get_foreign_structs(), canonical_name, info);
}

[[nodiscard]] static GjsForeignInfo* gjs_struct_foreign_lookup(
    JSContext* context, GIBaseInfo* interface_info) {
    GjsForeignInfo *retval = NULL;
    GHashTable *hash_table;
    char *key;

    key = g_strdup_printf("%s.%s",
                          g_base_info_get_namespace(interface_info),
                          g_base_info_get_name(interface_info));
    hash_table = get_foreign_structs();
    retval = (GjsForeignInfo*)g_hash_table_lookup(hash_table, key);
    if (!retval) {
        if (gjs_foreign_load_foreign_module(context, g_base_info_get_namespace(interface_info))) {
            retval = (GjsForeignInfo*)g_hash_table_lookup(hash_table, key);
        }
    }

    if (!retval) {
        gjs_throw(context, "Unable to find module implementing foreign type %s.%s",
                  g_base_info_get_namespace(interface_info),
                  g_base_info_get_name(interface_info));
    }

    g_free(key);

    return retval;
}

bool gjs_struct_foreign_convert_to_g_argument(
    JSContext* context, JS::Value value, GIBaseInfo* interface_info,
    const char* arg_name, GjsArgumentType argument_type, GITransfer transfer,
    GjsArgumentFlags flags, GArgument* arg) {
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return false;

    if (!foreign->to_func(context, value, arg_name, argument_type, transfer,
                          flags, arg))
        return false;

    return true;
}

bool
gjs_struct_foreign_convert_from_g_argument(JSContext             *context,
                                           JS::MutableHandleValue value_p,
                                           GIBaseInfo            *interface_info,
                                           GIArgument            *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return false;

    if (!foreign->from_func(context, value_p, arg))
        return false;

    return true;
}

bool
gjs_struct_foreign_release_g_argument(JSContext  *context,
                                      GITransfer  transfer,
                                      GIBaseInfo *interface_info,
                                      GArgument  *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return false;

    if (!foreign->release_func)
        return true;

    if (!foreign->release_func(context, transfer, arg))
        return false;

    return true;
}

