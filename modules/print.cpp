/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stddef.h>  // for size_t
#include <stdint.h>

#include <string>
#include <vector>

#include <glib.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/Conversions.h>
#include <js/Exception.h>
#include <js/PropertyAndElement.h>  // for JS_DefineFunctions
#include <js/PropertySpec.h>  // for JS_FN, JSFunctionSpec, JS_FS_END
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/deprecation.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
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

// The pretty-print functionality is best written in JS, but needs to be used
// from C++ code. This stores the prettyPrint() function in a slot on the global
// object so that it can be used internally by the Console module.
// This function is not available to user code.
GJS_JSAPI_RETURN_CONVENTION
static bool set_pretty_print_function(JSContext*, unsigned argc,
                                      JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    // can only be called internally, so OK to assert correct arguments
    g_assert(args.length() == 2 && "setPrettyPrintFunction takes 2 arguments");

    JS::Value v_global = args[0];
    JS::Value v_func = args[1];

    g_assert(v_global.isObject() && "first argument must be an object");
    g_assert(v_func.isObject() && "second argument must be an object");

    gjs_set_global_slot(&v_global.toObject(), GjsGlobalSlot::PRETTY_PRINT_FUNC,
                        v_func);

    args.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool get_pretty_print_function(JSContext*, unsigned argc,
                                      JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    g_assert(args.length() == 1 && "getPrettyPrintFunction takes 1 arguments");

    JS::Value v_global = args[0];

    g_assert(v_global.isObject() && "argument must be an object");

    JS::Value pretty_print = gjs_get_global_slot(
        &v_global.toObject(), GjsGlobalSlot::PRETTY_PRINT_FUNC);

    args.rval().set(pretty_print);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool warn_deprecated_once_per_callsite(JSContext* cx, unsigned argc,
                                              JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    g_assert(args.length() >= 1 &&
             "warnDeprecatedOncePerCallsite takes at least 1 argument");

    g_assert(
        args[0].isInt32() &&
        "warnDeprecatedOncePerCallsite argument 1 must be a message ID number");
    int32_t message_id = args[0].toInt32();
    g_assert(
        message_id >= 0 &&
        uint32_t(message_id) < GjsDeprecationMessageId::LastValue &&
        "warnDeprecatedOncePerCallsite argument 1 must be a message ID number");

    if (args.length() == 1) {
        _gjs_warn_deprecated_once_per_callsite(
            cx, GjsDeprecationMessageId(message_id), 2);
        return true;
    }

    std::vector<std::string> format_args;
    for (size_t ix = 1; ix < args.length(); ix++) {
        g_assert(args[ix].isString() &&
                 "warnDeprecatedOncePerCallsite subsequent arguments must be "
                 "strings");
        JS::RootedString v_format_arg{cx, args[ix].toString()};
        JS::UniqueChars format_arg = JS_EncodeStringToUTF8(cx, v_format_arg);
        if (!format_arg)
            return false;
        format_args.emplace_back(format_arg.get());
    }

    _gjs_warn_deprecated_once_per_callsite(
        cx, GjsDeprecationMessageId(message_id), format_args, 2);
    return true;
}

// clang-format off
static constexpr JSFunctionSpec funcs[] = {
    JS_FN("log", gjs_log, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("logError", gjs_log_error, 2, GJS_MODULE_PROP_FLAGS),
    JS_FN("print", gjs_print, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("printerr", gjs_printerr, 0, GJS_MODULE_PROP_FLAGS),
    JS_FN("setPrettyPrintFunction", set_pretty_print_function, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("getPrettyPrintFunction", get_pretty_print_function, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("warnDeprecatedOncePerCallsite", warn_deprecated_once_per_callsite, 1,
        GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

static constexpr JSPropertySpec props[] = {
    JSPropertySpec::int32Value("PLATFORM_SPECIFIC_TYPELIB",
        GJS_MODULE_PROP_FLAGS,
        GjsDeprecationMessageId::PlatformSpecificTypelib),
    JS_PS_END};
// clang-format on

bool gjs_define_print_stuff(JSContext* context,
                            JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(context));
    if (!module)
        return false;
    return JS_DefineFunctions(context, module, funcs) &&
           JS_DefineProperties(context, module, props);
}
