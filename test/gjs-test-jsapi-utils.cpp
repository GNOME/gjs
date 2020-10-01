/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 *
 * Copyright (c) 2020 Marco Trevisan <marco.trevisan@canonical.com>
 */

#include <glib-object.h>
#include <glib.h>
#include <stddef.h>  // for NULL
#include <type_traits>  // for remove_reference<>::type
#include <utility>   // for move, swap

#include "gjs/jsapi-util.h"

struct _GjsTestObject {
    GObject parent_instance;

    int stuff;
};

G_DECLARE_FINAL_TYPE(GjsTestObject, gjs_test_object, GJS_TEST, OBJECT, GObject)
G_DEFINE_TYPE(GjsTestObject, gjs_test_object, G_TYPE_OBJECT)

static void gjs_test_object_init(GjsTestObject*) {}
void gjs_test_object_class_init(GjsTestObjectClass*) {}
static GjsTestObject* gjs_test_object_new() {
    return GJS_TEST_OBJECT(g_object_new(gjs_test_object_get_type(), NULL));
}

static unsigned test_gjs_autopointer_refcount(GjsTestObject* test_object) {
    return G_OBJECT(test_object)->ref_count;
}

using GjsAutoTestObject =
    GjsAutoPointer<GjsTestObject, void, g_object_unref, g_object_ref>;

static void test_gjs_autopointer_size() {
    g_assert_cmpuint(sizeof(GjsAutoTestObject), ==, sizeof(GjsTestObject*));
}

static void test_gjs_autopointer_ctor_empty() {
    GjsAutoTestObject autoptr;
    g_assert_null(autoptr.get());
    g_assert_null(autoptr);
}

static void test_gjs_autopointer_ctor_basic() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    GjsAutoTestObject autoptr(ptr);
    g_assert(autoptr == ptr);
    g_assert(autoptr.get() == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_ctor_take_ownership() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    GjsAutoTestObject autoptr(ptr, GjsAutoTakeOwnership());
    g_assert(autoptr == ptr);
    g_assert(autoptr.get() == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);
    g_object_unref(ptr);
}

static void test_gjs_autopointer_ctor_assign() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    GjsAutoTestObject autoptr = ptr;
    g_assert(autoptr == ptr);
    g_assert(autoptr.get() == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_ctor_assign_other() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    GjsAutoTestObject autoptr1 = ptr;
    GjsAutoTestObject autoptr2 = autoptr1;

    g_assert(autoptr1 == ptr);
    g_assert(autoptr1.get() == ptr);
    g_assert(autoptr2 == ptr);
    g_assert(autoptr2.get() == ptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);
}

static void test_gjs_autopointer_dtor() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    {
        g_object_ref(ptr);
        g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);

        GjsAutoTestObject autoptr(ptr);
        g_assert(autoptr == ptr);
        g_assert(autoptr.get() == ptr);
    }

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
    g_object_unref(ptr);
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
        GjsAutoCppPointer<TestStruct> autoptr(ptr);
        g_assert(ptr == autoptr);
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
        // using GjsAutoCppPointer1 = GjsAutoPointer<TestStruct[], TestStruct[],
        // GjsAutoPointerDeleter<TestStruct[]>>;

        TestStruct* ptrs =
            new TestStruct[3]{dtor_callback, dtor_callback, dtor_callback};
        GjsAutoCppPointer<TestStruct[]> autoptr(ptrs);
        g_assert_cmpint(autoptr[0].val, ==, 5);
        g_assert_cmpint(autoptr[1].val, ==, 5);
        g_assert_cmpint(autoptr[2].val, ==, 5);
    }

    g_assert_cmpuint(deleted, ==, 3);
}

static void test_gjs_autopointer_dtor_take_ownership() {
    auto* ptr = gjs_test_object_new();
    g_assert_nonnull(ptr);

    {
        GjsAutoTestObject autoptr(ptr, GjsAutoTakeOwnership());
        g_assert(autoptr == ptr);
        g_assert(autoptr.get() == ptr);
        g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);
    }

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
    g_object_unref(ptr);
}

static void test_gjs_autopointer_dtor_default_free() {
    GjsAutoPointer<char, void> autoptr(g_strdup("Please, FREE ME!"));
    g_assert_cmpstr(autoptr, ==, "Please, FREE ME!");
}

static void test_gjs_autopointer_dtor_no_free_pointer() {
    const char* str = "DO NOT FREE ME";
    GjsAutoPointer<char, void, nullptr> autoptr(const_cast<char*>(str));
    g_assert_cmpstr(autoptr, ==, "DO NOT FREE ME");
}

static void test_gjs_autopointer_assign_operator() {
    GjsAutoTestObject autoptr;
    auto* ptr = gjs_test_object_new();

    autoptr = ptr;

    g_assert(autoptr == ptr);
    g_assert(autoptr.get() == ptr);
}

static void test_gjs_autopointer_assign_operator_other_ptr() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();

    GjsAutoTestObject autoptr(ptr1);

    g_object_ref(ptr1);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 2);

    autoptr = ptr2;

    g_assert(autoptr == ptr2);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 1);
    g_object_unref(ptr1);
}

static void test_gjs_autopointer_assign_operator_self_ptr() {
    auto* ptr = gjs_test_object_new();

    GjsAutoTestObject autoptr(ptr);

    g_object_ref(ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);

    autoptr = ptr;

    g_assert(autoptr == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_assign_operator_object() {
    GjsAutoTestObject autoptr1;
    GjsAutoTestObject autoptr2;
    auto* ptr = gjs_test_object_new();

    autoptr1 = ptr;
    autoptr2 = autoptr1;

    g_assert(autoptr1 == autoptr2);
    g_assert(autoptr2.get() == ptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);
}

static void test_gjs_autopointer_assign_operator_other_object() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();

    GjsAutoTestObject autoptr1(ptr1);
    GjsAutoTestObject autoptr2(ptr2);

    g_object_ref(ptr1);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 2);

    autoptr1 = autoptr2;

    g_assert(autoptr1 == ptr2);
    g_assert(autoptr2 == ptr2);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 1);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr2), ==, 2);
    g_object_unref(ptr1);
}

static void test_gjs_autopointer_assign_operator_self_object() {
    auto* ptr = gjs_test_object_new();

    GjsAutoTestObject autoptr(ptr);

    autoptr = autoptr;

    g_assert(autoptr == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_assign_operator_copy_and_swap() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    auto test_copy_fun = [ptr](GjsAutoTestObject data) {
        g_assert(data == ptr);
        g_assert_cmpuint(test_gjs_autopointer_refcount(data), ==, 2);
    };

    test_copy_fun(autoptr);
    g_assert(autoptr == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_operator_move() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    auto test_move_fun = [ptr](GjsAutoTestObject&& data) {
        g_assert(ptr == data);
        g_assert_cmpuint(test_gjs_autopointer_refcount(data.get()), ==, 1);
    };

    test_move_fun(std::move(autoptr));
    g_assert_nonnull(autoptr);

    GjsAutoTestObject autoptr2 = std::move(autoptr);
    g_assert(autoptr2 == ptr);
    g_assert_null(autoptr);
}

static void test_gjs_autopointer_operator_swap() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr1(ptr);
    GjsAutoTestObject autoptr2;

    std::swap(autoptr1, autoptr2);
    g_assert_null(autoptr1);
    g_assert(autoptr2 == ptr);
}

static void test_gjs_autopointer_assign_operator_arrow() {
    GjsAutoTestObject autoptr(gjs_test_object_new());

    int value = g_random_int();
    autoptr->stuff = value;
    g_assert_cmpint(autoptr->stuff, ==, value);
}

static void test_gjs_autopointer_assign_operator_deference() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    ptr->stuff = g_random_int();

    GjsTestObject tobj = *autoptr;
    g_assert_cmpint(ptr->stuff, ==, tobj.stuff);
    g_assert_cmpuint(ptr->parent_instance.ref_count, ==,
                     tobj.parent_instance.ref_count);
    g_assert_cmpuint(ptr->parent_instance.g_type_instance.g_class->g_type, ==,
                     tobj.parent_instance.g_type_instance.g_class->g_type);
}

static void test_gjs_autopointer_assign_operator_bool() {
    auto bool_to_gboolean = [](bool v) -> gboolean { return !!v; };

    g_assert_false(bool_to_gboolean(GjsAutoTestObject()));
    g_assert_true(bool_to_gboolean(GjsAutoTestObject(gjs_test_object_new())));

    GjsAutoTestObject autoptr(gjs_test_object_new());
    autoptr.reset();
    g_assert_false(bool_to_gboolean(autoptr));
}

static void test_gjs_autopointer_assign_operator_array() {
    auto* ptrs = g_new0(GjsTestObject, 5);
    GjsAutoPointer<GjsTestObject> autopointers(ptrs);

    for (int i = 0; i < 5; i++) {
        autopointers[i].stuff = i;
        g_assert_cmpint(ptrs[i].stuff, ==, i);
        g_assert_cmpint(autopointers[i].stuff, ==, i);
    }
}

static void test_gjs_autopointer_get() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    g_assert(ptr == autoptr.get());
}

static void test_gjs_autopointer_release() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    g_assert_nonnull(autoptr);

    auto* released = autoptr.release();
    g_assert(released == ptr);
    g_assert_null(autoptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
    g_object_unref(ptr);
}

static void test_gjs_autopointer_reset_nullptr() {
    GjsAutoTestObject empty;
    empty.reset();
    g_assert_null(empty);

    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    g_assert_nonnull(autoptr);

    g_object_ref(ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);

    autoptr.reset();
    g_assert_null(autoptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
    g_object_unref(ptr);
}

static void test_gjs_autopointer_reset_self_ptr() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    g_assert(autoptr == ptr);

    g_object_ref(ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);

    autoptr.reset(ptr);
    g_assert(autoptr == ptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_reset_other_ptr() {
    auto* ptr1 = gjs_test_object_new();
    auto* ptr2 = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr1);

    g_assert(autoptr == ptr1);

    g_object_ref(ptr1);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 2);

    autoptr.reset(ptr2);
    g_assert(autoptr == ptr2);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr1), ==, 1);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr2), ==, 1);

    g_object_unref(ptr1);
}

static void test_gjs_autopointer_swap_other_ptr() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr1(ptr);
    GjsAutoTestObject autoptr2;

    autoptr1.swap(autoptr2);
    g_assert_null(autoptr1);
    g_assert(autoptr2 == ptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_swap_self_ptr() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    autoptr.swap(autoptr);
    g_assert(autoptr == ptr);

    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_swap_empty() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr1(ptr);
    GjsAutoTestObject autoptr2;

    autoptr1.swap(autoptr2);
    g_assert_null(autoptr1);

    g_assert(autoptr2 == ptr);
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 1);
}

static void test_gjs_autopointer_copy() {
    auto* ptr = gjs_test_object_new();
    GjsAutoTestObject autoptr(ptr);

    g_assert(ptr == autoptr.copy());
    g_assert_cmpuint(test_gjs_autopointer_refcount(ptr), ==, 2);

    g_object_unref(ptr);
}

static void test_gjs_autopointer_as() {
    GjsAutoTestObject autoptr(gjs_test_object_new());

    g_assert_cmpuint(autoptr.as<GObject>()->ref_count, ==, 1);
}

static void test_gjs_autochar_init() {
    char* str = g_strdup("FoooBar");
    GjsAutoChar autoptr = str;

    g_assert_cmpstr(autoptr, ==, "FoooBar");
    g_assert_cmpuint(autoptr[4], ==, 'B');
    g_assert(autoptr == str);
}

static void test_gjs_autochar_init_take_ownership() {
    const char* str = "FoooBarConst";
    GjsAutoChar autoptr(str, GjsAutoTakeOwnership());

    g_assert_cmpstr(autoptr, ==, str);
    g_assert_cmpuint(autoptr[4], ==, 'B');
    g_assert(autoptr != str);
}

static void test_gjs_autochar_copy() {
    GjsAutoChar autoptr = g_strdup("FoooBar");

    char* copy = autoptr.copy();
    g_assert_cmpstr(autoptr, ==, copy);
    g_assert(autoptr != copy);

    g_free(copy);
}

static void test_gjs_autostrv_init() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    GjsAutoStrv autoptr = g_strdupv(const_cast<char**>(strv));

    g_assert_true(g_strv_equal(strv, autoptr));

    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(autoptr[i], ==, strv[i]);
}

static void test_gjs_autostrv_init_take_ownership() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    GjsAutoStrv autoptr(const_cast<char* const*>(strv), GjsAutoTakeOwnership());

    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(autoptr[i], ==, strv[i]);
    g_assert(autoptr != strv);
}

static void test_gjs_autostrv_copy() {
    const char* strv[] = {"FOO", "Bar", "BAZ", nullptr};
    GjsAutoStrv autoptr = g_strdupv(const_cast<char**>(strv));

    char** copy = autoptr.copy();
    for (int i = g_strv_length(const_cast<char**>(strv)); i >= 0; i--)
        g_assert_cmpstr(copy[i], ==, strv[i]);
    g_assert(autoptr != copy);

    g_strfreev(copy);
}

static void test_gjs_autotypeclass_init() {
    GjsAutoTypeClass<GObjectClass> autoclass(gjs_test_object_get_type());

    g_assert_nonnull(autoclass);
    g_assert_cmpint(autoclass->g_type_class.g_type, ==,
        gjs_test_object_get_type());
}

void gjs_test_add_tests_for_jsapi_utils(void) {
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/size",
                    test_gjs_autopointer_size);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/constructor/empty",
                    test_gjs_autopointer_ctor_empty);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/constructor/basic",
                    test_gjs_autopointer_ctor_basic);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/constructor/take_ownership",
        test_gjs_autopointer_ctor_take_ownership);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/constructor/assignment",
                    test_gjs_autopointer_ctor_assign);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/constructor/assignment/other",
        test_gjs_autopointer_ctor_assign_other);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor",
                    test_gjs_autopointer_dtor);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/destructor/take_ownership",
        test_gjs_autopointer_dtor_take_ownership);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/default_free",
                    test_gjs_autopointer_dtor_default_free);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/destructor/no_free_pointer",
        test_gjs_autopointer_dtor_no_free_pointer);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/c++",
                    test_gjs_autopointer_dtor_cpp);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/destructor/c++-array",
                    test_gjs_autopointer_dtor_cpp_array);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/assign",
                    test_gjs_autopointer_assign_operator);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/other_ptr",
        test_gjs_autopointer_assign_operator_other_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/assign/self_ptr",
                    test_gjs_autopointer_assign_operator_self_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/assign/object",
                    test_gjs_autopointer_assign_operator_object);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/other_object",
        test_gjs_autopointer_assign_operator_other_object);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/self_object",
        test_gjs_autopointer_assign_operator_self_object);
    g_test_add_func(
        "/gjs/jsapi-utils/gjs-autopointer/operator/assign/copy_and_swap",
        test_gjs_autopointer_assign_operator_copy_and_swap);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/move",
                    test_gjs_autopointer_operator_move);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/swap",
                    test_gjs_autopointer_operator_swap);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/arrow",
                    test_gjs_autopointer_assign_operator_arrow);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/deference",
                    test_gjs_autopointer_assign_operator_deference);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/bool",
                    test_gjs_autopointer_assign_operator_bool);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/operator/array",
                    test_gjs_autopointer_assign_operator_array);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/get",
                    test_gjs_autopointer_get);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/release",
                    test_gjs_autopointer_release);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/reset/nullptr",
                    test_gjs_autopointer_reset_nullptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/reset/other_ptr",
                    test_gjs_autopointer_reset_other_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/reset/self_ptr",
                    test_gjs_autopointer_reset_self_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/swap/other_ptr",
                    test_gjs_autopointer_swap_other_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/swap/self_ptr",
                    test_gjs_autopointer_swap_self_ptr);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/swap/empty",
                    test_gjs_autopointer_swap_empty);
    g_test_add_func("/gjs/jsapi-utils/gjs-autopointer/method/copy",
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
}
