#include <string.h>

#include <glib.h>

#include "gjs/context.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-wrapper.h"
#include "test/gjs-test-utils.h"

#define assert_match(str, pattern)                                            \
    G_STMT_START {                                                            \
        const char *__s1 = (str), *__s2 = (pattern);                          \
        if (!g_pattern_match_simple(__s2, __s1)) {                            \
            g_printerr("**\nExpected \"%s\" to match \"%s\"\n", __s1, __s2);  \
            g_assert_not_reached();                                           \
        }                                                                     \
    } G_STMT_END

typedef enum _test_enum {
    ZERO,
    ONE,
    TWO,
    THREE
} test_enum_t;

typedef enum _test_signed_enum {
    MINUS_THREE = -3,
    MINUS_TWO,
    MINUS_ONE
} test_signed_enum_t;

#define JSNATIVE_TEST_FUNC_BEGIN(name)                      \
    static bool                                             \
    name(JSContext *cx,                                     \
         unsigned   argc,                                   \
         JS::Value *vp)                                     \
    {                                                       \
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);   \
        bool retval;

#define JSNATIVE_TEST_FUNC_END           \
        if (retval)                      \
            args.rval().setUndefined();  \
        return retval;                   \
    }

JSNATIVE_TEST_FUNC_BEGIN(no_args)
    retval = gjs_parse_call_args(cx, "noArgs", args, "");
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(no_args_ignore_trailing)
    retval = gjs_parse_call_args(cx, "noArgsIgnoreTrailing", args, "!");
JSNATIVE_TEST_FUNC_END

#define JSNATIVE_NO_ASSERT_TYPE_TEST_FUNC(type, fmt)                      \
    JSNATIVE_TEST_FUNC_BEGIN(type##_arg_no_assert)                        \
        type val;                                                         \
        retval = gjs_parse_call_args(cx, #type "ArgNoAssert", args, fmt,  \
                                     "val", &val);                        \
    JSNATIVE_TEST_FUNC_END

JSNATIVE_NO_ASSERT_TYPE_TEST_FUNC(bool, "b");
JSNATIVE_NO_ASSERT_TYPE_TEST_FUNC(int, "i");

#undef JSNATIVE_NO_ASSERT_TYPE_TEST_FUNC

JSNATIVE_TEST_FUNC_BEGIN(object_arg_no_assert)
    JS::RootedObject val(cx);
    retval = gjs_parse_call_args(cx, "objectArgNoAssert", args, "o",
                                 "val", &val);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(optional_int_args_no_assert)
    int val1, val2;
    retval = gjs_parse_call_args(cx, "optionalIntArgsNoAssert", args, "i|i",
                                 "val1", &val1,
                                 "val2", &val2);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(args_ignore_trailing)
    int val;
    retval = gjs_parse_call_args(cx, "argsIgnoreTrailing", args, "!i",
                                 "val", &val);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(one_of_each_type)
    bool boolval;
    GjsAutoJSChar strval;
    GjsAutoChar fileval;
    int intval;
    unsigned uintval;
    int64_t int64val;
    double dblval;
    JS::RootedObject objval(cx);
    retval = gjs_parse_call_args(cx, "oneOfEachType", args, "bsFiutfo",
                                 "bool", &boolval,
                                 "str", &strval,
                                 "file", &fileval,
                                 "int", &intval,
                                 "uint", &uintval,
                                 "int64", &int64val,
                                 "dbl", &dblval,
                                 "obj", &objval);
    g_assert_cmpint(boolval, ==, true);
    g_assert_cmpstr(strval, ==, "foo");
    g_assert_cmpstr(fileval, ==, "foo");
    g_assert_cmpint(intval, ==, 1);
    g_assert_cmpint(uintval, ==, 1);
    g_assert_cmpint(int64val, ==, 1);
    g_assert_cmpfloat(dblval, ==, 1.0);
    g_assert_true(objval);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(optional_args_all)
    bool val1, val2, val3;
    retval = gjs_parse_call_args(cx, "optionalArgsAll", args, "b|bb",
                                 "val1", &val1,
                                 "val2", &val2,
                                 "val3", &val3);
    g_assert_cmpint(val1, ==, true);
    g_assert_cmpint(val2, ==, true);
    g_assert_cmpint(val3, ==, true);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(optional_args_only_required)
    bool val1 = false, val2 = false, val3 = false;
    retval = gjs_parse_call_args(cx, "optionalArgsOnlyRequired", args, "b|bb",
                                 "val1", &val1,
                                 "val2", &val2,
                                 "val3", &val3);
    g_assert_cmpint(val1, ==, true);
    g_assert_cmpint(val2, ==, false);
    g_assert_cmpint(val3, ==, false);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(only_optional_args)
    int val1, val2;
    retval = gjs_parse_call_args(cx, "onlyOptionalArgs", args, "|ii",
                                 "val1", &val1,
                                 "val2", &val2);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(unsigned_enum_arg)
    test_enum_t val;
    retval = gjs_parse_call_args(cx, "unsignedEnumArg", args, "i",
                                 "enum_param", &val);
    g_assert_cmpint(val, ==, ONE);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(signed_enum_arg)
    test_signed_enum_t val;
    retval = gjs_parse_call_args(cx, "signedEnumArg", args, "i",
                                 "enum_param", &val);
    g_assert_cmpint(val, ==, MINUS_ONE);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(one_of_each_nullable_type)
    GjsAutoJSChar strval;
    GjsAutoChar fileval;
    JS::RootedObject objval(cx);
    retval = gjs_parse_call_args(cx, "oneOfEachNullableType", args, "?s?F?o",
                                 "strval", &strval,
                                 "fileval", &fileval,
                                 "objval", &objval);
    g_assert_null(strval);
    g_assert_null(fileval);
    g_assert_false(objval);
JSNATIVE_TEST_FUNC_END

JSNATIVE_TEST_FUNC_BEGIN(unwind_free_test)
    int intval;
    unsigned uval;
    JS::RootedObject objval(cx);
    retval = gjs_parse_call_args(cx, "unwindFreeTest", args, "oiu",
                                 "objval", &objval,
                                 "intval", &intval,
                                 "error", &uval);
    g_assert_false(objval);
JSNATIVE_TEST_FUNC_END

#define JSNATIVE_BAD_NULLABLE_TEST_FUNC(type, fmt)                 \
    JSNATIVE_TEST_FUNC_BEGIN(type##_invalid_nullable)              \
        type val;                                                  \
        retval = gjs_parse_call_args(cx, #type "InvalidNullable",  \
                                     args, "?" fmt,                \
                                     "val", &val);                 \
    JSNATIVE_TEST_FUNC_END

JSNATIVE_BAD_NULLABLE_TEST_FUNC(bool, "b");
JSNATIVE_BAD_NULLABLE_TEST_FUNC(int, "i");
JSNATIVE_BAD_NULLABLE_TEST_FUNC(unsigned, "u");
JSNATIVE_BAD_NULLABLE_TEST_FUNC(int64_t, "t");
JSNATIVE_BAD_NULLABLE_TEST_FUNC(double, "f");

#undef JSNATIVE_BAD_NULLABLE_TEST_FUNC

#define JSNATIVE_BAD_TYPE_TEST_FUNC(type, ch)                            \
    JSNATIVE_TEST_FUNC_BEGIN(type##_invalid_type)                        \
        type val;                                                        \
        retval = gjs_parse_call_args(cx, #type "InvalidType", args, ch,  \
                                     "val", &val);                       \
    JSNATIVE_TEST_FUNC_END

JSNATIVE_BAD_TYPE_TEST_FUNC(bool, "i");
JSNATIVE_BAD_TYPE_TEST_FUNC(int, "u");
JSNATIVE_BAD_TYPE_TEST_FUNC(unsigned, "t");
JSNATIVE_BAD_TYPE_TEST_FUNC(int64_t, "f");
JSNATIVE_BAD_TYPE_TEST_FUNC(double, "b");
JSNATIVE_BAD_TYPE_TEST_FUNC(GjsAutoChar, "i");
JSNATIVE_BAD_TYPE_TEST_FUNC(GjsAutoJSChar, "i");

#undef JSNATIVE_BAD_TYPE_TEST_FUNC

JSNATIVE_TEST_FUNC_BEGIN(object_invalid_type)
    JS::RootedObject val(cx);
    retval = gjs_parse_call_args(cx, "objectInvalidType", args, "i",
                                 "val", &val);
JSNATIVE_TEST_FUNC_END

static JSFunctionSpec native_test_funcs[] = {
    JS_FN("noArgs", no_args, 0, 0),
    JS_FN("noArgsIgnoreTrailing", no_args_ignore_trailing, 0, 0),
    JS_FN("boolArgNoAssert", bool_arg_no_assert, 0, 0),
    JS_FN("intArgNoAssert", int_arg_no_assert, 0, 0),
    JS_FN("objectArgNoAssert", object_arg_no_assert, 0, 0),
    JS_FN("optionalIntArgsNoAssert", optional_int_args_no_assert, 0, 0),
    JS_FN("argsIgnoreTrailing", args_ignore_trailing, 0, 0),
    JS_FN("oneOfEachType", one_of_each_type, 0, 0),
    JS_FN("optionalArgsAll", optional_args_all, 0, 0),
    JS_FN("optionalArgsOnlyRequired", optional_args_only_required, 0, 0),
    JS_FN("onlyOptionalArgs", only_optional_args, 0, 0),
    JS_FN("unsignedEnumArg", unsigned_enum_arg, 0, 0),
    JS_FN("signedEnumArg", signed_enum_arg, 0, 0),
    JS_FN("oneOfEachNullableType", one_of_each_nullable_type, 0, 0),
    JS_FN("unwindFreeTest", unwind_free_test, 0, 0),
    JS_FN("boolInvalidNullable", bool_invalid_nullable, 0, 0),
    JS_FN("intInvalidNullable", int_invalid_nullable, 0, 0),
    JS_FN("unsignedInvalidNullable", unsigned_invalid_nullable, 0, 0),
    JS_FN("int64_tInvalidNullable", int64_t_invalid_nullable, 0, 0),
    JS_FN("doubleInvalidNullable", double_invalid_nullable, 0, 0),
    JS_FN("boolInvalidType", bool_invalid_type, 0, 0),
    JS_FN("intInvalidType", int_invalid_type, 0, 0),
    JS_FN("unsignedInvalidType", unsigned_invalid_type, 0, 0),
    JS_FN("int64_tInvalidType", int64_t_invalid_type, 0, 0),
    JS_FN("doubleInvalidType", double_invalid_type, 0, 0),
    JS_FN("GjsAutoCharInvalidType", GjsAutoChar_invalid_type, 0, 0),
    JS_FN("GjsAutoJSCharInvalidType", GjsAutoJSChar_invalid_type, 0, 0),
    JS_FN("objectInvalidType", object_invalid_type, 0, 0),
    JS_FS_END};

static void
setup(GjsUnitTestFixture *fx,
      gconstpointer       unused)
{
    gjs_unit_test_fixture_setup(fx, unused);

    JS::RootedObject global(fx->cx, gjs_get_import_global(fx->cx));
    bool success = JS_DefineFunctions(fx->cx, global, native_test_funcs);
    g_assert_true(success);
}

static void
run_code(GjsUnitTestFixture *fx,
         gconstpointer       code)
{
    const char *script = (const char *) code;

    JS::CompileOptions options(fx->cx, JSVERSION_UNKNOWN);
    options.setFileAndLine("unit test", 1);

    JS::RootedValue ignored(fx->cx);
    bool ok = JS::Evaluate(fx->cx, options, script, strlen(script), &ignored);

    g_assert_null(gjs_unit_test_exception_message(fx));
    g_assert_true(ok);
}

static void
run_code_expect_exception(GjsUnitTestFixture *fx,
                          gconstpointer       code)
{
    const char *script = (const char *) code;

    JS::CompileOptions options(fx->cx, JSVERSION_UNKNOWN);
    options.setFileAndLine("unit test", 1);

    JS::RootedValue ignored(fx->cx);
    bool ok = JS::Evaluate(fx->cx, options, script, strlen(script), &ignored);
    g_assert_false(ok);
    GjsAutoChar message = gjs_unit_test_exception_message(fx);
    g_assert_nonnull(message);

    /* Cheap way to shove an expected exception message into the data argument */
    const char *expected_msg = strstr((const char *) code, "//");
    if (expected_msg != NULL) {
        expected_msg += 2;
        assert_match(message, expected_msg);
    }
}

void
gjs_test_add_tests_for_parse_call_args(void)
{
#define ADD_CALL_ARGS_TEST_BASE(path, code, f)                         \
    g_test_add("/callargs/" path, GjsUnitTestFixture, code, setup, f,  \
               gjs_unit_test_fixture_teardown)
#define ADD_CALL_ARGS_TEST(path, code) \
    ADD_CALL_ARGS_TEST_BASE(path, code, run_code)
#define ADD_CALL_ARGS_TEST_XFAIL(path, code) \
    ADD_CALL_ARGS_TEST_BASE(path, code, run_code_expect_exception)

    ADD_CALL_ARGS_TEST("no-args-works", "noArgs()");
    ADD_CALL_ARGS_TEST_XFAIL("no-args-fails-on-extra-args",
                             "noArgs(1, 2, 3)//*Expected 0 arguments, got 3");
    ADD_CALL_ARGS_TEST("no-args-ignores-trailing",
                       "noArgsIgnoreTrailing(1, 2, 3)");
    ADD_CALL_ARGS_TEST_XFAIL("too-many-args-fails",
                             "intArgNoAssert(1, 2)"
                             "//*Expected 1 arguments, got 2");
    ADD_CALL_ARGS_TEST_XFAIL("too-many-args-fails-when-more-than-optional",
                             "optionalIntArgsNoAssert(1, 2, 3)"
                             "//*Expected minimum 1 arguments (and 1 optional), got 3");
    ADD_CALL_ARGS_TEST_XFAIL("too-few-args-fails",
                             "intArgNoAssert()//*Expected 1 arguments, got 0");
    ADD_CALL_ARGS_TEST_XFAIL("too-few-args-fails-with-optional",
                             "optionalIntArgsNoAssert()"
                             "//*Expected minimum 1 arguments (and 1 optional), got 0");
    ADD_CALL_ARGS_TEST("args-ignores-trailing", "argsIgnoreTrailing(1, 2, 3)");
    ADD_CALL_ARGS_TEST("one-of-each-type-works",
                       "oneOfEachType(true, 'foo', 'foo', 1, 1, 1, 1, {})");
    ADD_CALL_ARGS_TEST("optional-args-work-when-passing-all-args",
                       "optionalArgsAll(true, true, true)");
    ADD_CALL_ARGS_TEST("optional-args-work-when-passing-only-required-args",
                       "optionalArgsOnlyRequired(true)");
    ADD_CALL_ARGS_TEST("enum-types-work", "unsignedEnumArg(1)");
    ADD_CALL_ARGS_TEST("signed-enum-types-work", "signedEnumArg(-1)");
    ADD_CALL_ARGS_TEST("one-of-each-nullable-type-works",
                       "oneOfEachNullableType(null, null, null)");
    ADD_CALL_ARGS_TEST("passing-no-arguments-when-all-optional",
                       "onlyOptionalArgs()");
    ADD_CALL_ARGS_TEST("passing-some-arguments-when-all-optional",
                       "onlyOptionalArgs(1)");
    ADD_CALL_ARGS_TEST("passing-all-arguments-when-all-optional",
                       "onlyOptionalArgs(1, 1)");
    ADD_CALL_ARGS_TEST_XFAIL("allocated-args-are-freed-on-error",
                             "unwindFreeTest({}, 1, -1)"
                             "//*Value * is out of range");
    ADD_CALL_ARGS_TEST_XFAIL("nullable-bool-is-invalid",
                             "boolInvalidNullable(true)"
                             "//*Invalid format string combination ?b");
    ADD_CALL_ARGS_TEST_XFAIL("nullable-int-is-invalid",
                             "intInvalidNullable(1)"
                             "//*Invalid format string combination ?i");
    ADD_CALL_ARGS_TEST_XFAIL("nullable-unsigned-is-invalid",
                             "unsignedInvalidNullable(1)"
                             "//*Invalid format string combination ?u");
    ADD_CALL_ARGS_TEST_XFAIL("nullable-int64-is-invalid",
                             "int64_tInvalidNullable(1)"
                             "//*Invalid format string combination ?t");
    ADD_CALL_ARGS_TEST_XFAIL("nullable-double-is-invalid",
                             "doubleInvalidNullable(1)"
                             "//*Invalid format string combination ?f");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-bool-type",
                             "boolInvalidType(1)"
                             "//*Wrong type for i, got bool?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-int-type",
                             "intInvalidType(1)"
                             "//*Wrong type for u, got int32_t?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-unsigned-type",
                             "unsignedInvalidType(1)"
                             "//*Wrong type for t, got uint32_t?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-int64-type",
                             "int64_tInvalidType(1)"
                             "//*Wrong type for f, got int64_t?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-double-type",
                             "doubleInvalidType(false)"
                             "//*Wrong type for b, got double?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-autochar-type",
                             "GjsAutoCharInvalidType(1)"
                             "//*Wrong type for i, got GjsAutoChar?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-autojschar-type",
                             "GjsAutoJSCharInvalidType(1)"
                             "//*Wrong type for i, got GjsAutoJSChar?");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-object-type",
                             "objectInvalidType(1)"
                             "//*Wrong type for i, got JS::MutableHandleObject");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-boolean",
                             "boolArgNoAssert({})//*Not a boolean");
    ADD_CALL_ARGS_TEST_XFAIL("invalid-object",
                             "objectArgNoAssert(3)//*Not an object");

#undef ADD_CALL_ARGS_TEST_XFAIL
#undef ADD_CALL_ARGS_TEST
#undef ADD_CALL_ARGS_TEST_BASE
}
