/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento

#include <config.h>

#include <glib.h>

#include <js/CharacterEncoding.h>
#include <js/ErrorReport.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <jsapi.h>

#include "test/gjs-test-common.h"

// COMPAT: https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1553
#ifdef __clang_analyzer__
void g_assertion_message(const char*, const char*, int, const char*,
                         const char*) __attribute__((analyzer_noreturn));
#endif

char* gjs_test_get_exception_message(JSContext* cx) {
    if (!JS_IsExceptionPending(cx))
        return nullptr;

    JS::RootedValue v_exc(cx);
    g_assert_true(JS_GetPendingException(cx, &v_exc));
    g_assert_true(v_exc.isObject());

    JS::RootedObject exc(cx, &v_exc.toObject());
    JSErrorReport* report = JS_ErrorFromException(cx, exc);
    g_assert_nonnull(report);

    char* retval = g_strdup(report->message().c_str());
    g_assert_nonnull(retval);
    JS_ClearPendingException(cx);
    return retval;
}
