/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* vim: set ts=8 sw=4 et tw=78: */
// SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: 1998 Netscape Communications Corporation

#include <config.h>  // for HAVE_READLINE_READLINE_H

#ifdef HAVE_SIGNAL_H
#    include <setjmp.h>
#    include <signal.h>
#    ifdef _WIN32
#        define sigjmp_buf jmp_buf
#        define siglongjmp(e, v) longjmp (e, v)
#        define sigsetjmp(v, m) setjmp (v)
#    endif
#endif

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf

#if defined(HAVE_READLINE_READLINE_H) || defined(HAVE_SYS_IOCTL_H) || \
    defined(HAVE_UNISTD_H) || defined(G_OS_UNIX)
#    include <stdio.h>  // include before readline/readline.h and sys/ioctl.h
#endif

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#include <string>

#if defined(HAVE_SYS_IOCTL_H) && defined(HAVE_UNISTD_H)
#    include <fcntl.h>
#    include <sys/ioctl.h>
#    include <unistd.h>
#    if defined(TIOCGWINSZ)
#        define GET_SIZE_USE_IOCTL
#    endif
#endif

#ifdef HAVE_GIO_UNIX
#    include <gio/gio.h>
#    include <gio/gunixoutputstream.h>  // IWYU pragma: keep
#    include <glib-object.h>
#    include <string.h>
#endif

#include <js/CallArgs.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/PropertyDescriptor.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Warnings.h>
#include <jsapi.h>  // for JS_IsExceptionPending, Exce...

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "modules/console.h"

namespace mozilla {
union Utf8Unit;
}

static void gjs_console_warning_reporter(JSContext* cx, JSErrorReport* report) {
    JS::PrintError(cx, stderr, report, /* reportWarnings = */ true);
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

        JS::PrintError(m_cx, stderr, report, /* reportWarnings = */ false);

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

#ifdef HAVE_SIGNAL_H

// Adapted from https://stackoverflow.com/a/17035073/172999
class AutoCatchCtrlC {
    void (*m_prev_handler)(int);

    static void handler(int signal) {
        if (signal == SIGINT)
            siglongjmp(jump_buffer, 1);
    }

 public:
    static sigjmp_buf jump_buffer;

    AutoCatchCtrlC() {
        m_prev_handler = signal(SIGINT, &AutoCatchCtrlC::handler);
    }

    ~AutoCatchCtrlC() {
        if (m_prev_handler != SIG_ERR)
            signal(SIGINT, m_prev_handler);
    }

    void raise_default() {
        if (m_prev_handler != SIG_ERR)
            signal(SIGINT, m_prev_handler);
        raise(SIGINT);
    }
};
sigjmp_buf AutoCatchCtrlC::jump_buffer;

#else  // !HAVE_SIGNAL_H

struct AutoCatchCtrlC {};

#endif  // !HAVE_SIGNAL_H

[[nodiscard]] static bool gjs_console_readline(char** bufp,
                                               const char* prompt) {
#ifdef HAVE_READLINE_READLINE_H
    char *line;
    line = readline(prompt);
    if (!line)
        return false;
    if (line[0] != '\0')
        add_history(line);
    *bufp = line;
#else   // !HAVE_READLINE_READLINE_H
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return false;
    *bufp = g_strdup(line);
#endif  // !HAVE_READLINE_READLINE_H
    return true;
}

/* Return value of false indicates an uncatchable exception, rather than any
 * exception. (This is because the exception should be auto-printed around the
 * invocation of this function.)
 */
[[nodiscard]] static bool gjs_console_eval_and_print(JSContext* cx,
                                                     const std::string& bytes,
                                                     int lineno) {
    JS::SourceText<mozilla::Utf8Unit> source;
    if (!source.init(cx, bytes.c_str(), bytes.size(),
                     JS::SourceOwnership::Borrowed))
        return false;

    JS::CompileOptions options(cx);
    options.setFileAndLine("typein", lineno);

    JS::RootedValue result(cx);
    if (!JS::Evaluate(cx, options, source, &result)) {
        if (!JS_IsExceptionPending(cx))
            return false;
    }

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(cx);
    gjs->schedule_gc_if_needed();

    if (result.isUndefined())
        return true;

    g_fprintf(stdout, "%s\n", gjs_value_debug_string(cx, result).c_str());
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_console_interact(JSContext *context,
                     unsigned   argc,
                     JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    bool eof, exit_warning;
    JS::RootedObject global(context, gjs_get_import_global(context));
    char* temp_buf;
    int lineno;
    int startline;

#ifndef HAVE_READLINE_READLINE_H
    int rl_end = 0;  // nonzero if using readline and any text is typed in
#endif

    JS::SetWarningReporter(context, gjs_console_warning_reporter);

    AutoCatchCtrlC ctrl_c;

    // Separate initialization from declaration because of possible overwriting
    // when siglongjmp() jumps into this function
    eof = exit_warning = false;
    temp_buf = nullptr;
    lineno = 1;
    do {
        /*
         * Accumulate lines until we get a 'compilable unit' - one that either
         * generates an error (before running out of source) or that compiles
         * cleanly.  This should be whenever we get a complete statement that
         * coincides with the end of a line.
         */
        startline = lineno;
        std::string buffer;
        do {
#ifdef HAVE_SIGNAL_H
            // sigsetjmp() returns 0 if control flow encounters it normally, and
            // nonzero if it's been jumped to. In the latter case, use a while
            // loop so that we call sigsetjmp() a second time to reinit the jump
            // buffer.
            while (sigsetjmp(AutoCatchCtrlC::jump_buffer, 1) != 0) {
                g_fprintf(stdout, "\n");
                if (buffer.empty() && rl_end == 0) {
                    if (!exit_warning) {
                        g_fprintf(stdout,
                                  "(To exit, press Ctrl+C again or Ctrl+D)\n");
                        exit_warning = true;
                    } else {
                        ctrl_c.raise_default();
                    }
                } else {
                    exit_warning = false;
                }
                buffer.clear();
                startline = lineno = 1;
            }
#endif  // HAVE_SIGNAL_H

            if (!gjs_console_readline(
                    &temp_buf, startline == lineno ? "gjs> " : ".... ")) {
                eof = true;
                break;
            }
            buffer += temp_buf;
            buffer += "\n";
            g_free(temp_buf);
            lineno++;
        } while (!JS_Utf8BufferIsCompilableUnit(context, global, buffer.c_str(),
                                                buffer.size()));

        bool ok;
        {
            AutoReportException are(context);
            ok = gjs_console_eval_and_print(context, buffer, startline);
        }
        exit_warning = false;

        GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
        ok = gjs->run_jobs_fallible() && ok;

        if (!ok) {
            /* If this was an uncatchable exception, throw another uncatchable
             * exception on up to the surrounding JS::Evaluate() in main(). This
             * happens when you run gjs-console and type imports.system.exit(0);
             * at the prompt. If we don't throw another uncatchable exception
             * here, then it's swallowed and main() won't exit. */
            return false;
        }
    } while (!eof);

    g_fprintf(stdout, "\n");

    argv.rval().setUndefined();
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

/**
 * ANSI escape code sequence to clear the terminal screen.
 *
 * Combination of 0x1B (Escape) and the sequence nJ where n=2,
 * n=2 clears the entire display instead of only after the cursor.
 *
 * See https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
 */
constexpr const char* ANSI_CODE [[maybe_unused]] = "\x1b[2J";

bool gjs_console_clear_terminal(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

#ifdef HAVE_GIO_UNIX
    int fd = fileno(stdout);
    if (fd >= 0) {
        GjsAutoUnref<GOutputStream> ostream(
            g_unix_output_stream_new(fd, false));
        size_t bytes_written;
        GError* error = nullptr;

        if (!g_output_stream_write_all(ostream, ANSI_CODE, strlen(ANSI_CODE),
                                       &bytes_written, nullptr, &error))
            return gjs_throw_gerror_message(cx, error);

        if (!g_output_stream_flush(ostream, nullptr, &error))
            return gjs_throw_gerror_message(cx, error);

        if (!g_output_stream_close(ostream, nullptr, &error))
            return gjs_throw_gerror_message(cx, error);
    }
#endif

    args.rval().setUndefined();
    return true;
}

bool
gjs_define_console_stuff(JSContext              *context,
                         JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunction(context, module, "getTerminalSize",
                             gjs_console_get_terminal_size, 1,
                             GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunction(context, module, "clearTerminal",
                             gjs_console_clear_terminal, 1,
                             GJS_MODULE_PROP_FLAGS) &&
           JS_DefineFunctionById(context, module, atoms.interact(),
                                 gjs_console_interact, 1,
                                 GJS_MODULE_PROP_FLAGS);
}
