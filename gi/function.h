/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_FUNCTION_H_
#define GI_FUNCTION_H_

#include <config.h>

#include <unordered_set>
#include <vector>

#include <ffi.h>
#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

namespace JS {
class CallArgs;
}

typedef enum {
    PARAM_NORMAL,
    PARAM_SKIPPED,
    PARAM_ARRAY,
    PARAM_CALLBACK,
    PARAM_UNKNOWN,
} GjsParamType;

using GjsAutoGClosure =
    GjsAutoPointer<GClosure, GClosure, g_closure_unref, g_closure_ref>;

struct GjsCallbackTrampoline {
    GjsCallbackTrampoline(GICallableInfo* callable_info, GIScopeType scope,
                          bool is_vfunc);
    ~GjsCallbackTrampoline();

    constexpr GClosure* js_function() { return m_js_function; }
    constexpr ffi_closure* closure() { return m_closure; }

    gatomicrefcount ref_count;

    bool initialize(JSContext* cx, JS::HandleFunction function,
                    bool has_scope_object);

 private:
    void callback_closure(GIArgument** args, void* result);
    GJS_JSAPI_RETURN_CONVENTION
    bool callback_closure_inner(JSContext* cx, JS::HandleObject this_object,
                                JS::MutableHandleValue rval, GIArgument** args,
                                GITypeInfo* ret_type, int n_args,
                                int c_args_offset, void* result);
    void warn_about_illegal_js_callback(const char* when, const char* reason);

    GjsAutoCallableInfo m_info;
    GjsAutoGClosure m_js_function;

    ffi_closure* m_closure = nullptr;
    GIScopeType m_scope;
    std::vector<GjsParamType> m_param_types;

    bool m_is_vfunc;
    ffi_cif m_cif;
};

GJS_JSAPI_RETURN_CONVENTION
GjsCallbackTrampoline* gjs_callback_trampoline_new(
    JSContext* cx, JS::HandleFunction function, GICallableInfo* callable_info,
    GIScopeType scope, bool has_scope_object, bool is_vfunc);

void gjs_callback_trampoline_unref(GjsCallbackTrampoline *trampoline);
GjsCallbackTrampoline* gjs_callback_trampoline_ref(
    GjsCallbackTrampoline* trampoline);

using GjsAutoCallbackTrampoline =
    GjsAutoPointer<GjsCallbackTrampoline, GjsCallbackTrampoline,
                   gjs_callback_trampoline_unref, gjs_callback_trampoline_ref>;

// Stack allocation only!
struct GjsFunctionCallState {
    GIArgument* in_cvalues;
    GIArgument* out_cvalues;
    GIArgument* inout_original_cvalues;
    std::unordered_set<GIArgument*> ignore_release;
    JS::RootedObject instance_object;
    int gi_argc;
    bool call_completed : 1;
    bool is_method : 1;

    GjsFunctionCallState(JSContext* cx, GICallableInfo* callable, int args)
        : instance_object(cx),
          gi_argc(args),
          call_completed(false),
          is_method(g_callable_info_is_method(callable)) {
        int size = gi_argc + first_arg_offset();
        in_cvalues = new GIArgument[size] + first_arg_offset();
        out_cvalues = new GIArgument[size] + first_arg_offset();
        inout_original_cvalues = new GIArgument[size] + first_arg_offset();
    }

    ~GjsFunctionCallState() {
        delete[](in_cvalues - first_arg_offset());
        delete[](out_cvalues - first_arg_offset());
        delete[](inout_original_cvalues - first_arg_offset());
    }

    constexpr int first_arg_offset() const { return is_method ? 2 : 1; }
};

GJS_JSAPI_RETURN_CONVENTION
JSObject *gjs_define_function(JSContext       *context,
                              JS::HandleObject in_object,
                              GType            gtype,
                              GICallableInfo  *info);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_invoke_constructor_from_c(JSContext* cx, GIFunctionInfo* info,
                                   JS::HandleObject this_obj,
                                   const JS::CallArgs& args,
                                   GIArgument* rvalue);

void gjs_function_clear_async_closures();

#endif  // GI_FUNCTION_H_
