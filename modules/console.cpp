/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set ts=8 sw=4 et tw=78: */
// SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: 1998 Netscape Communications Corporation

#include <config.h>

#include <string>

#include <glib.h>

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf
#include <stdio.h>

#if defined(HAVE_SYS_IOCTL_H) && defined(HAVE_UNISTD_H)
#    include <fcntl.h>
#    include <sys/ioctl.h>
#    include <unistd.h>
#    if defined(TIOCGWINSZ)
#        define GET_SIZE_USE_IOCTL
#    endif
#endif

#include <js/CallAndConstruct.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/console.h"
#include "util/console.h"

namespace mozilla {
union Utf8Unit;
}

/* Based on js::shell::AutoReportException from SpiderMonkey. */
class AutoReportException {
    JSContext *m_cx;

public:
    explicit AutoReportException(JSContext *cx) : m_cx(cx) {}

    ~AutoReportException() {
        if (!JS_IsExceptionPending(m_cx))
            return;

        /* Get exception object before printing and clearing exception. */
        JS::ExceptionStack exnStack(m_cx);
        JS::ErrorReportBuilder report(m_cx);
        if (!JS::StealPendingExceptionStack(m_cx, &exnStack) ||
            !report.init(m_cx, exnStack,
                         JS::ErrorReportBuilder::NoSideEffects)) {
            g_printerr("(Unable to print exception)\n");
            JS_ClearPendingException(m_cx);
            return;
        }

        g_assert(!report.report()->isWarning());

        JS::PrintError(stderr, report, /* reportWarnings = */ false);

        if (exnStack.stack()) {
            GjsAutoChar stack_str =
                gjs_format_stack_trace(m_cx, exnStack.stack());
            if (!stack_str)
                g_printerr("(Unable to print stack trace)\n");
            else
                g_printerr("%s", stack_str.get());
        }

        JS_ClearPendingException(m_cx);
    }
};

[[nodiscard]] static bool gjs_console_readline(char** bufp,
                                               const char* prompt) {
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return false;
    *bufp = g_strdup(line);
    return true;
}

[[nodiscard]] static bool gjs_console_eval(JSContext* cx,
                                           const std::string& bytes, int lineno,
                                           JS::MutableHandleValue result) {
    JS::SourceText<mozilla::Utf8Unit> source;
    if (!source.init(cx, bytes.c_str(), bytes.size(),
                     JS::SourceOwnership::Borrowed))
        return false;

    JS::CompileOptions options(cx);
    options.setFileAndLine("typein", lineno);

    JS::RootedValue eval_result(cx);
    if (!JS::Evaluate(cx, options, source, &eval_result))
        return false;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    gjs->schedule_gc_if_needed();

    result.set(eval_result);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_interact(JSContext* context, unsigned argc,
                                 JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject global(context, gjs_get_import_global(context));

    JS::UniqueChars prompt;
    if (!gjs_parse_call_args(context, "interact", args, "s", "prompt", &prompt))
        return false;

    GjsAutoChar buffer;
    if (!gjs_console_readline(buffer.out(), prompt.get())) {
        return true;
    }

    return gjs_string_from_utf8(context, buffer, args.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_enable_raw_mode(JSContext* cx, unsigned argc,
                                        JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if (!gjs_parse_call_args(cx, "enableRawMode", args, ""))
        return false;

    args.rval().setBoolean(Gjs::Console::enable_raw_mode());
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_disable_raw_mode(JSContext* cx, unsigned argc,
                                         JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if (!gjs_parse_call_args(cx, "disableRawMode", args, ""))
        return false;

    args.rval().setBoolean(Gjs::Console::disable_raw_mode());
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_eval_js(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::UniqueChars expr;
    int lineno;
    if (!gjs_parse_call_args(cx, "eval", args, "si", "expression", &expr,
                             "lineNumber", &lineno))
        return false;

    return gjs_console_eval(cx, std::string(expr.get()), lineno, args.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_is_valid_js(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedString str(cx);
    if (!gjs_parse_call_args(cx, "isValid", args, "S", "code", &str))
        return false;

    JS::UniqueChars code;
    size_t code_len;
    if (!gjs_string_to_utf8_n(cx, str, &code, &code_len))
        return false;

    JS::RootedObject global(cx, gjs_get_import_global(cx));

    args.rval().setBoolean(
        JS_Utf8BufferIsCompilableUnit(cx, global, code.get(), code_len));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool gjs_console_clear_terminal(JSContext* cx, unsigned argc,
                                       JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if (!gjs_parse_call_args(cx, "clearTerminal", args, ""))
        return false;

    if (!Gjs::Console::is_tty(Gjs::Console::stdout_fd)) {
        args.rval().setBoolean(false);
        return true;
    }

    args.rval().setBoolean(Gjs::Console::clear());
    return true;
}

bool gjs_console_get_terminal_size(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
    JS::RootedObject obj(cx, JS_NewPlainObject(cx));
    if (!obj)
        return false;

    // Use 'int' because Windows uses int values, whereas most Unix systems
    // use 'short'
    unsigned int width, height;

    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
#ifdef GET_SIZE_USE_IOCTL
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        gjs_throw(cx, "No terminal output is present.\n");
        return false;
    }

    width = ws.ws_col;
    height = ws.ws_row;
#else
    // TODO(ewlsh): Implement Windows equivalent.
    // See
    // https://docs.microsoft.com/en-us/windows/console/window-and-screen-buffer-size.
    gjs_throw(cx, "Unable to retrieve terminal size on this platform.\n");
    return false;
#endif

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_DefinePropertyById(cx, obj, atoms.height(), height,
                               JSPROP_READONLY) ||
        !JS_DefinePropertyById(cx, obj, atoms.width(), width, JSPROP_READONLY))
        return false;

    argv.rval().setObject(*obj);
    return true;
}

static JSFunctionSpec console_module_funcs[] = {
    JS_FN("interact", gjs_console_interact, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("enableRawMode", gjs_console_enable_raw_mode, 0,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("getDimensions", gjs_console_get_terminal_size, 0,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("disableRawMode", gjs_console_disable_raw_mode, 0,
          GJS_MODULE_PROP_FLAGS),
    JS_FN("eval", gjs_console_eval_js, 2, GJS_MODULE_PROP_FLAGS),
    JS_FN("isValid", gjs_console_is_valid_js, 1, GJS_MODULE_PROP_FLAGS),
    JS_FN("clearTerminal", gjs_console_clear_terminal, 1,
          GJS_MODULE_PROP_FLAGS),
    JS_FS_END,
};

bool gjs_define_console_private_stuff(JSContext* cx,
                                      JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(cx));
    if (!module)
        return false;

    return JS_DefineFunctions(cx, module, console_module_funcs);
}
