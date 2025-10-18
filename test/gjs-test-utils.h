/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Endless Mobile, Inc.
// SPDX-FileContributor: Authored By: Sam Spilsbury <sam@endlessm.com>

#pragma once

#include <config.h>

#include <stdint.h>  // for uintptr_t

#include <limits>    // for numeric_limits
#include <string>
#include <type_traits>  // for is_same

#include <glib.h>  // for g_assert_...

#include <js/TypeDecls.h>

#include "gjs/context.h"

#define g_assert_ok(result)                                                  \
    G_STMT_START {                                                           \
        if G_UNLIKELY (result.isErr()) {                                     \
            std::string message{"'" #result "' should be OK but got " +      \
                                std::string{result.unwrapErr()}};            \
            g_assertion_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                                message.c_str());                            \
        }                                                                    \
    }                                                                        \
    G_STMT_END

#define g_assert_err(result)                                                 \
    G_STMT_START {                                                           \
        if G_UNLIKELY (result.isOk())                                        \
            g_assertion_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, \
                                "'" #result "' should be Err but got OK");   \
    }                                                                        \
    G_STMT_END

struct GjsUnitTestFixture {
    GjsContext* gjs_context;
    JSContext* cx;
    JS::Realm* realm;
};

void gjs_unit_test_fixture_setup(GjsUnitTestFixture*, const void* unused);

void gjs_unit_test_destroy_context(GjsUnitTestFixture*);

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture*, const void* unused);

void gjs_test_add_tests_for_coverage();

void gjs_test_add_tests_for_parse_call_args();

void gjs_test_add_tests_for_rooting();

void gjs_test_add_tests_for_jsapi_utils();

namespace Gjs {
namespace Test {

void add_tests_for_misc_utils();
void add_tests_for_toggle_queue();

template <typename T1, typename T2>
constexpr bool comparable_types() {
    if constexpr (std::is_same<T1, T2>()) {
        return true;
    } else if constexpr (std::is_arithmetic_v<T1> == std::is_arithmetic_v<T2>) {
        return std::is_signed_v<T1> == std::is_signed_v<T2>;
    } else if constexpr (std::is_enum_v<T1> == std::is_enum_v<T2>) {
        return std::is_signed_v<T1> == std::is_signed_v<T2>;
    } else {
        return false;
    }
}

template <typename T, typename U>
constexpr void assert_equal(T a, U b) {
    static_assert(comparable_types<T, U>());
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
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

template <typename T, typename U>
constexpr void assert_equal(std::pair<T, U> const& pair, T first, U second) {
    assert_equal(pair.first, first);
    assert_equal(pair.second, second);
}

}  // namespace Test
}  // namespace Gjs
