/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <string>

#include <glib.h>
#include <glib-object.h>
#include <util/glib.h>

#include <gjs/context.h>
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs-test-utils.h"
#include "util/error.h"

#define VALID_UTF8_STRING "\303\211\303\226 foobar \343\203\237"

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

#define JS_CLASS "\
const Lang    = imports.lang; \
const GObject = imports.gi.GObject; \
\
const FooBar = new Lang.Class({ \
    Name: 'FooBar', \
    Extends: GObject.Object, \
}); \
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
    g_assert(G_IS_OBJECT(foo));

    g_object_unref(foo);
    g_object_unref(context);
}

static void
gjstest_test_func_gjs_jsapi_util_string_js_string_utf8(GjsUnitTestFixture *fx,
                                                       gconstpointer       unused)
{
    char *utf8_result;
    JS::RootedValue js_string(fx->cx);

    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, -1, &js_string));
    g_assert(js_string.isString());
    g_assert(gjs_string_to_utf8(fx->cx, js_string, &utf8_result));
    g_assert_cmpstr(VALID_UTF8_STRING, ==, utf8_result);
    g_free(utf8_result);
}

static void
gjstest_test_func_gjs_jsapi_util_error_throw(GjsUnitTestFixture *fx,
                                             gconstpointer       unused)
{
    JS::RootedValue exc(fx->cx), value(fx->cx);
    char *s = NULL;

    /* Test that we can throw */

    gjs_throw(fx->cx, "This is an exception %d", 42);

    g_assert(JS_IsExceptionPending(fx->cx));

    JS_GetPendingException(fx->cx, &exc);
    g_assert(!exc.isUndefined());

    JS::RootedObject exc_obj(fx->cx, &exc.toObject());
    JS_GetProperty(fx->cx, exc_obj, "message", &value);

    g_assert(value.isString());

    gjs_string_to_utf8(fx->cx, value, &s);
    g_assert_nonnull(s);
    g_assert_cmpstr(s, ==, "This is an exception 42");
    JS_free(fx->cx, s);

    /* keep this around before we clear it */
    JS::RootedValue previous(fx->cx, exc);

    JS_ClearPendingException(fx->cx);

    g_assert(!JS_IsExceptionPending(fx->cx));

    /* Check that we don't overwrite a pending exception */
    JS_SetPendingException(fx->cx, previous);

    g_assert(JS_IsExceptionPending(fx->cx));

    gjs_throw(fx->cx, "Second different exception %s", "foo");

    g_assert(JS_IsExceptionPending(fx->cx));

    exc = JS::UndefinedValue();
    JS_GetPendingException(fx->cx, &exc);
    g_assert(!exc.isUndefined());
    g_assert(&exc.toObject() == &previous.toObject());
}

static void
test_jsapi_util_string_char16_data(GjsUnitTestFixture *fx,
                                   gconstpointer       unused)
{
    char16_t *chars;
    size_t len;
    JS::RootedValue v_string(fx->cx);

    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, -1,
                                       &v_string));
    g_assert_true(gjs_string_get_char16_data(fx->cx, v_string, &chars,
                                             &len));
    std::u16string result(chars, len);
    g_assert_true(result == u"\xc9\xd6 foobar \u30df");
    g_free(chars);

    /* Try with a string that is likely to be stored as Latin-1 */
    v_string.setString(JS_NewStringCopyZ(fx->cx, "abcd"));
    g_assert_true(gjs_string_get_char16_data(fx->cx, v_string, &chars,
                                             &len));

    result.assign(chars, len);
    g_assert_true(result == u"abcd");
    g_free(chars);
}

static void
test_jsapi_util_string_to_ucs4(GjsUnitTestFixture *fx,
                               gconstpointer       unused)
{
    gunichar *chars;
    size_t len;
    JS::RootedValue v_string(fx->cx);

    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, -1,
                                       &v_string));
    g_assert_true(gjs_string_to_ucs4(fx->cx, v_string, &chars, &len));

    std::u32string result(chars, chars + len);
    g_assert_true(result == U"\xc9\xd6 foobar \u30df");
    g_free(chars);

    /* Try with a string that is likely to be stored as Latin-1 */
    v_string.setString(JS_NewStringCopyZ(fx->cx, "abcd"));
    g_assert_true(gjs_string_to_ucs4(fx->cx, v_string, &chars, &len));

    result.assign(chars, chars + len);
    g_assert_true(result == U"abcd");
    g_free(chars);
}

static void
test_jsapi_util_debug_string_valid_utf8(GjsUnitTestFixture *fx,
                                        gconstpointer       unused)
{
    JS::RootedValue v_string(fx->cx);
    g_assert_true(gjs_string_from_utf8(fx->cx, VALID_UTF8_STRING, -1,
                                       &v_string));

    char *debug_output = gjs_value_debug_string(fx->cx, v_string);

    g_assert_nonnull(debug_output);
    g_assert_cmpstr("\"" VALID_UTF8_STRING "\"", ==, debug_output);

    g_free(debug_output);
}

static void
test_jsapi_util_debug_string_invalid_utf8(GjsUnitTestFixture *fx,
                                          gconstpointer       unused)
{
    g_test_skip("SpiderMonkey doesn't validate UTF-8 after encoding it");

    JS::RootedValue v_string(fx->cx);
    const char16_t invalid_unicode[] = { 0xffff, 0xffff };
    v_string.setString(JS_NewUCStringCopyN(fx->cx, invalid_unicode, 2));

    char *debug_output = gjs_value_debug_string(fx->cx, v_string);

    g_assert_nonnull(debug_output);
    /* g_assert_cmpstr("\"\\xff\\xff\\xff\\xff\"", ==, debug_output); */

    g_free(debug_output);
}

static void
test_jsapi_util_debug_string_object_with_complicated_to_string(GjsUnitTestFixture *fx,
                                                               gconstpointer       unused)
{
    const char16_t desserts[] = {
        0xd83c, 0xdf6a,  /* cookie */
        0xd83c, 0xdf69,  /* doughnut */
    };
    JS::AutoValueArray<2> contents(fx->cx);
    contents[0].setString(JS_NewUCStringCopyN(fx->cx, desserts, 2));
    contents[1].setString(JS_NewUCStringCopyN(fx->cx, desserts + 2, 2));
    JS::RootedObject array(fx->cx, JS_NewArrayObject(fx->cx, contents));
    JS::RootedValue v_array(fx->cx, JS::ObjectValue(*array));
    char *debug_output = gjs_value_debug_string(fx->cx, v_array);

    g_assert_nonnull(debug_output);
    g_assert_cmpstr(u8"🍪,🍩", ==, debug_output);

    g_free(debug_output);
}

static void
gjstest_test_func_util_glib_strv_concat_null(void)
{
    char **ret;

    ret = gjs_g_strv_concat(NULL, 0);
    g_assert(ret != NULL);
    g_assert(ret[0] == NULL);

    g_strfreev(ret);
}

static void
gjstest_test_func_util_glib_strv_concat_pointers(void)
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
    g_assert(ret != NULL);
    g_assert_cmpstr(ret[0], ==, strv0[0]);  /* same string */
    g_assert(ret[0] != strv0[0]);           /* different pointer */
    g_assert_cmpstr(ret[1], ==, strv3[0]);
    g_assert(ret[1] != strv3[0]);
    g_assert(ret[2] == NULL);

    g_strfreev(ret);
}

static void
gjstest_test_strip_shebang_no_advance_for_no_shebang(void)
{
    const char *script = "foo\nbar";
    gssize     script_len_original = strlen(script);
    gssize     script_len = script_len_original;
    int        line_number = 1;

    const char *stripped = gjs_strip_unix_shebang(script,
                                                  &script_len,
                                                  &line_number);

    g_assert_cmpstr(script, ==, stripped);
    g_assert(script_len == script_len_original);
    g_assert(line_number == 1);
}

static void
gjstest_test_strip_shebang_advance_for_shebang(void)
{
    const char *script = "#!foo\nbar";
    gssize     script_len_original = strlen(script);
    gssize     script_len = script_len_original;
    int        line_number = 1;

    const char *stripped = gjs_strip_unix_shebang(script,
                                                  &script_len,
                                                  &line_number);

    g_assert_cmpstr(stripped, ==, "bar");
    g_assert(script_len == 3);
    g_assert(line_number == 2);
}

static void
gjstest_test_strip_shebang_return_null_for_just_shebang(void)
{
    const char *script = "#!foo";
    gssize     script_len_original = strlen(script);
    gssize     script_len = script_len_original;
    int        line_number = 1;

    const char *stripped = gjs_strip_unix_shebang(script,
                                                  &script_len,
                                                  &line_number);

    g_assert(stripped == NULL);
    g_assert(script_len == 0);
    g_assert(line_number == -1);
}

int
main(int    argc,
     char **argv)
{
    /* give the unit tests 7 minutes to complete, unless an environment variable
     * is set; use this when running under GDB, for example */
    if (!g_getenv("GJS_TEST_SKIP_TIMEOUT"))
        gjs_crash_after_timeout(60 * 7);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/gjs/context/construct/destroy", gjstest_test_func_gjs_context_construct_destroy);
    g_test_add_func("/gjs/context/construct/eval", gjstest_test_func_gjs_context_construct_eval);
    g_test_add_func("/gjs/context/exit", gjstest_test_func_gjs_context_exit);
    g_test_add_func("/gjs/gobject/js_defined_type", gjstest_test_func_gjs_gobject_js_defined_type);
    g_test_add_func("/gjs/jsutil/strip_shebang/no_shebang", gjstest_test_strip_shebang_no_advance_for_no_shebang);
    g_test_add_func("/gjs/jsutil/strip_shebang/have_shebang", gjstest_test_strip_shebang_advance_for_shebang);
    g_test_add_func("/gjs/jsutil/strip_shebang/only_shebang", gjstest_test_strip_shebang_return_null_for_just_shebang);
    g_test_add_func("/util/glib/strv/concat/null", gjstest_test_func_util_glib_strv_concat_null);
    g_test_add_func("/util/glib/strv/concat/pointers", gjstest_test_func_util_glib_strv_concat_pointers);

#define ADD_JSAPI_UTIL_TEST(path, func)                            \
    g_test_add("/gjs/jsapi/util/" path, GjsUnitTestFixture, NULL,  \
               gjs_unit_test_fixture_setup, func,                  \
               gjs_unit_test_fixture_teardown)

    ADD_JSAPI_UTIL_TEST("error/throw",
                        gjstest_test_func_gjs_jsapi_util_error_throw);
    ADD_JSAPI_UTIL_TEST("string/js/string/utf8",
                        gjstest_test_func_gjs_jsapi_util_string_js_string_utf8);
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

#undef ADD_JSAPI_UTIL_TEST

    gjs_test_add_tests_for_coverage ();
    gjs_test_add_tests_for_parse_call_args();
    gjs_test_add_tests_for_rooting();

    g_test_run();

    return 0;
}
