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

#ifdef HAVE_READLINE_READLINE_H
#    include <stdio.h>  // include before readline/readline.h

#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#include <string>

#include <glib.h>
#include <glib/gprintf.h>  // for g_fprintf

#ifdef HAVE_READLINE_READLINE_H
#    include <gio/gio.h>
#    include <gio/gunixinputstream.h>
#endif

#include <js/CallAndConstruct.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>  // for JS_EncodeStringToUTF8
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/GlobalObject.h>  // for CurrentGlobalOrNull
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/SourceText.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>  // for JS_NewPlainObject
#include <mozilla/Maybe.h>

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/console.h"
#include "util/console.h"

namespace mozilla {
union Utf8Unit;
}

using mozilla::Maybe, mozilla::Nothing, mozilla::Some;

static void gjs_console_warning_reporter(JSContext*, JSErrorReport* report) {
    JS::PrintError(stderr, report, /* reportWarnings = */ true);
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
            JS::UniqueChars stack_str{
                format_saved_frame(m_cx, exnStack.stack(), 2)};
            if (!stack_str) {
                g_printerr("(Unable to print stack trace)\n");
            } else {
                Gjs::AutoChar encoded_stack_str{g_filename_from_utf8(
                    stack_str.get(), -1, nullptr, nullptr, nullptr)};
                if (!encoded_stack_str)
                    g_printerr("(Unable to print stack trace)\n");
                else
                    g_printerr("%s", stack_str.get());
            }
        }

        JS_ClearPendingException(m_cx);
    }
};


// Adapted from https://stackoverflow.com/a/17035073/172999
class AutoCatchCtrlC {
#ifdef HAVE_SIGNAL_H
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
#endif  // HAVE_SIGNAL_H
};

#ifdef HAVE_SIGNAL_H
sigjmp_buf AutoCatchCtrlC::jump_buffer;
#endif  // HAVE_SIGNAL_H

#ifdef HAVE_READLINE_READLINE_H
// Readline only has a global handler anyway, so may as well use global data
static Maybe<std::string> rl_async_line{};
static bool rl_async_done = true;

static gboolean on_stdin_ready(GPollableInputStream* pollable, void*) {
    while (g_pollable_input_stream_is_readable(pollable))
        rl_callback_read_char();
    return TRUE;  // don't remove source
}

static void on_readline_complete(char* line) {
    rl_async_line = line ? Some(line) : Nothing();
    rl_async_done = true;
    // This needs to be called from the callback handler, otherwise an extra
    // prompt is displayed.
    rl_callback_handler_remove();
}
#endif

[[nodiscard]]
static bool gjs_console_readline(std::string* bufp, const char* prompt,
                                 const char* repl_history_path
                                 [[maybe_unused]]) {
#ifdef HAVE_READLINE_READLINE_H
    g_assert(rl_async_done && "should not attempt two parallel readline calls");

    rl_callback_handler_install(prompt, on_readline_complete);
    rl_async_done = false;

    Gjs::AutoUnref<GInputStream> stdin_stream{
        g_unix_input_stream_new(fileno(rl_instream), /* close = */ false)};
    Gjs::AutoUnref<GSource> stdin_source{g_pollable_input_stream_create_source(
        G_POLLABLE_INPUT_STREAM(stdin_stream.get()), nullptr)};
    g_source_set_callback(stdin_source, G_SOURCE_FUNC(on_stdin_ready), nullptr,
                          nullptr);

    Gjs::AutoPointer<GMainContext, GMainContext, g_main_context_unref>
        main_context{g_main_context_ref_thread_default()};
    unsigned tag = g_source_attach(stdin_source, main_context);
    stdin_source.release();

    while (!rl_async_done) {
        // Yield to other things while waiting for input
        while (g_main_context_pending(main_context))
            g_main_context_iteration(main_context, /* may_block = */ false);
    }

    g_source_remove(tag);

    if (!rl_async_line)
        return false;

    *bufp = rl_async_line.extract();

    if ((*bufp)[0] != '\0') {
        add_history(bufp->c_str());
        gjs_console_write_repl_history(repl_history_path);
    }
#else   // !HAVE_READLINE_READLINE_H
    char line[256];
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin))
        return false;
    *bufp = line;
#endif  // !HAVE_READLINE_READLINE_H
    return true;
}

std::string print_string_value(JSContext* cx, JS::HandleValue v_string) {
    if (!v_string.isString())
        return "[unexpected result from printing value]";

    JS::RootedString printed_string(cx, v_string.toString());
    JS::AutoSaveExceptionState exc_state(cx);
    JS::UniqueChars chars(JS_EncodeStringToUTF8(cx, printed_string));
    exc_state.restore();
    if (!chars)
        return "[error printing value]";

    return chars.get();
}

/* Return value of false indicates an uncatchable exception, rather than any
 * exception. (This is because the exception should be auto-printed around the
 * invocation of this function.)
 */
[[nodiscard]] static bool gjs_console_eval_and_print(JSContext* cx,
                                                     JS::HandleObject global,
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

    JS::AutoSaveExceptionState exc_state(cx);
    JS::RootedValue v_printed_string(cx);
    JS::RootedValue v_pretty_print(
        cx, gjs_get_global_slot(global, GjsGlobalSlot::PRETTY_PRINT_FUNC));
    bool ok = JS::Call(cx, global, v_pretty_print, JS::HandleValueArray(result),
                       &v_printed_string);
    if (!ok)
        gjs_log_exception(cx);
    exc_state.restore();

    if (ok) {
        g_fprintf(stdout, "%s\n",
                  print_string_value(cx, v_printed_string).c_str());
    } else {
        g_fprintf(stdout, "[error printing value]\n");
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_console_interact(JSContext *context,
                     unsigned   argc,
                     JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    volatile bool eof, exit_warning;  // accessed after setjmp()
    JS::RootedObject global{context, JS::CurrentGlobalOrNull(context)};
    volatile int lineno;     // accessed after setjmp()
    volatile int startline;  // accessed after setjmp()
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);

#ifndef HAVE_READLINE_READLINE_H
    int rl_end = 0;  // nonzero if using readline and any text is typed in
#endif

    JS::SetWarningReporter(context, gjs_console_warning_reporter);

    AutoCatchCtrlC ctrl_c;

    // Separate initialization from declaration because of possible overwriting
    // when siglongjmp() jumps into this function
    eof = exit_warning = false;
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

            std::string temp_buf;
            if (!gjs_console_readline(&temp_buf,
                                      startline == lineno ? "gjs> " : ".... ",
                                      gjs->repl_history_path())) {
                eof = true;
                break;
            }
            buffer += temp_buf;
            buffer += "\n";
            lineno++;
        } while (!JS_Utf8BufferIsCompilableUnit(context, global, buffer.c_str(),
                                                buffer.size()));

        bool ok;
        {
            AutoReportException are(context);
            ok = gjs_console_eval_and_print(context, global, buffer, startline);
        }
        exit_warning = false;

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

bool
gjs_define_console_stuff(JSContext              *context,
                         JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(context));
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunctionById(context, module, atoms.interact(),
                                 gjs_console_interact, 1,
                                 GJS_MODULE_PROP_FLAGS);
}
