/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <utility>  // for move

#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/PropertyAndElement.h>  // for JS_DefineFunction
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gi/boxed.h"
#include "gi/gerror.h"
#include "gi/struct.h"
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
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
    JS::RootedObject prototype{cx};
    if (!BoxedPrototype::define_class_impl(cx, in_object, info, &prototype))
        return false;

    if (info.gtype() == G_TYPE_ERROR &&
        !JS_DefineFunction(cx, prototype, "toString", &ErrorBase::to_string, 0,
                           GJS_MODULE_PROP_FLAGS))
        return false;

    return true;
}

StructInstance::StructInstance(StructPrototype* prototype, JS::HandleObject obj)
    : BoxedInstance(prototype, obj) {
    GJS_INC_COUNTER(boxed_instance);
}

StructInstance::~StructInstance() {
    if (m_owning_ptr && m_allocated_directly && gtype() == G_TYPE_VALUE)
        g_value_unset(m_ptr.template as<GValue>());

    GJS_DEC_COUNTER(boxed_instance);
}

bool StructInstance::constructor_impl(JSContext* cx, JS::HandleObject obj,
                                      const JS::CallArgs& args) {
    if (gtype() == G_TYPE_VARIANT) {
        // Short-circuit construction for GVariants by calling into the JS
        // packing function
        const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
        if (!invoke_static_method(cx, obj, atoms.new_internal(), args))
            return false;

        // The return value of GLib.Variant.new_internal() gets its own
        // BoxedInstance, and the one we're setting up in this constructor is
        // discarded.
        debug_lifecycle(
            "Boxed construction delegated to GVariant constructor, boxed "
            "object discarded");

        return true;
    }

    if (!BoxedInstance::constructor_impl(cx, obj, args))
        return false;

    // Define the expected Error properties
    if (gtype() == G_TYPE_ERROR) {
        JS::RootedObject gerror(cx, &args.rval().toObject());
        if (!gjs_define_error_properties(cx, gerror))
            return false;
    }

    return true;
}

static bool define_extra_error_properties(JSContext* cx, JS::HandleObject obj) {
    StructBase* priv = StructBase::for_js(cx, obj);
    if (priv->gtype() != G_TYPE_ERROR)
        return true;
    return gjs_define_error_properties(cx, obj);
}

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
    JS::RootedObject obj{cx, new_for_c_struct_impl(cx, info, gboxed)};
    if (!obj || !define_extra_error_properties(cx, obj))
        return nullptr;
    return obj;
}

JSObject* StructInstance::new_for_c_struct(JSContext* cx,
                                           const GI::StructInfo info,
                                           void* gboxed,
                                           Boxed::NoCopy no_copy) {
    JS::RootedObject obj{
        cx, new_for_c_struct_impl(cx, info, gboxed, std::move(no_copy))};
    if (!obj || !define_extra_error_properties(cx, obj))
        return nullptr;
    return obj;
}
