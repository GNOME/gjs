/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Marco Trevisan <marco.trevisan@canonical.com>

#include <config.h>

#include <js/Class.h>
#include <js/Object.h>
#include <jsapi.h>

#include "gjs/jsapi-simple-wrapper.h"

namespace Gjs {

static const size_t DATA_SLOT = 0;
static const size_t DESTROY_NOTIFY_SLOT = 1;

static const JSClassOps class_ops = {
    .finalize =
        [](JS::GCContext*, JSObject* obj) {
            void* destroy_notify =
                JS::GetMaybePtrFromReservedSlot<void>(obj, DESTROY_NOTIFY_SLOT);
            void* data = JS::GetMaybePtrFromReservedSlot<void>(obj, DATA_SLOT);
            reinterpret_cast<SimpleWrapper::DestroyNotify>(destroy_notify)(
                data);
        },
};

static const JSClass data_class = {
    .name = "Object",  // user-visible
    .flags = JSCLASS_HAS_RESERVED_SLOTS(1),
};

static const JSClass destroy_notify_class = {
    .name = "Object",  // user-visible
    .flags = JSCLASS_HAS_RESERVED_SLOTS(2) | JSCLASS_FOREGROUND_FINALIZE,
    .cOps = &class_ops,
};

JSObject* SimpleWrapper::new_for_ptr(JSContext* cx, void* ptr,
                                     DestroyNotify destroy_notify) {
    if (!destroy_notify) {
        JSObject* retval = JS_NewObject(cx, &data_class);
        if (!retval)
            return nullptr;
        JS::SetReservedSlot(retval, DATA_SLOT, JS::PrivateValue(ptr));
        return retval;
    }

    JSObject* retval = JS_NewObject(cx, &destroy_notify_class);
    if (!retval)
        return nullptr;
    JS::SetReservedSlot(retval, DATA_SLOT, JS::PrivateValue(ptr));
    JS::SetReservedSlot(
        retval, DESTROY_NOTIFY_SLOT,
        JS::PrivateValue(reinterpret_cast<void*>(destroy_notify)));
    return retval;
}

void* SimpleWrapper::get_ptr(JSContext* cx, JS::HandleObject obj) {
    if (!JS_InstanceOf(cx, obj, &destroy_notify_class, nullptr) &&
        !JS_InstanceOf(cx, obj, &data_class, nullptr))
        return nullptr;
    return JS::GetMaybePtrFromReservedSlot<void>(obj, DATA_SLOT);
}

}  // namespace Gjs
