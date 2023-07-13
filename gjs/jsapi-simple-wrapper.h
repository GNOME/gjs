/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Marco Trevisan <marco.trevisan@canonical.com>

#pragma once

#include <config.h>

#include <type_traits>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/macros.h"

namespace Gjs {

class SimpleWrapper {
    template <typename T>
    using DestroyNotifyType = void (*)(T);

 public:
    using DestroyNotify = DestroyNotifyType<void*>;

 private:
    static JSObject* new_for_ptr(JSContext*, void* data, DestroyNotify);
    [[nodiscard]] static void* get_ptr(JSContext*, JS::HandleObject);

 public:
    template <typename T, typename DT>
    GJS_JSAPI_RETURN_CONVENTION static JSObject* new_for_ptr(
        JSContext* cx, T* data, DT destroy_notify) {
        static_assert(std::is_convertible_v<DT, DestroyNotifyType<T*>>,
                      "destroy notify can't be converted");
        return new_for_ptr(
            cx, static_cast<void*>(data),
            reinterpret_cast<DestroyNotify>(
                static_cast<DestroyNotifyType<T*>>(destroy_notify)));
    }

    template <typename T>
    GJS_JSAPI_RETURN_CONVENTION static JSObject* new_for_ptr(JSContext* cx,
                                                             T* data) {
        return new_for_ptr(cx, data, nullptr);
    }

    template <typename T, typename... Ts>
    GJS_JSAPI_RETURN_CONVENTION static JSObject* new_for_type(JSContext* cx,
                                                              Ts... args) {
        return new_for_ptr(cx, new T(args...), [](T* data) { delete data; });
    }

    template <typename T>
    [[nodiscard]] static T* get(JSContext* cx, JS::HandleObject obj) {
        return static_cast<T*>(get_ptr(cx, obj));
    }
};

}  // namespace Gjs
