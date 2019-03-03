/*
 * Copyright (c) 2018 Philip Chimento  <philip.chimento@gmail.com>
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
 *
 * Authored By: Philip Chimento <philip.chimento@gmail.com>
 */

#include <gio/gio.h>

#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"

#ifdef HAVE_READLINE_READLINE_H
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#endif

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#elif defined(XP_WIN)
#    include <io.h>
#    ifndef STDIN_FILENO
#        define STDIN_FILENO 0
#    endif
#endif

GJS_JSAPI_RETURN_CONVENTION
static bool quit(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JSAutoRequest ar(cx);
    int32_t exitcode;
    if (!gjs_parse_call_args(cx, "quit", args, "i", "exitcode", &exitcode))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    gjs->exit(exitcode);
    return false;  // without gjs_throw() == "throw uncatchable exception"
}

GJS_JSAPI_RETURN_CONVENTION
static bool do_readline(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JSAutoRequest ar(cx);

    JS::UniqueChars prompt;
    if (!gjs_parse_call_args(cx, "readline", args, "|s", "prompt", &prompt))
        return false;

    GjsAutoChar line;
    do {
        const char* real_prompt = prompt ? prompt.get() : "db> ";
#ifdef HAVE_READLINE_READLINE_H
        if (isatty(STDIN_FILENO)) {
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
            line.reset(g_strchomp(g_strdup(buf)));

            if (!isatty(STDIN_FILENO)) {
                if (feof(stdin)) {
                    g_print("[quit due to end of input]\n");
                    line.reset(g_strdup("quit"));
                } else {
                    g_print("%s\n", line.get());
                }
            }
        }

        /* EOF, return null */
        if (!line) {
            args.rval().setUndefined();
            return true;
        }
    } while (line && line[0] == '\0');

    /* Add line to history and convert it to a JSString so that we can pass it
     * back as the return value */
#ifdef HAVE_READLINE_READLINE_H
    add_history(line);
#endif
    args.rval().setString(JS_NewStringCopyZ(cx, line));
    return true;
}

// clang-format off
static JSFunctionSpec debugger_funcs[] = {
    JS_FN("quit", quit, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("readline", do_readline, 1, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};
// clang-format on

void gjs_context_setup_debugger_console(GjsContext* gjs) {
    auto cx = static_cast<JSContext*>(gjs_context_get_native_context(gjs));
    JSAutoRequest ar(cx);

    JS::RootedObject debuggee(cx, gjs_get_import_global(cx));
    JS::RootedObject debugger_compartment(cx, gjs_create_global_object(cx));

    /* Enter compartment of the debugger and initialize it with the debuggee */
    JSAutoCompartment compartment(cx, debugger_compartment);
    JS::RootedObject debuggee_wrapper(cx, debuggee);
    if (!JS_WrapObject(cx, &debuggee_wrapper)) {
        gjs_log_exception(cx);
        return;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue v_wrapper(cx, JS::ObjectValue(*debuggee_wrapper));
    if (!JS_SetPropertyById(cx, debugger_compartment, atoms.debuggee(),
                            v_wrapper) ||
        !JS_DefineFunctions(cx, debugger_compartment, debugger_funcs) ||
        !gjs_define_global_properties(cx, debugger_compartment, "debugger"))
        gjs_log_exception(cx);
}
