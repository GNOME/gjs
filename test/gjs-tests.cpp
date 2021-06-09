/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>
#include <string.h>  // for size_t, strlen

#include <limits>
#include <random>
#include <string>  // for u16string, u32string
#include <type_traits>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>  // for g_unlink

#include <js/Array.h>
#include <js/BigInt.h>
#include <js/CharacterEncoding.h>
#include <js/Id.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>
#include <jspubtd.h>  // for JSProto_Number
#include <mozilla/Span.h>  // for MakeStringSpan

#include "gi/arg-inl.h"
#include "gjs/context.h"
#include "gjs/error-types.h"
#include "gjs/jsapi-util.h"
#include "gjs/profiler.h"
#include "test/gjs-test-no-introspection-object.h"
#include "test/gjs-test-utils.h"
#include "util/misc.h"

// COMPAT: https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1553
#ifdef __clang_analyzer__
void g_assertion_message(const char*, const char*, int, const char*,
                         const char*) __attribute__((analyzer_noreturn));
#endif

#define VALID_UTF8_STRING "\303\211\303\226 foobar \343\203\237"

namespace Gjs {
namespace Test {
static unsigned cpp_random_seed = 0;

using Gjs::Test::assert_equal;

template <typename T>
T get_random_number() {
    std::mt19937_64 gen(cpp_random_seed);

    if constexpr (std::is_same_v<T, bool>) {
        return g_random_boolean();
    } else if constexpr (std::is_integral_v<T>) {
        T lowest_value = std::numeric_limits<T>::lowest();

        if constexpr (std::is_unsigned_v<T>)
            lowest_value = 1;

        return std::uniform_int_distribution<T>(lowest_value)(gen);
    } else if constexpr (std::is_arithmetic_v<T>) {
        T lowest_value = std::numeric_limits<T>::epsilon();
        return std::uniform_real_distribution<T>(lowest_value)(gen);
    } else if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(get_random_number<uintptr_t>());
    }
}

static void
gjstest_test_func_gjs_context_construct_destroy(void)
{
    GjsContext *context;

    /* Construct twice just to possibly a case where global state from
     * the first leaks.
     */
    context = gjs_context_new ();
    g_object_unref (context);

    context = gjs_context_new ();
    g_object_unref (context);
}

static void
gjstest_test_func_gjs_context_construct_eval(void)
{
    GjsContext *context;
    int estatus;
    GError *error = NULL;

    context = gjs_context_new ();
    if (!gjs_context_eval (context, "1+1", -1, "<input>", &estatus, &error))
        g_error ("%s", error->message);
    g_object_unref (context);
}

static void gjstest_test_func_gjs_context_eval_dynamic_import() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = NULL;
    int status;

    bool ok = gjs_context_eval(gjs, R"js(
        import('system')
            .catch(logError)
            .finally(() => imports.mainloop.quit());
        imports.mainloop.run();
    )js",
                               -1, "<main>", &status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
}

static void gjstest_test_func_gjs_context_eval_dynamic_import_relative() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = NULL;
    int status;

    bool ok = g_file_set_contents("num.js", "export default 77;", -1, &error);

    g_assert_true(ok);
    g_assert_no_error(error);

    ok = gjs_context_eval(gjs, R"js(
        let num;
        import('./num.js')
            .then(module => (num = module.default))
            .catch(logError)
            .finally(() => imports.mainloop.quit());
        imports.mainloop.run();
        num;
    )js",
                          -1, "<main>", &status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
    g_assert_cmpint(status, ==, 77);

    g_unlink("num.js");
}

static void gjstest_test_func_gjs_context_eval_dynamic_import_bad() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = NULL;
    int status;

    g_test_expect_message("Gjs", G_LOG_LEVEL_WARNING,
                          "*Unknown module: 'badmodule'*");

    bool ok = gjs_context_eval(gjs, R"js(
        let isBad = false;
        import('badmodule')
            .catch(err => {
                logError(err);
                isBad = true;
            })
            .finally(() => imports.mainloop.quit());
        imports.mainloop.run();

        if (isBad) imports.system.exit(10);
    )js",
                               -1, "<main>", &status, &error);

    g_assert_false(ok);
    g_assert_cmpuint(status, ==, 10);

    g_test_assert_expected_messages();

    g_clear_error(&error);
}

static void gjstest_test_func_gjs_context_eval_non_zero_terminated(void) {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = NULL;
    int status;

    // This string is invalid JS if it is treated as zero-terminated
    bool ok = gjs_context_eval(gjs, "77!", 2, "<input>", &status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
    g_assert_cmpint(status, ==, 77);
}

static void
gjstest_test_func_gjs_context_exit(void)
{
    GjsContext *context = gjs_context_new();
    GError *error = NULL;
    int status;

    bool ok = gjs_context_eval(context, "imports.system.exit(0);", -1,
                               "<input>", &status, &error);
    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT);
    g_assert_cmpuint(status, ==, 0);

    g_clear_error(&error);

    ok = gjs_context_eval(context, "imports.system.exit(42);", -1, "<input>",
                          &status, &error);
    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT);
    g_assert_cmpuint(status, ==, 42);

    g_clear_error(&error);
    g_object_unref(context);
}

static void gjstest_test_func_gjs_context_eval_module_file() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    uint8_t exit_status;
    GError* error = nullptr;

    bool ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/default.js",
        &exit_status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
    // for modules, last executed statement is _not_ the exit code
    g_assert_cmpuint(exit_status, ==, 0);
}

static void gjstest_test_func_gjs_context_eval_module_file_throw() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    uint8_t exit_status;
    GError* error = nullptr;

    g_test_expect_message("Gjs", G_LOG_LEVEL_CRITICAL, "*bad module*");

    bool ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/throws.js",
        &exit_status, &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_FAILED);
    g_assert_cmpuint(exit_status, ==, 1);

    g_test_assert_expected_messages();

    g_clear_error(&error);
}

static void gjstest_test_func_gjs_context_eval_module_file_exit() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;
    uint8_t exit_status;

    bool ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/exit0.js",
        &exit_status, &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT);
    g_assert_cmpuint(exit_status, ==, 0);

    g_clear_error(&error);

    ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/exit.js",
        &exit_status, &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT);
    g_assert_cmpuint(exit_status, ==, 42);

    g_clear_error(&error);
}

static void gjstest_test_func_gjs_context_eval_module_file_fail_instantiate() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;
    uint8_t exit_status;

    g_test_expect_message("Gjs", G_LOG_LEVEL_WARNING, "*foo*");

    // evaluating this module without registering 'foo' first should make it
    // fail ModuleInstantiate
    bool ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/import.js",
        &exit_status, &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_FAILED);
    g_assert_cmpuint(exit_status, ==, 1);

    g_test_assert_expected_messages();

    g_clear_error(&error);
}

static void gjstest_test_func_gjs_context_register_module_eval_module() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;

    bool ok = gjs_context_register_module(
        gjs, "foo", "resource:///org/gnome/gjs/mock/test/modules/default.js",
        &error);

    g_assert_true(ok);
    g_assert_no_error(error);

    uint8_t exit_status;
    ok = gjs_context_eval_module(gjs, "foo", &exit_status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
    g_assert_cmpuint(exit_status, ==, 0);
}

static void gjstest_test_func_gjs_context_register_module_eval_module_file() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;

    bool ok = gjs_context_register_module(
        gjs, "foo", "resource:///org/gnome/gjs/mock/test/modules/default.js",
        &error);

    g_assert_true(ok);
    g_assert_no_error(error);

    uint8_t exit_status;
    ok = gjs_context_eval_module_file(
        gjs, "resource:///org/gnome/gjs/mock/test/modules/import.js",
        &exit_status, &error);

    g_assert_true(ok);
    g_assert_no_error(error);
    g_assert_cmpuint(exit_status, ==, 0);
}

static void gjstest_test_func_gjs_context_register_module_non_existent() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;

    bool ok = gjs_context_register_module(gjs, "foo", "nonexist.js", &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_FAILED);

    g_clear_error(&error);
}

static void gjstest_test_func_gjs_context_eval_module_unregistered() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = nullptr;
    uint8_t exit_status;

    bool ok = gjs_context_eval_module(gjs, "foo", &exit_status, &error);

    g_assert_false(ok);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_FAILED);
    g_assert_cmpuint(exit_status, ==, 1);

    g_clear_error(&error);
}

#define JS_CLASS "\
const GObject = imports.gi.GObject; \
const FooBar = GObject.registerClass(class FooBar extends GObject.Object {}); \
"

static void
gjstest_test_func_gjs_gobject_js_defined_type(void)
{
    GjsContext *context = gjs_context_new();
    GError *error = NULL;
    int status;
    bool ok = gjs_context_eval(context, JS_CLASS, -1, "<input>", &status, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    GType foo_type = g_type_from_name("Gjs_FooBar");
    g_assert_cmpuint(foo_type, !=, G_TYPE_INVALID);

    gpointer foo = g_object_new(foo_type, NULL);
    g_assert_true(G_IS_OBJECT(foo));

    g_object_unref(foo);
    g_object_unref(context);
}

static void gjstest_test_func_gjs_gobject_without_introspection(void) {
    GjsAutoUnref<GjsContext> context = gjs_context_new();
    GError* error = nullptr;
    int status;

    /* Ensure class */
    g_type_class_ref(GJSTEST_TYPE_NO_INTROSPECTION_OBJECT);

#define TESTJS                                                         \
    "const {GObject} = imports.gi;"                                    \
    "var obj = GObject.Object.newv("                                   \
    "    GObject.type_from_name('GjsTestNoIntrospectionObject'), []);" \
    "obj.a_int = 1234;"

    bool ok = gjs_context_eval(context, TESTJS, -1, "<input>", &status, &error);
    g_assert_true(ok);
    g_assert_no_error(error);

    GjsTestNoIntrospectionObject* obj = gjstest_no_introspection_object_peek();
    g_assert_nonnull(obj);

    int val = 0;
    g_object_get(obj, "a-int", &val, NULL);
    g_assert_cmpint(val, ==, 1234);

#undef TESTJS
}

static void gjstest_test_func_gjs_jsapi_util_string_js_string_utf8(
    GjsUnitTestFixture* fx, const void*) {
    JS::RootedValue js_string(fx->cx);
    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, &js_string));
    g_assert_true(js_string.isString());

    JS::UniqueChars utf8_result = gjs_string_to_utf8(fx->cx, js_string);
    g_assert_nonnull(utf8_result);
    g_assert_cmpstr(VALID_UTF8_STRING, ==, utf8_result.get());
}

static void gjstest_test_func_gjs_jsapi_util_error_throw(GjsUnitTestFixture* fx,
                                                         const void*) {
    JS::RootedValue exc(fx->cx), value(fx->cx);

    /* Test that we can throw */

    gjs_throw(fx->cx, "This is an exception %d", 42);

    g_assert_true(JS_IsExceptionPending(fx->cx));

    JS_GetPendingException(fx->cx, &exc);
    g_assert_false(exc.isUndefined());

    JS::RootedObject exc_obj(fx->cx, &exc.toObject());
    JS_GetProperty(fx->cx, exc_obj, "message", &value);

    g_assert_true(value.isString());

    JS::UniqueChars s = gjs_string_to_utf8(fx->cx, value);
    g_assert_nonnull(s);
    g_assert_cmpstr(s.get(), ==, "This is an exception 42");

    /* keep this around before we clear it */
    JS::RootedValue previous(fx->cx, exc);

    JS_ClearPendingException(fx->cx);

    g_assert_false(JS_IsExceptionPending(fx->cx));

    /* Check that we don't overwrite a pending exception */
    JS_SetPendingException(fx->cx, previous);

    g_assert_true(JS_IsExceptionPending(fx->cx));

    gjs_throw(fx->cx, "Second different exception %s", "foo");

    g_assert_true(JS_IsExceptionPending(fx->cx));

    exc = JS::UndefinedValue();
    JS_GetPendingException(fx->cx, &exc);
    g_assert_false(exc.isUndefined());
    g_assert_true(&exc.toObject() == &previous.toObject());
}

static void test_jsapi_util_string_utf8_nchars_to_js(GjsUnitTestFixture* fx,
                                                     const void*) {
    JS::RootedValue v_out(fx->cx);
    bool ok = gjs_string_from_utf8_n(fx->cx, VALID_UTF8_STRING,
                                     strlen(VALID_UTF8_STRING), &v_out);
    g_assert_true(ok);
    g_assert_true(v_out.isString());
}

static void test_jsapi_util_string_char16_data(GjsUnitTestFixture* fx,
                                               const void*) {
    char16_t *chars;
    size_t len;

    JS::ConstUTF8CharsZ jschars(VALID_UTF8_STRING, strlen(VALID_UTF8_STRING));
    JS::RootedString str(fx->cx, JS_NewStringCopyUTF8Z(fx->cx, jschars));
    g_assert_true(gjs_string_get_char16_data(fx->cx, str, &chars, &len));
    std::u16string result(chars, len);
    g_assert_true(result == u"\xc9\xd6 foobar \u30df");
    g_free(chars);

    /* Try with a string that is likely to be stored as Latin-1 */
    str = JS_NewStringCopyZ(fx->cx, "abcd");
    bool ok = gjs_string_get_char16_data(fx->cx, str, &chars, &len);
    g_assert_true(ok);

    result.assign(chars, len);
    g_assert_true(result == u"abcd");
    g_free(chars);
}

static void test_jsapi_util_string_to_ucs4(GjsUnitTestFixture* fx,
                                           const void*) {
    gunichar *chars;
    size_t len;

    JS::ConstUTF8CharsZ jschars(VALID_UTF8_STRING, strlen(VALID_UTF8_STRING));
    JS::RootedString str(fx->cx, JS_NewStringCopyUTF8Z(fx->cx, jschars));
    g_assert_true(gjs_string_to_ucs4(fx->cx, str, &chars, &len));

    std::u32string result(chars, chars + len);
    g_assert_true(result == U"\xc9\xd6 foobar \u30df");
    g_free(chars);

    /* Try with a string that is likely to be stored as Latin-1 */
    str = JS_NewStringCopyZ(fx->cx, "abcd");
    bool ok = gjs_string_to_ucs4(fx->cx, str, &chars, &len);
    g_assert_true(ok);

    result.assign(chars, chars + len);
    g_assert_true(result == U"abcd");
    g_free(chars);
}

static void test_jsapi_util_debug_string_valid_utf8(GjsUnitTestFixture* fx,
                                                    const void*) {
    JS::RootedValue v_string(fx->cx);
    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, &v_string));

    std::string debug_output = gjs_value_debug_string(fx->cx, v_string);

    g_assert_cmpstr("\"" VALID_UTF8_STRING "\"", ==, debug_output.c_str());
}

static void test_jsapi_util_debug_string_invalid_utf8(GjsUnitTestFixture* fx,
                                                      const void*) {
    g_test_skip("SpiderMonkey doesn't validate UTF-8 after encoding it");

    JS::RootedValue v_string(fx->cx);
    const char16_t invalid_unicode[] = { 0xffff, 0xffff };
    v_string.setString(JS_NewUCStringCopyN(fx->cx, invalid_unicode, 2));

    std::string debug_output = gjs_value_debug_string(fx->cx, v_string);
    // g_assert_cmpstr("\"\\xff\\xff\\xff\\xff\"", ==, debug_output.c_str());
}

static void test_jsapi_util_debug_string_object_with_complicated_to_string(
    GjsUnitTestFixture* fx, const void*) {
    const char16_t desserts[] = {
        0xd83c, 0xdf6a,  /* cookie */
        0xd83c, 0xdf69,  /* doughnut */
    };
    JS::RootedValueArray<2> contents(fx->cx);
    contents[0].setString(JS_NewUCStringCopyN(fx->cx, desserts, 2));
    contents[1].setString(JS_NewUCStringCopyN(fx->cx, desserts + 2, 2));
    JS::RootedObject array(fx->cx, JS::NewArrayObject(fx->cx, contents));
    JS::RootedValue v_array(fx->cx, JS::ObjectValue(*array));
    std::string debug_output = gjs_value_debug_string(fx->cx, v_array);

    g_assert_cmpstr(u8"🍪,🍩", ==, debug_output.c_str());
}

static void test_gjs_debug_id_string_no_quotes(GjsUnitTestFixture* fx,
                                               const void*) {
    jsid id = gjs_intern_string_to_id(fx->cx, "prop_key");
    std::string debug_output = gjs_debug_id(id);

    g_assert_cmpstr(debug_output.c_str(), ==, "prop_key");
}

static void test_gjs_debug_string_quotes(GjsUnitTestFixture* fx, const void*) {
    JS::ConstUTF8CharsZ chars("a string", strlen("a string"));
    JSString* str = JS_NewStringCopyUTF8Z(fx->cx, chars);
    std::string debug_output = gjs_debug_string(str);

    g_assert_cmpstr(debug_output.c_str(), ==, "\"a string\"");
}

static void test_gjs_debug_value_bigint(GjsUnitTestFixture* fx, const void*) {
    JS::BigInt* bi = JS::NumberToBigInt(fx->cx, 42);
    JS::Value v = JS::BigIntValue(bi);
    std::string debug_output = gjs_debug_value(v);

    g_assert_cmpstr(debug_output.c_str(), ==, "42n");
}

static void test_gjs_debug_value_bigint_uint64(GjsUnitTestFixture* fx,
                                               const void*) {
    // gjs_debug_value(BigIntValue) prints whatever fits into int64_t, because
    // more complicated operations might be fallible
    JS::BigInt* bi = JS::NumberToBigInt(fx->cx, G_MAXUINT64);
    JS::Value v = JS::BigIntValue(bi);
    std::string debug_output = gjs_debug_value(v);

    g_assert_cmpstr(debug_output.c_str(), ==, "-1n");
}

static void test_gjs_debug_value_bigint_huge(GjsUnitTestFixture* fx,
                                             const void*) {
    JS::BigInt* bi = JS::SimpleStringToBigInt(
        fx->cx, mozilla::MakeStringSpan("10000000000000001"), 16);
    JS::Value v = JS::BigIntValue(bi);
    std::string debug_output = gjs_debug_value(v);

    g_assert_cmpstr(debug_output.c_str(), ==, "1n");
}

static void test_gjs_debug_value_string_quotes(GjsUnitTestFixture* fx,
                                               const void*) {
    JS::RootedValue v(fx->cx);
    bool ok = gjs_string_from_utf8(fx->cx, "a string", &v);

    g_assert_true(ok);

    std::string debug_output = gjs_debug_value(v);

    g_assert_cmpstr(debug_output.c_str(), ==, "\"a string\"");
}

static void
gjstest_test_func_util_misc_strv_concat_null(void)
{
    char **ret;

    ret = gjs_g_strv_concat(NULL, 0);
    g_assert_nonnull(ret);
    g_assert_null(ret[0]);

    g_strfreev(ret);
}

static void
gjstest_test_func_util_misc_strv_concat_pointers(void)
{
    char  *strv0[2] = {(char*)"foo", NULL};
    char  *strv1[1] = {NULL};
    char **strv2    = NULL;
    char  *strv3[2] = {(char*)"bar", NULL};
    char **stuff[4];
    char **ret;

    stuff[0] = strv0;
    stuff[1] = strv1;
    stuff[2] = strv2;
    stuff[3] = strv3;

    ret = gjs_g_strv_concat(stuff, 4);
    g_assert_nonnull(ret);
    g_assert_cmpstr(ret[0], ==, strv0[0]);  /* same string */
    g_assert_true(ret[0] != strv0[0]);      // different pointer
    g_assert_cmpstr(ret[1], ==, strv3[0]);
    g_assert_true(ret[1] != strv3[0]);
    g_assert_null(ret[2]);

    g_strfreev(ret);
}

static void
gjstest_test_profiler_start_stop(void)
{
    GjsAutoUnref<GjsContext> context =
        static_cast<GjsContext *>(g_object_new(GJS_TYPE_CONTEXT,
                                               "profiler-enabled", TRUE,
                                               nullptr));
    GjsProfiler *profiler = gjs_context_get_profiler(context);

    gjs_profiler_set_filename(profiler, "dont-conflict-with-other-test.syscap");
    gjs_profiler_start(profiler);

    for (size_t ix = 0; ix < 100; ix++) {
        GError *error = nullptr;
        int estatus;

#define TESTJS "[1,5,7,1,2,3,67,8].sort()"

        if (!gjs_context_eval(context, TESTJS, -1, "<input>", &estatus, &error))
            g_printerr("ERROR: %s", error->message);

#undef TESTJS
    }

    gjs_profiler_stop(profiler);

    if (g_unlink("dont-conflict-with-other-test.syscap") != 0)
        g_message("Temp profiler file not deleted");
}

static void gjstest_test_safe_integer_max(GjsUnitTestFixture* fx, const void*) {
    JS::RootedObject number_class_object(fx->cx);
    JS::RootedValue safe_value(fx->cx);

    g_assert_true(
        JS_GetClassObject(fx->cx, JSProto_Number, &number_class_object));
    g_assert_true(JS_GetProperty(fx->cx, number_class_object,
                                 "MAX_SAFE_INTEGER", &safe_value));

    g_assert_cmpint(safe_value.toNumber(), ==, max_safe_big_number<int64_t>());
}

static void gjstest_test_safe_integer_min(GjsUnitTestFixture* fx, const void*) {
    JS::RootedObject number_class_object(fx->cx);
    JS::RootedValue safe_value(fx->cx);

    g_assert_true(
        JS_GetClassObject(fx->cx, JSProto_Number, &number_class_object));
    g_assert_true(JS_GetProperty(fx->cx, number_class_object,
                                 "MIN_SAFE_INTEGER", &safe_value));

    g_assert_cmpint(safe_value.toNumber(), ==, min_safe_big_number<int64_t>());
}

static void gjstest_test_args_set_get_unset() {
    GIArgument arg = {0};

    gjs_arg_set(&arg, true);
    g_assert_true(arg.v_boolean);

    gjs_arg_set(&arg, false);
    g_assert_false(arg.v_boolean);

    gjs_arg_set(&arg, true);
    g_assert_true(arg.v_boolean);
    gjs_arg_unset<bool>(&arg);
    g_assert_false(arg.v_boolean);

    int8_t random_int8 = get_random_number<int8_t>();
    gjs_arg_set(&arg, random_int8);
    assert_equal(arg.v_int8, random_int8);
    assert_equal(gjs_arg_get<int8_t>(&arg), random_int8);

    uint8_t random_uint8 = get_random_number<uint8_t>();
    gjs_arg_set(&arg, random_uint8);
    assert_equal(arg.v_uint8, random_uint8);
    assert_equal(gjs_arg_get<uint8_t>(&arg), random_uint8);

    int16_t random_int16 = get_random_number<int16_t>();
    gjs_arg_set(&arg, random_int16);
    assert_equal(arg.v_int16, random_int16);
    assert_equal(gjs_arg_get<int16_t>(&arg), random_int16);

    uint16_t random_uint16 = get_random_number<uint16_t>();
    gjs_arg_set(&arg, random_uint16);
    assert_equal(arg.v_uint16, random_uint16);
    assert_equal(gjs_arg_get<uint16_t>(&arg), random_uint16);

    int32_t random_int32 = get_random_number<int32_t>();
    gjs_arg_set(&arg, random_int32);
    assert_equal(arg.v_int32, random_int32);
    assert_equal(gjs_arg_get<int32_t>(&arg), random_int32);

    uint32_t random_uint32 = get_random_number<uint32_t>();
    gjs_arg_set(&arg, random_uint32);
    assert_equal(arg.v_uint32, random_uint32);
    assert_equal(gjs_arg_get<uint32_t>(&arg), random_uint32);

    int64_t random_int64 = get_random_number<int64_t>();
    gjs_arg_set(&arg, random_int64);
    assert_equal(arg.v_int64, random_int64);
    assert_equal(gjs_arg_get<int64_t>(&arg), random_int64);

    uint64_t random_uint64 = get_random_number<uint64_t>();
    gjs_arg_set(&arg, random_uint64);
    assert_equal(arg.v_uint64, random_uint64);
    assert_equal(gjs_arg_get<uint64_t>(&arg), random_uint64);

    char32_t random_char32 = get_random_number<char32_t>();
    gjs_arg_set(&arg, random_char32);
    assert_equal(static_cast<char32_t>(arg.v_uint32), random_char32);
    assert_equal(gjs_arg_get<char32_t>(&arg), random_char32);

    float random_float = get_random_number<float>();
    gjs_arg_set(&arg, random_float);
    assert_equal(arg.v_float, random_float);
    assert_equal(gjs_arg_get<float>(&arg), random_float);

    double random_double = get_random_number<double>();
    gjs_arg_set(&arg, random_double);
    assert_equal(arg.v_double, random_double);
    assert_equal(gjs_arg_get<double>(&arg), random_double);

    void* random_ptr = get_random_number<void*>();
    gjs_arg_set(&arg, random_ptr);
    assert_equal(arg.v_pointer, random_ptr);
    assert_equal(gjs_arg_get<void*>(&arg), random_ptr);

    GjsAutoChar cstr = g_strdup("Gjs argument string");
    gjs_arg_set(&arg, cstr.get());
    assert_equal(arg.v_string, const_cast<char*>("Gjs argument string"));
    assert_equal(static_cast<void*>(arg.v_string),
                 static_cast<void*>(cstr.get()));

    gjs_arg_set<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg, TRUE);
    g_assert_true(arg.v_boolean);
    g_assert_true((gjs_arg_get<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg)));

    gjs_arg_set<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg, FALSE);
    g_assert_false(arg.v_boolean);
    g_assert_false((gjs_arg_get<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg)));

    gjs_arg_set<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg, TRUE);
    g_assert_true(arg.v_boolean);
    gjs_arg_unset<gboolean, GI_TYPE_TAG_BOOLEAN>(&arg);
    g_assert_false(arg.v_boolean);

    GType random_gtype = get_random_number<GType>();
    gjs_arg_set<GType, GI_TYPE_TAG_GTYPE>(&arg, random_gtype);
    if constexpr (std::is_same_v<GType, gsize>)
        assert_equal(static_cast<GType>(arg.v_size), random_gtype);
    else if constexpr (std::is_same_v<GType, gulong>)
        assert_equal(static_cast<GType>(arg.v_ulong), random_gtype);
    assert_equal(gjs_arg_get<GType, GI_TYPE_TAG_GTYPE>(&arg), random_gtype);

    int random_signed_iface = get_random_number<int>();
    gjs_arg_set<int, GI_TYPE_TAG_INTERFACE>(&arg, random_signed_iface);
    assert_equal(arg.v_int, random_signed_iface);
    assert_equal(gjs_arg_get<int, GI_TYPE_TAG_INTERFACE>(&arg),
                 random_signed_iface);

    unsigned random_unsigned_iface = get_random_number<unsigned>();
    gjs_arg_set<unsigned, GI_TYPE_TAG_INTERFACE>(&arg, random_unsigned_iface);
    assert_equal(arg.v_uint, random_unsigned_iface);
    assert_equal(gjs_arg_get<unsigned, GI_TYPE_TAG_INTERFACE>(&arg),
                 random_unsigned_iface);
}

static void gjstest_test_args_rounded_values() {
    GIArgument arg = {0};

    gjs_arg_set<int64_t>(&arg, std::numeric_limits<int64_t>::max());
    g_test_expect_message(
        G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
        "*cannot be safely stored in a JS Number and may be rounded");
    assert_equal(gjs_arg_get_maybe_rounded<int64_t>(&arg),
                 static_cast<double>(gjs_arg_get<int64_t>(&arg)));
    g_test_assert_expected_messages();

    gjs_arg_set<int64_t>(&arg, std::numeric_limits<int64_t>::min());
    g_test_expect_message(
        G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
        "*cannot be safely stored in a JS Number and may be rounded");
    assert_equal(gjs_arg_get_maybe_rounded<int64_t>(&arg),
                 static_cast<double>(gjs_arg_get<int64_t>(&arg)));
    g_test_assert_expected_messages();

    gjs_arg_set<uint64_t>(&arg, std::numeric_limits<uint64_t>::max());
    g_test_expect_message(
        G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
        "*cannot be safely stored in a JS Number and may be rounded");
    assert_equal(gjs_arg_get_maybe_rounded<uint64_t>(&arg),
                 static_cast<double>(gjs_arg_get<uint64_t>(&arg)));
    g_test_assert_expected_messages();

    gjs_arg_set<uint64_t>(&arg, std::numeric_limits<uint64_t>::min());
    assert_equal(gjs_arg_get_maybe_rounded<uint64_t>(&arg), 0.0);
}

static void gjstest_test_func_gjs_context_argv_array() {
    GjsAutoUnref<GjsContext> gjs = gjs_context_new();
    GError* error = NULL;
    int status;

    const char* argv[1] = {"test"};
    bool ok = gjs_context_define_string_array(gjs, "ARGV", 1, argv, &error);

    g_assert_no_error(error);
    g_clear_error(&error);
    g_assert_true(ok);

    ok = gjs_context_eval(gjs, R"js(
        imports.system.exit(ARGV[0] === "test" ? 0 : 1)
    )js",
                          -1, "<main>", &status, &error);

    g_assert_cmpint(status, ==, 0);
    g_assert_error(error, GJS_ERROR, GJS_ERROR_SYSTEM_EXIT);
    g_assert_false(ok);
}

}  // namespace Test
}  // namespace Gjs

int
main(int    argc,
     char **argv)
{
    using namespace Gjs::Test;  // NOLINT(build/namespaces)

    /* Avoid interference in the tests from stray environment variable */
    g_unsetenv("GJS_ENABLE_PROFILER");
    g_unsetenv("GJS_TRACE_FD");

    for (int i = 0; i < argc; i++) {
        const char* seed = nullptr;

        if (g_str_has_prefix(argv[i], "--cpp-seed=") && strlen(argv[i]) > 11)
            seed = argv[i] + 11;
        else if (i < argc - 1 && g_str_equal(argv[i], "--cpp-seed"))
            seed = argv[i + 1];

        if (seed)
            cpp_random_seed = std::stoi(seed);
    }

    g_test_init(&argc, &argv, nullptr);

    if (!cpp_random_seed)
        cpp_random_seed = g_test_rand_int();

    g_message("Using C++ random seed %u\n", cpp_random_seed);

    g_test_add_func("/gjs/context/construct/destroy", gjstest_test_func_gjs_context_construct_destroy);
    g_test_add_func("/gjs/context/construct/eval", gjstest_test_func_gjs_context_construct_eval);
    g_test_add_func("/gjs/context/argv",
                    gjstest_test_func_gjs_context_argv_array);
    g_test_add_func("/gjs/context/eval/dynamic-import",
                    gjstest_test_func_gjs_context_eval_dynamic_import);
    g_test_add_func("/gjs/context/eval/dynamic-import/relative",
                    gjstest_test_func_gjs_context_eval_dynamic_import_relative);
    g_test_add_func("/gjs/context/eval/dynamic-import/bad",
                    gjstest_test_func_gjs_context_eval_dynamic_import_bad);
    g_test_add_func("/gjs/context/eval/non-zero-terminated",
                    gjstest_test_func_gjs_context_eval_non_zero_terminated);
    g_test_add_func("/gjs/context/exit", gjstest_test_func_gjs_context_exit);
    g_test_add_func("/gjs/context/eval-module-file",
                    gjstest_test_func_gjs_context_eval_module_file);
    g_test_add_func("/gjs/context/eval-module-file/throw",
                    gjstest_test_func_gjs_context_eval_module_file_throw);
    g_test_add_func("/gjs/context/eval-module-file/exit",
                    gjstest_test_func_gjs_context_eval_module_file_exit);
    g_test_add_func(
        "/gjs/context/eval-module-file/fail-instantiate",
        gjstest_test_func_gjs_context_eval_module_file_fail_instantiate);
    g_test_add_func("/gjs/context/register-module/eval-module",
                    gjstest_test_func_gjs_context_register_module_eval_module);
    g_test_add_func(
        "/gjs/context/register-module/eval-module-file",
        gjstest_test_func_gjs_context_register_module_eval_module_file);
    g_test_add_func("/gjs/context/register-module/non-existent",
                    gjstest_test_func_gjs_context_register_module_non_existent);
    g_test_add_func("/gjs/context/eval-module/unregistered",
                    gjstest_test_func_gjs_context_eval_module_unregistered);
    g_test_add_func("/gjs/gobject/js_defined_type", gjstest_test_func_gjs_gobject_js_defined_type);
    g_test_add_func("/gjs/gobject/without_introspection",
                    gjstest_test_func_gjs_gobject_without_introspection);
    g_test_add_func("/gjs/profiler/start_stop", gjstest_test_profiler_start_stop);
    g_test_add_func("/util/misc/strv/concat/null",
                    gjstest_test_func_util_misc_strv_concat_null);
    g_test_add_func("/util/misc/strv/concat/pointers",
                    gjstest_test_func_util_misc_strv_concat_pointers);

    g_test_add_func("/gi/args/set-get-unset", gjstest_test_args_set_get_unset);
    g_test_add_func("/gi/args/rounded_values",
                    gjstest_test_args_rounded_values);

#define ADD_JSAPI_UTIL_TEST(path, func)                            \
    g_test_add("/gjs/jsapi/util/" path, GjsUnitTestFixture, NULL,  \
               gjs_unit_test_fixture_setup, func,                  \
               gjs_unit_test_fixture_teardown)

    ADD_JSAPI_UTIL_TEST("error/throw",
                        gjstest_test_func_gjs_jsapi_util_error_throw);
    ADD_JSAPI_UTIL_TEST("string/js/string/utf8",
                        gjstest_test_func_gjs_jsapi_util_string_js_string_utf8);
    ADD_JSAPI_UTIL_TEST("string/utf8-nchars-to-js",
                        test_jsapi_util_string_utf8_nchars_to_js);
    ADD_JSAPI_UTIL_TEST("string/char16_data",
                        test_jsapi_util_string_char16_data);
    ADD_JSAPI_UTIL_TEST("string/to_ucs4",
                        test_jsapi_util_string_to_ucs4);
    ADD_JSAPI_UTIL_TEST("debug_string/valid-utf8",
                        test_jsapi_util_debug_string_valid_utf8);
    ADD_JSAPI_UTIL_TEST("debug_string/invalid-utf8",
                        test_jsapi_util_debug_string_invalid_utf8);
    ADD_JSAPI_UTIL_TEST("debug_string/object-with-complicated-to-string",
                        test_jsapi_util_debug_string_object_with_complicated_to_string);

    ADD_JSAPI_UTIL_TEST("gi/args/safe-integer/max",
                        gjstest_test_safe_integer_max);
    ADD_JSAPI_UTIL_TEST("gi/args/safe-integer/min",
                        gjstest_test_safe_integer_min);

    // Debug functions
    ADD_JSAPI_UTIL_TEST("debug_id/string/no-quotes",
                        test_gjs_debug_id_string_no_quotes);
    ADD_JSAPI_UTIL_TEST("debug_string/quotes", test_gjs_debug_string_quotes);
    ADD_JSAPI_UTIL_TEST("debug_value/bigint", test_gjs_debug_value_bigint);
    ADD_JSAPI_UTIL_TEST("debug_value/bigint/uint64",
                        test_gjs_debug_value_bigint_uint64);
    ADD_JSAPI_UTIL_TEST("debug_value/bigint/huge",
                        test_gjs_debug_value_bigint_huge);
    ADD_JSAPI_UTIL_TEST("debug_value/string/quotes",
                        test_gjs_debug_value_string_quotes);

#undef ADD_JSAPI_UTIL_TEST

    gjs_test_add_tests_for_coverage ();

    g_test_run();

    return 0;
}
