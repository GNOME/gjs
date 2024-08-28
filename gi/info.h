/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <girepository.h>
#include <glib.h>

#include <js/GCPolicyAPI.h>

#include "gjs/auto.h"

// This file contains auto pointers for libgirepository.
// There are type aliases for auto pointers for every kind of introspection info
// (GI::AutoFooInfo), with wrappers for commonly used APIs such as
// g_base_info_get_name(), g_base_info_get_namespace(), and
// g_base_info_get_type().

namespace GI {

// Use this class for owning a GIBaseInfo* of indeterminate type. Any type (e.g.
// GIFunctionInfo*, GIObjectInfo*) will fit. If you know that the info is of a
// certain type (e.g. you are storing the return value of a function that
// returns GIFunctionInfo*,) use one of the derived classes below.
struct AutoBaseInfo : Gjs::AutoPointer<GIBaseInfo, GIBaseInfo,
                                       g_base_info_unref, g_base_info_ref> {
    using AutoPointer::AutoPointer;

    [[nodiscard]] const char* name() const {
        return g_base_info_get_name(*this);
    }
    [[nodiscard]] const char* ns() const {
        return g_base_info_get_namespace(*this);
    }
    [[nodiscard]] GIInfoType type() const {
        return g_base_info_get_type(*this);
    }
};

// Use GI::AutoInfo, preferably its typedefs below, when you know for sure that
// the info is either of a certain type or null.
template <GIInfoType TAG>
struct AutoInfo : AutoBaseInfo {
    using AutoBaseInfo::AutoBaseInfo;

    // Normally one-argument constructors should be explicit, but we are trying
    // to conform to the interface of std::unique_ptr here.
    AutoInfo(GIBaseInfo* ptr = nullptr)  // NOLINT(runtime/explicit)
        : AutoBaseInfo(ptr) {
#ifndef G_DISABLE_CAST_CHECKS
        validate();
#endif
    }

    void reset(GIBaseInfo* other = nullptr) {
        AutoBaseInfo::reset(other);
#ifndef G_DISABLE_CAST_CHECKS
        validate();
#endif
    }

    // You should not need this method, because you already know the answer.
    GIInfoType type() = delete;

 private:
    void validate() const {
        if (GIBaseInfo* base = *this)
            g_assert(g_base_info_get_type(base) == TAG);
    }
};

using AutoArgInfo = AutoInfo<GI_INFO_TYPE_ARG>;
using AutoEnumInfo = AutoInfo<GI_INFO_TYPE_ENUM>;
using AutoFieldInfo = AutoInfo<GI_INFO_TYPE_FIELD>;
using AutoFunctionInfo = AutoInfo<GI_INFO_TYPE_FUNCTION>;
using AutoInterfaceInfo = AutoInfo<GI_INFO_TYPE_INTERFACE>;
using AutoObjectInfo = AutoInfo<GI_INFO_TYPE_OBJECT>;
using AutoPropertyInfo = AutoInfo<GI_INFO_TYPE_PROPERTY>;
using AutoStructInfo = AutoInfo<GI_INFO_TYPE_STRUCT>;
using AutoSignalInfo = AutoInfo<GI_INFO_TYPE_SIGNAL>;
using AutoTypeInfo = AutoInfo<GI_INFO_TYPE_TYPE>;
using AutoValueInfo = AutoInfo<GI_INFO_TYPE_VALUE>;
using AutoVFuncInfo = AutoInfo<GI_INFO_TYPE_VFUNC>;

// GI::CallableInfo can be one of several tags, so we have to have a separate
// class, and use GI_IS_CALLABLE_INFO() to validate.
struct AutoCallableInfo : AutoBaseInfo {
    using AutoBaseInfo::AutoBaseInfo;

    AutoCallableInfo(GIBaseInfo* ptr = nullptr)  // NOLINT(runtime/explicit)
        : AutoBaseInfo(ptr) {
        validate();
    }

    void reset(GIBaseInfo* other = nullptr) {
        AutoBaseInfo::reset(other);
        validate();
    }

 private:
    void validate() const {
        if (*this)
            g_assert(GI_IS_CALLABLE_INFO(get()));
    }
};

}  // namespace GI

namespace Gjs {
template <>
struct SmartPointer<GIBaseInfo> : GI::AutoBaseInfo {
    using AutoBaseInfo::AutoBaseInfo;
};
}  // namespace Gjs

/* For use of GjsAutoInfo<TAG> in GC hash maps */
namespace JS {
template <GIInfoType TAG>
struct GCPolicy<GI::AutoInfo<TAG>> : public IgnoreGCPolicy<GI::AutoInfo<TAG>> {
};
}  // namespace JS
