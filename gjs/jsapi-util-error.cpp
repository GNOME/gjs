/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include <stdarg.h>

#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "util/log.h"
#include "util/misc.h"

/*
 * See:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=166436
 * https://bugzilla.mozilla.org/show_bug.cgi?id=215173
 *
 * Very surprisingly, jsapi.h lacks any way to "throw new Error()"
 *
 * So here is an awful hack inspired by
 * http://egachine.berlios.de/embedding-sm-best-practice/embedding-sm-best-practice.html#error-handling
 */
static void
G_GNUC_PRINTF(4, 0)
gjs_throw_valist(JSContext       *context,
                 JSProtoKey       error_kind,
                 const char      *error_name,
                 const char      *format,
                 va_list          args)
{
    char *s;
    bool result;

    s = g_strdup_vprintf(format, args);

    if (JS_IsExceptionPending(context)) {
        /* Often it's unclear whether a given jsapi.h function
         * will throw an exception, so we will throw ourselves
         * "just in case"; in those cases, we don't want to
         * overwrite an exception that already exists.
         * (Do log in case our second exception adds more info,
         * but don't log as topic ERROR because if the exception is
         * caught we don't want an ERROR in the logs.)
         */
        gjs_debug(GJS_DEBUG_CONTEXT,
                  "Ignoring second exception: '%s'",
                  s);
        g_free(s);
        return;
    }

    JS::RootedObject constructor(context);
    JS::RootedValue v_constructor(context), exc_val(context);
    JS::RootedObject new_exc(context);
    JS::AutoValueArray<1> error_args(context);
    result = false;

    if (!gjs_string_from_utf8(context, s, error_args[0])) {
        JS_ReportErrorUTF8(context, "Failed to copy exception string");
        goto out;
    }

    if (!JS_GetClassObject(context, error_kind, &constructor))
        goto out;

    /* throw new Error(message) */
    new_exc = JS_New(context, constructor, error_args);

    if (!new_exc)
        goto out;

    if (error_name) {
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        JS::RootedValue name_value(context);
        if (!gjs_string_from_utf8(context, error_name, &name_value) ||
            !JS_SetPropertyById(context, new_exc, atoms.name(), name_value))
            goto out;
    }

    exc_val.setObject(*new_exc);
    JS_SetPendingException(context, exc_val);

    result = true;

 out:

    if (!result) {
        /* try just reporting it to error handler? should not
         * happen though pretty much
         */
        JS_ReportErrorUTF8(context, "Failed to throw exception '%s'", s);
    }
    g_free(s);
}

/* Throws an exception, like "throw new Error(message)"
 *
 * If an exception is already set in the context, this will
 * NOT overwrite it. That's an important semantic since
 * we want the "root cause" exception. To overwrite,
 * use JS_ClearPendingException() first.
 */
void
gjs_throw(JSContext       *context,
          const char      *format,
          ...)
{
    va_list args;

    va_start(args, format);
    gjs_throw_valist(context, JSProto_Error, nullptr, format, args);
    va_end(args);
}

/*
 * Like gjs_throw, but allows to customize the error
 * class and 'name' property. Mainly used for throwing TypeError instead of
 * error.
 */
void
gjs_throw_custom(JSContext  *cx,
                 JSProtoKey  kind,
                 const char *error_name,
                 const char *format,
                 ...)
{
    va_list args;
    g_return_if_fail(kind == JSProto_Error || kind == JSProto_InternalError ||
                     kind == JSProto_EvalError || kind == JSProto_RangeError ||
                     kind == JSProto_ReferenceError ||
                     kind == JSProto_SyntaxError || kind == JSProto_TypeError ||
                     kind == JSProto_URIError);

    va_start(args, format);
    gjs_throw_valist(cx, kind, error_name, format, args);
    va_end(args);
}

/**
 * gjs_throw_literal:
 *
 * Similar to gjs_throw(), but does not treat its argument as
 * a format string.
 */
void
gjs_throw_literal(JSContext       *context,
                  const char      *string)
{
    gjs_throw(context, "%s", string);
}

/**
 * gjs_throw_gerror_message:
 *
 * Similar to gjs_throw_gerror(), but does not marshal the GError structure into
 * JavaScript. Instead, it creates a regular JavaScript Error object and copies
 * the GError's message into it.
 *
 * Use this when handling a GError in an internal function, where the error code
 * and domain don't matter. So, for example, don't use it to throw errors
 * around calling from JS into C code.
 *
 * Frees the GError.
 */
bool gjs_throw_gerror_message(JSContext* cx, GError* error) {
    g_return_val_if_fail(error, false);
    gjs_throw_literal(cx, error->message);
    g_error_free(error);
    return false;
}

/**
 * gjs_format_stack_trace:
 * @cx: the #JSContext
 * @saved_frame: a SavedFrame #JSObject
 *
 * Formats a stack trace as a string in filename encoding, suitable for
 * printing to stderr. Ignores any errors.
 *
 * Returns: unique string in filename encoding, or nullptr if no stack trace
 */
GjsAutoChar
gjs_format_stack_trace(JSContext       *cx,
                       JS::HandleObject saved_frame)
{
    JS::AutoSaveExceptionState saved_exc(cx);

    JS::RootedString stack_trace(cx);
    JS::UniqueChars stack_utf8;
    if (JS::BuildStackString(cx, saved_frame, &stack_trace, 2))
        stack_utf8.reset(JS_EncodeStringToUTF8(cx, stack_trace));

    saved_exc.restore();

    if (!stack_utf8)
        return nullptr;

    return g_filename_from_utf8(stack_utf8.get(), -1, nullptr, nullptr,
                                nullptr);
}

void gjs_warning_reporter(JSContext*, JSErrorReport* report) {
    const char *warning;
    GLogLevelFlags level;

    g_assert(report);

    if (gjs_environment_variable_is_set("GJS_ABORT_ON_OOM") &&
        report->flags == JSREPORT_ERROR &&
        report->errorNumber == 137) {
        /* 137, JSMSG_OUT_OF_MEMORY */
        g_error("GJS ran out of memory at %s: %i.",
                report->filename,
                report->lineno);
    }

    if ((report->flags & JSREPORT_WARNING) != 0) {
        warning = "WARNING";
        level = G_LOG_LEVEL_MESSAGE;

        /* suppress bogus warnings. See mozilla/js/src/js.msg */
        if (report->errorNumber == 162) {
            /* 162, JSMSG_UNDEFINED_PROP: warns every time a lazy property
             * is resolved, since the property starts out
             * undefined. When this is a real bug it should usually
             * fail somewhere else anyhow.
             */
            return;
        }
    } else {
        warning = "REPORTED";
        level = G_LOG_LEVEL_WARNING;
    }

    g_log(G_LOG_DOMAIN, level, "JS %s: [%s %d]: %s", warning, report->filename,
          report->lineno, report->message().c_str());
}
