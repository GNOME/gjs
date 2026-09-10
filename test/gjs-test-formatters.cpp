/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <format>
#include <new>
#include <string>
#include <utility>  // for move

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

struct InfoFormatterFixture {
    GI::Repository repo;
    GI::AutoFunctionInfo strsplit;
    GI::AutoStructInfo gerror;

    // self is uninitialized memory
    static void setup(InfoFormatterFixture* self, const void*) {
        GI::Repository repo;
        Gjs::GErrorResult<GITypelib*> result = repo.require("GLib", "2.0");
        g_assert_ok(result);

        Maybe<GI::AutoFunctionInfo> strsplit =
            repo.find_by_name<GI::InfoTag::FUNCTION>("GLib", "strsplit");
        g_assert_true(strsplit.isSome());

        Maybe<GI::AutoStructInfo> error =
            repo.find_by_name<GI::InfoTag::STRUCT>("GLib", "Error");
        g_assert_true(error.isSome());

        new (self) InfoFormatterFixture{.repo = std::move(repo),
                                        .strsplit = std::move(*strsplit),
                                        .gerror = std::move(*error)};
    }

    static void teardown(InfoFormatterFixture* self, const void*) {
        self->~InfoFormatterFixture();
    }
};

static void test_format_gi_arg_info(InfoFormatterFixture* fx, const void*) {
    Maybe<GI::AutoArgInfo> arg = fx->strsplit.args()[0];
    g_assert_true(arg.isSome());

    assert_format_eq(std::format("{}", *arg), "GLib.strsplit.string");
    assert_format_eq(std::format("{:t}", *arg), "GIArgInfo");
    assert_format_matches(
        std::format("{:?}", *arg),
        R"(^GIArgInfo 0x[0-9a-f]+ \(GLib\.strsplit\.string\)$)");
}

static void test_format_gi_field_info(InfoFormatterFixture* fx, const void*) {
    Maybe<GI::AutoFieldInfo> field = fx->gerror.fields()[0];
    g_assert_true(field.isSome());

    assert_format_eq(std::format("{}", *field), "GLib.Error.domain");
    assert_format_eq(std::format("{:t}", *field), "GIFieldInfo");
    assert_format_matches(
        std::format("{:?}", *field),
        R"(^GIFieldInfo 0x[0-9a-f]+ \(GLib\.Error\.domain\)$)");
}

static void test_format_gi_function_info(InfoFormatterFixture* fx,
                                         const void*) {
    assert_format_eq(std::format("{}", fx->strsplit), "GLib.strsplit");
    assert_format_eq(std::format("{:t}", fx->strsplit), "GIFunctionInfo");
    assert_format_matches(
        std::format("{:?}", fx->strsplit),
        R"(^GIFunctionInfo 0x[0-9a-f]+ \(GLib\.strsplit\), )"
        R"(\.details = \{ \.func = \{ \.retval_transfer = )"
        R"(GI_TRANSFER_EVERYTHING, \.n_args = 3, \.args = \{ )"
        R"(\{ GI_DIRECTION_IN, GI_TRANSFER_NOTHING \}, )"
        R"(\{ GI_DIRECTION_IN, GI_TRANSFER_NOTHING \}, )"
        R"(\{ GI_DIRECTION_IN, GI_TRANSFER_NOTHING \} )"
        R"(\} \} \}$)");
}

static void test_format_gi_struct_info(InfoFormatterFixture* fx, const void*) {
    assert_format_eq(std::format("{}", fx->gerror), "GLib.Error");
    assert_format_eq(std::format("{:t}", fx->gerror), "GIStructInfo");
    assert_format_matches(std::format("{:?}", fx->gerror),
                          R"(^GIStructInfo 0x[0-9a-f]+ \(GLib\.Error\)$)");
}

// A method's qualified name includes its container, and a callable with no
// arguments still gets a (mostly empty) details block.
static void test_format_gi_method_info(InfoFormatterFixture* fx, const void*) {
    Maybe<GI::AutoFunctionInfo> copy = fx->gerror.method("copy");
    g_assert_true(copy.isSome());
    g_assert_cmpuint(copy->n_args(), ==, 0);

    assert_format_eq(std::format("{}", *copy), "GLib.Error.copy");
    assert_format_eq(std::format("{:t}", *copy), "GIFunctionInfo");
    assert_format_matches(
        std::format("{:?}", *copy),
        R"(^GIFunctionInfo 0x[0-9a-f]+ \(GLib\.Error\.copy\), )"
        R"(\.details = \{ \.func = \{ \.retval_transfer = )"
        R"(GI_TRANSFER_EVERYTHING, \.n_args = 0, \.args = \{  \} \} \}$)");
}

static void test_format_gi_type_info(InfoFormatterFixture* fx, const void*) {
    GI::StackTypeInfo stack;
    fx->strsplit.load_return_type(&stack);
    GI::TypeInfo return_type = stack;
    g_assert_cmpint(return_type.tag(), ==, GI_TYPE_TAG_ARRAY);

    // container type
    assert_format_eq(std::format("{}", return_type), "array<utf8>");
    assert_format_eq(std::format("{:t}", return_type), "GITypeInfo");
    assert_format_matches(std::format("{:?}", return_type),
                          R"(^GITypeInfo 0x[0-9a-f]+ \(array<utf8>\)$)");

    // basic (and also dependent) type
    GI::AutoTypeInfo element_type = return_type.element_type();
    assert_format_eq(std::format("{}", element_type), "utf8");
    assert_format_eq(std::format("{:t}", element_type), "GITypeInfo");
    assert_format_matches(std::format("{:?}", element_type),
                          R"(^GITypeInfo 0x[0-9a-f]+ \(utf8\)$)");

    // interface type
    Maybe<GI::AutoFunctionInfo> func =
        fx->repo.find_by_name<GI::InfoTag::FUNCTION>("GLib",
                                                     "file_error_from_errno");
    g_assert_true(func.isSome());

    func->load_return_type(&stack);
    GI::TypeInfo interface_type = stack;
    g_assert_cmpint(interface_type.tag(), ==, GI_TYPE_TAG_INTERFACE);

    assert_format_eq(std::format("{}", interface_type),
                     "interface GLib.FileError");
    assert_format_eq(std::format("{:t}", interface_type), "GITypeInfo");
    assert_format_matches(
        std::format("{:?}", interface_type),
        R"(^GITypeInfo 0x[0-9a-f]+ \(interface GLib\.FileError\)$)");

    // hash type with two dependent types
    func = fx->repo.find_by_name<GI::InfoTag::FUNCTION>("GLib",
                                                        "uri_parse_params");
    g_assert_true(func.isSome());

    func->load_return_type(&stack);
    GI::TypeInfo hash_type = stack;
    g_assert_cmpint(hash_type.tag(), ==, GI_TYPE_TAG_GHASH);

    assert_format_eq(std::format("{}", hash_type), "ghash<utf8, utf8>");
    assert_format_matches(std::format("{:?}", hash_type),
                          R"(^GITypeInfo 0x[0-9a-f]+ \(ghash<utf8, utf8>\)$)");
}

// GI::Auto___Info should behave the same way as GI::___Info
static void test_format_gi_info_owned_matches_unowned(InfoFormatterFixture* fx,
                                                      const void*) {
    // unowned view of fx->gerror
    Maybe<const GI::StructInfo> unowned =
        fx->gerror.fields()[0]->container()->as<GI::InfoTag::STRUCT>();

    assert_format_eq(std::format("{}", *unowned),
                     std::format("{}", fx->gerror));
    assert_format_eq(std::format("{:t}", *unowned),
                     std::format("{:t}", fx->gerror));
    assert_format_eq(std::format("{:?}", *unowned),
                     std::format("{:?}", fx->gerror));
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

    g_test_add_func("/gjs/formatter/gi-type-tag", test_format_gi_type_tag);

#define ADD_GI_FORMATTER_TEST(path, func)                             \
    g_test_add("/gjs/formatter/" path, InfoFormatterFixture, nullptr, \
               &InfoFormatterFixture::setup, func,                    \
               &InfoFormatterFixture::teardown)

    ADD_GI_FORMATTER_TEST("gi-arg-info", test_format_gi_arg_info);
    ADD_GI_FORMATTER_TEST("gi-field-info", test_format_gi_field_info);
    ADD_GI_FORMATTER_TEST("gi-function-info", test_format_gi_function_info);
    ADD_GI_FORMATTER_TEST("gi-method-info", test_format_gi_method_info);
    ADD_GI_FORMATTER_TEST("gi-struct-info", test_format_gi_struct_info);
    ADD_GI_FORMATTER_TEST("gi-type-info", test_format_gi_type_info);
    ADD_GI_FORMATTER_TEST("gi-info/owned-matches-unowned",
                          test_format_gi_info_owned_matches_unowned);

#undef ADD_GI_FORMATTER_TEST

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
