/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_GERROR_H_
#define GI_GERROR_H_

#include <config.h>

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/TypeDecls.h>

#include "gi/cwrapper.h"
#include "gi/info.h"
#include "gi/wrapperutils.h"
#include "gjs/gerror-result.h"
#include "gjs/macros.h"
#include "util/log.h"

class ErrorPrototype;
class ErrorInstance;
struct JSFunctionSpec;
struct JSPropertySpec;
namespace JS {
class CallArgs;
}

/* To conserve memory, we have two different kinds of private data for GError
 * JS wrappers: ErrorInstance, and ErrorPrototype. Both inherit from ErrorBase
 * for their common functionality. For more information, see the notes in
 * wrapperutils.h.
 *
 * ErrorPrototype, unlike the other GIWrapperPrototype subclasses, represents a
 * single error domain instead of a single GType. All Errors have a GType of
 * G_TYPE_ERROR.
 *
 * Note that in some situations GError structs can show up as BoxedInstance
 * instead of ErrorInstance. We have some special cases in this code to deal
 * with that.
 */

class ErrorBase
    : public GIWrapperBase<ErrorBase, ErrorPrototype, ErrorInstance> {
    friend class CWrapperPointerOps<ErrorBase>;
    friend class GIWrapperBase<ErrorBase, ErrorPrototype, ErrorInstance>;

 protected:
    explicit ErrorBase(ErrorPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GERROR;
    static constexpr const char* DEBUG_TAG = "gerror";

    static const struct JSClassOps class_ops;

 public:
    // public in order to implement Error.isError()
    static const struct JSClass klass;

 protected:
    static JSPropertySpec proto_properties[];
    static JSFunctionSpec static_methods[];

    // Accessors

 public:
    [[nodiscard]] GQuark domain(void) const;

    // Property getters

 protected:
    GJS_JSAPI_RETURN_CONVENTION
    static bool get_domain(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool get_message(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool get_code(JSContext* cx, unsigned argc, JS::Value* vp);

    // JS methods

    GJS_JSAPI_RETURN_CONVENTION
    static bool value_of(JSContext* cx, unsigned argc, JS::Value* vp);

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp);

    // Helper methods

    GJS_JSAPI_RETURN_CONVENTION
    static GError* to_c_ptr(JSContext* cx, JS::HandleObject obj);

    GJS_JSAPI_RETURN_CONVENTION
    static bool transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                        GIArgument* arg,
                                        GIDirection transfer_direction,
                                        GITransfer transfer_ownership);

    GJS_JSAPI_RETURN_CONVENTION
    static bool typecheck(JSContext* cx, JS::HandleObject obj);
    [[nodiscard]] static bool typecheck(JSContext* cx, JS::HandleObject obj,
                                        GjsTypecheckNoThrow);
};

class ErrorPrototype
    : public GIWrapperPrototype<ErrorBase, ErrorPrototype, ErrorInstance,
                                GI::AutoEnumInfo, GI::EnumInfo> {
    friend class GIWrapperPrototype<ErrorBase, ErrorPrototype, ErrorInstance,
                                    GI::AutoEnumInfo, GI::EnumInfo>;
    friend class GIWrapperBase<ErrorBase, ErrorPrototype, ErrorInstance>;

    GQuark m_domain;

    explicit ErrorPrototype(const GI::EnumInfo, GType);
    ~ErrorPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool get_parent_proto(JSContext* cx, JS::MutableHandleObject proto) const;

 public:
    [[nodiscard]] GQuark domain(void) const { return m_domain; }

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             const GI::EnumInfo);
};

class ErrorInstance : public GIWrapperInstance<ErrorBase, ErrorPrototype,
                                               ErrorInstance, GError> {
    friend class GIWrapperInstance<ErrorBase, ErrorPrototype, ErrorInstance,
                                   GError>;
    friend class GIWrapperBase<ErrorBase, ErrorPrototype, ErrorInstance>;

    explicit ErrorInstance(ErrorPrototype* prototype, JS::HandleObject obj);
    ~ErrorInstance(void);

 public:
    void copy_gerror(GError* other) { m_ptr = g_error_copy(other); }
    GJS_JSAPI_RETURN_CONVENTION
    static GError* copy_ptr(JSContext*, GType, void* ptr) {
        return g_error_copy(static_cast<GError*>(ptr));
    }

    // Accessors

    [[nodiscard]] const char* message(void) const { return m_ptr->message; }
    [[nodiscard]] int code(void) const { return m_ptr->code; }

    // JS constructor

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

    // Public API

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* object_for_c_ptr(JSContext* cx, GError* gerror);
};

GJS_JSAPI_RETURN_CONVENTION
GError* gjs_gerror_make_from_thrown_value(JSContext* cx);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_error_properties(JSContext* cx, JS::HandleObject obj);

bool gjs_throw_gerror(JSContext* cx, Gjs::AutoError const&);

#endif  // GI_GERROR_H_
