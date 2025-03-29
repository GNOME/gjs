/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>

#include <js/AllocPolicy.h>
#include <js/CharacterEncoding.h>
#include <js/ColumnNumber.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/GCHashTable.h>  // for GCHashSet
#include <js/HashTable.h>    // for DefaultHasher
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/SavedFrameAPI.h>
#include <js/Stack.h>  // for BuildStackString
#include <js/String.h>  // for JS_NewStringCopyUTF8Z
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <mozilla/ScopeExit.h>

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"
#include "util/misc.h"

using CauseSet = JS::GCHashSet<JSObject*, js::DefaultHasher<JSObject*>,
                               js::SystemAllocPolicy>;

GJS_JSAPI_RETURN_CONVENTION
static bool get_last_cause(JSContext* cx, JS::HandleValue v_exc,
                           JS::MutableHandleObject last_cause,
                           JS::MutableHandle<CauseSet> seen_causes) {
    if (!v_exc.isObject()) {
        last_cause.set(nullptr);
        return true;
    }
    JS::RootedObject exc(cx, &v_exc.toObject());
    CauseSet::AddPtr entry = seen_causes.lookupForAdd(exc);
    if (entry) {
        last_cause.set(nullptr);
        return true;
    }
    if (!seen_causes.add(entry, exc)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    JS::RootedValue v_cause(cx);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_GetPropertyById(cx, exc, atoms.cause(), &v_cause))
        return false;

    if (v_cause.isUndefined()) {
        last_cause.set(exc);
        return true;
    }

    return get_last_cause(cx, v_cause, last_cause, seen_causes);
}

GJS_JSAPI_RETURN_CONVENTION
static bool append_new_cause(JSContext* cx, JS::HandleValue thrown,
                             JS::HandleValue new_cause, bool* appended) {
    g_assert(appended && "forgot out parameter");
    *appended = false;

    JS::Rooted<CauseSet> seen_causes(cx);
    JS::RootedObject last_cause{cx};
    if (!get_last_cause(cx, thrown, &last_cause, &seen_causes))
        return false;
    if (!last_cause)
        return true;

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    if (!JS_SetPropertyById(cx, last_cause, atoms.cause(), new_cause))
        return false;

    *appended = true;
    return true;
}

[[gnu::format(printf, 4, 0)]] static void gjs_throw_valist(
    JSContext* cx, JSExnType error_kind, const char* error_name,
    const char* format, va_list args) {
    Gjs::AutoChar s{g_strdup_vprintf(format, args)};
    auto fallback = mozilla::MakeScopeExit([cx, &s]() {
        // try just reporting it to error handler? should not
        // happen though pretty much
        JS_ReportErrorUTF8(cx, "Failed to throw exception '%s'", s.get());
    });

    JS::ConstUTF8CharsZ chars{s.get(), strlen(s.get())};
    JS::RootedString message{cx, JS_NewStringCopyUTF8Z(cx, chars)};
    if (!message)
        return;

    JS::RootedObject saved_frame{cx};
    if (!JS::CaptureCurrentStack(cx, &saved_frame))
        return;

    JS::RootedString source_string{cx};
    JS::GetSavedFrameSource(cx, /* principals = */ nullptr, saved_frame,
                            &source_string);
    uint32_t line_num;
    JS::GetSavedFrameLine(cx, nullptr, saved_frame, &line_num);
    JS::TaggedColumnNumberOneOrigin tagged_column;
    JS::GetSavedFrameColumn(cx, nullptr, saved_frame, &tagged_column);
    JS::ColumnNumberOneOrigin column_num{tagged_column.toLimitedColumnNumber()};
    // asserts that this isn't a WASM frame

    JS::RootedValue v_exc{cx};
    if (!JS::CreateError(cx, error_kind, saved_frame, source_string, line_num,
                         column_num, /* report = */ nullptr, message,
                         /* cause = */ JS::NothingHandleValue, &v_exc))
        return;

    if (error_name) {
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        JS::RootedValue v_name{cx};
        JS::RootedObject exc{cx, &v_exc.toObject()};
        if (!gjs_string_from_utf8(cx, error_name, &v_name) ||
            !JS_SetPropertyById(cx, exc, atoms.name(), v_name))
            return;
    }

    if (JS_IsExceptionPending(cx)) {
        // Often it's unclear whether a given jsapi.h function will throw an
        // exception, so we will throw ourselves "just in case"; in those cases,
        // we append the new exception as the cause of the original exception.
        // The second exception may add more info.
        JS::RootedValue pending(cx);
        JS_GetPendingException(cx, &pending);
        JS::AutoSaveExceptionState saved_exc{cx};
        bool appended;
        if (!append_new_cause(cx, pending, v_exc, &appended))
            saved_exc.restore();
        if (!appended)
            gjs_debug(GJS_DEBUG_CONTEXT, "Ignoring second exception: '%s'",
                      s.get());
    } else {
        JS_SetPendingException(cx, v_exc);
    }

    fallback.release();
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
    gjs_throw_valist(context, JSEXN_ERR, nullptr, format, args);
    va_end(args);
}

/*
 * Like gjs_throw, but allows to customize the error
 * class and 'name' property. Mainly used for throwing TypeError instead of
 * error.
 */
void gjs_throw_custom(JSContext *cx, JSExnType kind, const char *error_name,
                      const char *format, ...) {
    va_list args;

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
 */
bool gjs_throw_gerror_message(JSContext* cx, Gjs::AutoError const& error) {
    g_return_val_if_fail(error, false);
    gjs_throw_literal(cx, error->message);
    return false;
}

/**
 * format_saved_frame:
 * @cx: the #JSContext
 * @saved_frame: a SavedFrame #JSObject
 * @indent: (optional): spaces of indentation
 *
 * Formats a stack trace as a UTF-8 string. If there are errors, ignores them
 * and returns null.
 * If you print this to stderr, you will need to re-encode it in filename
 * encoding with g_filename_from_utf8().
 *
 * Returns (nullable) (transfer full): unique string
 */
JS::UniqueChars format_saved_frame(JSContext* cx, JS::HandleObject saved_frame,
                                   size_t indent /* = 0 */) {
    JS::AutoSaveExceptionState saved_exc(cx);

    JS::RootedString stack_trace(cx);
    JS::UniqueChars stack_utf8;
    if (JS::BuildStackString(cx, nullptr, saved_frame, &stack_trace, indent))
        stack_utf8 = JS_EncodeStringToUTF8(cx, stack_trace);

    saved_exc.restore();

    return stack_utf8;
}

void gjs_warning_reporter(JSContext*, JSErrorReport* report) {
    const char *warning;
    GLogLevelFlags level;

    g_assert(report);

    if (gjs_environment_variable_is_set("GJS_ABORT_ON_OOM") &&
        !report->isWarning() && report->errorNumber == 137) {
        /* 137, JSMSG_OUT_OF_MEMORY */
        g_error("GJS ran out of memory at %s:%u:%u.", report->filename.c_str(),
                report->lineno, report->column.oneOriginValue());
    }

    if (report->isWarning()) {
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

    g_log(G_LOG_DOMAIN, level, "JS %s: %s:%u:%u: %s", warning,
          report->filename.c_str(), report->lineno,
          report->column.oneOriginValue(), report->message().c_str());
}
