/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>

#include <string>  // for string methods

#include <girepository/girepository.h>
#include <glib-object.h>

#include <js/CallAndConstruct.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ColumnNumber.h>
#include <js/Exception.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_ENUMERATE
#include <js/PropertySpec.h>        // for JS_PSG
#include <js/RootingAPI.h>
#include <js/SavedFrameAPI.h>
#include <js/Stack.h>  // for BuildStackString, CaptureCurrentStack
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>    // for InformalValueTypeName, JS_GetClassObject
#include <jspubtd.h>  // for JSProtoKey, JSProto_Error, JSProt...
#include <mozilla/Maybe.h>

#include "gi/arg-inl.h"
#include "gi/enumeration.h"
#include "gi/gerror.h"
#include "gi/info.h"
#include "gi/repo.h"
#include "gi/struct.h"
#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/error-types.h"
#include "gjs/gerror-result.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "util/log.h"

using mozilla::Maybe;

ErrorPrototype::ErrorPrototype(const GI::EnumInfo info, GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_domain(g_quark_from_string(info.error_domain())) {
    GJS_INC_COUNTER(gerror_prototype);
}

ErrorPrototype::~ErrorPrototype(void) { GJS_DEC_COUNTER(gerror_prototype); }

ErrorInstance::ErrorInstance(ErrorPrototype* prototype, JS::HandleObject obj)
    : GIWrapperInstance(prototype, obj) {
    GJS_INC_COUNTER(gerror_instance);
}

ErrorInstance::~ErrorInstance(void) {
    GJS_DEC_COUNTER(gerror_instance);
}

/*
 * ErrorBase::domain:
 *
 * Fetches ErrorPrototype::domain() for instances as well as prototypes.
 */
GQuark ErrorBase::domain(void) const { return get_prototype()->domain(); }

// See GIWrapperBase::constructor().
bool ErrorInstance::constructor_impl(JSContext* context,
                                     JS::HandleObject object,
                                     const JS::CallArgs& argv) {
    if (argv.length() != 1 || !argv[0].isObject()) {
        gjs_throw(context, "Invalid parameters passed to GError constructor, expected one object");
        return false;
    }

    JS::RootedObject params_obj(context, &argv[0].toObject());
    JS::UniqueChars message;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (!gjs_object_require_property(context, params_obj, "GError constructor",
                                     atoms.message(), &message))
        return false;

    int32_t code;
    if (!gjs_object_require_property(context, params_obj, "GError constructor",
                                     atoms.code(), &code))
        return false;

    m_ptr = g_error_new_literal(domain(), code, message.get());

    /* We assume this error will be thrown in the same line as the constructor */
    return gjs_define_error_properties(context, object);
}

/*
 * ErrorBase::get_domain:
 *
 * JSNative property getter for `domain`. This property works on prototypes as
 * well as instances.
 */
bool ErrorBase::get_domain(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    args.rval().setInt32(priv->domain());
    return true;
}

// JSNative property getter for `message`.
bool ErrorBase::get_message(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    if (!priv->check_is_instance(cx, "get a field"))
        return false;

    return gjs_string_from_utf8(cx, priv->to_instance()->message(),
                                args.rval());
}

// JSNative property getter for `code`.
bool ErrorBase::get_code(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    if (!priv->check_is_instance(cx, "get a field"))
        return false;

    args.rval().setInt32(priv->to_instance()->code());
    return true;
}

// JSNative implementation of `toString()`.
bool ErrorBase::to_string(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(context, argc, vp, rec, self);

    Gjs::AutoChar descr;

    // An error created via `new GLib.Error` will have a Struct* private
    // pointer, not an Error*, so we can't call regular to_string() on it.
    if (StructBase::typecheck(context, self, G_TYPE_ERROR,
                              GjsTypecheckNoThrow{})) {
        auto* gerror = StructBase::to_c_ptr<GError>(context, self);
        if (!gerror)
            return false;
        descr =
            g_strdup_printf("GLib.Error %s: %s",
                            g_quark_to_string(gerror->domain), gerror->message);

        return gjs_string_from_utf8(context, descr, rec.rval());
    }

    ErrorBase* priv;
    if (!for_js_typecheck(context, self, &priv, &rec))
        return false;

    /* We follow the same pattern as standard JS errors, at the expense of
       hiding some useful information */

    if (priv->is_prototype()) {
        descr = g_strdup(priv->format_name().c_str());
    } else {
        descr = g_strdup_printf("%s: %s", priv->format_name().c_str(),
                                priv->to_instance()->message());
    }

    return gjs_string_from_utf8(context, descr, rec.rval());
}

// JSNative implementation of `valueOf()`.
bool ErrorBase::value_of(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(context, argc, vp, rec, self);
    JS::RootedObject prototype(context);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    if (!gjs_object_require_property(context, self, "constructor",
                                     atoms.prototype(), &prototype)) {
        /* This error message will be more informative */
        JS_ClearPendingException(context);
        gjs_throw(context, "GLib.Error.valueOf() called on something that is not"
                  " a constructor");
        return false;
    }

    ErrorBase* priv;
    if (!for_js_typecheck(context, prototype, &priv, &rec))
        return false;

    rec.rval().setInt32(priv->domain());
    return true;
}

// clang-format off
const struct JSClassOps ErrorBase::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    &ErrorBase::finalize,
};

const struct JSClass ErrorBase::klass = {
    "GLib_Error",
    JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE |
        JSCLASS_IS_DOMJSCLASS,  // needed for Error.isError()
    &ErrorBase::class_ops
};

/* We need to shadow all fields of GError, to prevent calling the getter from GBoxed
   (which would trash memory accessing the instance private data) */
JSPropertySpec ErrorBase::proto_properties[] = {
    JS_PSG("domain", &ErrorBase::get_domain, GJS_MODULE_PROP_FLAGS),
    JS_PSG("code", &ErrorBase::get_code, GJS_MODULE_PROP_FLAGS),
    JS_PSG("message", &ErrorBase::get_message, GJS_MODULE_PROP_FLAGS),
    JS_PS_END
};

JSFunctionSpec ErrorBase::static_methods[] = {
    JS_FN("valueOf", &ErrorBase::value_of, 0, GJS_MODULE_PROP_FLAGS),
    JS_FS_END
};
// clang-format on

// Overrides GIWrapperPrototype::get_parent_proto().
bool ErrorPrototype::get_parent_proto(JSContext* cx,
                                      JS::MutableHandleObject proto) const {
    GI::Repository repo{};
    repo.require("GLib", "2.0").unwrap();

    GI::AutoBaseInfo glib_error_info{
        repo.find_by_name("GLib", "Error").value()};

    proto.set(gjs_lookup_generic_prototype(cx, glib_error_info));
    return !!proto;
}

bool ErrorPrototype::define_class(JSContext* context,
                                  JS::HandleObject in_object,
                                  const GI::EnumInfo info) {
    JS::RootedObject prototype(context), constructor(context);
    if (!ErrorPrototype::create_class(context, in_object, info, G_TYPE_ERROR,
                                      &constructor, &prototype))
        return false;

    // Define a toString() on the prototype, as it does not exist on the
    // prototype of GLib.Error; and create_class() will not define it since we
    // supply a parent in get_parent_proto().
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    return JS_DefineFunctionById(context, prototype, atoms.to_string(),
                                 &ErrorBase::to_string, 0,
                                 GJS_MODULE_PROP_FLAGS) &&
           gjs_define_enum_values(context, constructor, info);
}

[[nodiscard]]
static Maybe<const GI::EnumInfo> find_error_domain_info(
    const GI::Repository& repo, GQuark domain) {
    /* first an attempt without loading extra libraries */
    Maybe<GI::AutoEnumInfo> info = repo.find_by_error_domain(domain);
    if (info)
        return info;

    /* load standard stuff */
    repo.require("GLib", "2.0").unwrap();
    repo.require("GObject", "2.0").unwrap();
    repo.require("Gio", "2.0").unwrap();
    info = repo.find_by_error_domain(domain);
    if (info)
        return info;

    /* last attempt: load GIRepository (for invoke errors, rarely
       needed) */
    repo.require("GIRepository", "3.0").unwrap();
    return repo.find_by_error_domain(domain);
}

/* define properties that JS Error() expose, such as
   fileName, lineNumber and stack
*/
GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_error_properties(JSContext* cx, JS::HandleObject obj) {
    JS::RootedObject frame(cx);
    JS::RootedString stack(cx);
    JS::RootedString source(cx);
    uint32_t line;
    JS::TaggedColumnNumberOneOrigin tagged_column;

    if (!JS::CaptureCurrentStack(cx, &frame) ||
        !JS::BuildStackString(cx, nullptr, frame, &stack))
        return false;

    auto ok = JS::SavedFrameResult::Ok;
    if (JS::GetSavedFrameSource(cx, nullptr, frame, &source) != ok ||
        JS::GetSavedFrameLine(cx, nullptr, frame, &line) != ok ||
        JS::GetSavedFrameColumn(cx, nullptr, frame, &tagged_column) != ok) {
        gjs_throw(cx, "Error getting saved frame information");
        return false;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    return JS_DefinePropertyById(cx, obj, atoms.stack(), stack,
                                 JSPROP_ENUMERATE) &&
           JS_DefinePropertyById(cx, obj, atoms.file_name(), source,
                                 JSPROP_ENUMERATE) &&
           JS_DefinePropertyById(cx, obj, atoms.line_number(), line,
                                 JSPROP_ENUMERATE) &&
           JS_DefinePropertyById(cx, obj, atoms.column_number(),
                                 tagged_column.oneOriginValue(),
                                 JSPROP_ENUMERATE);
}

[[nodiscard]] static JSProtoKey proto_key_from_error_enum(int val) {
    switch (val) {
    case GJS_JS_ERROR_EVAL_ERROR:
        return JSProto_EvalError;
    case GJS_JS_ERROR_INTERNAL_ERROR:
        return JSProto_InternalError;
    case GJS_JS_ERROR_RANGE_ERROR:
        return JSProto_RangeError;
    case GJS_JS_ERROR_REFERENCE_ERROR:
        return JSProto_ReferenceError;
    case GJS_JS_ERROR_SYNTAX_ERROR:
        return JSProto_SyntaxError;
    case GJS_JS_ERROR_TYPE_ERROR:
        return JSProto_TypeError;
    case GJS_JS_ERROR_URI_ERROR:
        return JSProto_URIError;
    case GJS_JS_ERROR_ERROR:
    default:
        return JSProto_Error;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
gjs_error_from_js_gerror(JSContext *cx,
                         GError    *gerror)
{
    JS::RootedValueArray<1> error_args(cx);
    if (!gjs_string_from_utf8(cx, gerror->message, error_args[0]))
        return nullptr;

    JSProtoKey error_kind = proto_key_from_error_enum(gerror->code);
    JS::RootedObject error_constructor(cx);
    if (!JS_GetClassObject(cx, error_kind, &error_constructor))
        return nullptr;

    JS::RootedValue v_error_constructor(cx,
                                        JS::ObjectValue(*error_constructor));
    JS::RootedObject error(cx);
    if (!JS::Construct(cx, v_error_constructor, error_args, &error))
        return nullptr;

    return error;
}

JSObject* ErrorInstance::object_for_c_ptr(JSContext* context, GError* gerror) {
    if (!gerror)
        return nullptr;

    if (gerror->domain == GJS_JS_ERROR)
        return gjs_error_from_js_gerror(context, gerror);

    GI::Repository repo;
    Maybe<GI::AutoEnumInfo> info = find_error_domain_info(repo, gerror->domain);

    if (!info) {
        /* We don't have error domain metadata */
        /* Marshal the error as a plain GError */
        GI::AutoStructInfo glib_boxed{
            repo.find_by_name<GI::InfoTag::STRUCT>("GLib", "Error").value()};
        return StructInstance::new_for_c_struct(context, glib_boxed, gerror);
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED, "Wrapping struct %s with JSObject",
                      info->name());

    JS::RootedObject obj{context,
                         gjs_new_object_with_generic_prototype(context, *info)};
    if (!obj)
        return nullptr;

    ErrorInstance* priv = ErrorInstance::new_for_js_object(context, obj);
    priv->copy_gerror(gerror);

    return obj;
}

GError* ErrorBase::to_c_ptr(JSContext* cx, JS::HandleObject obj) {
    /* If this is a plain GBoxed (i.e. a GError without metadata),
       delegate marshalling.
    */
    if (StructBase::typecheck(cx, obj, G_TYPE_ERROR, GjsTypecheckNoThrow{}))
        return StructBase::to_c_ptr<GError>(cx, obj);

    return GIWrapperBase::to_c_ptr<GError>(cx, obj);
}

bool ErrorBase::transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                        GIArgument* arg,
                                        GIDirection transfer_direction,
                                        GITransfer transfer_ownership) {
    g_assert(transfer_direction != GI_DIRECTION_INOUT &&
             "transfer_to_gi_argument() must choose between in or out");

    if (!ErrorBase::typecheck(cx, obj)) {
        gjs_arg_unset(arg);
        return false;
    }

    gjs_arg_set(arg, ErrorBase::to_c_ptr(cx, obj));
    if (!gjs_arg_get<void*>(arg))
        return false;

    if ((transfer_direction == GI_DIRECTION_IN &&
         transfer_ownership != GI_TRANSFER_NOTHING) ||
        (transfer_direction == GI_DIRECTION_OUT &&
         transfer_ownership == GI_TRANSFER_EVERYTHING)) {
        gjs_arg_set(arg, ErrorInstance::copy_ptr(cx, G_TYPE_ERROR,
                                                 gjs_arg_get<void*>(arg)));
        if (!gjs_arg_get<void*>(arg))
            return false;
    }

    return true;
}

// Overrides GIWrapperBase::typecheck()
bool ErrorBase::typecheck(JSContext* cx, JS::HandleObject obj) {
    if (StructBase::typecheck(cx, obj, G_TYPE_ERROR, GjsTypecheckNoThrow{}))
        return true;
    return GIWrapperBase::typecheck(cx, obj, G_TYPE_ERROR);
}

bool ErrorBase::typecheck(JSContext* cx, JS::HandleObject obj,
                          GjsTypecheckNoThrow no_throw) {
    if (StructBase::typecheck(cx, obj, G_TYPE_ERROR, no_throw))
        return true;
    return GIWrapperBase::typecheck(cx, obj, G_TYPE_ERROR, no_throw);
}

GJS_JSAPI_RETURN_CONVENTION
static GError* gerror_from_error_impl(JSContext* cx, JS::HandleObject obj) {
    if (ErrorBase::typecheck(cx, obj, GjsTypecheckNoThrow())) {
        /* This is already a GError, just copy it */
        GError* inner = ErrorBase::to_c_ptr(cx, obj);
        if (!inner)
            return nullptr;
        return g_error_copy(inner);
    }

    /* Try to make something useful from the error
       name and message (in case this is a JS error) */
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue v_name(cx);
    if (!JS_GetPropertyById(cx, obj, atoms.name(), &v_name))
        return nullptr;

    JS::RootedValue v_message(cx);
    if (!JS_GetPropertyById(cx, obj, atoms.message(), &v_message))
        return nullptr;

    if (!v_name.isString() || !v_message.isString()) {
        return g_error_new_literal(
            GJS_JS_ERROR, GJS_JS_ERROR_ERROR,
            "Object thrown with unexpected name or message property");
    }

    JS::UniqueChars name = gjs_string_to_utf8(cx, v_name);
    if (!name)
        return nullptr;

    JS::UniqueChars message = gjs_string_to_utf8(cx, v_message);
    if (!message)
        return nullptr;

    Gjs::AutoTypeClass<GEnumClass> klass{GJS_TYPE_JS_ERROR};
    const GEnumValue *value = g_enum_get_value_by_name(klass, name.get());
    int code;
    if (value)
        code = value->value;
    else
        code = GJS_JS_ERROR_ERROR;

    return g_error_new_literal(GJS_JS_ERROR, code, message.get());
}

/*
 * gjs_gerror_make_from_thrown_value:
 *
 * Attempts to convert a JavaScript thrown value (pending on @cx) into a
 * #GError. This function is infallible and will always return a #GError with
 * some message, even if the exception value couldn't be converted.
 *
 * Clears the pending exception on @cx.
 *
 * Returns: (transfer full): a new #GError
 */
GError* gjs_gerror_make_from_thrown_value(JSContext* cx) {
    g_assert(JS_IsExceptionPending(cx) &&
             "Should be called when an exception is pending");

    JS::RootedValue exc(cx);
    JS_GetPendingException(cx, &exc);
    JS_ClearPendingException(cx);  // don't log

    if (!exc.isObject()) {
        return g_error_new(GJS_JS_ERROR, GJS_JS_ERROR_ERROR,
                           "Non-exception %s value %s thrown",
                           JS::InformalValueTypeName(exc),
                           gjs_debug_value(exc).c_str());
    }

    JS::RootedObject obj(cx, &exc.toObject());
    GError* retval = gerror_from_error_impl(cx, obj);
    if (retval)
        return retval;

    // Make a GError with an InternalError even if it wasn't possible to convert
    // the exception into one
    gjs_log_exception(cx);  // log the inner exception
    return g_error_new_literal(GJS_JS_ERROR, GJS_JS_ERROR_INTERNAL_ERROR,
                               "Failed to convert JS thrown value into GError");
}

/*
 * gjs_throw_gerror:
 *
 * Converts a GError into a JavaScript exception.
 * Differently from gjs_throw(), it will overwrite an existing exception, as it
 * is used to report errors from C functions.
 *
 * Returns: false, for convenience in returning from the calling function.
 */
bool gjs_throw_gerror(JSContext* cx, Gjs::AutoError const& error) {
    // return false even if the GError is null, as presumably something failed
    // in the calling code, and the caller expects to throw.
    g_return_val_if_fail(error, false);

    JS::RootedObject err_obj(cx, ErrorInstance::object_for_c_ptr(cx, error));
    if (!err_obj || !gjs_define_error_properties(cx, err_obj))
        return false;

    JS::RootedValue err(cx, JS::ObjectValue(*err_obj));
    JS_SetPendingException(cx, err);

    return false;
}
