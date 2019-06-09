/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <string.h>

#include "boxed.h"
#include "enumeration.h"
#include "gerror.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem-private.h"
#include "repo.h"
#include "util/error.h"

#include <util/log.h>

#include <girepository.h>

ErrorPrototype::ErrorPrototype(GIEnumInfo* info, GType gtype)
    : GIWrapperPrototype(info, gtype),
      m_domain(g_quark_from_string(g_enum_info_get_error_domain(info))) {
    GJS_INC_COUNTER(gerror_prototype);
}

ErrorPrototype::~ErrorPrototype(void) { GJS_DEC_COUNTER(gerror_prototype); }

ErrorInstance::ErrorInstance(JSContext* cx, JS::HandleObject obj)
    : GIWrapperInstance(cx, obj) {
    GJS_INC_COUNTER(gerror_instance);
}

ErrorInstance::~ErrorInstance(void) {
    g_clear_error(&m_ptr);
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
    GJS_GET_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    args.rval().setInt32(priv->domain());
    return true;
}

// JSNative property getter for `message`.
bool ErrorBase::get_message(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    if (!priv->check_is_instance(cx, "get a field"))
        return false;

    return gjs_string_from_utf8(cx, priv->to_instance()->message(),
                                args.rval());
}

// JSNative property getter for `code`.
bool ErrorBase::get_code(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_WRAPPER_PRIV(cx, argc, vp, args, obj, ErrorBase, priv);
    if (!priv->check_is_instance(cx, "get a field"))
        return false;

    args.rval().setInt32(priv->to_instance()->code());
    return true;
}

// JSNative implementation of `toString()`.
bool ErrorBase::to_string(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(context, argc, vp, rec, self);

    GjsAutoChar descr;

    // An error created via `new GLib.Error` will have a Boxed* private pointer,
    // not an Error*, so we can't call regular to_string() on it.
    if (BoxedBase::typecheck(context, self, nullptr, G_TYPE_ERROR,
                             GjsTypecheckNoThrow())) {
        auto* gerror = BoxedBase::to_c_ptr<GError>(context, self);
        if (!gerror)
            return false;
        descr =
            g_strdup_printf("GLib.Error %s: %s",
                            g_quark_to_string(gerror->domain), gerror->message);

        return gjs_string_from_utf8(context, descr, rec.rval());
    }

    ErrorBase* priv = ErrorBase::for_js_typecheck(context, self, rec);
    if (priv == NULL)
        return false;

    /* We follow the same pattern as standard JS errors, at the expense of
       hiding some useful information */

    if (priv->is_prototype()) {
        descr = g_strdup_printf("%s.%s", priv->ns(), priv->name());
    } else {
        descr = g_strdup_printf("%s.%s: %s", priv->ns(), priv->name(),
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

    ErrorBase* priv = ErrorBase::for_js_typecheck(context, prototype, rec);
    if (priv == NULL)
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
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
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
    g_irepository_require(nullptr, "GLib", "2.0", GIRepositoryLoadFlags(0),
                          nullptr);
    GjsAutoStructInfo glib_error_info =
        g_irepository_find_by_name(nullptr, "GLib", "Error");
    proto.set(gjs_lookup_generic_prototype(cx, glib_error_info));
    return !!proto;
}

bool ErrorPrototype::define_class(JSContext* context,
                                  JS::HandleObject in_object,
                                  GIEnumInfo* info) {
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

GJS_USE
static GIEnumInfo *
find_error_domain_info(GQuark domain)
{
    GIEnumInfo *info;

    /* first an attempt without loading extra libraries */
    info = g_irepository_find_by_error_domain(NULL, domain);
    if (info)
        return info;

    /* load standard stuff */
    g_irepository_require(NULL, "GLib", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    g_irepository_require(NULL, "GObject", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    g_irepository_require(NULL, "Gio", "2.0", (GIRepositoryLoadFlags) 0, NULL);
    info = g_irepository_find_by_error_domain(NULL, domain);
    if (info)
        return info;

    /* last attempt: load GIRepository (for invoke errors, rarely
       needed) */
    g_irepository_require(NULL, "GIRepository", "1.0", (GIRepositoryLoadFlags) 0, NULL);
    info = g_irepository_find_by_error_domain(NULL, domain);

    return info;
}

/* define properties that JS Error() expose, such as
   fileName, lineNumber and stack
*/
GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_error_properties(JSContext* cx, JS::HandleObject obj) {
    JS::RootedObject frame(cx);
    JS::RootedString stack(cx);
    JS::RootedString source(cx);
    uint32_t line, column;

    if (!JS::CaptureCurrentStack(cx, &frame) ||
        !JS::BuildStackString(cx, frame, &stack))
        return false;

    auto ok = JS::SavedFrameResult::Ok;
    if (JS::GetSavedFrameSource(cx, frame, &source) != ok ||
        JS::GetSavedFrameLine(cx, frame, &line) != ok ||
        JS::GetSavedFrameColumn(cx, frame, &column) != ok) {
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
           JS_DefinePropertyById(cx, obj, atoms.column_number(), column,
                                 JSPROP_ENUMERATE);
}

GJS_USE
static JSProtoKey
proto_key_from_error_enum(int val)
{
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
    JS::AutoValueArray<1> error_args(cx);
    if (!gjs_string_from_utf8(cx, gerror->message, error_args[0]))
        return nullptr;

    JSProtoKey error_kind = proto_key_from_error_enum(gerror->code);
    JS::RootedObject error_constructor(cx);
    if (!JS_GetClassObject(cx, error_kind, &error_constructor))
        return nullptr;

    return JS_New(cx, error_constructor, error_args);
}

JSObject* ErrorInstance::object_for_c_ptr(JSContext* context, GError* gerror) {
    GIEnumInfo *info;

    if (gerror == NULL)
        return NULL;

    if (gerror->domain == GJS_JS_ERROR)
        return gjs_error_from_js_gerror(context, gerror);

    info = find_error_domain_info(gerror->domain);

    if (!info) {
        /* We don't have error domain metadata */
        /* Marshal the error as a plain GError */
        GIBaseInfo *glib_boxed;
        JSObject *retval;

        glib_boxed = g_irepository_find_by_name(NULL, "GLib", "Error");
        retval = BoxedInstance::new_for_c_struct(context, glib_boxed, gerror);

        g_base_info_unref(glib_boxed);
        return retval;
    }

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info));

    JS::RootedObject obj(context,
                         gjs_new_object_with_generic_prototype(context, info));
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
    if (BoxedBase::typecheck(cx, obj, nullptr, G_TYPE_ERROR,
                             GjsTypecheckNoThrow()))
        return BoxedBase::to_c_ptr<GError>(cx, obj);

    return GIWrapperBase::to_c_ptr<GError>(cx, obj);
}

bool ErrorBase::transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                        GIArgument* arg,
                                        GIDirection transfer_direction,
                                        GITransfer transfer_ownership) {
    g_assert(transfer_direction != GI_DIRECTION_INOUT &&
             "transfer_to_gi_argument() must choose between in or out");

    if (!ErrorBase::typecheck(cx, obj)) {
        arg->v_pointer = nullptr;
        return false;
    }

    arg->v_pointer = ErrorBase::to_c_ptr(cx, obj);
    if (!arg->v_pointer)
        return false;

    if ((transfer_direction == GI_DIRECTION_IN &&
         transfer_ownership != GI_TRANSFER_NOTHING) ||
        (transfer_direction == GI_DIRECTION_OUT &&
         transfer_ownership == GI_TRANSFER_EVERYTHING)) {
        arg->v_pointer =
            ErrorInstance::copy_ptr(cx, G_TYPE_ERROR, arg->v_pointer);
        if (!arg->v_pointer)
            return false;
    }

    return true;
}

// Overrides GIWrapperBase::typecheck()
bool ErrorBase::typecheck(JSContext* cx, JS::HandleObject obj) {
    if (BoxedBase::typecheck(cx, obj, nullptr, G_TYPE_ERROR,
                             GjsTypecheckNoThrow()))
        return true;
    return GIWrapperBase::typecheck(cx, obj, nullptr, G_TYPE_ERROR);
}

bool ErrorBase::typecheck(JSContext* cx, JS::HandleObject obj,
                          GjsTypecheckNoThrow no_throw) {
    if (BoxedBase::typecheck(cx, obj, nullptr, G_TYPE_ERROR, no_throw))
        return true;
    return GIWrapperBase::typecheck(cx, obj, nullptr, G_TYPE_ERROR, no_throw);
}

GError *
gjs_gerror_make_from_error(JSContext       *cx,
                           JS::HandleObject obj)
{
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

    JS::UniqueChars name;
    if (!gjs_string_to_utf8(cx, v_name, &name))
        return nullptr;

    JS::RootedValue v_message(cx);
    if (!JS_GetPropertyById(cx, obj, atoms.message(), &v_message))
        return nullptr;

    JS::UniqueChars message;
    if (!gjs_string_to_utf8(cx, v_message, &message))
        return nullptr;

    GjsAutoTypeClass<GEnumClass> klass(GJS_TYPE_JS_ERROR);
    const GEnumValue *value = g_enum_get_value_by_name(klass, name.get());
    int code;
    if (value)
        code = value->value;
    else
        code = GJS_JS_ERROR_ERROR;

    return g_error_new_literal(GJS_JS_ERROR, code, message.get());
}

/*
 * gjs_throw_gerror:
 *
 * Converts a GError into a JavaScript exception, and frees the GError.
 * Differently from gjs_throw(), it will overwrite an existing exception, as it
 * is used to report errors from C functions.
 *
 * Returns: false, for convenience in returning from the calling function.
 */
bool gjs_throw_gerror(JSContext* cx, GError* error) {
    // return false even if the GError is null, as presumably something failed
    // in the calling code, and the caller expects to throw.
    g_return_val_if_fail(error, false);

    JSAutoRequest ar(cx);

    JS::RootedObject err_obj(cx, ErrorInstance::object_for_c_ptr(cx, error));
    if (!err_obj || !gjs_define_error_properties(cx, err_obj))
        return false;

    g_error_free(error);

    JS::RootedValue err(cx, JS::ObjectValue(*err_obj));
    JS_SetPendingException(cx, err);

    return false;
}
