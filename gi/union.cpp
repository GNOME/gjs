/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2022 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <js/Class.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/boxed.h"
#include "gi/union.h"
#include "gjs/mem-private.h"

UnionPrototype::UnionPrototype(const GI::UnionInfo info, GType gtype)
    : BoxedPrototype(info, gtype) {
    GJS_INC_COUNTER(union_prototype);
}

UnionPrototype::~UnionPrototype(void) { GJS_DEC_COUNTER(union_prototype); }

UnionInstance::UnionInstance(UnionPrototype* prototype, JS::HandleObject obj)
    : BoxedInstance(prototype, obj) {
    GJS_INC_COUNTER(union_instance);
}

UnionInstance::~UnionInstance() { GJS_DEC_COUNTER(union_instance); }

// clang-format off
const struct JSClassOps UnionBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    &UnionBase::BoxedBase::new_enumerate,
    &UnionBase::BoxedBase::resolve,
    nullptr,  // mayResolve
    &UnionBase::BoxedBase::finalize,
    nullptr,  // call
    nullptr,  // construct
    &UnionBase::BoxedBase::trace
};

// We allocate 1 extra reserved slot; this is typically unused, but if the boxed
// is for a nested structure inside a parent structure, the reserved slot is
// used to hold onto the parent Javascript object and make sure it doesn't get
// freed.
const struct JSClass UnionBase::klass = {
    "GObject_Union",
    JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
    &UnionBase::class_ops
};
// clang-format on

bool UnionPrototype::define_class(JSContext* cx, JS::HandleObject in_object,
                                  const GI::UnionInfo info) {
    JS::RootedObject unused{cx};
    return BoxedPrototype::define_class_impl(cx, in_object, info, &unused);
}

JSObject* UnionInstance::new_for_c_union(JSContext* cx,
                                         const GI::UnionInfo info,
                                         void* gboxed) {
    return new_for_c_struct_impl(cx, info, gboxed);
}
