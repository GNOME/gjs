/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Endless Mobile, Inc.
// SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>

#ifndef TEST_GJS_TEST_UTILS_H_
#define TEST_GJS_TEST_UTILS_H_

#include <config.h>

#include <glib.h>    // for g_assert_...
#include <stdint.h>  // for uintptr_t
#include <iterator>  // for pair
#include <limits>    // for numeric_limits
#include <string>
#include <utility>  // IWYU pragma: keep

#include "gjs/context.h"

#include <js/TypeDecls.h>

struct GjsUnitTestFixture {
    GjsContext *gjs_context;
    JSContext *cx;
    JS::Realm* realm;
};

void gjs_unit_test_fixture_setup(GjsUnitTestFixture* fx, const void* unused);

void gjs_unit_test_destroy_context(GjsUnitTestFixture *fx);

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture* fx, const void* unused);

void gjs_test_add_tests_for_coverage ();

void gjs_test_add_tests_for_parse_call_args(void);

void gjs_test_add_tests_for_rooting(void);

void gjs_test_add_tests_for_jsapi_utils();

namespace Gjs {
namespace Test {

template <typename T>
constexpr void assert_equal(T a, T b) {
    if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_unsigned_v<T>)
            g_assert_cmpuint(a, ==, b);
        else
            g_assert_cmpint(a, ==, b);
    } else if constexpr (std::is_arithmetic_v<T>) {
        g_assert_cmpfloat_with_epsilon(a, b, std::numeric_limits<T>::epsilon());
    } else if constexpr (std::is_same_v<T, char*>) {
        g_assert_cmpstr(a, ==, b);
    } else if constexpr (std::is_same_v<T, std::string>) {
        assert_equal(a.c_str(), b.c_str());
    } else if constexpr (std::is_pointer_v<T>) {
        assert_equal(reinterpret_cast<uintptr_t>(a),
                     reinterpret_cast<uintptr_t>(b));
    } else {
        g_assert_true(a == b);
    }
}

}  // namespace Test
}  // namespace Gjs

#endif  // TEST_GJS_TEST_UTILS_H_
