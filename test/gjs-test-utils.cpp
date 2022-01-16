/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.

#include <config.h>

#include <glib-object.h>
#include <glib.h>

#include <js/Realm.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/context.h"
#include "gjs/jsapi-util.h"
#include "test/gjs-test-common.h"
#include "test/gjs-test-utils.h"

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void*) {
    fx->gjs_context = gjs_context_new();
    fx->cx = (JSContext *) gjs_context_get_native_context(fx->gjs_context);

    JS::RootedObject global(fx->cx, gjs_get_import_global(fx->cx));
    fx->realm = JS::EnterRealm(fx->cx, global);
}

void
gjs_unit_test_destroy_context(GjsUnitTestFixture *fx)
{
    GjsAutoChar message = gjs_test_get_exception_message(fx->cx);
    if (message)
        g_printerr("**\n%s\n", message.get());

    JS::LeaveRealm(fx->cx, fx->realm);

    g_object_unref(fx->gjs_context);
}

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void*) {
    gjs_unit_test_destroy_context(fx);
}
