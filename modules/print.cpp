/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set ts=8 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <config.h>  // for HAVE_READLINE_READLINE_H

#include <stdio.h>   // for fputc, fputs, stderr, size_t, fflush
#include <string.h>  // for strchr

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf

#include "gjs/jsapi-wrapper.h"
#include "js/CompilationAndEvaluation.h"
#include "js/SourceText.h"
#include "js/Warnings.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "modules/print.h"

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_log(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    if (argc != 1) {
        gjs_throw(cx, "Must pass a single argument to log()");
        return false;
    }

    /* JS::ToString might throw, in which case we will only log that the value
     * could not be converted to string */
    JS::AutoSaveExceptionState exc_state(cx);
    JS::RootedString jstr(cx, JS::ToString(cx, argv[0]));
    exc_state.restore();

    if (!jstr) {
        g_message("JS LOG: <cannot convert value to string>");
        return true;
    }

    JS::UniqueChars s(JS_EncodeStringToUTF8(cx, jstr));
    if (!s)
        return false;

    g_message("JS LOG: %s", s.get());

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_log_error(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    if ((argc != 1 && argc != 2) || !argv[0].isObject()) {
        gjs_throw(
            cx,
            "Must pass an exception and optionally a message to logError()");
        return false;
    }

    JS::RootedString jstr(cx);

    if (argc == 2) {
        /* JS::ToString might throw, in which case we will only log that the
         * value could not be converted to string */
        JS::AutoSaveExceptionState exc_state(cx);
        jstr = JS::ToString(cx, argv[1]);
        exc_state.restore();
    }

    gjs_log_exception_full(cx, argv[0], jstr);

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_print_parse_args(JSContext* cx, const JS::CallArgs& argv,
                                 GjsAutoChar* buffer) {
    GString* str;
    guint n;

    str = g_string_new("");
    for (n = 0; n < argv.length(); ++n) {
        /* JS::ToString might throw, in which case we will only log that the
         * value could not be converted to string */
        JS::AutoSaveExceptionState exc_state(cx);
        JS::RootedString jstr(cx, JS::ToString(cx, argv[n]));
        exc_state.restore();

        if (jstr) {
            JS::UniqueChars s(JS_EncodeStringToUTF8(cx, jstr));
            if (!s) {
                g_string_free(str, true);
                return false;
            }

            g_string_append(str, s.get());
            if (n < (argv.length() - 1))
                g_string_append_c(str, ' ');
        } else {
            *buffer = g_string_free(str, true);
            if (!*buffer)
                *buffer = g_strdup("<invalid string>");
            return true;
        }
    }
    *buffer = g_string_free(str, false);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_print(JSContext* context, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    GjsAutoChar buffer;
    if (!gjs_print_parse_args(context, argv, &buffer))
        return false;

    g_print("%s\n", buffer.get());

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_printerr(JSContext* context, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    GjsAutoChar buffer;
    if (!gjs_print_parse_args(context, argv, &buffer))
        return false;

    g_printerr("%s\n", buffer.get());

    argv.rval().setUndefined();
    return true;
}

bool gjs_define_print_stuff(JSContext* context,
                            JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(context));
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunction(context, module, "print", gjs_print, 1,
                             GJS_MODULE_PROP_FLAGS);
}
