/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <format>
#include <string>

#include <girepository/girepository.h>
#include <glib.h>

#include <js/BigInt.h>
#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewPlainObject, InformalValueTypeName
#include <mozilla/Maybe.h>
#include <mozilla/Result.h>

#include "gi/info.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util.h"  // IWYU pragma: keep (for formatters)
#include "test/gjs-test-utils.h"

using mozilla::Err, mozilla::Maybe, mozilla::Ok;

namespace Gjs::Test {

#define assert_format_eq(s1, s2)                                               \
    G_STMT_START {                                                             \
        std::string _s1 = (s1), _s2 = (s2);                                    \
        if (_s1 != _s2)                                                        \
            g_assertion_message_cmpstr(G_LOG_DOMAIN, __FILE__, __LINE__,       \
                                       G_STRFUNC, #s1 " == " #s2, _s1.c_str(), \
                                       "==", _s2.c_str());                     \
    }                                                                          \
    G_STMT_END

#define assert_format_matches(str, pattern)                                 \
    G_STMT_START {                                                          \
        std::string _s1 = (str);                                            \
        const char* _s2 = (pattern);                                        \
        if (!g_regex_match_simple(_s2, _s1.c_str(), G_REGEX_DEFAULT,        \
                                  G_REGEX_MATCH_DEFAULT)) {                 \
            g_assertion_message_cmpstr(                                     \
                G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,                \
                "assertion failed (\"" #str "\" matches \"" #pattern "\")", \
                _s1.c_str(), "~=", _s2);                                    \
        }                                                                   \
    }                                                                       \
    G_STMT_END

static void test_format_value(GjsUnitTestFixture* fx, const void*) {
    JS::RootedString str{fx->cx, JS_NewStringCopyZ(fx->cx, "foo")};
    g_assert_nonnull(str);
    JS::RootedBigInt bigint{fx->cx, JS::NumberToBigInt(fx->cx, 42)};
    g_assert_nonnull(bigint);
    JS::RootedSymbol symbol{fx->cx, JS::NewSymbol(fx->cx, str)};
    g_assert_nonnull(symbol);
    JS::RootedObject obj{fx->cx, JS_NewPlainObject(fx->cx)};
    g_assert_nonnull(obj);

    assert_format_eq(std::format("{}", JS::UndefinedValue()), "undefined");
    assert_format_eq(std::format("{}", JS::NullValue()), "null");
    assert_format_eq(std::format("{}", JS::Int32Value(42)), "42");
    assert_format_eq(std::format("{}", JS::DoubleValue(3.5)), "3.5");
    assert_format_eq(std::format("{}", JS::TrueValue()), "true");
    assert_format_eq(std::format("{}", JS::FalseValue()), "false");
    assert_format_eq(std::format("{}", JS::StringValue(str)), "\"foo\"");
    assert_format_eq(std::format("{}", JS::BigIntValue(bigint)),
                     "42n (modulo 2^64)");
    assert_format_eq(std::format("{}", JS::SymbolValue(symbol)),
                     "Symbol(\"foo\")");
    assert_format_matches(std::format("{}", JS::ObjectValue(*obj)),
                          "^<object Object at 0x[0-9a-f]+>$");
    assert_format_eq(std::format("{}", JS::MagicValue(JS_GENERIC_MAGIC)),
                     "<magic>");

    JS::Value values[] = {JS::UndefinedValue(),
                          JS::NullValue(),
                          JS::Int32Value(42),
                          JS::DoubleValue(3.5),
                          JS::TrueValue(),
                          JS::FalseValue(),
                          JS::StringValue(str),
                          JS::BigIntValue(bigint),
                          JS::SymbolValue(symbol),
                          JS::ObjectValue(*obj),
                          JS::MagicValue(JS_GENERIC_MAGIC)};

    for (JS::Value v : values) {
        // asRawBits() is an implementation detail of the engine
        assert_format_eq(std::format("{:?}", v),
                         std::format("JS::Value {:#x}", v.asRawBits()));
        // InformalValueTypeName is also an implementation detail of the engine
        assert_format_eq(std::format("{:t}", v), JS::InformalValueTypeName(v));
    }
}

static void test_format_value_rooted(GjsUnitTestFixture* fx, const void*) {
    JS::Value raw = JS::Int32Value(42);
    JS::RootedValue rooted{fx->cx, raw};
    JS::HandleValue handle = rooted;

    // RootedValue and HandleValue delegate to the JS::Value formatter, so all
    // three specifiers should match the underlying value.
    assert_format_eq(std::format("{}", rooted), std::format("{}", raw));
    assert_format_eq(std::format("{:t}", rooted), std::format("{:t}", raw));
    assert_format_eq(std::format("{:?}", rooted), std::format("{:?}", raw));
    assert_format_eq(std::format("{}", handle), std::format("{}", raw));
    assert_format_eq(std::format("{:t}", handle), std::format("{:t}", raw));
    assert_format_eq(std::format("{:?}", handle), std::format("{:?}", raw));
}

static void test_format_string(GjsUnitTestFixture* fx, const void*) {
    JSString* str = JS_NewStringCopyZ(fx->cx, "foo");
    g_assert_nonnull(str);

    assert_format_eq(std::format("{}", str), "\"foo\"");
    assert_format_matches(std::format("{:?}", str), "^JSString 0x[0-9a-f]+$");
}

static void test_format_string_null(GjsUnitTestFixture*, const void*) {
    JSString* nothing = nullptr;
    assert_format_eq(std::format("{}", nothing), "<null string>");
    assert_format_eq(std::format("{:?}", nothing), "JSString 0x0");
}

static void test_format_string_rooted(GjsUnitTestFixture* fx, const void*) {
    JSString* raw = JS_NewStringCopyZ(fx->cx, "foo");
    g_assert_nonnull(raw);

    JS::RootedString rooted{fx->cx, raw};
    JS::HandleString handle = rooted;
    assert_format_eq(std::format("{}", rooted), std::format("{}", raw));
    assert_format_eq(std::format("{}", handle), std::format("{}", raw));
    assert_format_eq(std::format("{:?}", rooted), std::format("{:?}", raw));
    assert_format_eq(std::format("{:?}", handle), std::format("{:?}", raw));
}

static void test_format_id(GjsUnitTestFixture* fx, const void*) {
    JS::RootedString str{fx->cx, JS_AtomizeAndPinString(fx->cx, "foo")};
    g_assert_nonnull(str);
    JS::RootedSymbol symbol{fx->cx, JS::NewSymbol(fx->cx, str)};
    g_assert_nonnull(symbol);

    JS::PropertyKey int_id = JS::PropertyKey::Int(42);
    JS::PropertyKey void_id = JS::PropertyKey::Void();
    JS::PropertyKey string_id = JS::PropertyKey::fromPinnedString(str);
    JS::PropertyKey symbol_id = JS::PropertyKey::Symbol(symbol);

    assert_format_eq(std::format("{}", int_id), "42");
    assert_format_eq(std::format("{}", void_id), "undefined");
    assert_format_eq(std::format("{}", string_id), "foo");
    assert_format_eq(std::format("{}", symbol_id), "Symbol(\"foo\")");

    // asRawBits() is an implementation detail of the engine
    JS::PropertyKey ids[] = {int_id, void_id, string_id, symbol_id};
    for (JS::PropertyKey id : ids)
        assert_format_eq(std::format("{:?}", id),
                         std::format("JSID {:#x}", id.asRawBits()));
}

static void test_format_id_rooted(GjsUnitTestFixture* fx, const void*) {
    JS::PropertyKey raw = JS::PropertyKey::Int(42);
    JS::RootedId rooted{fx->cx, raw};
    JS::HandleId handle = rooted;
    assert_format_eq(std::format("{}", rooted), std::format("{}", raw));
    assert_format_eq(std::format("{}", handle), std::format("{}", raw));
    assert_format_eq(std::format("{:?}", rooted), std::format("{:?}", raw));
    assert_format_eq(std::format("{:?}", handle), std::format("{:?}", raw));
}

static void test_format_object(GjsUnitTestFixture* fx, const void*) {
    JSObject* obj = JS_NewPlainObject(fx->cx);
    g_assert_nonnull(obj);

    assert_format_matches(std::format("{}", obj),
                          "^<object Object at 0x[0-9a-f]+>$");
    assert_format_matches(std::format("{:?}", obj), "^Object 0x[0-9a-f]+$");
}

static void test_format_object_null(GjsUnitTestFixture*, const void*) {
    JSObject* nothing = nullptr;
    assert_format_eq(std::format("{}", nothing), "<null object>");
    assert_format_eq(std::format("{:?}", nothing), "Object 0x0");
}

static void test_format_object_rooted(GjsUnitTestFixture* fx, const void*) {
    JSObject* raw = JS_NewPlainObject(fx->cx);
    g_assert_nonnull(raw);
    JS::RootedObject rooted{fx->cx, raw};
    JS::HandleObject handle = rooted;
    assert_format_eq(std::format("{}", rooted), std::format("{}", raw));
    assert_format_eq(std::format("{}", handle), std::format("{}", raw));
    assert_format_eq(std::format("{:?}", rooted), std::format("{:?}", raw));
    assert_format_eq(std::format("{:?}", handle), std::format("{:?}", raw));
}

static void test_format_gi_type_tag() {
    GITypeTag tags[] = {
        GI_TYPE_TAG_VOID,     GI_TYPE_TAG_BOOLEAN, GI_TYPE_TAG_INT8,
        GI_TYPE_TAG_UINT32,   GI_TYPE_TAG_INT64,   GI_TYPE_TAG_FLOAT,
        GI_TYPE_TAG_DOUBLE,   GI_TYPE_TAG_GTYPE,   GI_TYPE_TAG_UTF8,
        GI_TYPE_TAG_FILENAME, GI_TYPE_TAG_ARRAY,   GI_TYPE_TAG_INTERFACE,
        GI_TYPE_TAG_GLIST,    GI_TYPE_TAG_ERROR};
    for (GITypeTag tag : tags)
        assert_format_eq(std::format("{}", tag), gi_type_tag_to_string(tag));

    // We get string format specifiers for free
    assert_format_eq(
        std::format("{:>10}", GI_TYPE_TAG_VOID),
        std::format("{:>10}", gi_type_tag_to_string(GI_TYPE_TAG_VOID)));
}

static void test_format_gi_type_info() {
    GI::Repository repo;
    g_assert_true(repo.require("GLib", "2.0").isOk());

    Maybe<GI::AutoFunctionInfo> func =
        repo.find_by_name<GI::InfoTag::FUNCTION>("GLib", "strsplit");
    g_assert_true(func.isSome());

    GI::StackTypeInfo stack;
    func->load_return_type(&stack);
    GI::TypeInfo return_type = stack;
    g_assert_cmpint(return_type.tag(), ==, GI_TYPE_TAG_ARRAY);

    assert_format_eq(std::format("{}", return_type), "array");
    assert_format_matches(std::format("{:?}", return_type),
                          "^GITypeInfo 0x[0-9a-f]+$");

    // GI::AutoTypeInfo should behave the same way as GI::TypeInfo
    GI::AutoTypeInfo element_type = return_type.element_type();
    assert_format_eq(std::format("{}", element_type), "utf8");
    assert_format_matches(std::format("{:?}", element_type),
                          "^GITypeInfo 0x[0-9a-f]+$");
}

// Gjs::GErrorResult<T>

static void test_format_gerror_result_ok() {
    GErrorResult<> ok{Ok{}};
    assert_format_eq(std::format("{}", ok), "Ok");
}

static void test_format_gerror_result_ok_value() {
    GErrorResult<int> ok{42};
    assert_format_eq(std::format("{}", ok), "Ok(42)");
}

static void test_format_gerror_result_ok_pointer() {
    int dummy = 0;
    GErrorResult<int*> ok{&dummy};
    assert_format_matches(std::format("{}", ok), R"(^Ok\(0x[0-9a-f]+\)$)");
}

static void test_format_gerror_result_err() {
    GError* error =
        g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_EXIST, "Message");
    GErrorResult<> result{Err(error)};
    assert_format_eq(std::format("{}", result), "Message");
}

void add_tests_for_formatters() {
#define ADD_FORMATTER_FIXTURE_TEST(path, func)                      \
    g_test_add("/gjs/formatter/" path, GjsUnitTestFixture, nullptr, \
               gjs_unit_test_fixture_setup, func,                   \
               gjs_unit_test_fixture_teardown)

    ADD_FORMATTER_FIXTURE_TEST("value", test_format_value);
    ADD_FORMATTER_FIXTURE_TEST("value/rooted", test_format_value_rooted);
    ADD_FORMATTER_FIXTURE_TEST("string", test_format_string);
    ADD_FORMATTER_FIXTURE_TEST("string/null", test_format_string_null);
    ADD_FORMATTER_FIXTURE_TEST("string/rooted", test_format_string_rooted);
    ADD_FORMATTER_FIXTURE_TEST("id", test_format_id);
    ADD_FORMATTER_FIXTURE_TEST("id/rooted", test_format_id_rooted);
    ADD_FORMATTER_FIXTURE_TEST("object", test_format_object);
    ADD_FORMATTER_FIXTURE_TEST("object/null", test_format_object_null);
    ADD_FORMATTER_FIXTURE_TEST("object/rooted", test_format_object_rooted);

#undef ADD_FORMATTER_FIXTURE_TEST

    g_test_add_func("/gjs/formatter/gi-type-info", test_format_gi_type_info);
    g_test_add_func("/gjs/formatter/gi-type-tag", test_format_gi_type_tag);
    g_test_add_func("/gjs/formatter/gerror-result/ok",
                    test_format_gerror_result_ok);
    g_test_add_func("/gjs/formatter/gerror-result/ok-value",
                    test_format_gerror_result_ok_value);
    g_test_add_func("/gjs/formatter/gerror-result/ok-pointer",
                    test_format_gerror_result_ok_pointer);
    g_test_add_func("/gjs/formatter/gerror-result/err",
                    test_format_gerror_result_err);
}

}  // namespace Gjs::Test
