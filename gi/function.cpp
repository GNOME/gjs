/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <stdint.h>
#include <stdlib.h>  // for exit
#include <string.h>  // for strcmp, memset, size_t

#include <memory>  // for unique_ptr
#include <string>
#include <type_traits>
#include <vector>

#include <ffi.h>
#include <girepository.h>
#include <girffi.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/GCVector.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_PERMANENT
#include <js/PropertySpec.h>
#include <js/Realm.h>  // for GetRealmFunctionPrototype
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <jsapi.h>        // for HandleValueArray, JS_GetElement

#include "gi/arg-cache.h"
#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/closure.h"
#include "gi/function.h"
#include "gi/gerror.h"
#include "gi/object.h"
#include "gi/utils-inl.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "util/log.h"

/* We use guint8 for arguments; functions can't
 * have more than this.
 */
#define GJS_ARG_INDEX_INVALID G_MAXUINT8

typedef struct {
    GICallableInfo* info;

    GjsArgumentCache* arguments;

    uint8_t js_in_argc;
    guint8 js_out_argc;
    GIFunctionInvoker invoker;
} Function;

extern struct JSClass gjs_function_class;

/* Because we can't free the mmap'd data for a callback
 * while it's in use, this list keeps track of ones that
 * will be freed the next time we invoke a C function.
 */
static std::vector<GjsAutoCallbackTrampoline> completed_trampolines;

GJS_DEFINE_PRIV_FROM_JS(Function, gjs_function_class)

GjsCallbackTrampoline* gjs_callback_trampoline_ref(
    GjsCallbackTrampoline* trampoline) {
    g_atomic_ref_count_inc(&trampoline->ref_count);
    return trampoline;
}

void
gjs_callback_trampoline_unref(GjsCallbackTrampoline *trampoline)
{
    if (g_atomic_ref_count_dec(&trampoline->ref_count))
        delete trampoline;
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
static inline std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>
set_ffi_arg(void* result, GIArgument* value) {
    *static_cast<ffi_sarg*>(result) = gjs_arg_get<T, TAG>(value);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
static inline std::enable_if_t<std::is_floating_point_v<T> ||
                               std::is_unsigned_v<T>>
set_ffi_arg(void* result, GIArgument* value) {
    *static_cast<ffi_arg*>(result) = gjs_arg_get<T, TAG>(value);
}

template <typename T, GITypeTag TAG = GI_TYPE_TAG_VOID>
static inline std::enable_if_t<std::is_pointer_v<T>> set_ffi_arg(
    void* result, GIArgument* value) {
    *static_cast<ffi_arg*>(result) =
        gjs_pointer_to_int<ffi_arg>(gjs_arg_get<T, TAG>(value));
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
    JSContext *context;
    GITypeInfo ret_type;

    if (G_UNLIKELY(!gjs_closure_is_valid(m_js_function))) {
        warn_about_illegal_js_callback(
            "during shutdown",
            "destroying a Clutter actor or GTK widget with ::destroy signal "
            "connected, or using the destroy(), dispose(), or remove() vfuncs");
        gjs_dumpstack();
        return;
    }

    context = gjs_closure_get_context(m_js_function);
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

    JSAutoRealm ar(
        context, JS_GetFunctionObject(gjs_closure_get_callable(m_js_function)));

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
                completed_trampolines.emplace_back(trampoline);
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
    GIArgument* error_argument = nullptr;

    if (g_callable_info_can_throw_gerror(m_info))
        error_argument = args[n_args + c_args_offset];

    if (!callback_closure_inner(context, this_object, &rval, args, &ret_type,
                                n_args, c_args_offset, result)) {
        if (!JS_IsExceptionPending(context)) {
            // "Uncatchable" exception thrown, we have to exit. We may be in a
            // main loop, or maybe not, but there's no way to tell, so we have
            // to exit here instead of propagating the exception back to the
            // original calling JS code.
            uint8_t code;
            if (gjs->should_exit(&code))
                exit(code);

            // Some other uncatchable exception, e.g. out of memory
            JSFunction* fn = gjs_closure_get_callable(m_js_function);
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

        // If the callback has a GError** argument and invoking the closure
        // returned an error, try to make a GError from it
        if (error_argument && rval.isObject()) {
            JS::RootedObject exc_object(context, &rval.toObject());
            GError* local_error =
                gjs_gerror_make_from_error(context, exc_object);

            if (local_error) {
                // the GError ** pointer is the last argument, and is not
                // included in the n_args
                auto* gerror = gjs_arg_get<GError**>(error_argument);
                g_propagate_error(gerror, local_error);
                JS_ClearPendingException(context);  // don't log
            }
        } else if (!rval.isUndefined()) {
            JS_SetPendingException(context, rval);
        }
        gjs_log_exception_uncaught(context);
    }
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
                JS::RootedValue length(context);

                g_callable_info_load_arg(m_info, array_length_pos,
                                         &array_length_arg);
                g_arg_info_load_type(&array_length_arg, &arg_type_info);
                if (!gjs_value_from_g_argument(context, &length, &arg_type_info,
                                               args[array_length_pos + c_args_offset],
                                               true))
                    return false;

                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                if (!gjs_value_from_explicit_array(context, jsargs[n_jsargs++],
                                                   &type_info,
                                                   args[i + c_args_offset],
                                                   length.toInt32()))
                    return false;
                break;
            }
            case PARAM_NORMAL: {
                if (!jsargs.growBy(1))
                    g_error("Unable to grow vector");

                GIArgument* arg = args[i + c_args_offset];
                if (g_arg_info_get_direction(&arg_info) == GI_DIRECTION_INOUT)
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

    if (!gjs_closure_invoke(m_js_function, this_object, jsargs, rval, true))
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

            if (!gjs_value_to_arg(context, rval, &arg_info,
                                  *reinterpret_cast<GIArgument **>(args[i + c_args_offset])))
                return false;

            break;
        }
    } else {
        bool is_array = rval.isObject();
        if (!JS::IsArrayObject(context, rval, &is_array))
            return false;

        if (!is_array) {
            JSFunction* fn = gjs_closure_get_callable(m_js_function);
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

            if (!gjs_value_to_arg(context, elem, &arg_info,
                                  *(GIArgument **)args[i + c_args_offset]))
                return false;

            elem_idx++;
        }
    }

    return true;
}

GjsCallbackTrampoline* gjs_callback_trampoline_new(
    JSContext* context, JS::HandleFunction function,
    GICallableInfo* callable_info, GIScopeType scope, bool has_scope_object,
    bool is_vfunc) {
    g_assert(function);

    GjsAutoCallbackTrampoline trampoline =
        new GjsCallbackTrampoline(callable_info, scope, is_vfunc);

    if (!trampoline->initialize(context, function, has_scope_object))
        return nullptr;

    return trampoline.release();
}

GjsCallbackTrampoline::GjsCallbackTrampoline(GICallableInfo* callable_info,
                                             GIScopeType scope, bool is_vfunc)
    : m_info(g_base_info_ref(callable_info)),
      m_scope(scope),
      m_param_types(g_callable_info_get_n_args(callable_info), {}),
      m_is_vfunc(is_vfunc) {
    g_atomic_ref_count_init(&ref_count);
}

GjsCallbackTrampoline::~GjsCallbackTrampoline() {
    g_assert(g_atomic_ref_count_compare(&ref_count, 0));

    if (m_info && m_closure)
        g_callable_info_free_closure(m_info, m_closure);
}

bool GjsCallbackTrampoline::initialize(JSContext* cx,
                                       JS::HandleFunction function,
                                       bool has_scope_object) {
    g_assert(!m_js_function);
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
                gjs_throw(cx,
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
                        gjs_throw(cx,
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

    m_closure = g_callable_info_prepare_closure(
        m_info, &m_cif,
        [](ffi_cif*, void* result, void** ffi_args, void* data) {
            auto** args = reinterpret_cast<GIArgument**>(ffi_args);
            g_assert(data && "Trampoline data is not set");
            GjsAutoCallbackTrampoline trampoline(
                static_cast<GjsCallbackTrampoline*>(data),
                GjsAutoTakeOwnership());

            trampoline->callback_closure(args, result);
        },
        this);

    // The rule is:
    // - notify callbacks in GObject methods are traced from the scope object
    // - async and call callbacks, and other notify callbacks, are rooted
    // - vfuncs are traced from the GObject prototype
    bool should_root = m_scope != GI_SCOPE_TYPE_NOTIFIED || !has_scope_object;
    m_js_function = gjs_closure_new(cx, function, m_info.name(), should_root);

    return true;
}

/* Intended for error messages. Return value must be freed */
[[nodiscard]] static char* format_function_name(Function* function) {
    if (g_callable_info_is_method(function->info))
        return g_strdup_printf(
            "method %s.%s.%s", g_base_info_get_namespace(function->info),
            g_base_info_get_name(g_base_info_get_container(function->info)),
            g_base_info_get_name(function->info));
    return g_strdup_printf("function %s.%s",
                           g_base_info_get_namespace(function->info),
                           g_base_info_get_name(function->info));
}

void gjs_function_clear_async_closures() { completed_trampolines.clear(); }

static void* get_return_ffi_pointer_from_giargument(
    GjsArgumentCache* return_arg, GIFFIReturnValue* return_value) {
    // This should be the inverse of gi_type_info_extract_ffi_return_value().
    if (return_arg->skip_out)
        return nullptr;

    // FIXME: Note that v_long and v_ulong don't have type-safe template
    // overloads yet, and I don't understand why they won't compile
    switch (g_type_info_get_tag(&return_arg->type_info)) {
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_INT32:
            return &return_value->v_long;
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_UNICHAR:
            return &return_value->v_ulong;
        case GI_TYPE_TAG_INT64:
            return &gjs_arg_member<int64_t>(return_value);
        case GI_TYPE_TAG_UINT64:
            return &gjs_arg_member<uint64_t>(return_value);
        case GI_TYPE_TAG_FLOAT:
            return &gjs_arg_member<float>(return_value);
        case GI_TYPE_TAG_DOUBLE:
            return &gjs_arg_member<double>(return_value);
        case GI_TYPE_TAG_INTERFACE: {
            GjsAutoBaseInfo info =
                g_type_info_get_interface(&return_arg->type_info);

            switch (g_base_info_get_type(info)) {
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                    return &return_value->v_long;
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
GJS_JSAPI_RETURN_CONVENTION
static bool gjs_invoke_c_function(JSContext* context, Function* function,
                                  const JS::CallArgs& args,
                                  JS::HandleObject this_obj = nullptr,
                                  GIArgument* r_value = nullptr) {
    g_assert((args.isConstructing() || !this_obj) &&
             "If not a constructor, then pass the 'this' object via CallArgs");

    void* return_value_p;  // will point inside the return GIArgument union
    GIFFIReturnValue return_value;

    int gi_argc, gi_arg_pos;
    bool can_throw_gerror;
    bool did_throw_gerror = false;
    GError *local_error = NULL;
    bool failed, postinvoke_release_failed;

    bool is_method;
    JS::RootedValueVector return_values(context);

    is_method = g_callable_info_is_method(function->info);
    can_throw_gerror = g_callable_info_can_throw_gerror(function->info);

    unsigned ffi_argc = function->invoker.cif.nargs;
    gi_argc = g_callable_info_get_n_args( (GICallableInfo*) function->info);
    if (gi_argc > GjsArgumentCache::MAX_ARGS) {
        GjsAutoChar name = format_function_name(function);
        gjs_throw(context, "Function %s has too many arguments", name.get());
        return false;
    }

    // ffi_argc is the number of arguments that the underlying C function takes.
    // gi_argc is the number of arguments the GICallableInfo describes (which
    // does not include "this" or GError**). function->js_in_argc is the number
    // of arguments we expect the JS function to take (which does not include
    // PARAM_SKIPPED args).
    // args.length() is the number of arguments that were actually passed.
    if (args.length() > function->js_in_argc) {
        GjsAutoChar name = format_function_name(function);

        if (!JS::WarnUTF8(context,
                          "Too many arguments to %s: expected %u, got %u",
                          name.get(), function->js_in_argc, args.length()))
            return false;
    } else if (args.length() < function->js_in_argc) {
        GjsAutoChar name = format_function_name(function);

        args.reportMoreArgsNeeded(context, name, function->js_in_argc,
                                  args.length());
        return false;
    }

    // These arrays hold argument pointers.
    // - state.in_cvalues: C values which are passed on input (in or inout)
    // - state.out_cvalues: C values which are returned as arguments (out or
    //   inout)
    // - state.inout_original_cvalues: For the special case of (inout) args, we
    //   need to keep track of the original values we passed into the function,
    //   in case we need to free it.
    // - ffi_arg_pointers: For passing data to FFI, we need to create another
    //   layer of indirection; this array is a pointer to an element in
    //   state.in_cvalues or state.out_cvalues.
    // - return_value: The actual return value of the C function, i.e. not an
    //   (out) param
    //
    // The 3 GIArgument arrays are indexed by the GI argument index, with the
    // following exceptions:
    // - [-1] is the return value (which can be nothing/garbage if the function
    //   function returns void)
    // - [-2] is the instance parameter, if present
    // ffi_arg_pointers, on the other hand, represents the actual C arguments,
    // in the way ffi expects them.
    //
    // Use gi_arg_pos to index inside the GIArgument array. Use ffi_arg_pos to
    // index inside ffi_arg_pointers.
    GjsFunctionCallState state(context);
    if (is_method) {
        state.in_cvalues = g_newa(GIArgument, gi_argc + 2) + 2;
        state.out_cvalues = g_newa(GIArgument, gi_argc + 2) + 2;
        state.inout_original_cvalues = g_newa(GIArgument, gi_argc + 2) + 2;
    } else {
        state.in_cvalues = g_newa(GIArgument, gi_argc + 1) + 1;
        state.out_cvalues = g_newa(GIArgument, gi_argc + 1) + 1;
        state.inout_original_cvalues = g_newa(GIArgument, gi_argc + 1) + 1;
    }

    void** ffi_arg_pointers = g_newa(void*, ffi_argc);

    failed = false;
    unsigned ffi_arg_pos = 0;  // index into ffi_arg_pointers
    unsigned js_arg_pos = 0;   // index into args

    JS::RootedObject obj(context, this_obj);
    if (!args.isConstructing() && !args.computeThis(context, &obj))
        return false;

    if (is_method) {
        GjsArgumentCache* cache = &function->arguments[-2];
        GIArgument* in_value = &state.in_cvalues[-2];
        JS::RootedValue in_js_value(context, JS::ObjectValue(*obj));

        if (!cache->marshallers->in(context, cache, &state, in_value,
                                    in_js_value))
            return false;

        ffi_arg_pointers[ffi_arg_pos] = in_value;
        ++ffi_arg_pos;

        // Callback lifetimes will be attached to the instance object if it is
        // a GObject or GInterface
        if (cache->contents.info) {
            GType gtype =
                g_registered_type_info_get_g_type(cache->contents.info);
            if (g_type_is_a(gtype, G_TYPE_OBJECT) ||
                g_type_is_a(gtype, G_TYPE_INTERFACE))
                state.instance_object = obj;
        }
    }

    unsigned processed_c_args = ffi_arg_pos;
    for (gi_arg_pos = 0; gi_arg_pos < gi_argc; gi_arg_pos++, ffi_arg_pos++) {
        GjsArgumentCache* cache = &function->arguments[gi_arg_pos];
        GIArgument* in_value = &state.in_cvalues[gi_arg_pos];

        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "Marshalling argument '%s' in, %d/%d GI args, %u/%u "
                          "C args, %u/%u JS args",
                          cache->arg_name, gi_arg_pos, gi_argc, ffi_arg_pos,
                          ffi_argc, js_arg_pos, args.length());

        ffi_arg_pointers[ffi_arg_pos] = in_value;

        if (!cache->marshallers->in) {
            gjs_throw(context,
                      "Error invoking %s.%s: impossible to determine what "
                      "to pass to the '%s' argument. It may be that the "
                      "function is unsupported, or there may be a bug in "
                      "its annotations.",
                      g_base_info_get_namespace(function->info),
                      g_base_info_get_name(function->info), cache->arg_name);
            failed = true;
            break;
        }

        JS::RootedValue js_in_arg(context);
        if (js_arg_pos < args.length())
            js_in_arg = args[js_arg_pos];

        if (!cache->marshallers->in(context, cache, &state, in_value,
                                    js_in_arg)) {
            failed = true;
            break;
        }

        if (!cache->skip_in)
            js_arg_pos++;

        processed_c_args++;
    }

    // This pointer needs to exist on the stack across the ffi_call() call
    GError** errorp = &local_error;

    /* Did argument conversion fail?  In that case, skip invocation and jump to release
     * processing. */
    if (failed) {
        did_throw_gerror = false;
        goto release;
    }

    if (can_throw_gerror) {
        g_assert(ffi_arg_pos < ffi_argc && "GError** argument number mismatch");
        ffi_arg_pointers[ffi_arg_pos] = &errorp;
        ffi_arg_pos++;

        /* don't update processed_c_args as we deal with local_error
         * separately */
    }

    g_assert_cmpuint(ffi_arg_pos, ==, ffi_argc);
    g_assert_cmpuint(gi_arg_pos, ==, gi_argc);

    return_value_p = get_return_ffi_pointer_from_giargument(
        &function->arguments[-1], &return_value);
    ffi_call(&(function->invoker.cif), FFI_FN(function->invoker.native_address),
             return_value_p, ffi_arg_pointers);

    /* Return value and out arguments are valid only if invocation doesn't
     * return error. In arguments need to be released always.
     */
    if (can_throw_gerror) {
        did_throw_gerror = local_error != NULL;
    } else {
        did_throw_gerror = false;
    }

    if (!r_value)
        args.rval().setUndefined();

    if (!function->arguments[-1].skip_out) {
        gi_type_info_extract_ffi_return_value(
            &function->arguments[-1].type_info, &return_value,
            &state.out_cvalues[-1]);
    }

    // Process out arguments and return values. This loop is skipped if we fail
    // the type conversion above, or if did_throw_gerror is true.
    js_arg_pos = 0;
    for (gi_arg_pos = -1; gi_arg_pos < gi_argc; gi_arg_pos++) {
        GjsArgumentCache* cache = &function->arguments[gi_arg_pos];
        GIArgument* out_value = &state.out_cvalues[gi_arg_pos];

        gjs_debug_marshal(GJS_DEBUG_GFUNCTION,
                          "Marshalling argument '%s' out, %d/%d GI args",
                          cache->arg_name, gi_arg_pos, gi_argc);

        JS::RootedValue js_out_arg(context);
        if (!r_value) {
            if (!cache->marshallers->out(context, cache, &state, out_value,
                                         &js_out_arg)) {
                failed = true;
                break;
            }
        }

        if (!cache->skip_out) {
            if (!r_value) {
                if (!return_values.append(js_out_arg)) {
                    JS_ReportOutOfMemory(context);
                    failed = true;
                    break;
                }
            }
            js_arg_pos++;
        }
    }

    g_assert(failed || did_throw_gerror || js_arg_pos == function->js_out_argc);

release:
    // If we failed before calling the function, or if the function threw an
    // exception, then any GI_TRANSFER_EVERYTHING or GI_TRANSFER_CONTAINER
    // in-parameters were not transferred. Treat them as GI_TRANSFER_NOTHING so
    // that they are freed.
    if (!failed && !did_throw_gerror)
        state.call_completed = true;

    // In this loop we use ffi_arg_pos just to ensure we don't release stuff
    // we haven't allocated yet, if we failed in type conversion above.
    // If we start from -1 (the return value), we need to process 1 more than
    // processed_c_args.
    // If we start from -2 (the instance parameter), we need to process 2 more
    ffi_arg_pos = is_method ? 1 : 0;
    unsigned ffi_arg_max = processed_c_args + (is_method ? 2 : 1);
    postinvoke_release_failed = false;
    for (gi_arg_pos = is_method ? -2 : -1;
         gi_arg_pos < gi_argc && ffi_arg_pos < ffi_arg_max;
         gi_arg_pos++, ffi_arg_pos++) {
        GjsArgumentCache* cache = &function->arguments[gi_arg_pos];
        GIArgument* in_value = &state.in_cvalues[gi_arg_pos];
        GIArgument* out_value = &state.out_cvalues[gi_arg_pos];

        gjs_debug_marshal(
            GJS_DEBUG_GFUNCTION,
            "Releasing argument '%s', %d/%d GI args, %u/%u C args",
            cache->arg_name, gi_arg_pos, gi_argc, ffi_arg_pos,
            processed_c_args);

        // Only process in or inout arguments if we failed, the rest is garbage
        if (failed && cache->skip_in)
            continue;

        // Save the return GIArgument if it was requested
        if (r_value && gi_arg_pos == -1) {
            *r_value = *out_value;
            continue;
        }

        if (!cache->marshallers->release(context, cache, &state, in_value,
                                         out_value)) {
            postinvoke_release_failed = true;
            // continue with the release even if we fail, to avoid leaks
        }
    }

    if (postinvoke_release_failed)
        failed = true;

    g_assert(ffi_arg_pos == processed_c_args + (is_method ? 2 : 1));

    if (!r_value && function->js_out_argc > 0 &&
        (!failed && !did_throw_gerror)) {
        // If we have one return value or out arg, return that item on its
        // own, otherwise return a JavaScript array with [return value,
        // out arg 1, out arg 2, ...]
        if (function->js_out_argc == 1) {
            args.rval().set(return_values[0]);
        } else {
            JSObject* array = JS::NewArrayObject(context, return_values);
            if (!array) {
                failed = true;
            } else {
                args.rval().setObject(*array);
            }
        }
    }

    if (!failed && did_throw_gerror) {
        return gjs_throw_gerror(context, local_error);
    } else if (failed) {
        return false;
    } else {
        return true;
    }
}

GJS_JSAPI_RETURN_CONVENTION
static bool
function_call(JSContext *context,
              unsigned   js_argc,
              JS::Value *vp)
{
    JS::CallArgs js_argv = JS::CallArgsFromVp(js_argc, vp);
    JS::RootedObject callee(context, &js_argv.callee());

    Function *priv;

    priv = priv_from_js(context, callee);
    gjs_debug_marshal(GJS_DEBUG_GFUNCTION, "Call callee %p priv %p",
                      callee.get(), priv);

    if (priv == NULL)
        return true; /* we are the prototype, or have the wrong class */

    return gjs_invoke_c_function(context, priv, js_argv);
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(function)

/* Does not actually free storage for structure, just
 * reverses init_cached_function_data
 */
static void
uninit_cached_function_data (Function *function)
{
    if (function->arguments) {
        g_assert(function->info &&
                 "Don't know how to free cache without GI info");

        // Careful! function->arguments is offset by one or two elements inside
        // the allocated space, so we have to free index -1 or -2.
        int start_index = g_callable_info_is_method(function->info) ? -2 : -1;
        int gi_argc = MIN(g_callable_info_get_n_args(function->info),
                          function->js_in_argc + function->js_out_argc);

        for (int i = 0; i < gi_argc; i++) {
            int ix = start_index + i;

            if (!function->arguments[ix].marshallers)
                break;

            if (function->arguments[ix].marshallers->free)
                function->arguments[ix].marshallers->free(
                    &function->arguments[ix]);
        }

        g_free(&function->arguments[start_index]);
        function->arguments = nullptr;
    }

    g_clear_pointer(&function->info, g_base_info_unref);
    g_function_invoker_destroy(&function->invoker);
}

static void function_finalize(JSFreeOp*, JSObject* obj) {
    Function *priv;

    priv = (Function *) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    uninit_cached_function_data(priv);

    GJS_DEC_COUNTER(function);
    g_free(priv);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
get_num_arguments (JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    rec.rval().setInt32(priv->js_in_argc);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
function_to_string (JSContext *context,
                    guint      argc,
                    JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, to, Function, priv);
    int i, n_args, n_jsargs;
    GString *arg_names_str;
    gchar *arg_names;

    if (priv == NULL) {
        JSString* retval = JS_NewStringCopyZ(context, "function () {\n}");
        if (!retval)
            return false;
        rec.rval().setString(retval);
        return true;
    }

    n_args = g_callable_info_get_n_args(priv->info);
    n_jsargs = 0;
    arg_names_str = g_string_new("");
    for (i = 0; i < n_args; i++) {
        if (priv->arguments[i].skip_in)
            continue;

        if (n_jsargs > 0)
            g_string_append(arg_names_str, ", ");

        n_jsargs++;
        g_string_append(arg_names_str, priv->arguments[i].arg_name);
    }
    arg_names = g_string_free(arg_names_str, false);

    GjsAutoChar descr;
    if (g_base_info_get_type(priv->info) == GI_INFO_TYPE_FUNCTION) {
        descr = g_strdup_printf(
            "function %s(%s) {\n\t/* wrapper for native symbol %s(); */\n}",
            g_base_info_get_name(priv->info), arg_names,
            g_function_info_get_symbol(priv->info));
    } else {
        descr = g_strdup_printf(
            "function %s(%s) {\n\t/* wrapper for native symbol */\n}",
            g_base_info_get_name(priv->info), arg_names);
    }

    g_free(arg_names);

    return gjs_string_from_utf8(context, descr, rec.rval());
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_function_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    function_finalize,
    function_call};

struct JSClass gjs_function_class = {
    "GIRepositoryFunction", /* means "new GIRepositoryFunction()" works */
    JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
    &gjs_function_class_ops
};

static JSPropertySpec gjs_function_proto_props[] = {
    JS_PSG("length", get_num_arguments, JSPROP_PERMANENT),
    JS_STRING_SYM_PS(toStringTag, "GIRepositoryFunction", JSPROP_READONLY),
    JS_PS_END};

/* The original Function.prototype.toString complains when
   given a GIRepository function as an argument */
static JSFunctionSpec gjs_function_proto_funcs[] = {
    JS_FN("toString", function_to_string, 0, 0),
    JS_FS_END
};

static JSFunctionSpec *gjs_function_static_funcs = nullptr;

GJS_JSAPI_RETURN_CONVENTION
static bool
init_cached_function_data (JSContext      *context,
                           Function       *function,
                           GType           gtype,
                           GICallableInfo *info)
{
    guint8 i, n_args;
    GError *error = NULL;
    GIInfoType info_type;

    info_type = g_base_info_get_type((GIBaseInfo *)info);

    if (info_type == GI_INFO_TYPE_FUNCTION) {
        if (!g_function_info_prep_invoker((GIFunctionInfo *)info,
                                          &(function->invoker),
                                          &error)) {
            return gjs_throw_gerror(context, error);
        }
    } else if (info_type == GI_INFO_TYPE_VFUNC) {
        gpointer addr;

        addr = g_vfunc_info_get_address((GIVFuncInfo *)info, gtype, &error);
        if (error != NULL) {
            if (error->code != G_INVOKE_ERROR_SYMBOL_NOT_FOUND)
                return gjs_throw_gerror(context, error);

            gjs_throw(context, "Virtual function not implemented: %s",
                      error->message);
            g_clear_error(&error);
            return false;
        }

        if (!g_function_invoker_new_for_address(addr, info,
                                                &(function->invoker),
                                                &error)) {
            return gjs_throw_gerror(context, error);
        }
    }

    bool is_method = g_callable_info_is_method(info);
    n_args = g_callable_info_get_n_args((GICallableInfo*) info);

    // arguments is one or two inside an array of n_args + 2, so
    // arguments[-1] is the return value (which can be skipped if void)
    // arguments[-2] is the instance parameter
    size_t offset = is_method ? 2 : 1;
    GjsArgumentCache* arguments =
        g_new0(GjsArgumentCache, n_args + offset) + offset;

    function->arguments = arguments;
    function->info = g_base_info_ref(info);
    function->js_in_argc = 0;
    function->js_out_argc = 0;

    if (is_method &&
        !gjs_arg_cache_build_instance(context, &arguments[-2], info))
        return false;

    bool inc_counter;
    if (!gjs_arg_cache_build_return(context, &arguments[-1], arguments, info,
                                    &inc_counter))
        return false;

    function->js_out_argc = inc_counter ? 1 : 0;

    for (i = 0; i < n_args; i++) {
        GIDirection direction;
        GIArgInfo arg_info;

        if (arguments[i].skip_in || arguments[i].skip_out)
            continue;

        g_callable_info_load_arg((GICallableInfo*) info, i, &arg_info);
        direction = g_arg_info_get_direction(&arg_info);

        if (!gjs_arg_cache_build_arg(context, &arguments[i], arguments, i,
                                     direction, &arg_info, info, &inc_counter))
            return false;

        if (inc_counter) {
            switch (direction) {
                case GI_DIRECTION_INOUT:
                    function->js_out_argc++;
                    [[fallthrough]];
                case GI_DIRECTION_IN:
                    function->js_in_argc++;
                    break;
                case GI_DIRECTION_OUT:
                    function->js_out_argc++;
                    break;
                default:
                    g_assert_not_reached();
            }
        }
    }

    return true;
}

[[nodiscard]] static inline JSObject* gjs_builtin_function_get_proto(
    JSContext* cx) {
    return JS::GetRealmFunctionPrototype(cx);
}

GJS_DEFINE_PROTO_FUNCS_WITH_PARENT(function, builtin_function)

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
function_new(JSContext      *context,
             GType           gtype,
             GICallableInfo *info)
{
    Function *priv;

    JS::RootedObject proto(context);
    if (!gjs_function_define_proto(context, nullptr, &proto))
        return nullptr;

    JS::RootedObject function(context,
        JS_NewObjectWithGivenProto(context, &gjs_function_class, proto));
    if (!function) {
        gjs_debug(GJS_DEBUG_GFUNCTION, "Failed to construct function");
        return NULL;
    }

    priv = g_new0(Function, 1);

    GJS_INC_COUNTER(function);

    g_assert(priv_from_js(context, function) == NULL);
    JS_SetPrivate(function, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GFUNCTION,
                        "function constructor, obj %p priv %p", function.get(),
                        priv);

    if (!init_cached_function_data(context, priv, gtype, (GICallableInfo *)info))
      return NULL;

    return function;
}

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

    JS::RootedObject function(context, function_new(context, gtype, info));
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
    Function function;

    memset(&function, 0, sizeof(Function));
    if (!init_cached_function_data(context, &function, 0, info))
        return false;

    bool result = gjs_invoke_c_function(context, &function, args, obj, rvalue);
    uninit_cached_function_data(&function);
    return result;
}
