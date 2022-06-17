/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <utility>  // for move

#include <js/Class.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/boxed.h"
#include "gi/struct.h"
#include "gjs/mem-private.h"

// clang-format off
const struct JSClassOps StructBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    &StructBase::BoxedBase::new_enumerate,
    &StructBase::BoxedBase::resolve,
    nullptr,  // mayResolve
    &StructBase::BoxedBase::finalize,
    nullptr,  // call
    nullptr,  // construct
    &StructBase::BoxedBase::trace
};

// We allocate 1 extra reserved slot; this is typically unused, but if the boxed
// is for a nested structure inside a parent structure, the reserved slot is
// used to hold onto the parent Javascript object and make sure it doesn't get
// freed.
const struct JSClass StructBase::klass = {
    "GObject_Struct",
    JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
    &StructBase::class_ops
};
// clang-format on

StructPrototype::StructPrototype(const GI::StructInfo info, GType gtype)
    : BoxedPrototype(info, gtype) {
    GJS_INC_COUNTER(boxed_prototype);
}

StructPrototype::~StructPrototype() { GJS_DEC_COUNTER(boxed_prototype); }

bool StructPrototype::define_class(JSContext* cx, JS::HandleObject in_object,
                                   const GI::StructInfo info) {
    return BoxedPrototype::define_class_impl(cx, in_object, info);
}

StructInstance::StructInstance(StructPrototype* prototype, JS::HandleObject obj)
    : BoxedInstance(prototype, obj) {
    GJS_INC_COUNTER(boxed_instance);
}

StructInstance::~StructInstance() { GJS_DEC_COUNTER(boxed_instance); }

/*
 * StructInstance::new_for_c_struct:
 *
 * Creates a new StructInstance JS object from a C boxed struct pointer.
 *
 * There are two overloads of this method; the NoCopy overload will simply take
 * the passed-in pointer but not own it, while the normal method will take a
 * reference, or if the boxed type can be directly allocated, copy the memory.
 */
JSObject* StructInstance::new_for_c_struct(JSContext* cx,
                                           const GI::StructInfo info,
                                           void* gboxed) {
    return new_for_c_struct_impl(cx, info, gboxed);
}

JSObject* StructInstance::new_for_c_struct(JSContext* cx,
                                           const GI::StructInfo info,
                                           void* gboxed,
                                           Boxed::NoCopy no_copy) {
    return new_for_c_struct_impl(cx, info, gboxed, std::move(no_copy));
}
