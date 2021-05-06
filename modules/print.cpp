/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <string>

#include <glib.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/Conversions.h>
#include <js/PropertySpec.h>  // for JS_FN, JSFunctionSpec, JS_FS_END
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <jsapi.h>

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

    gjs_log_exception_full(cx, argv[0], jstr, G_LOG_LEVEL_WARNING);

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_print_parse_args(JSContext* cx, const JS::CallArgs& argv,
                                 std::string* buffer) {
    g_assert(buffer && "forgot out parameter");
    buffer->clear();
    for (unsigned n = 0; n < argv.length(); ++n) {
        /* JS::ToString might throw, in which case we will only log that the
         * value could not be converted to string */
        JS::AutoSaveExceptionState exc_state(cx);
        JS::RootedString jstr(cx, JS::ToString(cx, argv[n]));
        exc_state.restore();

        if (jstr) {
            JS::UniqueChars s(JS_EncodeStringToUTF8(cx, jstr));
            if (!s)
                return false;

            *buffer += s.get();
            if (n < (argv.length() - 1))
                *buffer += ' ';
        } else {
            *buffer = "<invalid string>";
            return true;
        }
    }
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_print(JSContext* context, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    std::string buffer;
    if (!gjs_print_parse_args(context, argv, &buffer))
        return false;

    g_print("%s\n", buffer.c_str());

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_printerr(JSContext* context, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);

    std::string buffer;
    if (!gjs_print_parse_args(context, argv, &buffer))
        return false;

    g_printerr("%s\n", buffer.c_str());

    argv.rval().setUndefined();
    return true;
}

// clang-format off
static constexpr JSFunctionSpec funcs[] = {
    JS_FN("log", gjs_log, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("logError", gjs_log_error, 2, GJS_MODULE_PROP_FLAGS),
    JS_FN("print", gjs_print, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("printerr", gjs_printerr, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};
// clang-format on

bool gjs_define_print_stuff(JSContext* context,
                            JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(context));
    if (!module)
        return false;
    return JS_DefineFunctions(context, module, funcs);
}
