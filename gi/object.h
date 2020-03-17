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

#ifndef GI_OBJECT_H_
#define GI_OBJECT_H_

#include <config.h>

#include <stddef.h>  // for size_t

#include <forward_list>
#include <functional>
#include <vector>

#include <girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <js/GCHashTable.h>  // for GCHashMap
#include <js/PropertySpec.h>
#include <js/TypeDecls.h>

#include "gi/wrapperutils.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "util/log.h"

class GjsAtoms;
namespace JS {
class CallArgs;
}
namespace js {
class SystemAllocPolicy;
}
namespace mozilla {
template <class Key>
struct DefaultHasher;
}
class ObjectInstance;
class ObjectPrototype;

class GjsListLink {
 private:
    ObjectInstance* m_prev;
    ObjectInstance* m_next;

 public:
    GJS_USE ObjectInstance* prev(void) const { return m_prev; }
    GJS_USE ObjectInstance* next(void) const { return m_next; }

    void prepend(ObjectInstance* this_instance, ObjectInstance* head);
    void unlink(void);
    GJS_USE size_t size(void) const;
};

struct AutoGValueVector : public std::vector<GValue> {
    ~AutoGValueVector() {
        for (GValue value : *this)
            g_value_unset(&value);
    }
};

/*
 * ObjectBase:
 *
 * Specialization of GIWrapperBase for GObject instances. See the documentation
 * in wrapperutils.h.
 *
 * It's important that ObjectBase and ObjectInstance not grow in size without a
 * very good reason. There can be tens, maybe hundreds of thousands of these
 * objects alive in a typical gnome-shell run, so even 8 more bytes will add up.
 * It's less critical that ObjectPrototype stay small, since only one of these
 * is allocated per GType.
 */
class ObjectBase
    : public GIWrapperBase<ObjectBase, ObjectPrototype, ObjectInstance> {
    friend class GIWrapperBase<ObjectBase, ObjectPrototype, ObjectInstance>;

 protected:
    /* a list of all GClosures installed on this object (from
     * signals, trampolines, explicit GClosures, and vfuncs on prototypes),
     * used when tracing */
    std::forward_list<GClosure*> m_closures;

    explicit ObjectBase(ObjectPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}

 public:
    static const GjsDebugTopic debug_topic = GJS_DEBUG_GOBJECT;
    static constexpr const char* debug_tag = "GObject";

    static const struct JSClassOps class_ops;
    static const struct JSClass klass;
    static JSFunctionSpec proto_methods[];

    static GObject* to_c_ptr(JSContext* cx, JS::HandleObject obj) = delete;
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_c_ptr(JSContext* cx, JS::HandleObject obj, GObject** ptr);
    GJS_JSAPI_RETURN_CONVENTION
    static bool transfer_to_gi_argument(JSContext* cx, JS::HandleObject obj,
                                        GIArgument* arg,
                                        GIDirection transfer_direction,
                                        GITransfer transfer_ownership,
                                        GType expected_gtype,
                                        GIBaseInfo* expected_info = nullptr);

 private:
    // This is used in debug methods only.
    GJS_USE const void* jsobj_addr(void) const;

    /* Helper methods */

 protected:
    void debug_lifecycle(const char* message) const {
        GIWrapperBase::debug_lifecycle(jsobj_addr(), message);
    }

    GJS_USE bool id_is_never_lazy(jsid name, const GjsAtoms& atoms);
    GJS_USE bool is_custom_js_class();

 public:
    void type_query_dynamic_safe(GTypeQuery* query);

    GJS_JSAPI_RETURN_CONVENTION
    static bool typecheck(JSContext* cx, JS::HandleObject obj,
                          GIObjectInfo* expected_info, GType expected_gtype);
    GJS_USE
    static bool typecheck(JSContext* cx, JS::HandleObject obj,
                          GIObjectInfo* expected_info, GType expected_gtype,
                          GjsTypecheckNoThrow no_throw) {
        return GIWrapperBase::typecheck(cx, obj, expected_info, expected_gtype,
                                        no_throw);
    }

    /* Methods to manipulate the list of closures */

 protected:
    void invalidate_all_closures(void);

 public:
    void associate_closure(JSContext* cx, GClosure* closure);
    static void closure_invalidated_notify(void* data, GClosure* closure);

    /* JSClass operations */

    static bool add_property(JSContext* cx, JS::HandleObject obj,
                             JS::HandleId id, JS::HandleValue value);

 private:
    void trace_impl(JSTracer* tracer);

    /* JS property getters/setters */

 public:
    GJS_JSAPI_RETURN_CONVENTION
    static bool prop_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool prop_setter(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    /* JS methods */

    GJS_JSAPI_RETURN_CONVENTION
    static bool connect(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool connect_after(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool emit(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool signal_find(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool signals_block(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool signals_unblock(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool signals_disconnect(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_USE const char* to_string_kind(void) const;
    GJS_JSAPI_RETURN_CONVENTION
    static bool init_gobject(JSContext* cx, unsigned argc, JS::Value* vp);
    GJS_JSAPI_RETURN_CONVENTION
    static bool hook_up_vfunc(JSContext* cx, unsigned argc, JS::Value* vp);

    /* Quarks */

    GJS_USE static GQuark custom_type_quark(void);
    GJS_USE static GQuark custom_property_quark(void);
};

// See https://bugzilla.mozilla.org/show_bug.cgi?id=1614220
struct IdHasher {
    typedef jsid Lookup;
    static mozilla::HashNumber hash(jsid id) {
        if (MOZ_LIKELY(JSID_IS_ATOM(id)))
            return js::DefaultHasher<JSAtom*>::hash(JSID_TO_ATOM(id));
        if (JSID_IS_SYMBOL(id))
            return js::DefaultHasher<JS::Symbol*>::hash(JSID_TO_SYMBOL(id));
        return mozilla::HashGeneric(JSID_BITS(id));
    }
    static bool match(jsid id1, jsid id2) { return id1 == id2; }
};

class ObjectPrototype
    : public GIWrapperPrototype<ObjectBase, ObjectPrototype, ObjectInstance> {
    friend class GIWrapperPrototype<ObjectBase, ObjectPrototype,
                                    ObjectInstance>;
    friend class GIWrapperBase<ObjectBase, ObjectPrototype, ObjectInstance>;

    using PropertyCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoParam,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
    using FieldCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoInfo<GI_INFO_TYPE_FIELD>,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
    using NegativeLookupCache =
        JS::GCHashSet<jsid, IdHasher, js::SystemAllocPolicy>;

    PropertyCache m_property_cache;
    FieldCache m_field_cache;
    NegativeLookupCache m_unresolvable_cache;

    ObjectPrototype(GIObjectInfo* info, GType gtype);
    ~ObjectPrototype();

    static constexpr InfoType::Tag info_type_tag = InfoType::Object;

 public:
    GJS_USE
    static ObjectPrototype* for_gtype(GType gtype);

    /* Helper methods */
 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool get_parent_proto(JSContext* cx, JS::MutableHandleObject proto) const;

    GJS_USE
    bool is_vfunc_unchanged(GIVFuncInfo* info);
    GJS_JSAPI_RETURN_CONVENTION
    bool lazy_define_gobject_property(JSContext* cx, JS::HandleObject obj,
                                      JS::HandleId id, bool* resolved,
                                      const char* name);

    enum ResolveWhat { ConsiderOnlyMethods, ConsiderMethodsAndProperties };
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_no_info(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                         bool* resolved, const char* name,
                         ResolveWhat resolve_props);
    GJS_JSAPI_RETURN_CONVENTION
    bool uncached_resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                          const char* name, bool* resolved);

 public:
    void set_type_qdata(void);
    GJS_JSAPI_RETURN_CONVENTION
    GParamSpec* find_param_spec_from_id(JSContext* cx, JS::HandleString key);
    GJS_JSAPI_RETURN_CONVENTION
    GIFieldInfo* lookup_cached_field_info(JSContext* cx, JS::HandleString key);
    GJS_JSAPI_RETURN_CONVENTION
    bool props_to_g_parameters(JSContext* cx, JS::HandleObject props,
                               std::vector<const char*>* names,
                               AutoGValueVector* values);

    GJS_JSAPI_RETURN_CONVENTION
    static bool define_class(JSContext* cx, JS::HandleObject in_object,
                             GIObjectInfo* info, GType gtype,
                             JS::MutableHandleObject constructor,
                             JS::MutableHandleObject prototype);

    /* These are currently only needed in the GObject base init and finalize
     * functions, for prototypes, even though m_closures is in ObjectBase. */
    void ref_closures(void) {
        for (GClosure* closure : m_closures)
            g_closure_ref(closure);
    }
    void unref_closures(void) {
        for (GClosure* closure : m_closures)
            g_closure_unref(closure);
    }

    /* JSClass operations */
 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      const char* prop_name, bool* resolved);

    GJS_JSAPI_RETURN_CONVENTION
    bool new_enumerate_impl(JSContext* cx, JS::HandleObject obj,
                            JS::MutableHandleIdVector properties,
                            bool only_enumerable);
    void trace_impl(JSTracer* tracer);

    /* JS methods */
 public:
    GJS_JSAPI_RETURN_CONVENTION
    bool hook_up_vfunc_impl(JSContext* cx, const JS::CallArgs& args,
                            JS::HandleObject prototype);
};

class ObjectInstance : public GIWrapperInstance<ObjectBase, ObjectPrototype,
                                                ObjectInstance, GObject> {
    friend class GIWrapperInstance<ObjectBase, ObjectPrototype, ObjectInstance,
                                   GObject>;
    friend class GIWrapperBase<ObjectBase, ObjectPrototype, ObjectInstance>;
    friend class ObjectBase;  // for add_property, prop_getter, etc.

    // GIWrapperInstance::m_ptr may be null in ObjectInstance.

    GjsMaybeOwned<JSObject*> m_wrapper;

    GjsListLink m_instance_link;

    bool m_wrapper_finalized : 1;
    bool m_gobj_disposed : 1;

    /* True if this object has visible JS state, and thus its lifecycle is
     * managed using toggle references. False if this object just keeps a
     * hard ref on the underlying GObject, and may be finalized at will. */
    bool m_uses_toggle_ref : 1;

    static bool s_weak_pointer_callback;

    /* Constructors */

 private:
    ObjectInstance(JSContext* cx, JS::HandleObject obj);
    ~ObjectInstance();

    GJS_JSAPI_RETURN_CONVENTION
    static ObjectInstance* new_for_gobject(JSContext* cx, GObject* gobj);

    // Extra method to get an existing ObjectInstance from qdata

 public:
    GJS_USE
    static ObjectInstance* for_gobject(GObject* gobj);

    /* Accessors */

 private:
    GJS_USE
    bool has_wrapper(void) const { return !!m_wrapper; }

 public:
    GJS_USE
    JSObject* wrapper(void) const { return m_wrapper; }

    /* Methods to manipulate the JS object wrapper */

 private:
    void discard_wrapper(void) { m_wrapper.reset(); }
    void switch_to_rooted(JSContext* cx) { m_wrapper.switch_to_rooted(cx); }
    void switch_to_unrooted(JSContext* cx) { m_wrapper.switch_to_unrooted(cx); }
    GJS_USE
    bool update_after_gc(void) { return m_wrapper.update_after_gc(); }
    GJS_USE
    bool wrapper_is_rooted(void) const { return m_wrapper.rooted(); }
    void release_native_object(void);
    void associate_js_gobject(JSContext* cx, JS::HandleObject obj,
                              GObject* gobj);
    void disassociate_js_gobject(void);
    void handle_context_dispose(void);
    GJS_USE bool weak_pointer_was_finalized(void);
    static void ensure_weak_pointer_callback(JSContext* cx);
    static void update_heap_wrapper_weak_pointers(JSContext* cx,
                                                  JS::Compartment* compartment,
                                                  void* data);

 public:
    void toggle_down(void);
    void toggle_up(void);

    GJS_JSAPI_RETURN_CONVENTION
    static JSObject* wrapper_from_gobject(JSContext* cx, GObject* ptr);

    /* Helper methods */

 private:
    void set_object_qdata(void);
    void unset_object_qdata(void);
    void check_js_object_finalized(void);
    void ensure_uses_toggle_ref(JSContext* cx);
    GJS_USE bool check_gobject_disposed(const char* for_what) const;
    GJS_JSAPI_RETURN_CONVENTION
    bool signal_match_arguments_from_object(JSContext* cx,
                                            JS::HandleObject props_obj,
                                            GSignalMatchType* mask_out,
                                            unsigned* signal_id_out,
                                            GQuark* detail_out,
                                            JS::MutableHandleFunction func_out);

 public:
    static GObject* copy_ptr(JSContext* G_GNUC_UNUSED, GType G_GNUC_UNUSED,
                             void* ptr) {
        return G_OBJECT(g_object_ref(G_OBJECT(ptr)));
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool init_custom_class_from_gobject(JSContext* cx, JS::HandleObject wrapper,
                                        GObject* gobj);

    /* Methods to manipulate the linked list of instances */

 private:
    static ObjectInstance* wrapped_gobject_list;
    GJS_USE
    ObjectInstance* next(void) const { return m_instance_link.next(); }
    void link(void);
    void unlink(void);
    GJS_USE
    static size_t num_wrapped_gobjects(void) {
        return wrapped_gobject_list
                   ? wrapped_gobject_list->m_instance_link.size()
                   : 0;
    }
    using Action = std::function<void(ObjectInstance*)>;
    using Predicate = std::function<bool(ObjectInstance*)>;
    static void iterate_wrapped_gobjects(const Action& action);
    static void remove_wrapped_gobjects_if(const Predicate& predicate,
                                           const Action& action);

 public:
    GJS_USE GjsListLink* get_link(void) { return &m_instance_link; }
    static void prepare_shutdown(void);

    /* JSClass operations */

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool add_property_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                           JS::HandleValue value);
    void finalize_impl(JSFreeOp* fop, JSObject* obj);

    /* JS property getters/setters */

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool prop_getter_impl(JSContext* cx, JS::HandleString name,
                          JS::MutableHandleValue rval);
    GJS_JSAPI_RETURN_CONVENTION
    bool field_getter_impl(JSContext* cx, JS::HandleString name,
                           JS::MutableHandleValue rval);
    GJS_JSAPI_RETURN_CONVENTION
    bool prop_setter_impl(JSContext* cx, JS::HandleString name,
                          JS::HandleValue value);
    GJS_JSAPI_RETURN_CONVENTION
    bool field_setter_not_impl(JSContext* cx, JS::HandleString name);

    // JS constructor

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

    /* JS methods */

 private:
    GJS_JSAPI_RETURN_CONVENTION
    bool connect_impl(JSContext* cx, const JS::CallArgs& args, bool after);
    GJS_JSAPI_RETURN_CONVENTION
    bool emit_impl(JSContext* cx, const JS::CallArgs& args);
    GJS_JSAPI_RETURN_CONVENTION
    bool signal_find_impl(JSContext* cx, const JS::CallArgs& args);
    GJS_JSAPI_RETURN_CONVENTION
    bool signals_block_impl(JSContext* cx, const JS::CallArgs& args);
    GJS_JSAPI_RETURN_CONVENTION
    bool signals_unblock_impl(JSContext* cx, const JS::CallArgs& args);
    GJS_JSAPI_RETURN_CONVENTION
    bool signals_disconnect_impl(JSContext* cx, const JS::CallArgs& args);
    GJS_JSAPI_RETURN_CONVENTION
    bool init_impl(JSContext* cx, const JS::CallArgs& args,
                   JS::MutableHandleObject obj);
    GJS_USE const char* to_string_kind(void) const;

    GJS_JSAPI_RETURN_CONVENTION
    bool typecheck_impl(JSContext* cx, GIBaseInfo* expected_info,
                        GType expected_type) const;

    /* Notification callbacks */

 public:
    void gobj_dispose_notify(void);
    static void context_dispose_notify(void* data,
                                       GObject* where_the_object_was);
};

GJS_JSAPI_RETURN_CONVENTION
bool gjs_lookup_object_constructor(JSContext             *context,
                                   GType                  gtype,
                                   JS::MutableHandleValue value_p);
GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_lookup_object_constructor_from_info(JSContext* cx,
                                                  GIObjectInfo* info,
                                                  GType gtype);

void gjs_object_clear_toggles(void);
void gjs_object_shutdown_toggle_queue(void);

#endif  // GI_OBJECT_H_
