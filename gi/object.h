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

#include <forward_list>
#include <functional>
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

class ObjectPrototype;

/* To conserve memory, we have two different kinds of private data for GObject
 * JS wrappers: ObjectInstance, and ObjectPrototype. Both inherit from
 * ObjectBase for their common functionality.
 *
 * It's important that ObjectBase and ObjectInstance not grow in size without a
 * very good reason. There can be tens, maybe hundreds of thousands of these
 * objects alive in a typical gnome-shell run, so even 8 more bytes will add up.
 * It's less critical that ObjectPrototype stay small, since only one of these
 * is allocated per GType.
 *
 * Sadly, we cannot have virtual methods in ObjectBase, because SpiderMonkey can
 * be compiled with or without RTTI, so we cannot count on being able to cast
 * ObjectBase to ObjectInstance or ObjectPrototype with dynamic_cast<>, and the
 * vtable would take up just as much space anyway. Instead, we have the
 * to_prototype() and to_instance() methods which will give you a pointer if the
 * ObjectBase is of the correct type (and assert if not.)
 */
class ObjectBase {
 protected:
    /* nullptr if this is an ObjectPrototype; points to the corresponding
     * ObjectPrototype if this is an ObjectInstance */
    ObjectPrototype* m_proto;

    /* a list of all GClosures installed on this object (from
     * signals, trampolines, explicit GClosures, and vfuncs on prototypes),
     * used when tracing */
    std::forward_list<GClosure*> m_closures;

    explicit ObjectBase(ObjectPrototype* proto = nullptr) : m_proto(proto) {}

    /* Methods to get an existing ObjectBase */

 public:
    static ObjectBase* for_js(JSContext* cx, JS::HandleObject obj);
    static ObjectBase* for_js_nocheck(JSObject* obj);

    /* Methods for getting a pointer to the correct subclass. We don't use
     * standard C++ subclasses because that would occupy another 8 bytes in
     * ObjectInstance for a vtable. */

    bool is_prototype(void) const { return !m_proto; }

    /* The to_instance() and to_prototype() methods assert that this ObjectBase
     * is of the correct subclass. If you don't want an assert, then either
     * check beforehand or use get_prototype(). */

    ObjectPrototype* to_prototype(void) {
        g_assert(is_prototype());
        return reinterpret_cast<ObjectPrototype*>(this);
    }
    const ObjectPrototype* to_prototype(void) const {
        g_assert(is_prototype());
        return reinterpret_cast<const ObjectPrototype*>(this);
    }
    ObjectInstance* to_instance(void) {
        g_assert(!is_prototype());
        return reinterpret_cast<ObjectInstance*>(this);
    }
    const ObjectInstance* to_instance(void) const {
        g_assert(!is_prototype());
        return reinterpret_cast<const ObjectInstance*>(this);
    }

    /* get_prototype() doesn't assert. If you call it on an ObjectPrototype, it
     * returns you the same object cast to the correct type; if you call it on
     * an ObjectInstance, it returns you the ObjectPrototype belonging to the
     * corresponding JS prototype. */
    ObjectPrototype* get_prototype(void) {
        return is_prototype() ? to_prototype() : m_proto;
    }
    const ObjectPrototype* get_prototype(void) const {
        return is_prototype() ? to_prototype() : m_proto;
    }

    /* Accessors */

    /* Both ObjectInstance and ObjectPrototype have GIObjectInfo and GType,
     * but for space reasons we store it only on ObjectPrototype. */
    GIObjectInfo* info(void) const;
    GType gtype(void) const;

    const char* ns(void) const {
        return info() ? g_base_info_get_namespace(info()) : "";
    }
    const char* name(void) const {
        return info() ? g_base_info_get_name(info()) : type_name();
    }
    const char* type_name(void) const { return g_type_name(gtype()); }
    bool is_custom_js_class(void) const { return !info(); }

 private:
    /* These are used in debug methods only. */
    const void* gobj_addr(void) const;
    const void* jsobj_addr(void) const;

    /* Helper methods */

 public:
    bool check_is_instance(JSContext* cx, const char* for_what) const;

 protected:
    void debug_lifecycle(const char* message) const {
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                            "[%p: GObject %p JS wrapper %p %s.%s (%s)] %s",
                            this, gobj_addr(), jsobj_addr(), ns(), name(),
                            type_name(), message);
    }
    void debug_jsprop_base(const char* message, const char* id,
                           JSObject* obj) const {
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "[%p: GObject %p JS object %p %s.%s (%s)] %s '%s'",
                         this, gobj_addr(), obj, ns(), name(), type_name(),
                         message, id);
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

    /* Methods to manipulate the list of closures */

 protected:
    void invalidate_all_closures(void);

 public:
    void associate_closure(JSContext* cx, GClosure* closure);
    static void closure_invalidated_notify(void* data, GClosure* closure);

    /* JSClass operations */

    static bool add_property(JSContext* cx, JS::HandleObject obj,
                             JS::HandleId id, JS::HandleValue value);
    static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                        bool* resolved);
    static bool may_resolve(const JSAtomState& names, jsid id,
        JSObject *maybe_obj);
    static void finalize(JSFreeOp* fop, JSObject* obj);
    static void trace(JSTracer* tracer, JSObject* obj);

 protected:
    void trace_impl(JSTracer* tracer);

    /* JS property getters/setters */

 public:
    static bool prop_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool field_getter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool prop_setter(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool field_setter(JSContext* cx, unsigned argc, JS::Value* vp);

    /* JS methods */

    static bool connect(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool connect_after(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool emit(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool to_string(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool init(JSContext* cx, unsigned argc, JS::Value* vp);
    static bool hook_up_vfunc(JSContext* cx, unsigned argc, JS::Value* vp);

    /* Quarks */

    static GQuark custom_type_quark(void);
    static GQuark custom_property_quark(void);
};

class ObjectPrototype : public ObjectBase {
    // ObjectBase needs to call private methods (such as trace_impl) because
    // of the unusual inheritance scheme
    friend class ObjectBase;

    using PropertyCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoParam,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;
    using FieldCache =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoInfo<GIFieldInfo>,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;

    GIObjectInfo* m_info;
    GType m_gtype;

    PropertyCache m_property_cache;
    FieldCache m_field_cache;

    ObjectPrototype(GIObjectInfo* info, GType gtype);
    ~ObjectPrototype();

 public:
    /* Public constructor for instances (uses GSlice allocator) */
    static ObjectPrototype* new_for_js_object(GIObjectInfo* info, GType gtype);

    static ObjectPrototype* for_js(JSContext* cx, JS::HandleObject obj) {
        return ObjectBase::for_js(cx, obj)->to_prototype();
    }
    static ObjectPrototype* for_gtype(GType gtype);
    static ObjectPrototype* for_js_prototype(JSContext* cx,
                                             JS::HandleObject obj);

    /* Helper methods */
 private:
    bool is_vfunc_unchanged(GIVFuncInfo* info);
    bool lazy_define_gobject_property(JSContext* cx, JS::HandleObject obj,
                                      JS::HandleId id, bool* resolved,
                                      const char* name);
    enum ResolveWhat { ConsiderOnlyMethods, ConsiderMethodsAndProperties };
    bool resolve_no_info(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                         bool* resolved, const char* name,
                         ResolveWhat resolve_props);

 public:
    void set_type_qdata(void);
    GParamSpec* find_param_spec_from_id(JSContext* cx, JS::HandleString key);
    GIFieldInfo* find_field_info_from_id(JSContext* cx, JS::HandleString key);
    bool props_to_g_parameters(JSContext* cx, const JS::HandleValueArray& args,
                               std::vector<const char*>* names,
                               AutoGValueVector* values);

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
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved);
    bool may_resolve_impl(JSFlatString *str);
    void trace_impl(JSTracer* tracer);

    /* JS methods */
 private:
    bool to_string_impl(JSContext* cx, const JS::CallArgs& args);
    bool hook_up_vfunc_impl(JSContext* cx, const JS::CallArgs& args,
                            JS::HandleObject prototype);

    ObjectPrototype(const ObjectPrototype& other) = delete;
    ObjectPrototype(ObjectPrototype&& other) = delete;
    ObjectPrototype& operator=(const ObjectPrototype& other) = delete;
    ObjectPrototype& operator=(ObjectPrototype&& other) = delete;
};

class ObjectInstance : public ObjectBase {
    // ObjectBase needs to call private methods (such as trace_impl) because
    // of the unusual inheritance scheme
    friend class ObjectBase;

    GObject* m_gobj;  // may be null
    GjsMaybeOwned<JSObject*> m_wrapper;

    GjsListLink m_instance_link;

    bool m_wrapper_finalized : 1;
    bool m_gobj_disposed : 1;

    /* True if this object has visible JS state, and thus its lifecycle is
     * managed using toggle references. False if this object just keeps a
     * hard ref on the underlying GObject, and may be finalized at will. */
    bool m_uses_toggle_ref : 1;

 public:
    static std::stack<JS::PersistentRootedObject> object_init_list;

    /* Constructors */

 private:
    ObjectInstance(JSContext* cx, JS::HandleObject obj);
    ~ObjectInstance();

 public:
    /* Public constructor for instances (uses GSlice allocator) */
    static ObjectInstance* new_for_js_object(JSContext* cx,
                                             JS::HandleObject obj);

    static ObjectInstance* for_gobject(GObject* gobj);

    /* Accessors */

 private:
    bool has_wrapper(void) const { return !!m_wrapper; }

 public:
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

    /* Helper methods */

 private:
    void set_object_qdata(void);
    void unset_object_qdata(void);
    void check_js_object_finalized(void);

 public:
    void ensure_uses_toggle_ref(JSContext* cx);
    bool check_gobject_disposed(const char* for_what) const;

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
    void finalize_impl(JSFreeOp* fop, JSObject* obj);

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

    /* JS methods */

 private:
    bool connect_impl(JSContext* cx, const JS::CallArgs& args, bool after);
    bool emit_impl(JSContext* cx, const JS::CallArgs& args);
    bool to_string_impl(JSContext* cx, const JS::CallArgs& args);
    bool init_impl(JSContext* cx, const JS::CallArgs& args,
                   JS::MutableHandleObject obj);

    /* Methods connected to "public" API */
 private:
    static JS::PersistentRootedSymbol hook_up_vfunc_root;

 public:
    bool typecheck_object(JSContext* cx, GType expected_type, bool throw_error);
    static JS::Symbol* hook_up_vfunc_symbol(JSContext* cx);

    /* Notification callbacks */

    void gobj_dispose_notify(void);
    void context_dispose_notify(void);

    ObjectInstance(const ObjectInstance& other) = delete;
    ObjectInstance(ObjectInstance&& other) = delete;
    ObjectInstance& operator=(const ObjectInstance& other) = delete;
    ObjectInstance& operator=(ObjectInstance&& other) = delete;
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
