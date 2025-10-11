/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Chun-wei Fan <fanchunwei@src.gnome.org>
// SPDX-FileCopyrightText: 2018-2020  Canonical, Ltd
// SPDX-FileCopyrightText: 2018, 2024 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <stdlib.h>

#include <type_traits>
#include <utility>

#include <glib-object.h>
#include <glib.h>
// IWYU pragma: no_forward_declare _GVariant

#include <js/Utility.h>  // for UniqueChars

// Auto pointers. We don't use GLib's g_autofree and friends because they only
// work on GCC and Clang, and we try to support MSVC where possible. But this is
// C++, so we use C++ classes.

namespace Gjs {

// A sentinel object used to pick the AutoPointer constructor that adds a
// reference: AutoFoo foo{pointer, TakeOwnership{}};
struct TakeOwnership {};

template <typename F = void>
using AutoPointerRefFunction = F* (*)(F*);

template <typename F = void>
using AutoPointerFreeFunction = void (*)(F*);

template <typename T, typename F = void,
          AutoPointerFreeFunction<F> free_func = free,
          AutoPointerRefFunction<F> ref_func = nullptr>
struct AutoPointer {
    using Tp =
        std::conditional_t<std::is_array_v<T>, std::remove_extent_t<T>, T>;
    using Ptr = std::add_pointer_t<Tp>;
    using ConstPtr = std::add_pointer_t<std::add_const_t<Tp>>;
    using RvalueRef = std::add_lvalue_reference_t<Tp>;

 protected:
    using BaseType = AutoPointer<T, F, free_func, ref_func>;

 private:
    template <typename FunctionType, FunctionType function>
    static constexpr bool has_function() {
        using NullType = std::integral_constant<FunctionType, nullptr>;
        using ActualType = std::integral_constant<FunctionType, function>;

        return !std::is_same_v<ActualType, NullType>;
    }

 public:
    static constexpr bool has_free_function() {
        return has_function<AutoPointerFreeFunction<F>, free_func>();
    }

    static constexpr bool has_ref_function() {
        return has_function<AutoPointerRefFunction<F>, ref_func>();
    }

    constexpr AutoPointer(Ptr ptr = nullptr)  // NOLINT(runtime/explicit)
        : m_ptr(ptr) {}
    template <typename U, typename = std::enable_if_t<std::is_same_v<U, Tp> &&
                                                      std::is_array_v<T>>>
    explicit constexpr AutoPointer(U ptr[]) : m_ptr(ptr) {}

    constexpr AutoPointer(Ptr ptr, const TakeOwnership&) : AutoPointer(ptr) {
        m_ptr = copy();
    }
    constexpr AutoPointer(ConstPtr ptr, const TakeOwnership& o)
        : AutoPointer(const_cast<Ptr>(ptr), o) {}
    constexpr AutoPointer(AutoPointer&& other) : AutoPointer() {
        this->swap(other);
    }
    constexpr AutoPointer(AutoPointer const& other) : AutoPointer() {
        *this = other;
    }

    constexpr AutoPointer& operator=(Ptr ptr) {
        reset(ptr);
        return *this;
    }

    constexpr AutoPointer& operator=(AutoPointer&& other) {
        this->swap(other);
        return *this;
    }

    constexpr AutoPointer& operator=(AutoPointer const& other) {
        AutoPointer dup{other.get(), TakeOwnership{}};
        this->swap(dup);
        return *this;
    }

    template <typename U = T>
    constexpr std::enable_if_t<!std::is_array_v<U>, Ptr> operator->() {
        return m_ptr;
    }

    template <typename U = T>
    constexpr std::enable_if_t<!std::is_array_v<U>, ConstPtr> operator->()
        const {
        return m_ptr;
    }

    template <typename U = T>
    constexpr std::enable_if_t<std::is_array_v<U>, RvalueRef> operator[](
        int index) {
        return m_ptr[index];
    }

    template <typename U = T>
    constexpr std::enable_if_t<std::is_array_v<U>, std::add_const_t<RvalueRef>>
    operator[](int index) const {
        return m_ptr[index];
    }

    constexpr Tp operator*() const { return *m_ptr; }
    constexpr operator Ptr() { return m_ptr; }
    constexpr operator Ptr() const { return m_ptr; }
    constexpr operator ConstPtr() const { return m_ptr; }
    constexpr operator bool() const { return m_ptr != nullptr; }

    constexpr Ptr get() const { return m_ptr; }
    constexpr Ptr* out() { return &m_ptr; }
    constexpr ConstPtr* out() const { return const_cast<ConstPtr*>(&m_ptr); }

    constexpr Ptr release() {
        auto* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    constexpr void reset(Ptr ptr = nullptr) {
        Ptr old_ptr = m_ptr;
        m_ptr = ptr;

        if constexpr (has_free_function()) {
            if (old_ptr)
                free_func(reinterpret_cast<F*>(old_ptr));
        }
    }

    constexpr void swap(AutoPointer& other) {
        std::swap(this->m_ptr, other.m_ptr);
    }

    /* constexpr */ ~AutoPointer() {  // one day, with -std=c++2a
        reset();
    }

    template <typename U = T>
    [[nodiscard]]
    constexpr std::enable_if_t<!std::is_array_v<U>, Ptr> copy() const {
        static_assert(has_ref_function(), "No ref function provided");
        return m_ptr ? reinterpret_cast<Ptr>(
                           ref_func(reinterpret_cast<F*>(m_ptr)))
                     : nullptr;
    }

    template <typename C>
    [[nodiscard]]
    constexpr C* as() const {
        return const_cast<C*>(reinterpret_cast<const C*>(m_ptr));
    }

 private:
    Ptr m_ptr;
};

template <typename T, typename F, AutoPointerFreeFunction<F> free_func,
          AutoPointerRefFunction<F> ref_func>
constexpr bool operator==(AutoPointer<T, F, free_func, ref_func> const& lhs,
                          AutoPointer<T, F, free_func, ref_func> const& rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
using AutoFree = AutoPointer<T>;

struct AutoCharFuncs {
    static char* dup(char* str) { return g_strdup(str); }
    static void free(char* str) { g_free(str); }
};
using AutoChar =
    AutoPointer<char, char, AutoCharFuncs::free, AutoCharFuncs::dup>;

// This moves a string owned by the JS runtime into the GLib domain. This is
// only possible because currently, js_free() and g_free() both ultimately call
// free(). It would cause crashes if SpiderMonkey were to stop supporting
// embedders using the system allocator in the future. In that case, this
// function would have to copy the string.
[[nodiscard]] inline AutoChar js_chars_to_glib(JS::UniqueChars&& js_chars) {
    return {js_chars.release()};
}

using AutoStrv = AutoPointer<char*, char*, g_strfreev, g_strdupv>;

template <typename T>
using AutoUnref = AutoPointer<T, void, g_object_unref, g_object_ref>;

using AutoGVariant =
    AutoPointer<GVariant, GVariant, g_variant_unref, g_variant_ref>;

using AutoParam =
    AutoPointer<GParamSpec, GParamSpec, g_param_spec_unref, g_param_spec_ref>;

using AutoGClosure =
    AutoPointer<GClosure, GClosure, g_closure_unref, g_closure_ref>;

template <typename V, typename T>
constexpr void AutoPointerDeleter(T v) {
    if constexpr (std::is_array_v<V>)
        delete[] reinterpret_cast<std::remove_extent_t<V>*>(v);
    else
        delete v;
}

template <typename T>
using AutoCppPointer = AutoPointer<T, T, AutoPointerDeleter<T>>;

template <typename T = GTypeClass>
class AutoTypeClass : public AutoPointer<T, void, &g_type_class_unref> {
    explicit AutoTypeClass(void* ptr = nullptr)
        : AutoPointer<T, void, g_type_class_unref>(static_cast<T*>(ptr)) {}

 public:
    explicit AutoTypeClass(GType gtype)
        : AutoTypeClass(g_type_class_ref(gtype)) {}
};

template <typename T>
struct SmartPointer : AutoPointer<T> {
    using AutoPointer<T>::AutoPointer;
};

template <>
struct SmartPointer<char*> : AutoStrv {
    using AutoStrv::AutoPointer;
};

template <>
struct SmartPointer<GStrv> : AutoStrv {
    using AutoStrv::AutoPointer;
};

template <>
struct SmartPointer<GObject> : AutoUnref<GObject> {
    using AutoUnref<GObject>::AutoPointer;
};

template <>
struct SmartPointer<GVariant> : AutoGVariant {
    using AutoGVariant::AutoPointer;
};

template <>
struct SmartPointer<GList> : AutoPointer<GList, GList, g_list_free> {
    using AutoPointer::AutoPointer;
};

template <>
struct SmartPointer<GSList> : AutoPointer<GSList, GSList, g_slist_free> {
    using AutoPointer::AutoPointer;
};

}  // namespace Gjs
