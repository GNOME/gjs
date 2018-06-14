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

#ifndef __GJS_OBJECT_H__
#define __GJS_OBJECT_H__

#include <glib-object.h>
#include <girepository.h>

#include <functional>
#include <set>
#include <stack>
#include <vector>

#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"

#include "js/GCHashTable.h"

class ObjectInstance;

class GjsListLink {
 private:
    ObjectInstance* m_prev;
    ObjectInstance* m_next;

 public:
    ObjectInstance* prev(void) const { return m_prev; }
    ObjectInstance* next(void) const { return m_next; }

    void prepend(ObjectInstance* this_instance, ObjectInstance* head);
    void unlink(void);
    size_t size(void) const;
};

struct AutoGValueVector : public std::vector<GValue> {
    ~AutoGValueVector() {
        for (GValue value : *this)
            g_value_unset(&value);
    }
};

class ObjectInstance {
    GIObjectInfo* m_info;
    GObject* m_gobj;  // nullptr if we are the prototype and not an instance
    GjsMaybeOwned<JSObject*> m_wrapper;
    GType m_gtype;

    /* a list of all GClosures installed on this object (from
     * signals, trampolines and explicit GClosures), used when tracing */
    std::set<GClosure*> m_closures;

    /* the GObjectClass wrapped by this JS Object (only used for
       prototypes) */
    GTypeClass* m_class;

    GjsListLink m_instance_link;

    bool m_wrapper_finalized : 1;
    bool m_gobj_disposed : 1;

    /* True if this object has visible JS state, and thus its lifecycle is
     * managed using toggle references. False if this object just keeps a
     * hard ref on the underlying GObject, and may be finalized at will. */
    bool m_uses_toggle_ref : 1;

 public:
    static std::stack<JS::PersistentRootedObject> object_init_list;

    /* Static methods to get an existing ObjectInstance */

 private:
    static ObjectInstance* for_js_prototype(JSContext* cx,
                                            JS::HandleObject obj);

 public:
    static ObjectInstance* for_gobject(GObject* gobj);
    static ObjectInstance* for_js(JSContext* cx, JS::HandleObject obj);
    static ObjectInstance* for_js_nocheck(JSObject* obj);
    static ObjectInstance* for_gtype(GType gtype);  // Prototype-only

    /* Constructors */

 private:
    /* Constructor for instances */
    ObjectInstance(JSContext* cx, JS::HandleObject obj);

 public:
    /* Public constructor for instances (uses GSlice allocator) */
    static ObjectInstance* new_for_js_object(JSContext* cx,
                                             JS::HandleObject obj);

    /* Constructor for prototypes (only called from gjs_define_object_class) */
    ObjectInstance(JSObject* prototype, GIObjectInfo* info, GType gtype);

    /* Accessors */

 private:
    bool is_prototype(void) const { return !m_gobj; }
    bool is_custom_js_class(void) const { return !m_info; }
    bool has_wrapper(void) const { return !!m_wrapper; }
    const char* ns(void) const {
        return m_info ? g_base_info_get_namespace(m_info) : "";
    }
    const char* name(void) const {
        return m_info ? g_base_info_get_name(m_info) : type_name();
    }
    const char* type_name(void) const { return g_type_name(m_gtype); }

    using PropertyCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoParam,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
    using FieldCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoInfo<GIFieldInfo>,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
    PropertyCache* get_property_cache(void);  // Prototype-only
    FieldCache* get_field_cache(void);        // Prototype-only

 public:
    GType gtype(void) const { return m_gtype; }
    GObject* gobj(void) const { return m_gobj; }
    JSObject* wrapper(void) const { return m_wrapper; }

    /* Methods to manipulate the JS object wrapper */

 private:
    void discard_wrapper(void) { m_wrapper.reset(); }
    void switch_to_rooted(JSContext* cx) { m_wrapper.switch_to_rooted(cx); }
    void switch_to_unrooted(void) { m_wrapper.switch_to_unrooted(); }
    bool update_after_gc(void) { return m_wrapper.update_after_gc(); }

 public:
    bool wrapper_is_rooted(void) const { return m_wrapper.rooted(); }
    void release_native_object(void);
    void associate_js_gobject(JSContext* cx, JS::HandleObject obj,
                              GObject* gobj);
    void disassociate_js_gobject(void);
    bool weak_pointer_was_finalized(void);
    void toggle_down(void);
    void toggle_up(void);

    /* Methods to manipulate the list of closures */

 private:
    void invalidate_all_closures(void);

 public:
    void associate_closure(JSContext* cx, GClosure* closure);
    void ref_closures(void) {
        for (GClosure* closure : m_closures)
            g_closure_ref(closure);
    }
    void unref_closures(void) {
        for (GClosure* closure : m_closures)
            g_closure_unref(closure);
    }

    /* Helper methods for both prototypes and instances */

 private:
    bool check_is_instance(JSContext* cx, const char* for_what) const;
    void debug_lifecycle(const char* message) const {
        gjs_debug_lifecycle(
            GJS_DEBUG_GOBJECT, "[%p: GObject %p JS wrapper %p %s.%s (%s)] %s",
            this, m_gobj, m_wrapper.get(), ns(), name(), type_name(), message);
    }
    void debug_jsprop_base(const char* message, const char* id,
                           JSObject* obj) const {
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "[%p: GObject %p JS object %p %s.%s (%s)] %s '%s'",
                         this, m_gobj, obj, ns(), name(), type_name(), message,
                         id);
    }
    void debug_jsprop(const char* message, jsid id, JSObject* obj) const {
        debug_jsprop_base(message, gjs_debug_id(id).c_str(), obj);
    }
    void debug_jsprop(const char* message, JSString* id, JSObject* obj) const {
        debug_jsprop_base(message, gjs_debug_string(id).c_str(), obj);
    }
    static void debug_jsprop_static(const char* message, jsid id,
                                    JSObject* obj) {
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "[JS object %p] %s '%s', no instance associated", obj,
                         message, gjs_debug_id(id).c_str());
    }

 public:
    void type_query_dynamic_safe(GTypeQuery* query);

    /* Instance-only helper methods */

 private:
    void set_object_qdata(void);
    void unset_object_qdata(void);
    void check_js_object_finalized(void);

 public:
    void ensure_uses_toggle_ref(JSContext* cx);
    bool check_gobject_disposed(const char* for_what) const;

    /* Prototype-only helper methods */

 private:
    GParamSpec* find_param_spec_from_id(JSContext* cx, JS::HandleString key);
    GIFieldInfo* find_field_info_from_id(JSContext* cx, JS::HandleString key);
    bool props_to_g_parameters(JSContext* cx, const JS::HandleValueArray& args,
                               std::vector<const char*>* names,
                               AutoGValueVector* values);
    bool is_vfunc_unchanged(GIVFuncInfo* info);
    bool resolve_no_info(JSContext* cx, JS::HandleObject obj, bool* resolved,
                         const char* name);

 public:
    void set_type_qdata(void);

    /* Methods to manipulate the linked list of instances */

 private:
    static ObjectInstance* wrapped_gobject_list;
    ObjectInstance* next(void) const { return m_instance_link.next(); }
    void link(void);
    void unlink(void);

 public:
    GjsListLink* get_link(void) { return &m_instance_link; }
    static size_t num_wrapped_gobjects(void) {
        return wrapped_gobject_list
                   ? wrapped_gobject_list->m_instance_link.size()
                   : 0;
    }
    using Action = std::function<void(ObjectInstance*)>;
    using Predicate = std::function<bool(ObjectInstance*)>;
    static void iterate_wrapped_gobjects(Action action);
    static void remove_wrapped_gobjects_if(Predicate predicate, Action action);

    /* JSClass operations */

 private:
    bool add_property_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                           JS::HandleValue value);
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);
    void trace_impl(JSTracer* tracer);
    void finalize_impl(JSFreeOp* fop, JSObject* obj);

 public:
    static bool add_property(JSContext* cx, JS::HandleObject obj,
                             JS::HandleId id, JS::HandleValue value);
    static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                        bool* resolved);
    static void finalize(JSFreeOp* fop, JSObject* obj);
    static void trace(JSTracer* tracer, JSObject* obj);

    /* JS property getters/setters */

 private:
    bool prop_getter_impl(JSContext* cx, JS::HandleObject obj,
                          JS::HandleString name, JS::MutableHandleValue rval);
    bool field_getter_impl(JSContext* cx, JS::HandleObject obj,
                           JS::HandleString name, JS::MutableHandleValue rval);
    bool prop_setter_impl(JSContext* cx, JS::HandleObject obj,
                          JS::HandleString name, JS::HandleValue value);
    bool field_setter_impl(JSContext* cx, JS::HandleObject obj,
                           JS::HandleString name, JS::HandleValue value);
    static bool prop_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool prop_setter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    /* JS methods */

 private:
    bool connect_impl(JSContext* cx, const JS::CallArgs& args, bool after);
    bool emit_impl(JSContext* cx, const JS::CallArgs& args);
    bool to_string_impl(JSContext* cx, const JS::CallArgs& args);
    bool init_impl(JSContext* cx, const JS::CallArgs& args,
                   JS::MutableHandleObject obj);
    bool hook_up_vfunc_impl(JSContext* cx, const JS::CallArgs& args);

 public:
    static bool connect(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool connect_after(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool emit(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool init(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool hook_up_vfunc(JSContext* cx, unsigned argc, JS::Value* vp);

    /* Methods connected to "public" API */
 private:
    static JS::PersistentRootedSymbol hook_up_vfunc_root;

 public:
    bool typecheck_object(JSContext* cx, GType expected_type, bool throw_error);
    static JS::Symbol* hook_up_vfunc_symbol(JSContext* cx);

    /* Notification callbacks */

    void gobj_dispose_notify(void);
    void context_dispose_notify(void);
    static void closure_invalidated_notify(void* data, GClosure* closure) {
        static_cast<ObjectInstance*>(data)->m_closures.erase(closure);
    }

    /* Quarks */
    static GQuark custom_type_quark(void);
    static GQuark custom_property_quark(void);
};

G_BEGIN_DECLS

bool gjs_define_object_class(JSContext              *cx,
                             JS::HandleObject        in_object,
                             GIObjectInfo           *info,
                             GType                   gtype,
                             JS::MutableHandleObject constructor,
                             JS::MutableHandleObject prototype);

bool gjs_lookup_object_constructor(JSContext             *context,
                                   GType                  gtype,
                                   JS::MutableHandleValue value_p);
JSObject* gjs_lookup_object_constructor_from_info(JSContext* cx,
                                                  GIObjectInfo* info,
                                                  GType gtype);

JSObject* gjs_object_from_g_object      (JSContext     *context,
                                         GObject       *gobj);

GObject  *gjs_g_object_from_object(JSContext       *context,
                                   JS::HandleObject obj);

bool      gjs_typecheck_object(JSContext       *context,
                               JS::HandleObject obj,
                               GType            expected_type,
                               bool             throw_error);

bool      gjs_typecheck_is_object(JSContext       *context,
                                  JS::HandleObject obj,
                                  bool             throw_error);

void gjs_object_prepare_shutdown(void);
void gjs_object_clear_toggles(void);
void gjs_object_shutdown_toggle_queue(void);
void gjs_object_context_dispose_notify(void    *data,
                                       GObject *where_the_object_was);

bool gjs_object_define_static_methods(JSContext       *context,
                                      JS::HandleObject constructor,
                                      GType            gtype,
                                      GIObjectInfo    *object_info);

bool gjs_object_associate_closure(JSContext       *cx,
                                  JS::HandleObject obj,
                                  GClosure        *closure);

G_END_DECLS

#endif  /* __GJS_OBJECT_H__ */
