/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdarg.h>

#include <glib.h>

#include <js/AllocPolicy.h>
#include <js/CallAndConstruct.h>
#include <js/CharacterEncoding.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/GCHashTable.h>  // for GCHashSet
#include <js/HashTable.h>    // for DefaultHasher
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/Stack.h>  // for BuildStackString
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/ValueArray.h>
#include <jsapi.h>              // for JS_GetClassObject
#include <jspubtd.h>            // for JSProtoKey, JSProto_Error, JSProto...
#include <mozilla/HashTable.h>  // for HashSet<>::AddPtr

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"
#include "util/misc.h"

using CauseSet = JS::GCHashSet<JSObject*, js::DefaultHasher<JSObject*>,
                               js::SystemAllocPolicy>;

GJS_JSAPI_RETURN_CONVENTION
static bool get_last_cause_impl(JSContext* cx, JS::HandleValue v_exc,
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

    return get_last_cause_impl(cx, v_cause, last_cause, seen_causes);
}

GJS_JSAPI_RETURN_CONVENTION
static bool get_last_cause(JSContext* cx, JS::HandleValue v_exc,
                           JS::MutableHandleObject last_cause) {
    JS::Rooted<CauseSet> seen_causes(cx);
    return get_last_cause_impl(cx, v_exc, last_cause, &seen_causes);
}

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
[[gnu::format(printf, 4, 0)]] static void gjs_throw_valist(
    JSContext* context, JSProtoKey error_kind, const char* error_name,
    const char* format, va_list args) {
    char *s;
    bool result;

    s = g_strdup_vprintf(format, args);

    JS::RootedObject constructor(context);
    JS::RootedValue v_constructor(context), exc_val(context);
    JS::RootedObject new_exc(context);
    JS::RootedValueArray<1> error_args(context);
    result = false;

    if (!gjs_string_from_utf8(context, s, error_args[0])) {
        JS_ReportErrorUTF8(context, "Failed to copy exception string");
        goto out;
    }

    if (!JS_GetClassObject(context, error_kind, &constructor))
        goto out;

    v_constructor.setObject(*constructor);

    /* throw new Error(message) */
    if (!JS::Construct(context, v_constructor, error_args, &new_exc))
        goto out;

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

    if (JS_IsExceptionPending(context)) {
        // Often it's unclear whether a given jsapi.h function will throw an
        // exception, so we will throw ourselves "just in case"; in those cases,
        // we append the new exception as the cause of the original exception.
        // The second exception may add more info.
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        JS::RootedValue pending(context);
        JS_GetPendingException(context, &pending);
        JS::RootedObject last_cause(context);
        if (!get_last_cause(context, pending, &last_cause))
            goto out;
        if (last_cause) {
            if (!JS_SetPropertyById(context, last_cause, atoms.cause(),
                                    exc_val))
                goto out;
        } else {
            gjs_debug(GJS_DEBUG_CONTEXT, "Ignoring second exception: '%s'", s);
        }
    } else {
        JS_SetPendingException(context, exc_val);
    }

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

    switch (kind) {
        case JSProto_Error:
        case JSProto_EvalError:
        case JSProto_InternalError:
        case JSProto_RangeError:
        case JSProto_ReferenceError:
        case JSProto_SyntaxError:
        case JSProto_TypeError:
        case JSProto_URIError:
            break;
        default:
            g_return_if_reached();
    }

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
    if (JS::BuildStackString(cx, nullptr, saved_frame, &stack_trace, 2))
        stack_utf8 = JS_EncodeStringToUTF8(cx, stack_trace);

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
        !report->isWarning() && report->errorNumber == 137) {
        /* 137, JSMSG_OUT_OF_MEMORY */
        g_error("GJS ran out of memory at %s: %i.",
                report->filename,
                report->lineno);
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

    g_log(G_LOG_DOMAIN, level, "JS %s: [%s %d]: %s", warning, report->filename,
          report->lineno, report->message().c_str());
}
