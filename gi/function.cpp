/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stddef.h>  // for NULL, size_t
#include <stdint.h>

#include <memory>  // for unique_ptr
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>  // for move
#include <vector>

#ifndef G_DISABLE_ASSERT
#    include <limits>  // for numeric_limits
#endif

#include <ffi.h>
#include <girepository/girepository.h>
#include <girepository/girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/Exception.h>
#include <js/HeapAPI.h>  // for RuntimeHeapIsCollecting
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetRealmFunctionPrototype
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>    // for HandleValueArray
#include <jspubtd.h>  // for JSProtoKey
#include <mozilla/Maybe.h>

#ifndef G_DISABLE_ASSERT
#    include <js/CallAndConstruct.h>  // for IsCallable
#endif

#include "gi/arg-cache.h"
#include "gi/arg-inl.h"
#include "gi/arg-types-inl.h"
#include "gi/arg.h"
#include "gi/closure.h"
#include "gi/cwrapper.h"
#include "gi/function.h"
#include "gi/gerror.h"
#include "gi/info.h"
#include "gi/object.h"
#include "gi/utils-inl.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/gerror-result.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "gjs/profiler-private.h"
#include "util/log.h"

using mozilla::Maybe, mozilla::Some;

/* We use guint8 for arguments; functions can't
 * have more than this.
 */
#define GJS_ARG_INDEX_INVALID G_MAXUINT8

namespace Gjs {

class Function : public CWrapper<Function> {
    friend CWrapperPointerOps<Function>;
    friend CWrapper<Function>;

    static constexpr auto PROTOTYPE_SLOT = GjsGlobalSlot::PROTOTYPE_function;
    static constexpr GjsDebugTopic DEBUG_TOPIC = GJS_DEBUG_GFUNCTION;

    GI::AutoCallableInfo m_info;

    ArgsCache m_arguments;

    uint8_t m_js_in_argc;
    uint8_t m_js_out_argc;
    GIFunctionInvoker m_invoker;

    explicit Function(const GI::CallableInfo info)
        : m_info(info), m_js_in_argc(0), m_js_out_argc(0), m_invoker({}) {
        GJS_INC_COUNTER(function);
    }
    ~Function();

    GJS_JSAPI_RETURN_CONVENTION
    bool init(JSContext* cx, GType gtype = G_TYPE_NONE);

    /**
     * Like CWrapperPointerOps::for_js_typecheck(), but additionally checks that
     * the pointer is not null, which is the case for prototype objects.
     */
    GJS_JSAPI_RETURN_CONVENTION
    static bool for_js_instance(JSContext* cx, JS::HandleObject obj,
                                Function** out, JS::CallArgs* args) {
        Function* priv;
        if (!Function::for_js_typecheck(cx, obj, &priv, args))
            return false;
        if (!priv) {
            // This is the prototype
            gjs_throw(cx, "Impossible on prototype; only on instances");
            return false;
        }
        *out = priv;
        return true;
    }

    GJS_JSAPI_RETURN_CONVENTION
    static bool call(JSContext* cx, unsigned argc, JS::Value* vp);

    static void finalize_impl(JS::GCContext*, Function* priv);

    GJS_JSAPI_RETURN_CONVENTION
    static bool get_length(JSContext* cx, unsigned argc, JS::Value* vp);

    GJS_JSAPI_RETURN_CONVENTION
    static bool get_name(JSContext* cx, unsigned argc, JS::Value* vp);

    GJS_JSAPI_RETURN_CONVENTION
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp);

    GJS_JSAPI_RETURN_CONVENTION
    bool to_string_impl(JSContext* cx, JS::MutableHandleValue rval);

    GJS_JSAPI_RETURN_CONVENTION
    bool finish_invoke(JSContext* cx, const JS::CallArgs& args,
                       GjsFunctionCallState* state,
                       GIArgument* r_value = nullptr);

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* inherit_builtin_function(JSContext* cx, JSProtoKey) {
        JS::RootedObject builtin_function_proto(
            cx, JS::GetRealmFunctionPrototype(cx));
        return JS_NewObjectWithGivenProto(cx, nullptr, builtin_function_proto);
    }

    static const JSClassOps class_ops;
    static const JSPropertySpec proto_props[];
    static const JSFunctionSpec proto_funcs[];

    static constexpr js::ClassSpec class_spec = {
        nullptr,  // createConstructor
        &Function::inherit_builtin_function,
        nullptr,  // constructorFunctions
        nullptr,  // constructorProperties
        Function::proto_funcs,
        Function::proto_props,
        nullptr,  // finishInit
        js::ClassSpec::DontDefineConstructor};

    static constexpr JSClass klass = {
        "GIRepositoryFunction",
        JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_BACKGROUND_FINALIZE,
        &Function::class_ops, &Function::class_spec};

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* create(JSContext*, GType, const GI::CallableInfo);

    [[nodiscard]] std::string format_name();

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke(JSContext* cx, const JS::CallArgs& args,
                JS::HandleObject this_obj = nullptr,
                GIArgument* r_value = nullptr);

    GJS_JSAPI_RETURN_CONVENTION
    static bool invoke_constructor_uncached(JSContext* cx,
                                            const GI::FunctionInfo info,
                                            JS::HandleObject obj,
                                            const JS::CallArgs& args,
                                            GIArgument* rvalue) {
        Function function(info);
        if (!function.init(cx))
            return false;
        return function.invoke(cx, args, obj, rvalue);
    }
};

}  // namespace Gjs

template <typename TAG>
static inline void set_ffi_arg(void* result, GIArgument* value) {
    using T = Gjs::Tag::RealT<TAG>;
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        *static_cast<ffi_sarg*>(result) = gjs_arg_get<TAG>(value);
    } else if constexpr (std::is_floating_point_v<T> || std::is_unsigned_v<T>) {
        *static_cast<ffi_arg*>(result) = gjs_arg_get<TAG>(value);
    } else if constexpr (std::is_pointer_v<T>) {
        *static_cast<ffi_arg*>(result) =
            gjs_pointer_to_int<ffi_arg>(gjs_arg_get<TAG>(value));
    }
}

static void set_return_ffi_arg_from_gi_argument(const GI::TypeInfo ret_type,
                                                void* result,
                                                GIArgument* return_value) {
    // Be consistent with gjs_value_to_gi_argument()
    switch (ret_type.tag()) {
    case GI_TYPE_TAG_VOID:
        g_assert_not_reached();
    case GI_TYPE_TAG_INT8:
        set_ffi_arg<int8_t>(result, return_value);
        break;
    case GI_TYPE_TAG_UINT8:
        set_ffi_arg<uint8_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INT16:
        set_ffi_arg<int16_t>(result, return_value);
        break;
    case GI_TYPE_TAG_UINT16:
        set_ffi_arg<uint16_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INT32:
        set_ffi_arg<int32_t>(result, return_value);
        break;
    case GI_TYPE_TAG_UINT32:
        set_ffi_arg<uint32_t>(result, return_value);
        break;
    case GI_TYPE_TAG_BOOLEAN:
        set_ffi_arg<Gjs::Tag::GBoolean>(result, return_value);
        break;
    case GI_TYPE_TAG_UNICHAR:
        set_ffi_arg<char32_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INT64:
        set_ffi_arg<int64_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INTERFACE:
        if (ret_type.interface().is_enum_or_flags())
            set_ffi_arg<Gjs::Tag::Enum>(result, return_value);
        else
            set_ffi_arg<void*>(result, return_value);
        break;
    case GI_TYPE_TAG_UINT64:
        // Other primitive types need to squeeze into 64-bit ffi_arg too
        set_ffi_arg<uint64_t>(result, return_value);
        break;
    case GI_TYPE_TAG_FLOAT:
        set_ffi_arg<float>(result, return_value);
        break;
    case GI_TYPE_TAG_DOUBLE:
        set_ffi_arg<double>(result, return_value);
        break;
    case GI_TYPE_TAG_GTYPE:
        set_ffi_arg<Gjs::Tag::GType>(result, return_value);
        break;
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
        set_ffi_arg<char*>(result, return_value);
        break;
    case GI_TYPE_TAG_ARRAY:
    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GHASH:
    case GI_TYPE_TAG_ERROR:
    default:
        set_ffi_arg<void*>(result, return_value);
        break;
    }
}

void GjsCallbackTrampoline::warn_about_illegal_js_callback(const char* when,
                                                           const char* reason,
                                                           bool        dump_stack) {
    std::ostringstream message;

    message << "Attempting to run a JS callback " << when << ". "
            << "This is most likely caused by " << reason << ". "
            << "Because it would crash the application, it has been blocked.\n"
            << "The offending callback was " << m_info.name() << "()"
            << (m_is_vfunc ? ", a vfunc." : ".");

    if (dump_stack) {
        message << "\n" << gjs_dumpstack_string();
    }
    g_critical("%s", message.str().c_str());
}

/* This is our main entry point for ffi_closure callbacks.
 * ffi_prep_closure is doing pure magic and replaces the original
 * function call with this one which gives us the ffi arguments,
 * a place to store the return value and our use data.
 * In other words, everything we need to call the JS function and
 * getting the return value back.
 */
void GjsCallbackTrampoline::callback_closure(GIArgument** args, void* result) {
    GI::StackTypeInfo ret_type;

    // Fill in the result with some hopefully neutral value
    m_info.load_return_type(&ret_type);
    if (ret_type.tag() != GI_TYPE_TAG_VOID) {
        GIArgument argument = {};
        gjs_arg_unset(&argument);
        set_return_ffi_arg_from_gi_argument(ret_type, result, &argument);
    }

    if (G_UNLIKELY(!is_valid())) {
        warn_about_illegal_js_callback(
            "during shutdown",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs",
            true);
        return;
    }

    JSContext* context = this->context();
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    if (JS::RuntimeHeapIsCollecting()) {
        warn_about_illegal_js_callback(
            "during garbage collection",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs",
            true);
        return;
    }

    if (G_UNLIKELY(!gjs->is_owner_thread())) {
        warn_about_illegal_js_callback("on a different thread",
                                       "an API not intended to be used in JS",
                                       false);
        return;
    }

    JSAutoRealm ar(context, callable());

    unsigned n_args = m_info.n_args();

    struct AutoCallbackData {
        AutoCallbackData(GjsCallbackTrampoline* trampoline,
                         GjsContextPrivate* gjs)
            : trampoline(trampoline), gjs(gjs) {}
        ~AutoCallbackData() {
            if (trampoline->m_scope == GI_SCOPE_TYPE_ASYNC) {
                // We don't release the trampoline here as we've an extra ref
                // that has been set in gjs_marshal_callback_in()
                gjs_debug_closure("Saving async closure for gc cleanup %p",
                                  trampoline);
                gjs->async_closure_enqueue_for_gc(trampoline);
            }
            gjs->schedule_gc_if_needed();
        }

        GjsCallbackTrampoline* trampoline;
        GjsContextPrivate* gjs;
    };

    AutoCallbackData callback_data(this, gjs);
    JS::RootedObject this_object(context);
    unsigned c_args_offset = 0;
    GObject* gobj = nullptr;
    if (m_is_vfunc) {
        gobj = G_OBJECT(gjs_arg_get<GObject*>(args[0]));
        if (gobj) {
            this_object = ObjectInstance::wrapper_from_gobject(context, gobj);
            if (!this_object) {
                if (g_object_get_qdata(gobj, ObjectBase::disposed_quark())) {
                    warn_about_illegal_js_callback(
                        "on disposed object",
                        "using the destroy(), dispose(), or remove() vfuncs",
                        false);
                }
                gjs_log_exception(context);
                return;
            }
        }

        /* "this" is not included in the GI signature, but is in the C (and
         * FFI) signature */
        c_args_offset = 1;
    }

    JS::RootedValue rval(context);

    if (!callback_closure_inner(context, this_object, gobj, &rval, args,
                                ret_type, n_args, c_args_offset, result)) {
        if (!JS_IsExceptionPending(context)) {
            // "Uncatchable" exception thrown, we have to exit. We may be in a
            // main loop, or maybe not, but there's no way to tell, so we have
            // to exit here instead of propagating the exception back to the
            // original calling JS code.
            uint8_t code;
            if (gjs->should_exit(&code))
                gjs->exit_immediately(code);

            // Some other uncatchable exception, e.g. out of memory
            g_error("Call to %s (%s.%s) terminated with uncatchable exception",
                    gjs_debug_callable(callable()).c_str(), m_info.ns(),
                    m_info.name());
        }

        // If the callback has a GError** argument, then make a GError from the
        // value that was thrown. Otherwise, log it as "uncaught" (critical
        // instead of warning)

        if (!m_info.can_throw_gerror()) {
            gjs_log_exception_uncaught(context);
            return;
        }

        // The GError** pointer is the last argument, and is not included in
        // the n_args
        GIArgument* error_argument = args[n_args + c_args_offset];
        auto* gerror = gjs_arg_get<GError**>(error_argument);
        GError* local_error = gjs_gerror_make_from_thrown_value(context);
        g_propagate_error(gerror, local_error);
    }
}

inline GIArgument* get_argument_for_arg_info(const GI::ArgInfo arg_info,
                                             GIArgument** args, int index) {
    if (!arg_info.caller_allocates())
        return *reinterpret_cast<GIArgument**>(args[index]);
    else
        return args[index];
}

bool GjsCallbackTrampoline::callback_closure_inner(
    JSContext* context, JS::HandleObject this_object, GObject* gobject,
    JS::MutableHandleValue rval, GIArgument** args, const GI::TypeInfo ret_type,
    unsigned n_args, unsigned c_args_offset, void* result) {
    unsigned n_outargs = 0;
    JS::RootedValueVector jsargs(context);

    if (!jsargs.reserve(n_args))
        g_error("Unable to reserve space for vector");

    GITypeTag ret_tag = ret_type.tag();
    bool ret_type_is_void = ret_tag == GI_TYPE_TAG_VOID;
    bool in_args_to_cleanup = false;

    for (unsigned i = 0, n_jsargs = 0; i < n_args; i++) {
        GI::StackArgInfo arg_info;
        GI::StackTypeInfo type_info;
        GjsParamType param_type;

        m_info.load_arg(i, &arg_info);
        arg_info.load_type(&type_info);

        /* Skip void * arguments */
        if (type_info.tag() == GI_TYPE_TAG_VOID)
            continue;

        if (arg_info.direction() == GI_DIRECTION_OUT) {
            n_outargs++;
            continue;
        }

        if (arg_info.direction() == GI_DIRECTION_INOUT)
            n_outargs++;

        if (arg_info.ownership_transfer() != GI_TRANSFER_NOTHING)
            in_args_to_cleanup = m_scope != GI_SCOPE_TYPE_FOREVER;

        param_type = m_param_types[i];

        switch (param_type) {
            case PARAM_SKIPPED:
                continue;
            case PARAM_ARRAY: {
                // In initialize(), we already don't store PARAM_ARRAY for non-
                // fixed-size arrays
                unsigned array_length_pos =
                    type_info.array_length_index().value();

                GI::StackArgInfo array_length_arg;
                GI::StackTypeInfo arg_type_info;

                m_info.load_arg(array_length_pos, &array_length_arg);
                array_length_arg.load_type(&arg_type_info);
                size_t length = gjs_gi_argument_get_array_length(
                    arg_type_info.tag(),
                    args[array_length_pos + c_args_offset]);

                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                if (!gjs_value_from_explicit_array(
                        context, jsargs[n_jsargs++], type_info,
                        arg_info.ownership_transfer(), args[i + c_args_offset],
                        length))
                    return false;
                break;
            }
            case PARAM_NORMAL: {
                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                GIArgument* arg = args[i + c_args_offset];
                if (arg_info.direction() == GI_DIRECTION_INOUT &&
                    !arg_info.caller_allocates())
                    arg = *reinterpret_cast<GIArgument**>(arg);

                if (!gjs_value_from_gi_argument(context, jsargs[n_jsargs++],
                                                type_info, arg, false))
                    return false;
                break;
            }
            case PARAM_CALLBACK:
                /* Callbacks that accept another callback as a parameter are not
                 * supported, see gjs_callback_trampoline_new() */
            case PARAM_UNKNOWN:
                // PARAM_UNKNOWN is currently not ever set on a callback's args.
            default:
                g_assert_not_reached();
        }
    }

    if (!invoke(this_object, jsargs, rval))
        return false;

    if (n_outargs == 0 && ret_type_is_void) {
        /* void return value, no out args, nothing to do */
    } else if (n_outargs == 0) {
        GIArgument argument;

        GITransfer transfer = m_info.caller_owns();
        /* non-void return value, no out args. Should
         * be a single return value. */
        if (!gjs_value_to_gi_argument(context, rval, ret_type, "callback",
                                      GJS_ARGUMENT_RETURN_VALUE, transfer,
                                      GjsArgumentFlags::MAY_BE_NULL, &argument))
            return false;

        set_return_ffi_arg_from_gi_argument(ret_type, result, &argument);
    } else if (n_outargs == 1 && ret_type_is_void) {
        /* void return value, one out args. Should
         * be a single return value. */
        for (unsigned i = 0; i < n_args; i++) {
            GI::StackArgInfo arg_info;
            m_info.load_arg(i, &arg_info);
            if (arg_info.direction() == GI_DIRECTION_IN)
                continue;

            if (!gjs_value_to_callback_out_arg(
                    context, rval, arg_info,
                    get_argument_for_arg_info(arg_info, args,
                                              i + c_args_offset)))
                return false;

            break;
        }
    } else {
        bool is_array = rval.isObject();
        if (!JS::IsArrayObject(context, rval, &is_array))
            return false;

        if (!is_array) {
            gjs_throw(context,
                      "Call to %s (%s.%s) returned unexpected value, expecting "
                      "an Array",
                      gjs_debug_callable(callable()).c_str(), m_info.ns(),
                      m_info.name());
            return false;
        }

        JS::RootedValue elem(context);
        JS::RootedObject out_array(context, rval.toObjectOrNull());
        gsize elem_idx = 0;
        /* more than one of a return value or an out argument.
         * Should be an array of output values. */

        if (!ret_type_is_void) {
            GIArgument argument;
            GITransfer transfer = m_info.caller_owns();

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                return false;

            if (!gjs_value_to_gi_argument(context, elem, ret_type, "callback",
                                          GJS_ARGUMENT_RETURN_VALUE, transfer,
                                          GjsArgumentFlags::MAY_BE_NULL,
                                          &argument))
                return false;

            if ((ret_tag == GI_TYPE_TAG_FILENAME ||
                 ret_tag == GI_TYPE_TAG_UTF8) &&
                transfer == GI_TRANSFER_NOTHING) {
                // We duplicated the string so not to leak we need to both
                // ensure that the string is bound to the object lifetime or
                // created once
                if (gobject) {
                    ObjectInstance::associate_string(
                        gobject, gjs_arg_get<char*>(&argument));
                } else {
                    Gjs::AutoChar str{gjs_arg_steal<char*>(&argument)};
                    gjs_arg_set(&argument, g_intern_string(str));
                }
            }

            set_return_ffi_arg_from_gi_argument(ret_type, result, &argument);

            elem_idx++;
        }

        for (unsigned i = 0; i < n_args; i++) {
            GI::StackArgInfo arg_info;
            m_info.load_arg(i, &arg_info);
            if (arg_info.direction() == GI_DIRECTION_IN)
                continue;

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                return false;

            if (!gjs_value_to_callback_out_arg(
                    context, elem, arg_info,
                    get_argument_for_arg_info(arg_info, args,
                                              i + c_args_offset)))
                return false;

            elem_idx++;
        }
    }

    if (!in_args_to_cleanup)
        return true;

    for (unsigned i = 0; i < n_args; i++) {
        GI::StackArgInfo arg_info;
        m_info.load_arg(i, &arg_info);
        GITransfer transfer = arg_info.ownership_transfer();

        if (transfer == GI_TRANSFER_NOTHING)
            continue;

        if (arg_info.direction() != GI_DIRECTION_IN)
            continue;

        GIArgument* arg = args[i + c_args_offset];
        if (m_scope == GI_SCOPE_TYPE_CALL) {
            GI::StackTypeInfo type_info;
            arg_info.load_type(&type_info);

            if (!gjs_gi_argument_release(context, transfer, type_info, arg))
                return false;

            continue;
        }

        struct InvalidateData {
            GI::StackArgInfo arg_info;
            GIArgument arg;
        };

        auto* data = new InvalidateData({std::move(arg_info), *arg});
        g_closure_add_invalidate_notifier(
            this, data, [](void* invalidate_data, GClosure* c) {
                auto* self = static_cast<GjsCallbackTrampoline*>(c);
                std::unique_ptr<InvalidateData> data(
                    static_cast<InvalidateData*>(invalidate_data));
                GITransfer transfer = data->arg_info.ownership_transfer();

                GI::StackTypeInfo type_info;
                data->arg_info.load_type(&type_info);
                if (!gjs_gi_argument_release(self->context(), transfer,
                                             type_info, &data->arg)) {
                    gjs_throw(self->context(),
                              "Impossible to release closure argument '%s'",
                              data->arg_info.name());
                }
            });
    }

    return true;
}

GjsCallbackTrampoline* GjsCallbackTrampoline::create(
    JSContext* cx, JS::HandleObject callable,
    const GI::CallableInfo callable_info, GIScopeType scope,
    bool has_scope_object, bool is_vfunc) {
    g_assert(JS::IsCallable(callable) &&
             "tried to create a callback trampoline for a non-callable object");

    auto* trampoline = new GjsCallbackTrampoline(
        cx, callable, callable_info, scope, has_scope_object, is_vfunc);

    if (!trampoline->initialize()) {
        g_closure_unref(trampoline);
        return nullptr;
    }

    return trampoline;
}

decltype(GjsCallbackTrampoline::s_forever_closure_list)
    GjsCallbackTrampoline::s_forever_closure_list;

GjsCallbackTrampoline::GjsCallbackTrampoline(
    // optional?
    JSContext* cx, JS::HandleObject callable,
    const GI::CallableInfo callable_info, GIScopeType scope,
    bool has_scope_object, bool is_vfunc)
    // The rooting rule is:
    // - notify callbacks in GObject methods are traced from the scope object
    // - async and call callbacks, and other notify callbacks, are rooted
    // - vfuncs are traced from the GObject prototype
    : Closure(cx, callable,
              scope != GI_SCOPE_TYPE_NOTIFIED || !has_scope_object,
              callable_info.name()),
      m_info(callable_info),
      m_param_types(std::make_unique<GjsParamType[]>(callable_info.n_args())),
      m_scope(scope),
      m_is_vfunc(is_vfunc) {
    add_finalize_notifier<GjsCallbackTrampoline>();
}

GjsCallbackTrampoline::~GjsCallbackTrampoline() {
    if (m_closure)
        m_info.destroy_closure(m_closure);
}

void GjsCallbackTrampoline::mark_forever() {
    s_forever_closure_list.emplace_back(this, Gjs::TakeOwnership{});
}

void GjsCallbackTrampoline::prepare_shutdown() {
    s_forever_closure_list.clear();
}

ffi_closure* GjsCallbackTrampoline::create_closure() {
    auto callback = [](ffi_cif*, void* result, void** ffi_args, void* data) {
        auto** args = reinterpret_cast<GIArgument**>(ffi_args);
        g_assert(data && "Trampoline data is not set");
        Gjs::Closure::Ptr trampoline{static_cast<GjsCallbackTrampoline*>(data),
                                     Gjs::TakeOwnership{}};

        trampoline.as<GjsCallbackTrampoline>()->callback_closure(args, result);
    };

    return m_info.create_closure(&m_cif, callback, this);
}

bool GjsCallbackTrampoline::initialize() {
    g_assert(is_valid());
    g_assert(!m_closure);

    /* Analyze param types and directions, similarly to
     * init_cached_function_data */
    unsigned n_param_types = m_info.n_args();
    for (unsigned i = 0; i < n_param_types; i++) {
        GI::StackArgInfo arg_info;
        GI::StackTypeInfo type_info;

        if (m_param_types[i] == PARAM_SKIPPED)
            continue;

        m_info.load_arg(i, &arg_info);
        arg_info.load_type(&type_info);

        GIDirection direction = arg_info.direction();
        GITypeTag type_tag = type_info.tag();

        if (direction != GI_DIRECTION_IN) {
            /* INOUT and OUT arguments are handled differently. */
            continue;
        }

        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            if (type_info.interface().is_callback()) {
                gjs_throw(context(),
                          "%s %s accepts another callback as a parameter. This "
                          "is not supported",
                          m_is_vfunc ? "VFunc" : "Callback", m_info.name());
                return false;
            }
        } else if (type_tag == GI_TYPE_TAG_ARRAY) {
            if (type_info.array_type() == GI_ARRAY_TYPE_C) {
                Maybe<unsigned> array_length_pos =
                    type_info.array_length_index();
                if (!array_length_pos)
                    continue;

                if (*array_length_pos < n_param_types) {
                    GI::StackArgInfo length_arg_info;
                    m_info.load_arg(*array_length_pos, &length_arg_info);

                    if (length_arg_info.direction() != direction) {
                        gjs_throw(context(),
                                  "%s %s has an array with different-direction "
                                  "length argument. This is not supported",
                                  m_is_vfunc ? "VFunc" : "Callback",
                                  m_info.name());
                        return false;
                    }

                    m_param_types[*array_length_pos] = PARAM_SKIPPED;
                    m_param_types[i] = PARAM_ARRAY;
                }
            }
        }
    }

    m_closure = create_closure();
    return true;
}

// Intended for error messages
std::string Gjs::Function::format_name() {
    bool is_method = m_info.is_method();
    std::string retval = is_method ? "method" : "function";
    retval += ' ';
    retval += m_info.ns();
    retval += '.';
    if (is_method) {
        retval += m_info.container()->name();
        retval += '.';
    }
    retval += m_info.name();
    return retval;
}

namespace Gjs {

static void* get_return_ffi_pointer_from_gi_argument(
    Maybe<Arg::ReturnTag> return_tag, GIFFIReturnValue* return_value) {
    if (!return_tag)
        return nullptr;
    if (return_tag->is_pointer())
        return &gjs_arg_member<void*>(return_value);
    switch (return_tag->tag()) {
        case GI_TYPE_TAG_VOID:
            g_assert_not_reached();
        case GI_TYPE_TAG_INT8:
            return &gjs_arg_member<int8_t>(return_value);
        case GI_TYPE_TAG_INT16:
            return &gjs_arg_member<int16_t>(return_value);
        case GI_TYPE_TAG_INT32:
            return &gjs_arg_member<int32_t>(return_value);
        case GI_TYPE_TAG_UINT8:
            return &gjs_arg_member<uint8_t>(return_value);
        case GI_TYPE_TAG_UINT16:
            return &gjs_arg_member<uint16_t>(return_value);
        case GI_TYPE_TAG_UINT32:
            return &gjs_arg_member<uint32_t>(return_value);
        case GI_TYPE_TAG_BOOLEAN:
            return &gjs_arg_member<Gjs::Tag::GBoolean>(return_value);
        case GI_TYPE_TAG_UNICHAR:
            return &gjs_arg_member<uint32_t>(return_value);
        case GI_TYPE_TAG_INT64:
            return &gjs_arg_member<int64_t>(return_value);
        case GI_TYPE_TAG_UINT64:
            return &gjs_arg_member<uint64_t>(return_value);
        case GI_TYPE_TAG_FLOAT:
            return &gjs_arg_member<float>(return_value);
        case GI_TYPE_TAG_DOUBLE:
            return &gjs_arg_member<double>(return_value);
        case GI_TYPE_TAG_INTERFACE: {
            if (return_tag->is_enum_or_flags_interface())
                return &gjs_arg_member<Gjs::Tag::UnsignedEnum>(return_value);
            [[fallthrough]];
        }
        default:
            return &gjs_arg_member<void*>(return_value);
    }
}

// This function can be called in two different ways. You can either use it to
// create JavaScript objects by calling it without @r_value, or you can decide
// to keep the return values in GIArgument format by providing a @r_value
// argument.
bool Function::invoke(JSContext* context, const JS::CallArgs& args,
                      JS::HandleObject this_obj /* = nullptr */,
                      GIArgument* r_value /* = nullptr */) {
    g_assert((args.isConstructing() || !this_obj) &&
             "If not a constructor, then pass the 'this' object via CallArgs");

    GIFFIReturnValue return_value;

    unsigned ffi_argc = m_invoker.cif.nargs;
    GjsFunctionCallState state(context, m_info);

    if (state.gi_argc > Argument::MAX_ARGS) {
        gjs_throw(context, "Function %s has too many arguments",
                  format_name().c_str());
        return false;
    }

    // ffi_argc is the number of arguments that the underlying C function takes.
    // state.gi_argc is the number of arguments the GICallableInfo describes
    // (which does not include "this" or GError**). m_js_in_argc is the number
    // of arguments we expect the JS function to take (which does not include
    // PARAM_SKIPPED args).
    // args.length() is the number of arguments that were actually passed.
    if (args.length() > m_js_in_argc) {
        if (!JS::WarnUTF8(context,
                          "Too many arguments to %s: expected %u, got %u",
                          format_name().c_str(), m_js_in_argc, args.length()))
            return false;
    } else if (args.length() < m_js_in_argc) {
        args.reportMoreArgsNeeded(context, format_name().c_str(), m_js_in_argc,
                                  args.length());
        return false;
    }

    // These arrays hold argument pointers.
    // - state.in_cvalue(): C values which are passed on input (in or inout)
    // - state.out_cvalue(): C values which are returned as arguments (out or
    //   inout)
    // - state.inout_original_cvalue(): For the special case of (inout) args,
    //   we need to keep track of the original values we passed into the
    //   function, in case we need to free it.
    // - ffi_arg_pointers: For passing data to FFI, we need to create another
    //   layer of indirection; this array is a pointer to an element in
    //   state.in_cvalue() or state.out_cvalue().
    // - return_value: The actual return value of the C function, i.e. not an
    //   (out) param
    //
    // The 3 GIArgument arrays are indexed by the GI argument index.
    // ffi_arg_pointers, on the other hand, represents the actual C arguments,
    // in the way ffi expects them.

    auto ffi_arg_pointers = std::make_unique<void*[]>(ffi_argc);

    int gi_arg_pos = 0;        // index into GIArgument array
    unsigned ffi_arg_pos = 0;  // index into ffi_arg_pointers
    unsigned js_arg_pos = 0;   // index into args

    JS::RootedObject obj(context, this_obj);
    if (!args.isConstructing() && !args.computeThis(context, &obj))
        return false;

    std::string dynamicString("(unknown)");

    if (state.is_method) {
        GIArgument* in_value = state.instance();
        JS::RootedValue in_js_value(context, JS::ObjectValue(*obj));

        if (!m_arguments.instance().value()->in(context, &state, in_value,
                                                in_js_value))
            return false;

        ffi_arg_pointers[ffi_arg_pos] = in_value;
        ++ffi_arg_pos;

        // Callback lifetimes will be attached to the instance object if it is
        // a GObject or GInterface
        Maybe<GType> gtype = m_arguments.instance_type();
        if (gtype) {
            if (g_type_is_a(*gtype, G_TYPE_OBJECT) ||
                g_type_is_a(*gtype, G_TYPE_INTERFACE))
                state.instance_object = obj;

            if (g_type_is_a(*gtype, G_TYPE_OBJECT)) {
                auto* o = ObjectBase::for_js(context, obj);
                dynamicString =
                    GJS_PROFILER_DYNAMIC_STRING(context, o->format_name());
            }
        }
    }
    std::string full_name{GJS_PROFILER_DYNAMIC_STRING(
        context, dynamicString + "." + format_name())};
    AutoProfilerLabel label{context, "", full_name};

    g_assert(ffi_arg_pos + state.gi_argc <
             std::numeric_limits<decltype(state.processed_c_args)>::max());

    state.processed_c_args = ffi_arg_pos;
    for (gi_arg_pos = 0; gi_arg_pos < state.gi_argc;
         gi_arg_pos++, ffi_arg_pos++) {
        GIArgument* in_value = &state.in_cvalue(gi_arg_pos);
        Argument* gjs_arg = m_arguments.argument(gi_arg_pos);

        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "Marshalling argument '%s' in, %d/%d GI args, %u/%u "
                          "C args, %u/%u JS args",
                          gjs_arg ? gjs_arg->arg_name() : "<unknown>",
                          gi_arg_pos, state.gi_argc, ffi_arg_pos, ffi_argc,
                          js_arg_pos, args.length());

        ffi_arg_pointers[ffi_arg_pos] = in_value;

        if (!gjs_arg) {
            GI::StackArgInfo arg_info;
            m_info.load_arg(gi_arg_pos, &arg_info);
            gjs_throw(context,
                      "Error invoking %s: impossible to determine what to pass "
                      "to the '%s' argument. It may be that the function is "
                      "unsupported, or there may be a bug in its annotations.",
                      format_name().c_str(), arg_info.name());
            state.failed = true;
            break;
        }

        JS::RootedValue js_in_arg(context);
        if (js_arg_pos < args.length())
            js_in_arg = args[js_arg_pos];

        if (!gjs_arg->in(context, &state, in_value, js_in_arg)) {
            state.failed = true;
            break;
        }

        if (!gjs_arg->skip_in())
            js_arg_pos++;

        state.processed_c_args++;
    }

    // This pointer needs to exist on the stack across the ffi_call() call
    GError** errorp = &state.local_error;

    /* Did argument conversion fail?  In that case, skip invocation and jump to release
     * processing. */
    if (state.failed)
        return finish_invoke(context, args, &state, r_value);

    if (state.can_throw_gerror) {
        g_assert(ffi_arg_pos < ffi_argc && "GError** argument number mismatch");
        ffi_arg_pointers[ffi_arg_pos] = &errorp;
        ffi_arg_pos++;

        /* don't update state.processed_c_args as we deal with local_error
         * separately */
    }

    g_assert_cmpuint(ffi_arg_pos, ==, ffi_argc);
    g_assert_cmpuint(gi_arg_pos, ==, state.gi_argc);

    Maybe<Arg::ReturnTag> return_tag = m_arguments.return_tag();
    // return_value_p will point inside the return GIFFIReturnValue union if the
    // C function has a non-void return type
    void* return_value_p =
        get_return_ffi_pointer_from_gi_argument(return_tag, &return_value);
    ffi_call(&m_invoker.cif, FFI_FN(m_invoker.native_address), return_value_p,
             ffi_arg_pointers.get());

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (!r_value)
        args.rval().setUndefined();

    if (return_tag) {
        gi_type_tag_extract_ffi_return_value(
            return_tag->tag(), return_tag->interface_gtype(), &return_value,
            state.return_value());
    }

    // Process out arguments and return values. This loop is skipped if we fail
    // the type conversion above, or if state.did_throw_gerror is true.
    js_arg_pos = 0;
    for (gi_arg_pos = -1; gi_arg_pos < state.gi_argc; gi_arg_pos++) {
        Maybe<Argument*> gjs_arg;
        GIArgument* out_value;

        if (gi_arg_pos == -1) {
            out_value = state.return_value();
            gjs_arg = m_arguments.return_value();
        } else {
            out_value = &state.out_cvalue(gi_arg_pos);
            gjs_arg = Some(m_arguments.argument(gi_arg_pos));
        }

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION, "Marshalling argument '%s' out, %d/%d GI args",
            gjs_arg.map(std::mem_fn(&Argument::arg_name)).valueOr("<unknown>"),
            gi_arg_pos, state.gi_argc);

        JS::RootedValue js_out_arg(context);
        if (!r_value) {
            if (!gjs_arg && gi_arg_pos >= 0) {
                GI::StackArgInfo arg_info;
                m_info.load_arg(gi_arg_pos, &arg_info);
                gjs_throw(
                    context,
                    "Error invoking %s: impossible to determine what to pass "
                    "to the out '%s' argument. It may be that the function is "
                    "unsupported, or there may be a bug in its annotations.",
                    format_name().c_str(), arg_info.name());
                state.failed = true;
                break;
            }

            if (gjs_arg &&
                !(*gjs_arg)->out(context, &state, out_value, &js_out_arg)) {
                state.failed = true;
                break;
            }
        }

        if (gjs_arg && !(*gjs_arg)->skip_out()) {
            if (!r_value) {
                if (!state.return_values.append(js_out_arg)) {
                    JS_ReportOutOfMemory(context);
                    state.failed = true;
                    break;
                }
            }
            js_arg_pos++;
        }
    }

    g_assert(state.failed || state.did_throw_gerror() ||
             js_arg_pos == m_js_out_argc);

    // If we failed before calling the function, or if the function threw an
    // exception, then any GI_TRANSFER_EVERYTHING or GI_TRANSFER_CONTAINER
    // in-parameters were not transferred. Treat them as GI_TRANSFER_NOTHING so
    // that they are freed.
    return finish_invoke(context, args, &state, r_value);
}

bool Function::finish_invoke(JSContext* cx, const JS::CallArgs& args,
                             GjsFunctionCallState* state,
                             GIArgument* r_value /* = nullptr */) {
    // In this loop we use ffi_arg_pos just to ensure we don't release stuff
    // we haven't allocated yet, if we failed in type conversion above.
    // If we start from -1 (the return value), we need to process 1 more than
    // state.processed_c_args.
    // If we start from -2 (the instance parameter), we need to process 2 more
    unsigned ffi_arg_pos = state->first_arg_offset() - 1;
    unsigned ffi_arg_max = state->last_processed_index();
    bool postinvoke_release_failed = false;
    for (int gi_arg_pos = -(state->first_arg_offset());
         gi_arg_pos < state->gi_argc && ffi_arg_pos < ffi_arg_max;
         gi_arg_pos++, ffi_arg_pos++) {
        Maybe<Argument*> gjs_arg;
        GIArgument* in_value = nullptr;
        GIArgument* out_value = nullptr;

        if (gi_arg_pos == -2) {
            in_value = state->instance();
            gjs_arg = m_arguments.instance();
        } else if (gi_arg_pos == -1) {
            out_value = state->return_value();
            gjs_arg = m_arguments.return_value();
        } else {
            in_value = &state->in_cvalue(gi_arg_pos);
            out_value = &state->out_cvalue(gi_arg_pos);
            gjs_arg = Some(m_arguments.argument(gi_arg_pos));
        }

        if (!gjs_arg)
            continue;

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION,
            "Releasing argument '%s', %d/%d GI args, %u/%u C args",
            (*gjs_arg)->arg_name(), gi_arg_pos, state->gi_argc, ffi_arg_pos,
            state->processed_c_args);

        // Only process in or inout arguments if we failed, the rest is garbage
        if (state->failed && (*gjs_arg)->skip_in())
            continue;

        // Save the return GIArgument if it was requested
        if (r_value && gi_arg_pos == -1) {
            *r_value = *out_value;
            continue;
        }

        if (!(*gjs_arg)->release(cx, state, in_value, out_value)) {
            postinvoke_release_failed = true;
            // continue with the release even if we fail, to avoid leaks
        }
    }

    if (postinvoke_release_failed)
        state->failed = true;

    g_assert(ffi_arg_pos == state->last_processed_index());

    if (!r_value && m_js_out_argc > 0 && state->call_completed()) {
        // If we have one return value or out arg, return that item on its
        // own, otherwise return a JavaScript array with [return value,
        // out arg 1, out arg 2, ...]
        if (m_js_out_argc == 1) {
            args.rval().set(state->return_values[0]);
        } else {
            JSObject* array = JS::NewArrayObject(cx, state->return_values);
            if (!array) {
                state->failed = true;
            } else {
                args.rval().setObject(*array);
            }
        }
    }

    if (!state->failed && state->did_throw_gerror()) {
        return gjs_throw_gerror(cx, state->local_error.release());
    } else if (state->failed) {
        return false;
    } else {
        return true;
    }
}

bool Function::call(JSContext* context, unsigned js_argc, JS::Value* vp) {
    JS::CallArgs js_argv = JS::CallArgsFromVp(js_argc, vp);
    JS::RootedObject callee(context, &js_argv.callee());

    Function* priv;
    if (!Function::for_js_typecheck(context, callee, &priv, &js_argv))
        return false;

    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Call callee %p priv %p",
                      callee.get(), priv);

    g_assert(priv);
    return priv->invoke(context, js_argv);
}

Function::~Function() {
    gi_function_invoker_clear(&m_invoker);
    GJS_DEC_COUNTER(function);
}

void Function::finalize_impl(JS::GCContext*, Function* priv) {
    g_assert(priv);
    delete priv;
}

bool Function::get_length(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_GET_THIS(cx, argc, vp, args, this_obj);
    Function* priv;
    if (!Function::for_js_instance(cx, this_obj, &priv, &args))
        return false;
    args.rval().setInt32(priv->m_js_in_argc);
    return true;
}

bool Function::get_name(JSContext* cx, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(cx, argc, vp, rec, this_obj, Function, priv);

    if (auto func_info = priv->m_info.as<GI::InfoTag::FUNCTION>())
        return gjs_string_from_utf8(cx, func_info->symbol(), rec.rval());

    return gjs_string_from_utf8(cx, priv->format_name().c_str(), rec.rval());
}

bool Function::to_string(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(context, argc, vp, rec, this_obj, Function, priv);
    return priv->to_string_impl(context, rec.rval());
}

bool Function::to_string_impl(JSContext* cx, JS::MutableHandleValue rval) {
    int i, n_jsargs;

    int n_args = m_info.n_args();
    n_jsargs = 0;
    std::string arg_names;
    for (i = 0; i < n_args; i++) {
        Argument* gjs_arg = m_arguments.argument(i);
        if (!gjs_arg || gjs_arg->skip_in())
            continue;

        if (n_jsargs > 0)
            arg_names += ", ";

        n_jsargs++;
        arg_names += gjs_arg->arg_name();
    }

    AutoChar descr;
    if (auto func_info = m_info.as<GI::InfoTag::FUNCTION>()) {
        descr = g_strdup_printf(
            "%s(%s) {\n\t/* wrapper for native symbol %s() */\n}",
            format_name().c_str(), arg_names.c_str(), func_info->symbol());
    } else {
        descr =
            g_strdup_printf("%s(%s) {\n\t/* wrapper for native symbol */\n}",
                            format_name().c_str(), arg_names.c_str());
    }

    return gjs_string_from_utf8(cx, descr, rval);
}

const JSClassOps Function::class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    &Function::finalize,
    &Function::call,
};

const JSPropertySpec Function::proto_props[] = {
    JS_PSG("length", &Function::get_length, JSPROP_PERMANENT),
    JS_PSG("name", &Function::get_name, JSPROP_PERMANENT),
    JS_STRING_SYM_PS(toStringTag, "GIRepositoryFunction", JSPROP_READONLY),
    JS_PS_END};

/* The original Function.prototype.toString complains when
   given a GIRepository function as an argument */
// clang-format off
const JSFunctionSpec Function::proto_funcs[] = {
    JS_FN("toString", &Function::to_string, 0, 0),
    JS_FS_END};
// clang-format on

bool Function::init(JSContext* context, GType gtype /* = G_TYPE_NONE */) {
    guint8 i;

    if (auto func_info = m_info.as<GI::InfoTag::FUNCTION>()) {
        GErrorResult<> result = func_info->prep_invoker(&m_invoker);
        if (result.isErr())
            return gjs_throw_gerror(context, result.unwrapErr());
    } else if (auto vfunc_info = m_info.as<GI::InfoTag::VFUNC>()) {
        Gjs::GErrorResult<void*> result = vfunc_info->address(gtype);
        if (!result.isOk()) {
            if (result.inspectErr()->code != GI_INVOKE_ERROR_SYMBOL_NOT_FOUND)
                return gjs_throw_gerror(context, result.unwrapErr());

            gjs_throw(context, "Virtual function not implemented: %s",
                      result.inspectErr()->message);
            return false;
        }

        GErrorResult<> result2 =
            m_info.init_function_invoker(result.unwrap(), &m_invoker);
        if (result2.isErr())
            return gjs_throw_gerror(context, result2.unwrapErr());
    }

    uint8_t n_args = m_info.n_args();

    if (!m_arguments.initialize(context, m_info))
        return false;

    m_arguments.build_instance(m_info);

    bool inc_counter;
    m_arguments.build_return(m_info, &inc_counter);

    if (inc_counter)
        m_js_out_argc++;

    for (i = 0; i < n_args; i++) {
        Argument* gjs_arg = m_arguments.argument(i);
        GI::StackArgInfo arg_info;

        if (gjs_arg && (gjs_arg->skip_in() || gjs_arg->skip_out())) {
            continue;
        }

        m_info.load_arg(i, &arg_info);
        GIDirection direction = arg_info.direction();

        m_arguments.build_arg(i, direction, arg_info, m_info, &inc_counter);

        if (inc_counter) {
            switch (direction) {
                case GI_DIRECTION_INOUT:
                    m_js_out_argc++;
                    [[fallthrough]];
                case GI_DIRECTION_IN:
                    m_js_in_argc++;
                    break;
                case GI_DIRECTION_OUT:
                    m_js_out_argc++;
                    break;
                default:
                    g_assert_not_reached();
            }
        }
    }

    return true;
}

JSObject* Function::create(JSContext* context, GType gtype,
                           const GI::CallableInfo info) {
    JS::RootedObject proto(context, Function::create_prototype(context));
    if (!proto)
        return nullptr;

    JS::RootedObject function(
        context, JS_NewObjectWithGivenProto(context, &Function::klass, proto));
    if (!function) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to construct function");
        return NULL;
    }

    auto* priv = new Function(info);

    Function::init_private(function, priv);

    debug_lifecycle(function, priv, "Constructor");

    if (!priv->init(context, gtype))
        return nullptr;

    return function;
}

}  // namespace Gjs

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_define_function(JSContext* context, JS::HandleObject in_object,
                              GType gtype, const GI::CallableInfo info) {
    std::string name;

    JS::RootedObject function(context,
                              Gjs::Function::create(context, gtype, info));
    if (!function)
        return NULL;

    if (info.is_function()) {
        name = info.name();
    } else if (info.is_vfunc()) {
        name = "vfunc_" + std::string(info.name());
    } else {
        g_assert_not_reached ();
    }

    if (!JS_DefineProperty(context, in_object, name.c_str(), function,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to define function");
        function = NULL;
    }

    return function;
}

bool gjs_invoke_constructor_from_c(JSContext* context,
                                   const GI::FunctionInfo info,
                                   JS::HandleObject obj,
                                   const JS::CallArgs& args,
                                   GIArgument* rvalue) {
    return Gjs::Function::invoke_constructor_uncached(context, info, obj, args,
                                                      rvalue);
}
