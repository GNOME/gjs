/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GI_FUNCTION_H_
#define GI_FUNCTION_H_

#include <config.h>

#include <stdint.h>

#include <memory>  // for unique_ptr
#include <unordered_set>
#include <vector>

#include <ffi.h>
#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCVector.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <mozilla/Maybe.h>

#include "gi/closure.h"
#include "gi/info.h"
#include "gjs/auto.h"
#include "gjs/gerror-result.h"
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

struct GjsCallbackTrampoline : public Gjs::Closure {
    GJS_JSAPI_RETURN_CONVENTION
    static GjsCallbackTrampoline* create(JSContext*, JS::HandleObject callable,
                                         const GI::CallableInfo, GIScopeType,
                                         bool has_scope_object, bool is_vfunc);

    ~GjsCallbackTrampoline();

    void* closure() const { return m_info.closure_native_address(m_closure); }

    ffi_closure* get_ffi_closure() const {
        return m_closure;
    }

    void mark_forever();

    static void prepare_shutdown();

 private:
    ffi_closure* create_closure();
    GJS_JSAPI_RETURN_CONVENTION bool initialize();
    GjsCallbackTrampoline(JSContext*, JS::HandleObject callable,
                          const GI::CallableInfo, GIScopeType,
                          bool has_scope_object, bool is_vfunc);

    void callback_closure(GIArgument** args, void* result);
    GJS_JSAPI_RETURN_CONVENTION
    bool callback_closure_inner(JSContext* cx, JS::HandleObject this_object,
                                GObject* gobject, JS::MutableHandleValue rval,
                                GIArgument** args, const GI::TypeInfo ret_type,
                                unsigned n_args, unsigned c_args_offset,
                                void* result);
    void warn_about_illegal_js_callback(const char* when, const char* reason,
                                        bool dump_stack);

    static std::vector<Gjs::AutoGClosure> s_forever_closure_list;

    GI::AutoCallableInfo m_info;
    ffi_closure* m_closure = nullptr;
    std::unique_ptr<GjsParamType[]> m_param_types;
    ffi_cif m_cif;

    GIScopeType m_scope : 3;
    bool m_is_vfunc : 1;
};

// Stack allocation only!
class GjsFunctionCallState {
    Gjs::AutoCppPointer<GIArgument[]> m_in_cvalues;
    Gjs::AutoCppPointer<GIArgument[]> m_out_cvalues;
    Gjs::AutoCppPointer<GIArgument[]> m_inout_original_cvalues;

 public:
    std::unordered_set<GIArgument*> ignore_release;
    JS::RootedObject instance_object;
    JS::RootedVector<JS::Value> return_values;
    Gjs::AutoError local_error;
    const GI::CallableInfo info;
    uint8_t gi_argc = 0;
    uint8_t processed_c_args = 0;
    bool failed : 1;
    bool can_throw_gerror : 1;
    bool is_method : 1;

    GjsFunctionCallState(JSContext* cx, const GI::CallableInfo callable)
        : instance_object(cx),
          return_values(cx),
          info(callable),
          gi_argc(callable.n_args()),
          failed(false),
          can_throw_gerror(callable.can_throw_gerror()),
          is_method(callable.is_method()) {
        int size = gi_argc + first_arg_offset();
        m_in_cvalues = new GIArgument[size];
        m_out_cvalues = new GIArgument[size];
        m_inout_original_cvalues = new GIArgument[size];
    }

    GjsFunctionCallState(const GjsFunctionCallState&) = delete;
    GjsFunctionCallState& operator=(const GjsFunctionCallState&) = delete;

    constexpr int first_arg_offset() const { return is_method ? 2 : 1; }

    // The list always contains the return value, and the arguments
    constexpr GIArgument* instance() {
        return is_method ? &m_in_cvalues[1] : nullptr;
    }

    constexpr GIArgument* return_value() { return &m_out_cvalues[0]; }

    constexpr GIArgument& in_cvalue(int index) const {
        return m_in_cvalues[index + first_arg_offset()];
    }

    constexpr GIArgument& out_cvalue(int index) const {
        return m_out_cvalues[index + first_arg_offset()];
    }

    constexpr GIArgument& inout_original_cvalue(int index) const {
        return m_inout_original_cvalues[index + first_arg_offset()];
    }

    constexpr bool did_throw_gerror() const {
        return can_throw_gerror && local_error;
    }

    constexpr bool call_completed() { return !failed && !did_throw_gerror(); }

    constexpr unsigned last_processed_index() {
        return first_arg_offset() + processed_c_args;
    }

    [[nodiscard]] Gjs::AutoChar display_name() {
        mozilla::Maybe<const GI::BaseInfo> container = info.container();
        if (container) {
            return g_strdup_printf("%s.%s.%s", container->ns(),
                                   container->name(), info.name());
        }
        return g_strdup_printf("%s.%s", info.ns(), info.name());
    }
};

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_define_function(JSContext*, JS::HandleObject in_object, GType,
                              const GI::CallableInfo);

GJS_JSAPI_RETURN_CONVENTION
bool gjs_invoke_constructor_from_c(JSContext*, const GI::FunctionInfo,
                                   JS::HandleObject this_obj,
                                   const JS::CallArgs&, GIArgument* rvalue);

#endif  // GI_FUNCTION_H_
