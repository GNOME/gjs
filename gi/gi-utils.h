/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <stdint.h>
#include <string.h>

#include <utility>  // for pair

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <mozilla/Maybe.h>

#include "gi/info.h"

namespace GI {

[[nodiscard]]
constexpr bool uses_signed_type(const EnumInfo& enum_info) {
    switch (enum_info.storage_type()) {
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_INT64:
            return true;
        default:
            return false;
    }
}

// This is hacky - gi_function_info_invoke() and gi_field_info_get/set_field()
// expect the enum value in gjs_arg_member<int>(arg) and depend on all flags and
// enumerations being passed on the stack in a 32-bit field. See FIXME comment
// in gi_field_info_get_field(). The same assumption of enums cast to 32-bit
// signed integers is found in g_value_set_enum() / g_value_set_flags().
[[nodiscard]]
constexpr int64_t enum_from_int(const EnumInfo& enum_info, int int_value) {
    if (uses_signed_type(enum_info))
        return int64_t{int_value};
    return int64_t{static_cast<uint32_t>(int_value)};
}

// Here for symmetry, but result is the same for the two cases
[[nodiscard]]
constexpr int enum_to_int(int64_t value) {
    return static_cast<int>(value);
}

[[nodiscard]]
inline bool is_gdk_atom(const RegisteredTypeInfo& rt) {
    return strcmp("Atom", rt.name()) == 0 && strcmp("Gdk", rt.ns()) == 0;
}

[[nodiscard]]
inline bool is_g_value(const RegisteredTypeInfo& rt) {
    return g_type_is_a(rt.gtype(), G_TYPE_VALUE);
}

[[nodiscard]]
inline bool is_supported_ghash_key_type(const GITypeTag tag) {
    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UNICHAR:
            return true;
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        // FIXME: The above four could be supported, but are currently not. The
        // ones below cannot be key types in a regular JS object; we would need
        // to allow marshalling Map objects into GHashTables to support those,
        // as well as refactoring this function to take GITypeInfo* and
        // splitting out the marshalling for basic types into a different
        // function.
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_INTERFACE:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ARRAY:
            return false;
    }
    g_assert_not_reached();
}

[[nodiscard]]
inline bool is_supported_gobject_field_type(const TypeInfo& type) {
    switch (type.tag()) {
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_UNICHAR:
            return true;
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_INTERFACE:
            return false;
    }
    g_assert_not_reached();
}

template <InfoTag TAG>
[[nodiscard]]
bool struct_is_simple(const UnownedInfo<TAG>&);

template <InfoTag TAG>
[[nodiscard]]
bool simple_struct_has_pointers(const UnownedInfo<TAG>&);

using ConstructorIndex = unsigned;

template <InfoTag TAG>
[[nodiscard]]
std::pair<mozilla::Maybe<ConstructorIndex>, mozilla::Maybe<ConstructorIndex>>
find_boxed_constructor_indices(const UnownedInfo<TAG>&);

}  // namespace GI
