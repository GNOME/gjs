/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#include <config.h>

#include <stddef.h>  // for NULL

#include <utility>   // for move, swap

#include <glib-object.h>
#include <glib.h>

#include "gjs/auto.h"
#include "gjs/gerror-result.h"
#include "test/gjs-test-utils.h"

struct _GjsTestObject {
    GObject parent_instance;

    int stuff;
};

G_DECLARE_FINAL_TYPE(GjsTestObject, gjs_test_object, GJS_TEST, OBJECT, GObject)
G_DEFINE_TYPE(GjsTestObject, gjs_test_object, G_TYPE_OBJECT)

struct Fixture {
    GjsTestObject* ptr;
};

static void gjs_test_object_init(GjsTestObject*) {}
void gjs_test_object_class_init(GjsTestObjectClass*) {}
static GjsTestObject* gjs_test_object_new() {
    return GJS_TEST_OBJECT(g_object_new(gjs_test_object_get_type(), NULL));
}

static void setup(Fixture* fx, const void*) {
    fx->ptr = gjs_test_object_new();
    g_assert_nonnull(fx->ptr);
    g_object_add_weak_pointer(G_OBJECT(fx->ptr),
                              reinterpret_cast<void**>(&fx->ptr));
}

static void teardown(Fixture* fx, const void*) {
    // Weak pointer will have reset the pointer to null if the last reference
    // was dropped
    g_assert_null(fx->ptr);
}

using GjsAutoTestObject =
    Gjs::AutoPointer<GjsTestObject, void, g_object_unref, g_object_ref>;

static void test_gjs_autopointer_size() {
    g_assert_cmpuint(sizeof(GjsAutoTestObject), ==, sizeof(GjsTestObject*));
}

static void test_gjs_autopointer_ctor_empty() {
    GjsAutoTestObject autoptr;
    g_assert_null(autoptr.get());
    g_assert_null(autoptr);
}

static void test_gjs_autopointer_ctor_basic(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);
    g_assert_true(autoptr == fx->ptr);
    g_assert_true(autoptr.get() == fx->ptr);
}

static void test_gjs_autopointer_ctor_take_ownership(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr{fx->ptr, Gjs::TakeOwnership{}};
    g_assert_true(autoptr == fx->ptr);
    g_assert_true(autoptr.get() == fx->ptr);
    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_ctor_assign(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr = fx->ptr;
    g_assert_true(autoptr == fx->ptr);
    g_assert_true(autoptr.get() == fx->ptr);
}

static void test_gjs_autopointer_ctor_assign_other(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr1 = fx->ptr;
    GjsAutoTestObject autoptr2 = autoptr1;

    g_assert_true(autoptr1 == fx->ptr);
    g_assert_true(autoptr1.get() == fx->ptr);
    g_assert_true(autoptr2 == fx->ptr);
    g_assert_true(autoptr2.get() == fx->ptr);
}

static void test_gjs_autopointer_dtor(Fixture* fx, const void*) {
    g_object_ref(fx->ptr);

    {
        GjsAutoTestObject autoptr(fx->ptr);
        g_assert_true(autoptr == fx->ptr);
        g_assert_true(autoptr.get() == fx->ptr);
    }

    g_assert_nonnull(fx->ptr);
    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_dtor_cpp() {
    bool deleted = false;
    auto dtor_callback = [&deleted] { deleted = true; };

    struct TestStruct {
        explicit TestStruct(decltype(dtor_callback) cb) : _delete_cb(cb) {}
        ~TestStruct() { _delete_cb(); }

        decltype(dtor_callback) _delete_cb;
    };

    g_assert_false(deleted);

    {
        auto* ptr = new TestStruct(dtor_callback);
        Gjs::AutoCppPointer<TestStruct> autoptr{ptr};
        g_assert_true(ptr == autoptr);
    }

    g_assert_true(deleted);
}

static void test_gjs_autopointer_dtor_cpp_array() {
    unsigned deleted = 0;
    auto dtor_callback = [&deleted] { deleted++; };

    struct TestStruct {
        TestStruct(decltype(dtor_callback) cb)  // NOLINT(runtime/explicit)
            : _delete_cb(cb) {}
        ~TestStruct() { _delete_cb(); }

        int val = 5;
        decltype(dtor_callback) _delete_cb;
    };

    g_assert_cmpint(deleted, ==, 0);

    {
        // using GjsAutoCppPointer1 = Gjs::AutoPointer<TestStruct[],
        // TestStruct[], Gjs::AutoPointerDeleter<TestStruct[]>>;

        TestStruct* ptrs =
            new TestStruct[3]{dtor_callback, dtor_callback, dtor_callback};
        Gjs::AutoCppPointer<TestStruct[]> autoptr{ptrs};
        g_assert_cmpint(autoptr[0].val, ==, 5);
        g_assert_cmpint(autoptr[1].val, ==, 5);
        g_assert_cmpint(autoptr[2].val, ==, 5);

        autoptr[1].val = 4;

        TestStruct const& const_struct_const_1 = autoptr[1];
        g_assert_cmpint(const_struct_const_1.val, ==, 4);
        // const_struct_const_1.val = 3;  // This will would not compile

        TestStruct& test_struct_1 = autoptr[1];
        test_struct_1.val = 3;
        g_assert_cmpint(test_struct_1.val, ==, 3);

        int* int_ptrs = new int[3]{5, 6, 7};
        Gjs::AutoCppPointer<int[]> int_autoptr{int_ptrs};
        g_assert_cmpint(int_autoptr[0], ==, 5);
        g_assert_cmpint(int_autoptr[1], ==, 6);
        g_assert_cmpint(int_autoptr[2], ==, 7);
    }

    g_assert_cmpuint(deleted, ==, 3);
}

static void test_gjs_autopointer_dtor_take_ownership(Fixture* fx, const void*) {
    {
        GjsAutoTestObject autoptr{fx->ptr, Gjs::TakeOwnership{}};
        g_assert_true(autoptr == fx->ptr);
        g_assert_true(autoptr.get() == fx->ptr);
    }

    g_assert_nonnull(fx->ptr);
    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_dtor_default_free() {
    Gjs::AutoPointer<char, void> autoptr{g_strdup("Please, FREE ME!")};
    g_assert_cmpstr(autoptr, ==, "Please, FREE ME!");
}

static void test_gjs_autopointer_dtor_no_free_pointer() {
    const char* str = "DO NOT FREE ME";
    Gjs::AutoPointer<char, void, nullptr> autoptr{const_cast<char*>(str)};
    g_assert_cmpstr(autoptr, ==, "DO NOT FREE ME");
}

static void gobject_free(GObject* p) { g_object_unref(p); }
static GObject* gobject_copy(GObject* p) {
    return static_cast<GObject*>(g_object_ref(p));
}

static void test_gjs_autopointer_cast_free_func_type() {
    // No assertions; this test fails to compile if the casts are wrong
    using TypedAutoPointer =
        Gjs::AutoPointer<GjsTestObject, GObject, gobject_free, gobject_copy>;
    TypedAutoPointer autoptr{gjs_test_object_new()};
    TypedAutoPointer copy{autoptr.copy()};
}

static void test_gjs_autopointer_assign_operator() {
    GjsAutoTestObject autoptr;
    auto* ptr = gjs_test_object_new();

    autoptr = ptr;

    g_assert_true(autoptr == ptr);
    g_assert_true(autoptr.get() == ptr);
}

static void test_gjs_autopointer_assign_operator_other_ptr() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();
    g_object_add_weak_pointer(G_OBJECT(ptr1), reinterpret_cast<void**>(&ptr1));

    GjsAutoTestObject autoptr(ptr1);

    g_object_ref(ptr1);

    autoptr = ptr2;

    g_assert_true(autoptr == ptr2);
    g_assert_nonnull(ptr1);
    g_object_unref(ptr1);
    g_assert_null(ptr1);
}

static void test_gjs_autopointer_assign_operator_self_ptr(Fixture* fx,
                                                          const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_object_ref(fx->ptr);

    autoptr = fx->ptr;

    g_assert_true(autoptr == fx->ptr);
}

static void test_gjs_autopointer_assign_operator_object(Fixture* fx,
                                                        const void*) {
    GjsAutoTestObject autoptr1;
    GjsAutoTestObject autoptr2;

    autoptr1 = fx->ptr;
    autoptr2 = autoptr1;

    g_assert_true(autoptr1 == autoptr2);
    g_assert_true(autoptr2.get() == fx->ptr);
}

static void test_gjs_autopointer_assign_operator_other_object() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();
    g_object_add_weak_pointer(G_OBJECT(ptr1), reinterpret_cast<void**>(&ptr1));
    g_object_add_weak_pointer(G_OBJECT(ptr2), reinterpret_cast<void**>(&ptr2));

    {
        GjsAutoTestObject autoptr1(ptr1);
        GjsAutoTestObject autoptr2(ptr2);

        g_object_ref(ptr1);

        autoptr1 = autoptr2;

        g_assert_true(autoptr1 == ptr2);
        g_assert_true(autoptr2 == ptr2);
        g_assert_nonnull(ptr1);
        g_object_unref(ptr1);
    }

    g_assert_null(ptr1);
    g_assert_null(ptr2);
}

static void test_gjs_autopointer_assign_operator_self_object(Fixture* fx,
                                                             const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    autoptr = *&autoptr;

    g_assert_true(autoptr == fx->ptr);
}

static void test_gjs_autopointer_assign_operator_copy_and_swap(Fixture* fx,
                                                               const void*) {
    GjsAutoTestObject autoptr(fx->ptr);
    auto* ptr = fx->ptr;

    auto test_copy_fun = [ptr](GjsAutoTestObject data) {
        g_assert_true(data == ptr);
    };

    test_copy_fun(autoptr);
    g_assert_true(autoptr == fx->ptr);
}

static void test_gjs_autopointer_operator_move(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);
    void* ptr = fx->ptr;

    auto test_move_fun = [ptr](GjsAutoTestObject&& data) {
        g_assert_true(ptr == data);
    };

    // Accessing a value after moving out of it is bad in general, but here it
    // is done on purpose, to test that the autoptr's move constructor empties
    // the old autoptr.

    test_move_fun(std::move(autoptr));
    g_assert_nonnull(autoptr);  // cppcheck-suppress accessMoved

    // cppcheck-suppress accessMoved
    GjsAutoTestObject autoptr2 = std::move(autoptr);
    g_assert_true(autoptr2 == fx->ptr);
    g_assert_null(autoptr);  // cppcheck-suppress accessMoved
}

static void test_gjs_autopointer_operator_swap(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr1(fx->ptr);
    GjsAutoTestObject autoptr2;

    std::swap(autoptr1, autoptr2);
    g_assert_null(autoptr1);
    g_assert_true(autoptr2 == fx->ptr);
}

static void test_gjs_autopointer_assign_operator_arrow(Fixture* fx,
                                                       const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    int value = g_random_int();
    autoptr->stuff = value;
    g_assert_cmpint(autoptr->stuff, ==, value);
}

static void test_gjs_autopointer_assign_operator_deference(Fixture* fx,
                                                           const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    fx->ptr->stuff = g_random_int();

    GjsTestObject tobj = *autoptr;
    g_assert_cmpint(fx->ptr->stuff, ==, tobj.stuff);
}

static void test_gjs_autopointer_assign_operator_bool(Fixture* fx,
                                                      const void*) {
    auto bool_to_gboolean = [](bool v) -> gboolean { return !!v; };

    g_assert_false(bool_to_gboolean(GjsAutoTestObject()));
    g_assert_true(bool_to_gboolean(GjsAutoTestObject(gjs_test_object_new())));

    GjsAutoTestObject autoptr(fx->ptr);
    autoptr.reset();
    g_assert_false(bool_to_gboolean(autoptr));
}

static void test_gjs_autopointer_assign_operator_array() {
    auto* ptrs = g_new0(GjsTestObject, 5);
    Gjs::AutoPointer<GjsTestObject> autopointers{ptrs};

    for (int i = 0; i < 5; i++) {
        autopointers[i].stuff = i;
        g_assert_cmpint(ptrs[i].stuff, ==, i);
        g_assert_cmpint(autopointers[i].stuff, ==, i);
    }
}

static void test_gjs_autopointer_get(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_true(fx->ptr == autoptr.get());
}

static void test_gjs_autopointer_out(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_true(fx->ptr == *(autoptr.out()));
}

static void test_gjs_autopointer_release(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_nonnull(autoptr);

    auto* released = autoptr.release();
    g_assert_true(released == fx->ptr);
    g_assert_null(autoptr);

    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_reset_nullptr(Fixture* fx, const void*) {
    GjsAutoTestObject empty;
    empty.reset();
    g_assert_null(empty);

    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_nonnull(autoptr);

    g_object_ref(fx->ptr);

    autoptr.reset();
    g_assert_null(autoptr);

    g_assert_nonnull(fx->ptr);
    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_reset_self_ptr(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_true(autoptr == fx->ptr);

    g_object_ref(fx->ptr);

    autoptr.reset(fx->ptr);
    g_assert_true(autoptr == fx->ptr);

    g_assert_nonnull(fx->ptr);
}

static void test_gjs_autopointer_reset_other_ptr() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();
    g_object_add_weak_pointer(G_OBJECT(ptr1), reinterpret_cast<void**>(&ptr1));
    g_object_add_weak_pointer(G_OBJECT(ptr2), reinterpret_cast<void**>(&ptr2));

    {
        GjsAutoTestObject autoptr(ptr1);

        g_assert_true(autoptr == ptr1);

        g_object_ref(ptr1);

        autoptr.reset(ptr2);
        g_assert_true(autoptr == ptr2);

        g_assert_nonnull(ptr1);
        g_assert_nonnull(ptr2);

        g_object_unref(ptr1);
    }

    g_assert_null(ptr1);
    g_assert_null(ptr2);
}

static void test_gjs_autopointer_swap_other_ptr(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr1(fx->ptr);
    GjsAutoTestObject autoptr2;

    autoptr1.swap(autoptr2);
    g_assert_null(autoptr1);
    g_assert_true(autoptr2 == fx->ptr);

    g_assert_nonnull(fx->ptr);
}

static void test_gjs_autopointer_swap_self_ptr(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    autoptr.swap(autoptr);
    g_assert_true(autoptr == fx->ptr);

    g_assert_nonnull(fx->ptr);
}

static void test_gjs_autopointer_swap_empty(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr1(fx->ptr);
    GjsAutoTestObject autoptr2;

    autoptr1.swap(autoptr2);
    g_assert_null(autoptr1);

    g_assert_true(autoptr2 == fx->ptr);
    g_assert_nonnull(fx->ptr);
}

static void test_gjs_autopointer_copy(Fixture* fx, const void*) {
    GjsAutoTestObject autoptr(fx->ptr);

    g_assert_true(fx->ptr == autoptr.copy());

    g_object_unref(fx->ptr);
}

static void test_gjs_autopointer_as() {
    GjsAutoTestObject autoptr(gjs_test_object_new());

    g_assert_cmpuint(autoptr.as<GObject>()->ref_count, ==, 1);
}

static void test_gjs_autochar_init() {
    char* str = g_strdup("FoooBar");
    Gjs::AutoChar autoptr = str;

    g_assert_cmpstr(autoptr, ==, "FoooBar");
    g_assert_cmpuint(autoptr[4], ==, 'B');
    g_assert_true(autoptr == str);
}

static void test_gjs_autochar_init_take_ownership() {
    const char* str = "FoooBarConst";
    Gjs::AutoChar autoptr{str, Gjs::TakeOwnership{}};

    g_assert_cmpstr(autoptr, ==, str);
    g_assert_cmpuint(autoptr[4], ==, 'B');
    g_assert_true(autoptr != str);
}

static void test_gjs_autochar_copy() {
    Gjs::AutoChar autoptr{g_strdup("FoooBar")};

    char* copy = autoptr.copy();
    g_assert_cmpstr(autoptr, ==, copy);
    g_assert_true(autoptr != copy);

    g_free(copy);
}

static void test_gjs_autostrv_init() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    Gjs::AutoStrv autoptr{g_strdupv(const_cast<char**>(strv))};

    g_assert_true(g_strv_equal(strv, autoptr));

    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(autoptr[i], ==, strv[i]);
}

static void test_gjs_autostrv_init_take_ownership() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    Gjs::AutoStrv autoptr{const_cast<char* const*>(strv), Gjs::TakeOwnership{}};

    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(autoptr[i], ==, strv[i]);
    g_assert_false(autoptr == strv);
}

static void test_gjs_autostrv_copy() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    Gjs::AutoStrv autoptr{g_strdupv(const_cast<char**>(strv))};

    char** copy = autoptr.copy();
    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(copy[i], ==, strv[i]);
    g_assert_false(autoptr == copy);

    g_strfreev(copy);
}

static void test_gjs_autotypeclass_init() {
    Gjs::AutoTypeClass<GObjectClass> autoclass{gjs_test_object_get_type()};

    g_assert_nonnull(autoclass);
    g_assert_cmpint(autoclass->g_type_class.g_type, ==,
        gjs_test_object_get_type());
}

static void test_gjs_error_init() {
    Gjs::AutoError error{
        g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_EXIST, "Message")};

    g_assert_nonnull(error);
    g_assert_cmpint(error->domain, ==, G_FILE_ERROR);
    g_assert_cmpint(error->code, ==, G_FILE_ERROR_EXIST);
    g_assert_cmpstr(error->message, ==, "Message");

    error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_FAILED, "Other");
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED);
    g_assert_cmpstr(error->message, ==, "Other");
}

static void test_gjs_error_out() {
    Gjs::AutoError error{
        g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_EXIST, "Message")};
    g_clear_error(&error);
    g_assert_null(error);
}

#define ADD_AUTOPTRTEST(path, func) \
    g_test_add(path, Fixture, nullptr, setup, func, teardown);

void gjs_test_add_tests_for_jsapi_utils(void) {
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/size",
                    test_gjs_autopointer_size);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/constructor/empty",
                    test_gjs_autopointer_ctor_empty);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/constructor/basic",
                    test_gjs_autopointer_ctor_basic);
    ADD_AUTOPTRTEST(
        "/gjs/jsapi-utils/gjs-autopointer/constructor/take_ownership",
        test_gjs_autopointer_ctor_take_ownership);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/constructor/assignment",
                    test_gjs_autopointer_ctor_assign);
    ADD_AUTOPTRTEST(
        "/gjs/jsapi-utils/gjs-autopointer/constructor/assignment/other",
        test_gjs_autopointer_ctor_assign_other);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/destructor",
                    test_gjs_autopointer_dtor);
    ADD_AUTOPTRTEST(
        "/gjs/jsapi-utils/gjs-autopointer/destructor/take_ownership",
        test_gjs_autopointer_dtor_take_ownership);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/default_free",
                    test_gjs_autopointer_dtor_default_free);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/destructor/no_free_pointer",
        test_gjs_autopointer_dtor_no_free_pointer);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/free_and_ref_funcs",
                    test_gjs_autopointer_cast_free_func_type);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/c++",
                    test_gjs_autopointer_dtor_cpp);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/c++-array",
                    test_gjs_autopointer_dtor_cpp_array);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/assign",
                    test_gjs_autopointer_assign_operator);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/other_ptr",
        test_gjs_autopointer_assign_operator_other_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/assign/self_ptr",
                    test_gjs_autopointer_assign_operator_self_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/assign/object",
                    test_gjs_autopointer_assign_operator_object);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/other_object",
        test_gjs_autopointer_assign_operator_other_object);
    ADD_AUTOPTRTEST(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/self_object",
        test_gjs_autopointer_assign_operator_self_object);
    ADD_AUTOPTRTEST(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/copy_and_swap",
        test_gjs_autopointer_assign_operator_copy_and_swap);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/move",
                    test_gjs_autopointer_operator_move);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/swap",
                    test_gjs_autopointer_operator_swap);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/arrow",
                    test_gjs_autopointer_assign_operator_arrow);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/deference",
                    test_gjs_autopointer_assign_operator_deference);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/operator/bool",
                    test_gjs_autopointer_assign_operator_bool);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/array",
                    test_gjs_autopointer_assign_operator_array);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/get",
                    test_gjs_autopointer_get);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/out",
                    test_gjs_autopointer_out);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/release",
                    test_gjs_autopointer_release);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/reset/nullptr",
                    test_gjs_autopointer_reset_nullptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/reset/other_ptr",
                    test_gjs_autopointer_reset_other_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/reset/self_ptr",
                    test_gjs_autopointer_reset_self_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/swap/other_ptr",
                    test_gjs_autopointer_swap_other_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/swap/self_ptr",
                    test_gjs_autopointer_swap_self_ptr);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/swap/empty",
                    test_gjs_autopointer_swap_empty);
    ADD_AUTOPTRTEST("/gjs/jsapi-utils/gjs-autopointer/method/copy",
                    test_gjs_autopointer_copy);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/as",
                    test_gjs_autopointer_as);

    //  Other implementations
    g_test_add_func("/gjs/jsapi-utils/gjs-autochar/init",
                    test_gjs_autochar_init);
    g_test_add_func("/gjs/jsapi-utils/gjs-autochar/init/take_ownership",
                    test_gjs_autochar_init_take_ownership);
    g_test_add_func("/gjs/jsapi-utils/gjs-autochar/copy",
                    test_gjs_autochar_copy);

    g_test_add_func("/gjs/jsapi-utils/gjs-autostrv/init",
                    test_gjs_autostrv_init);
    g_test_add_func("/gjs/jsapi-utils/gjs-autostrv/init/take_ownership",
                    test_gjs_autostrv_init_take_ownership);
    g_test_add_func("/gjs/jsapi-utils/gjs-autostrv/copy",
                    test_gjs_autostrv_copy);

    g_test_add_func("/gjs/jsapi-utils/gjs-autotypeclass/init",
                    test_gjs_autotypeclass_init);

    g_test_add_func("/gjs/jsapi-utils/gjs-autoerror/init", test_gjs_error_init);
    g_test_add_func("/gjs/jsapi-utils/gjs-autoerror/as-out-value",
                    test_gjs_error_out);
}
