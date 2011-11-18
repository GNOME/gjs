/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2009  Red Hat, Inc.
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

#include "gettext-native.h"
#include "../gi/closure.h"
#include <util/log.h>
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <jsapi.h>
#include <glib/gi18n.h>
#include <gjs/compat.h>

static JSBool
gjs_textdomain(JSContext *context,
               uintN      argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *domain;

    if (!gjs_parse_args(context, "textdomain", "s", argc, argv,
                        "domain", &domain))
        return JS_FALSE;

    textdomain(domain);
    g_free(domain);

    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_bindtextdomain(JSContext *context,
                   uintN      argc,
                   jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *domain;
    char *location;

    if (!gjs_parse_args (context, "bindtextdomain", "sF", argc, argv,
                         "domain", &domain,
                         "location", &location))
        return JS_FALSE;

    bindtextdomain(domain, location);
    /* Always use UTF-8; we assume it internally here */
    bind_textdomain_codeset(domain, "UTF-8");
    g_free (domain);
    g_free (location);
    JS_SET_RVAL(context, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_gettext(JSContext *context,
            uintN      argc,
            jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *msgid;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "gettext", "s", argc, argv,
                         "msgid", &msgid))
      return JS_FALSE;

    translated = gettext(msgid);
    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid);
    return result;
}

static JSBool
gjs_dgettext(JSContext *context,
             uintN      argc,
             jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *domain;
    char *msgid;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "dgettext", "zs", argc, argv,
                         "domain", &domain, "msgid", &msgid))
      return JS_FALSE;

    translated = dgettext(domain, msgid);
    g_free (domain);

    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid);
    return result;
}

static JSBool
gjs_ngettext(JSContext *context,
             uintN      argc,
             jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *msgid1;
    char *msgid2;
    guint32 n;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "ngettext", "ssu", argc, argv,
                         "msgid1", &msgid1, "msgid2", &msgid2, "n", &n))
      return JS_FALSE;

    translated = ngettext(msgid1, msgid2, n);

    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid1);
    g_free (msgid2);
    return result;
}

static JSBool
gjs_dngettext(JSContext *context,
              uintN      argc,
              jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *domain;
    char *msgid1;
    char *msgid2;
    guint n;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "dngettext", "zssu", argc, argv,
                         "domain", &domain, "msgid1", &msgid1,
                         "msgid2", &msgid2, "n", &n))
      return JS_FALSE;

    translated = dngettext(domain, msgid1, msgid2, n);
    g_free (domain);

    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid1);
    g_free (msgid2);
    return result;
}

static JSBool
gjs_pgettext(JSContext *context,
             uintN      argc,
             jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *src_context;
    char *msgid;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "pgettext", "ss", argc, argv,
                         "context", &src_context, "msgid", &msgid))
      return JS_FALSE;

    translated = g_dpgettext2(NULL, src_context, msgid);
    g_free (src_context);

    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid);
    return result;
}

static JSBool
gjs_dpgettext(JSContext *context,
              uintN      argc,
              jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    char *domain;
    char *src_context;
    char *msgid;
    const char *translated;
    JSBool result;
    jsval retval;

    if (!gjs_parse_args (context, "dpgettext", "sss", argc, argv,
                         "domain", &domain, "context", &src_context,
                         "msgid", &msgid))
        return JS_FALSE;

    translated = g_dpgettext2(domain, src_context, msgid);
    g_free (domain);
    g_free (src_context);

    result = gjs_string_from_utf8(context, translated, -1, &retval);
    if (result)
        JS_SET_RVAL(context, vp, retval);
    g_free (msgid);
    return result;
}

JSBool
gjs_define_gettext_stuff(JSContext      *context,
                         JSObject      *module_obj)
{
    if (!JS_DefineFunction(context, module_obj,
                           "textdomain",
                           (JSNative)gjs_textdomain,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "bindtextdomain",
                           (JSNative)gjs_bindtextdomain,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "gettext",
                           (JSNative)gjs_gettext,
                           1, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "dgettext",
                           (JSNative)gjs_dgettext,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "ngettext",
                           (JSNative)gjs_ngettext,
                           3, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "dngettext",
                           (JSNative)gjs_dngettext,
                           4, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "pgettext",
                           (JSNative)gjs_pgettext,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "dpgettext",
                           (JSNative)gjs_dpgettext,
                           3, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}

GJS_REGISTER_NATIVE_MODULE("gettextNative", gjs_define_gettext_stuff)
