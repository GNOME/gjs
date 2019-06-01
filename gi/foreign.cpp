/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <string.h>  // for strcmp

#include <girepository.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"

static struct {
    char *gi_namespace;
    char *module; // relative to "imports."
    bool loaded;
} foreign_modules[] = {
    { (char*)"cairo", (char*)"cairo", false },
    { NULL }
};

static GHashTable* foreign_structs_table = NULL;

GJS_USE
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

GJS_USE
static bool
gjs_foreign_load_foreign_module(JSContext *context,
                                const gchar *gi_namespace)
{
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
        if (!gjs->eval_with_scope(nullptr, script, -1, "<internal>", &retval)) {
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

GJS_USE
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

bool
gjs_struct_foreign_convert_to_g_argument(JSContext      *context,
                                         JS::Value       value,
                                         GIBaseInfo     *interface_info,
                                         const char     *arg_name,
                                         GjsArgumentType argument_type,
                                         GITransfer      transfer,
                                         bool            may_be_null,
                                         GArgument      *arg)
{
    GjsForeignInfo *foreign;

    foreign = gjs_struct_foreign_lookup(context, interface_info);
    if (!foreign)
        return false;

    if (!foreign->to_func(context, value, arg_name,
                           argument_type, transfer, may_be_null, arg))
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

