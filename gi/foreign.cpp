/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010  litl, LLC
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
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <girepository.h>

#include "arg.h"
#include "foreign.h"

static struct {
    char *gi_namespace;
    char *module; // relative to "imports."
    gboolean loaded;
} foreign_modules[] = {
    { (char*)"cairo", (char*)"cairo", FALSE },
    { NULL }
};

static GHashTable* foreign_structs_table = NULL;

static GHashTable*
get_foreign_structs(void)
{
    // FIXME: look into hasing on GITypeInfo instead.
    if (!foreign_structs_table) {
        foreign_structs_table = g_hash_table_new_full(g_str_hash, g_str_equal,
                                     (GDestroyNotify)g_free,
                                     NULL);
    }

    return foreign_structs_table;
}

static JSBool
gjs_foreign_load_foreign_module(JSContext *context,
                                const gchar *gi_namespace)
{
    int i;

    for (i = 0; foreign_modules[i].gi_namespace; ++i) {
        int code;
        GError *error = NULL;
        char *script;

        if (strcmp(gi_namespace, foreign_modules[i].gi_namespace) != 0)
            continue;

        if (foreign_modules[i].loaded)
            return JS_TRUE;

        // FIXME: Find a way to check if a module is imported
        //        and only execute this statement if isn't
        script = g_strdup_printf("imports.%s;", gi_namespace);
        if (!gjs_context_eval((GjsContext*) JS_GetContextPrivate(context), script, strlen(script),
                              "<internal>", &code,
                              &error)) {
            g_printerr("ERROR: %s\n", error->message);
            g_free(script);
            g_error_free(error);
            return JS_FALSE;
        }
        g_free(script);
        foreign_modules[i].loaded = TRUE;
        return JS_TRUE;
    }

    return JS_FALSE;
}

JSBool
gjs_struct_foreign_register(const char *gi_namespace,
                            const char *type_name,
                            GjsForeignInfo *info)
{
    char *canonical_name;

    g_return_val_if_fail(info != NULL, JS_FALSE);
    g_return_val_if_fail(info->to_func != NULL, JS_FALSE);
    g_return_val_if_fail(info->from_func != NULL, JS_FALSE);

    canonical_name = g_strdup_printf("%s.%s", gi_namespace, type_name);
    g_hash_table_insert(get_foreign_structs(), canonical_name, info);
    return JS_TRUE;
}

static GjsForeignInfo *
gjs_struct_foreign_lookup(JSContext  *context,
                          GIBaseInfo *interface_info)
{
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

JSBool
gjs_struct_foreign_convert_to_g_argument(JSContext      *context,
                                         jsval           value,
                                         GIBaseInfo     *interface_info,
                                         const char     *arg_name,
                                         GjsArgumentType argument_type,
                                         GITransfer      transfer,
                                         gboolean        may_be_null,
                                         GArgument      *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return JS_FALSE;

    if (!foreign->to_func(context, value, arg_name,
                           argument_type, transfer, may_be_null, arg))
        return JS_FALSE;

    return JS_TRUE;
}

JSBool
gjs_struct_foreign_convert_from_g_argument(JSContext  *context,
                                           jsval      *value_p,
                                           GIBaseInfo *interface_info,
                                           GArgument  *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return JS_FALSE;

    if (!foreign->from_func(context, value_p, arg))
        return JS_FALSE;

    return JS_TRUE;
}

JSBool
gjs_struct_foreign_release_g_argument(JSContext  *context,
                                      GITransfer  transfer,
                                      GIBaseInfo *interface_info,
                                      GArgument  *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return JS_FALSE;

    if (!foreign->release_func)
        return JS_TRUE;

    if (!foreign->release_func(context, transfer, arg))
        return JS_FALSE;

    return JS_TRUE;
}

