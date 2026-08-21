/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.

#include <config.h>

#include <string>

#include <glib-object.h>
#include <glib.h>

#include <js/Context.h>  // for JS_GetContextPrivate
#include <js/Realm.h>
#include <js/TypeDecls.h>
#include <mozilla/Maybe.h>

#include "gjs/context-private.h"
#include "gjs/context.h"
#include "test/gjs-test-common.h"
#include "test/gjs-test-utils.h"

using mozilla::Maybe;

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void*) {
    fx->gjs_context = gjs_context_new();
    fx->cx = static_cast<JSContext*>(
        gjs_context_get_native_context(fx->gjs_context));

    auto* gjs = static_cast<GjsContextPrivate*>(JS_GetContextPrivate(fx->cx));
    fx->realm = JS::EnterRealm(fx->cx, gjs->global());
}

void gjs_unit_test_destroy_context(GjsUnitTestFixture* fx) {
    Maybe<std::string> message = gjs_test_get_exception_message(fx->cx);
    if (message)
        g_printerr("**\n%s\n", message->c_str());

    JS::LeaveRealm(fx->cx, fx->realm);

    g_object_unref(fx->gjs_context);
}

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void*) {
    gjs_unit_test_destroy_context(fx);
}
