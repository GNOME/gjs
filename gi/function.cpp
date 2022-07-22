/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>
#include <stdlib.h>  // for exit

#include <memory>  // for unique_ptr
#include <string>
#include <vector>

#include <ffi.h>
#include <girepository.h>
#include <girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory
#include <js/Exception.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetRealmFunctionPrototype
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>        // for HandleValueArray
#include <jspubtd.h>      // for JSProtoKey

#include "gi/arg-cache.h"
#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/closure.h"
#include "gi/cwrapper.h"
#include "gi/function.h"
#include "gi/gerror.h"
#include "gi/object.h"
#include "gi/utils-inl.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/global.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/mem-private.h"
#include "gjs/profiler-private.h"
#include "util/log.h"

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

    GjsAutoCallableInfo m_info;

    ArgsCache m_arguments;

    uint8_t m_js_in_argc;
    uint8_t m_js_out_argc;
    GIFunctionInvoker m_invoker;

    explicit Function(GICallableInfo* info)
        : m_info(info, GjsAutoTakeOwnership()),
          m_js_in_argc(0),
          m_js_out_argc(0),
          m_invoker({}) {
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
        return JS_NewObjectWithGivenProto(cx, &Function::klass,
                                          builtin_function_proto);
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
    static JSObject* create(JSContext* cx, GType gtype, GICallableInfo* info);

    [[nodiscard]] std::string format_name();

    GJS_JSAPI_RETURN_CONVENTION
    bool invoke(JSContext* cx, const JS::CallArgs& args,
                JS::HandleObject this_obj = nullptr,
                GIArgument* r_value = nullptr);

    GJS_JSAPI_RETURN_CONVENTION
    static bool invoke_constructor_uncached(JSContext* cx, GIFunctionInfo* info,
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

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
static inline void set_ffi_arg(void* result, GIArgument* value) {
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        *static_cast<ffi_sarg*>(result) = gjs_arg_get<T, TAG>(value);
    } else if constexpr (std::is_floating_point_v<T> || std::is_unsigned_v<T>) {
        *static_cast<ffi_arg*>(result) = gjs_arg_get<T, TAG>(value);
    } else if constexpr (std::is_pointer_v<T>) {
        *static_cast<ffi_arg*>(result) =
            gjs_pointer_to_int<ffi_arg>(gjs_arg_get<T, TAG>(value));
    }
}

static void
set_return_ffi_arg_from_giargument (GITypeInfo  *ret_type,
                                    void        *result,
                                    GIArgument  *return_value)
{
    // Be consistent with gjs_value_to_g_argument()
    switch (g_type_info_get_tag(ret_type)) {
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
        set_ffi_arg<gboolean, GI_TYPE_TAG_BOOLEAN>(result, return_value);
        break;
    case GI_TYPE_TAG_UNICHAR:
        set_ffi_arg<char32_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INT64:
        set_ffi_arg<int64_t>(result, return_value);
        break;
    case GI_TYPE_TAG_INTERFACE:
        {
            GIInfoType interface_type;

            GjsAutoBaseInfo interface_info(g_type_info_get_interface(ret_type));
            interface_type = g_base_info_get_type(interface_info);

            if (interface_type == GI_INFO_TYPE_ENUM ||
                interface_type == GI_INFO_TYPE_FLAGS)
                set_ffi_arg<int, GI_TYPE_TAG_INTERFACE>(result, return_value);
            else
                set_ffi_arg<void*>(result, return_value);
        }
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
        set_ffi_arg<GType, GI_TYPE_TAG_GTYPE>(result, return_value);
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
                                                           const char* reason) {
    g_critical("Attempting to run a JS callback %s. This is most likely caused "
               "by %s. Because it would crash the application, it has been "
               "blocked.", when, reason);
    if (m_info)
        g_critical("The offending callback was %s()%s.", m_info.name(),
                   m_is_vfunc ? ", a vfunc" : "");
}

/* This is our main entry point for ffi_closure callbacks.
 * ffi_prep_closure is doing pure magic and replaces the original
 * function call with this one which gives us the ffi arguments,
 * a place to store the return value and our use data.
 * In other words, everything we need to call the JS function and
 * getting the return value back.
 */
void GjsCallbackTrampoline::callback_closure(GIArgument** args, void* result) {
    GITypeInfo ret_type;

    if (G_UNLIKELY(!is_valid())) {
        warn_about_illegal_js_callback(
            "during shutdown",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs");
        gjs_dumpstack();
        return;
    }

    JSContext* context = this->context();
    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    if (G_UNLIKELY(gjs->sweeping())) {
        warn_about_illegal_js_callback(
            "during garbage collection",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs");
        gjs_dumpstack();
        return;
    }

    if (G_UNLIKELY(!gjs->is_owner_thread())) {
        warn_about_illegal_js_callback("on a different thread",
                                       "an API not intended to be used in JS");
        return;
    }

    JSAutoRealm ar(context, JS_GetFunctionObject(callable()));

    int n_args = m_param_types.size();
    g_assert(n_args >= 0);

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
    int c_args_offset = 0;
    if (m_is_vfunc) {
        GObject* gobj = G_OBJECT(gjs_arg_get<GObject*>(args[0]));
        if (gobj) {
            this_object = ObjectInstance::wrapper_from_gobject(context, gobj);
            if (!this_object) {
                if (g_object_get_qdata(gobj, ObjectBase::disposed_quark())) {
                    warn_about_illegal_js_callback(
                        "on disposed object",
                        "using the destroy(), dispose(), or remove() vfuncs");
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

    g_callable_info_load_return_type(m_info, &ret_type);

    if (!callback_closure_inner(context, this_object, &rval, args, &ret_type,
                                n_args, c_args_offset, result)) {
        if (!JS_IsExceptionPending(context)) {
            // "Uncatchable" exception thrown, we have to exit. We may be in a
            // main loop, or maybe not, but there's no way to tell, so we have
            // to exit here instead of propagating the exception back to the
            // original calling JS code.
            uint8_t code;
            if (gjs->should_exit(&code)) {
                gjs->warn_about_unhandled_promise_rejections();
                exit(code);
            }

            // Some other uncatchable exception, e.g. out of memory
            JSFunction* fn = callable();
            g_error("Function %s (%s.%s) terminated with uncatchable exception",
                    gjs_debug_string(JS_GetFunctionDisplayId(fn)).c_str(),
                    m_info.ns(), m_info.name());
        }

        // Fill in the result with some hopefully neutral value
        if (g_type_info_get_tag(&ret_type) != GI_TYPE_TAG_VOID) {
            GIArgument argument = {};
            g_callable_info_load_return_type(m_info, &ret_type);
            gjs_gi_argument_init_default(&ret_type, &argument);
            set_return_ffi_arg_from_giargument(&ret_type, result, &argument);
        }

        // If the callback has a GError** argument, then make a GError from the
        // value that was thrown. Otherwise, log it as "uncaught" (critical
        // instead of warning)

        if (!g_callable_info_can_throw_gerror(m_info)) {
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

inline GIArgument* get_argument_for_arg_info(GIArgInfo* arg_info,
                                             GIArgument** args, int index) {
    if (!g_arg_info_is_caller_allocates(arg_info))
        return *reinterpret_cast<GIArgument**>(args[index]);
    else
        return args[index];
}

bool GjsCallbackTrampoline::callback_closure_inner(
    JSContext* context, JS::HandleObject this_object,
    JS::MutableHandleValue rval, GIArgument** args, GITypeInfo* ret_type,
    int n_args, int c_args_offset, void* result) {
    int n_outargs = 0;
    JS::RootedValueVector jsargs(context);

    if (!jsargs.reserve(n_args))
        g_error("Unable to reserve space for vector");

    bool ret_type_is_void = g_type_info_get_tag(ret_type) == GI_TYPE_TAG_VOID;

    for (int i = 0, n_jsargs = 0; i < n_args; i++) {
        GIArgInfo arg_info;
        GITypeInfo type_info;
        GjsParamType param_type;

        g_callable_info_load_arg(m_info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        /* Skip void * arguments */
        if (g_type_info_get_tag(&type_info) == GI_TYPE_TAG_VOID)
            continue;

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_OUT) {
            n_outargs++;
            continue;
        }

        if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_INOUT)
            n_outargs++;

        param_type = m_param_types[i];

        switch (param_type) {
            case PARAM_SKIPPED:
                continue;
            case PARAM_ARRAY: {
                gint array_length_pos = g_type_info_get_array_length(&type_info);
                GIArgInfo array_length_arg;
                GITypeInfo arg_type_info;

                g_callable_info_load_arg(m_info, array_length_pos,
                                         &array_length_arg);
                g_arg_info_load_type(&array_length_arg, &arg_type_info);
                size_t length = gjs_g_argument_get_array_length(
                    g_type_info_get_tag(&arg_type_info),
                    args[array_length_pos + c_args_offset]);

                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                if (!gjs_value_from_explicit_array(
                        context, jsargs[n_jsargs++], &type_info,
                        args[i + c_args_offset], length))
                    return false;
                break;
            }
            case PARAM_NORMAL: {
                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                GIArgument* arg = args[i + c_args_offset];
                if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_INOUT &&
                    !g_arg_info_is_caller_allocates(&arg_info))
                    arg = *reinterpret_cast<GIArgument**>(arg);

                if (!gjs_value_from_g_argument(context, jsargs[n_jsargs++],
                                               &type_info, arg, false))
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
        GITransfer transfer;

        transfer = g_callable_info_get_caller_owns(m_info);
        /* non-void return value, no out args. Should
         * be a single return value. */
        if (!gjs_value_to_g_argument(context, rval, ret_type, "callback",
                                     GJS_ARGUMENT_RETURN_VALUE, transfer,
                                     GjsArgumentFlags::MAY_BE_NULL, &argument))
            return false;

        set_return_ffi_arg_from_giargument(ret_type, result, &argument);
    } else if (n_outargs == 1 && ret_type_is_void) {
        /* void return value, one out args. Should
         * be a single return value. */
        for (int i = 0; i < n_args; i++) {
            GIArgInfo arg_info;
            g_callable_info_load_arg(m_info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            if (!gjs_value_to_callback_out_arg(
                    context, rval, &arg_info,
                    get_argument_for_arg_info(&arg_info, args,
                                              i + c_args_offset)))
                return false;

            break;
        }
    } else {
        bool is_array = rval.isObject();
        if (!JS::IsArrayObject(context, rval, &is_array))
            return false;

        if (!is_array) {
            JSFunction* fn = callable();
            gjs_throw(context,
                      "Function %s (%s.%s) returned unexpected value, "
                      "expecting an Array",
                      gjs_debug_string(JS_GetFunctionDisplayId(fn)).c_str(),
                      m_info.ns(), m_info.name());
            return false;
        }

        JS::RootedValue elem(context);
        JS::RootedObject out_array(context, rval.toObjectOrNull());
        gsize elem_idx = 0;
        /* more than one of a return value or an out argument.
         * Should be an array of output values. */

        if (!ret_type_is_void) {
            GIArgument argument;
            GITransfer transfer = g_callable_info_get_caller_owns(m_info);

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                return false;

            if (!gjs_value_to_g_argument(context, elem, ret_type, "callback",
                                         GJS_ARGUMENT_RETURN_VALUE, transfer,
                                         GjsArgumentFlags::MAY_BE_NULL,
                                         &argument))
                return false;

            set_return_ffi_arg_from_giargument(ret_type, result, &argument);

            elem_idx++;
        }

        for (int i = 0; i < n_args; i++) {
            GIArgInfo arg_info;
            g_callable_info_load_arg(m_info, i, &arg_info);
            if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_IN)
                continue;

            if (!JS_GetElement(context, out_array, elem_idx, &elem))
                return false;

            if (!gjs_value_to_callback_out_arg(
                    context, elem, &arg_info,
                    get_argument_for_arg_info(&arg_info, args,
                                              i + c_args_offset)))
                return false;

            elem_idx++;
        }
    }

    return true;
}

GjsCallbackTrampoline* GjsCallbackTrampoline::create(
    JSContext* cx, JS::HandleFunction function, GICallableInfo* callable_info,
    GIScopeType scope, bool has_scope_object, bool is_vfunc) {
    g_assert(function);
    auto* trampoline = new GjsCallbackTrampoline(
        cx, function, callable_info, scope, has_scope_object, is_vfunc);

    if (!trampoline->initialize()) {
        g_closure_unref(trampoline);
        return nullptr;
    }

    return trampoline;
}

decltype(GjsCallbackTrampoline::s_forever_closure_list)
    GjsCallbackTrampoline::s_forever_closure_list;

GjsCallbackTrampoline::GjsCallbackTrampoline(
    JSContext* cx, JS::HandleFunction function, GICallableInfo* callable_info,
    GIScopeType scope, bool has_scope_object, bool is_vfunc)
    // The rooting rule is:
    // - notify callbacks in GObject methods are traced from the scope object
    // - async and call callbacks, and other notify callbacks, are rooted
    // - vfuncs are traced from the GObject prototype
    : Closure(cx, function,
              scope != GI_SCOPE_TYPE_NOTIFIED || !has_scope_object,
              g_base_info_get_name(callable_info)),
      m_info(callable_info, GjsAutoTakeOwnership()),
      m_param_types(g_callable_info_get_n_args(callable_info), {}),
      m_scope(scope),
      m_is_vfunc(is_vfunc) {
    add_finalize_notifier<GjsCallbackTrampoline>();
}

GjsCallbackTrampoline::~GjsCallbackTrampoline() {
    if (m_info && m_closure) {
#if GI_CHECK_VERSION(1, 71, 0)
        g_callable_info_destroy_closure(m_info, m_closure);
#else
        g_callable_info_free_closure(m_info, m_closure);
#endif
    }
}

void GjsCallbackTrampoline::mark_forever() {
    s_forever_closure_list.emplace_back(this, GjsAutoTakeOwnership{});
}

void GjsCallbackTrampoline::prepare_shutdown() {
    s_forever_closure_list.clear();
}

ffi_closure* GjsCallbackTrampoline::create_closure() {
    auto callback = [](ffi_cif*, void* result, void** ffi_args, void* data) {
        auto** args = reinterpret_cast<GIArgument**>(ffi_args);
        g_assert(data && "Trampoline data is not set");
        Gjs::Closure::Ptr trampoline(static_cast<GjsCallbackTrampoline*>(data),
                                     GjsAutoTakeOwnership());

        trampoline.as<GjsCallbackTrampoline>()->callback_closure(args, result);
    };

#if GI_CHECK_VERSION(1, 71, 0)
    return g_callable_info_create_closure(m_info, &m_cif, callback, this);
#else
    return g_callable_info_prepare_closure(m_info, &m_cif, callback, this);
#endif
}

bool GjsCallbackTrampoline::initialize() {
    g_assert(is_valid());
    g_assert(!m_closure);

    /* Analyze param types and directions, similarly to
     * init_cached_function_data */
    for (size_t i = 0; i < m_param_types.size(); i++) {
        GIDirection direction;
        GIArgInfo arg_info;
        GITypeInfo type_info;
        GITypeTag type_tag;

        if (m_param_types[i] == PARAM_SKIPPED)
            continue;

        g_callable_info_load_arg(m_info, i, &arg_info);
        g_arg_info_load_type(&arg_info, &type_info);

        direction = g_arg_info_get_direction(&arg_info);
        type_tag = g_type_info_get_tag(&type_info);

        if (direction != GI_DIRECTION_IN) {
            /* INOUT and OUT arguments are handled differently. */
            continue;
        }

        if (type_tag == GI_TYPE_TAG_INTERFACE) {
            GIInfoType interface_type;

            GjsAutoBaseInfo interface_info =
                g_type_info_get_interface(&type_info);
            interface_type = g_base_info_get_type(interface_info);
            if (interface_type == GI_INFO_TYPE_CALLBACK) {
                gjs_throw(context(),
                          "%s %s accepts another callback as a parameter. This "
                          "is not supported",
                          m_is_vfunc ? "VFunc" : "Callback", m_info.name());
                return false;
            }
        } else if (type_tag == GI_TYPE_TAG_ARRAY) {
            if (g_type_info_get_array_type(&type_info) == GI_ARRAY_TYPE_C) {
                int array_length_pos = g_type_info_get_array_length(&type_info);

                if (array_length_pos < 0)
                    continue;

                if (static_cast<size_t>(array_length_pos) <
                    m_param_types.size()) {
                    GIArgInfo length_arg_info;

                    g_callable_info_load_arg(m_info, array_length_pos,
                                             &length_arg_info);
                    if (g_arg_info_get_direction(&length_arg_info) != direction) {
                        gjs_throw(context(),
                                  "%s %s has an array with different-direction "
                                  "length argument. This is not supported",
                                  m_is_vfunc ? "VFunc" : "Callback",
                                  m_info.name());
                        return false;
                    }

                    m_param_types[array_length_pos] = PARAM_SKIPPED;
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
    bool is_method = g_callable_info_is_method(m_info);
    std::string retval = is_method ? "method" : "function";
    retval += ' ';
    retval += m_info.ns();
    retval += '.';
    if (is_method) {
        retval += g_base_info_get_name(g_base_info_get_container(m_info));
        retval += '.';
    }
    retval += m_info.name();
    return retval;
}

namespace Gjs {

static void* get_return_ffi_pointer_from_giargument(
    GITypeInfo* return_type, GIFFIReturnValue* return_value) {
    // This should be the inverse of gi_type_info_extract_ffi_return_value().
    if (!return_type)
        return nullptr;

    switch (g_type_info_get_tag(return_type)) {
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
            return &gjs_arg_member<gboolean, GI_TYPE_TAG_BOOLEAN>(return_value);
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
            GjsAutoBaseInfo info = g_type_info_get_interface(return_type);

            switch (g_base_info_get_type(info)) {
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                    return &gjs_arg_member<int, GI_TYPE_TAG_INTERFACE>(
                        return_value);
                default:
                    return &gjs_arg_member<void*>(return_value);
            }
            break;
        }
        default:
            return &gjs_arg_member<void*>(return_value);
    }
}

// This function can be called in two different ways. You can either use it to
// create JavaScript objects by calling it without @r_value, or you can decide
// to keep the return values in #GArgument format by providing a @r_value
// argument.
bool Function::invoke(JSContext* context, const JS::CallArgs& args,
                      JS::HandleObject this_obj /* = nullptr */,
                      GIArgument* r_value /* = nullptr */) {
    g_assert((args.isConstructing() || !this_obj) &&
             "If not a constructor, then pass the 'this' object via CallArgs");

    void* return_value_p;  // will point inside the return GIArgument union
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

        if (!m_arguments.instance()->in(context, &state, in_value, in_js_value))
            return false;

        ffi_arg_pointers[ffi_arg_pos] = in_value;
        ++ffi_arg_pos;

        // Callback lifetimes will be attached to the instance object if it is
        // a GObject or GInterface
        GType gtype = m_arguments.instance_type();
        if (gtype != G_TYPE_NONE) {
            if (g_type_is_a(gtype, G_TYPE_OBJECT) ||
                g_type_is_a(gtype, G_TYPE_INTERFACE))
                state.instance_object = obj;

            if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                auto* o = ObjectBase::for_js(context, obj);
                dynamicString = o->format_name();
            }
        }
    }

    dynamicString += '.';
    dynamicString += format_name();
    AutoProfilerLabel label(context, "", dynamicString.c_str());

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
            GIArgInfo arg_info;
            g_callable_info_load_arg(m_info, gi_arg_pos, &arg_info);
            gjs_throw(context,
                      "Error invoking %s: impossible to determine what to pass "
                      "to the '%s' argument. It may be that the function is "
                      "unsupported, or there may be a bug in its annotations.",
                      format_name().c_str(), g_base_info_get_name(&arg_info));
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
    GError** errorp = state.local_error.out();

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

    GITypeInfo* return_type = m_arguments.return_type();
    return_value_p =
        get_return_ffi_pointer_from_giargument(return_type, &return_value);
    ffi_call(&m_invoker.cif, FFI_FN(m_invoker.native_address), return_value_p,
             ffi_arg_pointers.get());

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (!r_value)
        args.rval().setUndefined();

    if (return_type) {
        gi_type_info_extract_ffi_return_value(return_type, &return_value,
                                              state.return_value());
    }

    // Process out arguments and return values. This loop is skipped if we fail
    // the type conversion above, or if state.did_throw_gerror is true.
    js_arg_pos = 0;
    for (gi_arg_pos = -1; gi_arg_pos < state.gi_argc; gi_arg_pos++) {
        Argument* gjs_arg;
        GIArgument* out_value;

        if (gi_arg_pos == -1) {
            out_value = state.return_value();
            gjs_arg = m_arguments.return_value();
        } else {
            out_value = &state.out_cvalue(gi_arg_pos);
            gjs_arg = m_arguments.argument(gi_arg_pos);
        }

        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "Marshalling argument '%s' out, %d/%d GI args",
                          gjs_arg ? gjs_arg->arg_name() : "<unknown>",
                          gi_arg_pos, state.gi_argc);

        JS::RootedValue js_out_arg(context);
        if (!r_value) {
            if (!gjs_arg && gi_arg_pos >= 0) {
                GIArgInfo arg_info;
                g_callable_info_load_arg(m_info, gi_arg_pos, &arg_info);
                gjs_throw(
                    context,
                    "Error invoking %s.%s: impossible to determine what "
                    "to pass to the out '%s' argument. It may be that the "
                    "function is unsupported, or there may be a bug in "
                    "its annotations.",
                    g_base_info_get_namespace(m_info),
                    g_base_info_get_name(m_info),
                    g_base_info_get_name(&arg_info));
                state.failed = true;
                break;
            }

            if (gjs_arg &&
                !gjs_arg->out(context, &state, out_value, &js_out_arg)) {
                state.failed = true;
                break;
            }
        }

        if (gjs_arg && !gjs_arg->skip_out()) {
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
        Argument* gjs_arg;
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
            gjs_arg = m_arguments.argument(gi_arg_pos);
        }

        if (!gjs_arg)
            continue;

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION,
            "Releasing argument '%s', %d/%d GI args, %u/%u C args",
            gjs_arg->arg_name(), gi_arg_pos, state->gi_argc, ffi_arg_pos,
            state->processed_c_args);

        // Only process in or inout arguments if we failed, the rest is garbage
        if (state->failed && gjs_arg->skip_in())
            continue;

        // Save the return GIArgument if it was requested
        if (r_value && gi_arg_pos == -1) {
            *r_value = *out_value;
            continue;
        }

        if (!gjs_arg->release(cx, state, in_value, out_value)) {
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

    if (priv == NULL)
        return true;  // we are the prototype

    return priv->invoke(context, js_argv);
}

Function::~Function() {
    g_function_invoker_destroy(&m_invoker);
    GJS_DEC_COUNTER(function);
}

void Function::finalize_impl(JS::GCContext*, Function* priv) {
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */
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

bool Function::to_string(JSContext* context, unsigned argc, JS::Value* vp) {
    GJS_CHECK_WRAPPER_PRIV(context, argc, vp, rec, this_obj, Function, priv);

    if (priv == NULL) {
        JSString* retval = JS_NewStringCopyZ(context, "function () {\n}");
        if (!retval)
            return false;
        rec.rval().setString(retval);
        return true;
    }

    return priv->to_string_impl(context, rec.rval());
}

bool Function::to_string_impl(JSContext* cx, JS::MutableHandleValue rval) {
    int i, n_jsargs;

    int n_args = g_callable_info_get_n_args(m_info);
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

    GjsAutoChar descr;
    if (g_base_info_get_type(m_info) == GI_INFO_TYPE_FUNCTION) {
        descr = g_strdup_printf(
            "%s(%s) {\n\t/* wrapper for native symbol %s() */\n}",
            format_name().c_str(), arg_names.c_str(),
            g_function_info_get_symbol(m_info));
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
    GError *error = NULL;

    if (m_info.type() == GI_INFO_TYPE_FUNCTION) {
        if (!g_function_info_prep_invoker(m_info, &m_invoker, &error))
            return gjs_throw_gerror(context, error);
    } else if (m_info.type() == GI_INFO_TYPE_VFUNC) {
        void* addr = g_vfunc_info_get_address(m_info, gtype, &error);
        if (error != NULL) {
            if (error->code != G_INVOKE_ERROR_SYMBOL_NOT_FOUND)
                return gjs_throw_gerror(context, error);

            gjs_throw(context, "Virtual function not implemented: %s",
                      error->message);
            g_clear_error(&error);
            return false;
        }

        if (!g_function_invoker_new_for_address(addr, m_info, &m_invoker,
                                                &error))
            return gjs_throw_gerror(context, error);
    }

    uint8_t n_args = g_callable_info_get_n_args(m_info);

    if (!m_arguments.initialize(context, m_info))
        return false;

    m_arguments.build_instance(m_info);

    bool inc_counter;
    m_arguments.build_return(m_info, &inc_counter);

    if (inc_counter)
        m_js_out_argc++;

    for (i = 0; i < n_args; i++) {
        Argument* gjs_arg = m_arguments.argument(i);
        GIDirection direction;
        GIArgInfo arg_info;

        if (gjs_arg && (gjs_arg->skip_in() || gjs_arg->skip_out())) {
            continue;
        }

        g_callable_info_load_arg(m_info, i, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        m_arguments.build_arg(i, direction, &arg_info, m_info, &inc_counter);

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
                           GICallableInfo* info) {
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
JSObject*
gjs_define_function(JSContext       *context,
                    JS::HandleObject in_object,
                    GType            gtype,
                    GICallableInfo  *info)
{
    GIInfoType info_type;
    gchar *name;
    bool free_name;

    info_type = g_base_info_get_type((GIBaseInfo *)info);

    JS::RootedObject function(context,
                              Gjs::Function::create(context, gtype, info));
    if (!function)
        return NULL;

    if (info_type == GI_INFO_TYPE_FUNCTION) {
        name = (gchar *) g_base_info_get_name((GIBaseInfo*) info);
        free_name = false;
    } else if (info_type == GI_INFO_TYPE_VFUNC) {
        name = g_strdup_printf("vfunc_%s", g_base_info_get_name((GIBaseInfo*) info));
        free_name = true;
    } else {
        g_assert_not_reached ();
    }

    if (!JS_DefineProperty(context, in_object, name, function,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to define function");
        function = NULL;
    }

    if (free_name)
        g_free(name);

    return function;
}

bool gjs_invoke_constructor_from_c(JSContext* context, GIFunctionInfo* info,
                                   JS::HandleObject obj,
                                   const JS::CallArgs& args,
                                   GIArgument* rvalue) {
    return Gjs::Function::invoke_constructor_uncached(context, info, obj, args,
                                                      rvalue);
}
