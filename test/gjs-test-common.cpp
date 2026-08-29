/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento

#include <config.h>

#include <utility>  // for move

#include <glib.h>

#include <js/CharacterEncoding.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <mozilla/Maybe.h>

#include "test/gjs-test-common.h"

using mozilla::Maybe, mozilla::Some;

Maybe<std::string> gjs_test_get_exception_message(JSContext* cx) {
    if (!JS_IsExceptionPending(cx))
        return {};

    JS::RootedValue v_exc(cx);
    g_assert_true(JS_GetPendingException(cx, &v_exc));
    g_assert_true(v_exc.isObject());

    JS::RootedObject exc(cx, &v_exc.toObject());
    JSErrorReport* report = JS_ErrorFromException(cx, exc);
    g_assert_nonnull(report);

    g_assert_true(report->message());
    std::string retval{report->message().c_str()};
    JS_ClearPendingException(cx);
    return Some(std::move(retval));
}
