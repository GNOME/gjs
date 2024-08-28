// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018-2020  Canonical, Ltd

#pragma once

#include <config.h>

#include <glib.h>

#include "gjs/auto.h"

namespace Gjs {

struct AutoErrorFuncs {
    static GError* error_copy(GError* error) { return g_error_copy(error); }
};

struct AutoError
    : AutoPointer<GError, GError, g_error_free, AutoErrorFuncs::error_copy> {
    using BaseType::BaseType;
    using BaseType::operator=;

    constexpr BaseType::ConstPtr* operator&()  // NOLINT(runtime/operator)
        const {
        return out();
    }
    constexpr BaseType::Ptr* operator&() {  // NOLINT(runtime/operator)
        return out();
    }
};

template <>
struct SmartPointer<GError> : AutoError {
    using AutoError::AutoError;
    using AutoError::operator=;
    using AutoError::operator&;
};

}  // namespace Gjs
