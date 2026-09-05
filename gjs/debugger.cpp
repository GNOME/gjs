// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileContributor: Philip Chimento <philip.chimento@gmail.com>

#include <config.h>  // for HAVE_READLINE_READLINE_H, HAVE_UNISTD_H

#include <stdint.h>
#include <stdio.h>  // for feof, fflush, fgets, stdin, stdout

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <glib-object.h>
#include <glib.h>

#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/ErrorReport.h>  // for ReportUncatchableException
#include <js/ObjectWithStashedPointer.h>
#include <js/PropertyAndElement.h>
#include <js/PropertySpec.h>
#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>  // for JS_WrapObject

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/gerror-result.h"  // for AutoError
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/module.h"

#include "util/console.h"

GJS_JSAPI_RETURN_CONVENTION
static bool quit(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    int32_t exitcode;
    if (!gjs_parse_call_args(cx, "quit", args, "i", "exitcode", &exitcode))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    gjs->exit(exitcode);
    JS::ReportUncatchableException(cx);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool do_readline(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::UniqueChars prompt;
    if (!gjs_parse_call_args(cx, "readline", args, "|s", "prompt", &prompt))
        return false;

    Gjs::AutoChar line;
    do {
        const char* real_prompt = prompt ? prompt.get() : "db> ";
#ifdef HAVE_READLINE_READLINE_H
        if (gjs_console_is_tty(stdin_fd)) {
            line = readline(real_prompt);
        } else {
#else
        {
#endif  // HAVE_READLINE_READLINE_H
            char buf[256];
            g_print("%s", real_prompt);
            fflush(stdout);
            if (!fgets(buf, sizeof buf, stdin))
                buf[0] = '\0';
            line.reset(g_strdup(g_strchomp(buf)));

            if (!gjs_console_is_tty(stdin_fd)) {
                if (feof(stdin)) {
                    g_print("[quit due to end of input]\n");
                    line.reset(g_strdup("quit"));
                } else {
                    g_print("%s\n", line.get());
                }
            }
        }

        // EOF, return null
        if (!line) {
            args.rval().setNull();
            return true;
        }
    } while (line && line.get()[0] == '\0');

    /* Add line to history and convert it to a JSString so that we can pass it
     * back as the return value */
#ifdef HAVE_READLINE_READLINE_H
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    add_history(line);
    gjs_console_write_repl_history(gjs->repl_history_path());
#endif
    args.rval().setString(JS_NewStringCopyZ(cx, line));
    return true;
}

static bool get_source_map_registry(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);

    JS::RootedObject registry{cx, gjs_get_source_map_registry(gjs->global())};
    if (!JS_WrapObject(cx, &registry)) {
        gjs_log_exception(cx);
        return false;
    }
    args.rval().setObject(*registry);
    return true;
}

static bool launch_file(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);

    JS::UniqueChars filename;
    if (!gjs_parse_call_args(cx, "launchFile", args, "s", "filename",
                             &filename))
        return false;

    Gjs::AutoUnref<GFile> output{
        g_file_new_for_commandline_arg(filename.get())};
    Gjs::AutoChar uri{g_file_get_uri(output)};
    auto result = gjs->register_module(uri, uri);
    if (result.isErr()) {
        gjs_throw(cx, "Error loading file: %s", result.inspectErr()->message);
        return false;
    }

    uint8_t exit_code;
    result = gjs->eval_module(uri, &exit_code);
    if (result.isErr()) {
        gjs_throw(cx, "Error evaluating file: %s",
                  result.inspectErr()->message);
        return false;
    }

    args.rval().setUndefined();
    return true;
}

static bool open_input_stream(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    int32_t fd;
    if (!gjs_parse_call_args(cx, "openInputStream", args, "i", "fd", &fd))
        return false;

    Gjs::AutoUnref<GInputStream> base_stream =
        g_unix_input_stream_new(fd, /* close_fd = */ false);
    Gjs::AutoUnref<GDataInputStream> stream =
        g_data_input_stream_new(base_stream);
    g_filter_input_stream_set_close_base_stream(stream.as<GFilterInputStream>(),
                                                false);

    JS::RootedObject obj{
        cx, JS::NewObjectWithStashedPointer(
                cx, static_cast<void*>(stream.release()), g_object_unref)};
    if (!obj)
        return false;

    args.rval().setObject(*obj);
    return true;
}

static bool read_line(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject obj{cx};
    if (!gjs_parse_call_args(cx, "readLine", args, "o", "stream", &obj))
        return false;

    auto* stream = JS::ObjectGetStashedPointer<GDataInputStream>(cx, obj);

    size_t len;
    Gjs::AutoError error;
    Gjs::AutoChar line = g_data_input_stream_read_line_utf8(
        stream, &len, /* cancellable = */ nullptr, error.out());
    if (!line) {
        gjs_throw(cx, "Error reading DAP Content-Length header: %s",
                  error->message);
        return false;
    }

    JS::UTF8Chars chars{line, len};
    JS::RootedString str{cx, JS_NewStringCopyUTF8N(cx, chars)};
    if (!str)
        return false;

    args.rval().setString(str);
    return true;
}

static bool read_bytes(JSContext* cx, unsigned argc, JS::Value* vp) {
    using AutoBytes =
        Gjs::AutoPointer<GBytes, GBytes, g_bytes_unref, g_bytes_ref>;

    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JS::RootedObject obj{cx};
    uint32_t nbytes;
    if (!gjs_parse_call_args(cx, "readBytes", args, "ou", "stream", &obj,
                             "nbytes", &nbytes))
        return false;

    auto* stream = JS::ObjectGetStashedPointer<GInputStream>(cx, obj);

    Gjs::AutoError error;
    AutoBytes bytes = g_input_stream_read_bytes(
        stream, nbytes, /* cancellable = */ nullptr, error.out());
    if (!bytes) {
        gjs_throw(cx, "Error reading DAP message body: %s", error->message);
        return false;
    }

    size_t len;
    const void* pointer = g_bytes_get_data(bytes, &len);
    JS::UTF8Chars chars{static_cast<const char*>(pointer), len};
    JS::RootedString str{cx, JS_NewStringCopyUTF8N(cx, chars)};
    if (!str)
        return false;

    args.rval().setString(str);
    return true;
}

static JSFunctionSpec debugger_funcs[] = {
    JS_FN("quit", quit, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("readline", do_readline, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("getSourceMapRegistry", get_source_map_registry, 0,
          GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

void gjs_context_setup_debugger_console(GjsContext* self) {
    auto* gjs = GjsContextPrivate::from_object(self);
    JSContext* cx = gjs->context();

    JS::RootedObject debugger_global(
        cx, gjs_create_global_object(cx, GjsGlobalType::DEBUGGER));

    // Enter realm of the debugger and initialize it with the debuggee
    JSAutoRealm ar(cx, debugger_global);
    JS::RootedObject debuggee{cx, gjs->global()};
    if (!JS_WrapObject(cx, &debuggee)) {
        gjs_log_exception(cx);
        return;
    }

    JS::RootedValue v_debuggee(cx, JS::ObjectValue(*debuggee));
    if (!JS_SetPropertyById(cx, debugger_global, gjs->atoms().debuggee(),
                            v_debuggee) ||
        !JS_DefineFunctions(cx, debugger_global, debugger_funcs) ||
        !gjs_define_global_properties(cx, debugger_global,
                                      GjsGlobalType::DEBUGGER, "GJS debugger",
                                      "debugger"))
        gjs_log_exception(cx);
}

static JSFunctionSpec inspector_funcs[] = {
    JS_FN("quit", quit, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("launchFile", launch_file, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("openInputStream", open_input_stream, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("readLine", read_line, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("readBytes", read_bytes, 2, GJS_MODULE_PROP_FLAGS),
    JS_FS_END};

void gjs_context_setup_inspector(GjsContext* self) {
    auto* gjs = GjsContextPrivate::from_object(self);
    JSContext* cx = gjs->context();

    JS::RootedObject inspector_global(
        cx, gjs_create_global_object(cx, GjsGlobalType::DEBUGGER));

    // Enter realm of the inspector and initialize it with the debuggee
    JSAutoRealm ar{cx, inspector_global};
    JS::RootedObject debuggee{cx, gjs->global()};
    if (!JS_WrapObject(cx, &debuggee)) {
        gjs_log_exception(cx);
        return;
    }

    JS::RootedValue v_debuggee{cx, JS::ObjectValue(*debuggee)};
    if (!JS_SetPropertyById(cx, inspector_global, gjs->atoms().debuggee(),
                            v_debuggee) ||
        !JS_DefineFunctions(cx, inspector_global, inspector_funcs) ||
        !gjs_define_global_properties(cx, inspector_global,
                                      GjsGlobalType::DEBUGGER, "GJS inspector",
                                      "inspector"))
        gjs_log_exception(cx);
}
