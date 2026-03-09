/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <algorithm>  // for all_of, any_of

#include <glib.h>

#include "gi/gi-utils.h"
#include "gi/info.h"

using mozilla::Maybe, mozilla::Some;

namespace GI {

[[nodiscard]]
static bool type_can_be_allocated_directly(const TypeInfo& type_info) {
    if (type_info.is_pointer()) {
        if (type_info.tag() == GI_TYPE_TAG_ARRAY &&
            type_info.array_type() == GI_ARRAY_TYPE_C)
            return type_can_be_allocated_directly(type_info.element_type());

        return true;
    }

    if (type_info.tag() != GI_TYPE_TAG_INTERFACE)
        return true;

    AutoBaseInfo interface_info{type_info.interface()};
    if (auto struct_info = interface_info.as<InfoTag::STRUCT>())
        return struct_is_simple(struct_info.value());
    if (auto union_info = interface_info.as<InfoTag::UNION>())
        return struct_is_simple(union_info.value());
    if (interface_info.is_enum_or_flags())
        return true;
    return false;
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
template <InfoTag TAG>
[[nodiscard]]
bool struct_is_simple(const UnownedInfo<TAG>& info) {
    static_assert(TAG == InfoTag::STRUCT || TAG == InfoTag::UNION);

    typename UnownedInfo<TAG>::FieldsIterator iter = info.fields();

    // If it's opaque, it's not simple
    if (iter.size() == 0)
        return false;

    return std::all_of(
        iter.begin(), iter.end(), [](const AutoFieldInfo& field_info) {
            return type_can_be_allocated_directly(field_info.type_info());
        });
}

[[nodiscard]]
static bool direct_allocation_has_pointers(const TypeInfo& type_info) {
    if (type_info.is_pointer()) {
        if (type_info.tag() == GI_TYPE_TAG_ARRAY &&
            type_info.array_type() == GI_ARRAY_TYPE_C) {
            return direct_allocation_has_pointers(type_info.element_type());
        }

        return type_info.tag() != GI_TYPE_TAG_VOID;
    }

    if (type_info.tag() != GI_TYPE_TAG_INTERFACE)
        return false;

    AutoBaseInfo interface{type_info.interface()};
    if (auto struct_info = interface.as<InfoTag::STRUCT>())
        return simple_struct_has_pointers(struct_info.value());
    if (auto union_info = interface.as<InfoTag::UNION>())
        return simple_struct_has_pointers(union_info.value());

    return false;
}

template <InfoTag TAG>
bool simple_struct_has_pointers(const UnownedInfo<TAG>& info) {
    static_assert(TAG == InfoTag::STRUCT || TAG == InfoTag::UNION);

    g_assert(struct_is_simple(info) &&
             "Don't call simple_struct_has_pointers() on a non-simple struct");

    typename UnownedInfo<TAG>::FieldsIterator fields = info.fields();
    return std::any_of(
        fields.begin(), fields.end(), [](const AutoFieldInfo& field) {
            return direct_allocation_has_pointers(field.type_info());
        });
}

template <InfoTag TAG>
std::pair<Maybe<ConstructorIndex>, Maybe<ConstructorIndex>>
find_boxed_constructor_indices(const UnownedInfo<TAG>& info) {
    static_assert(TAG == InfoTag::STRUCT || TAG == InfoTag::UNION);

    if (info.gtype() == G_TYPE_NONE)
        return {};

    ConstructorIndex i = 0;
    Maybe<ConstructorIndex> first_constructor;
    Maybe<ConstructorIndex> zero_args_constructor;
    Maybe<ConstructorIndex> default_constructor;

    /* If the structure is registered as a boxed, we can create a new instance
     * by looking for a zero-args constructor and calling it; constructors don't
     * really make sense for non-boxed types, since there is no memory
     * management for the return value.
     */
    for (const GI::AutoFunctionInfo& func_info : info.methods()) {
        if (func_info.is_constructor()) {
            if (!first_constructor)
                first_constructor = Some(i);

            if (!zero_args_constructor && func_info.n_args() == 0)
                zero_args_constructor = Some(i);

            if (!default_constructor && strcmp(func_info.name(), "new") == 0)
                default_constructor = Some(i);
        }
        i++;
    }

    if (!default_constructor && zero_args_constructor)
        default_constructor = zero_args_constructor;
    if (!default_constructor && first_constructor)
        default_constructor = first_constructor;

    return {zero_args_constructor, default_constructor};
}

template bool struct_is_simple(const StructInfo&);
template bool simple_struct_has_pointers(const StructInfo&);
template std::pair<Maybe<ConstructorIndex>, Maybe<ConstructorIndex>>
find_boxed_constructor_indices(const StructInfo&);

template bool struct_is_simple(const UnionInfo&);
template bool simple_struct_has_pointers(const UnionInfo&);
template std::pair<Maybe<ConstructorIndex>, Maybe<ConstructorIndex>>
find_boxed_constructor_indices(const UnionInfo&);

}  // namespace GI
