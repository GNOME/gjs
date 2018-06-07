/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2018  Philip Chimento <philip.chimento@gmail.com>
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

#include <functional>
#include <memory>
#include <set>
#include <stack>
#include <string.h>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "object.h"
#include "gtype.h"
#include "interface.h"
#include "gjs/jsapi-util-args.h"
#include "arg.h"
#include "repo.h"
#include "function.h"
#include "proxyutils.h"
#include "param.h"
#include "toggle.h"
#include "value.h"
#include "closure.h"
#include "gjs_gi_trace.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util-root.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/context-private.h"
#include "gjs/mem.h"

#include <util/log.h>
#include <girepository.h>

#include "js/GCHashTable.h"

class ObjectInstance;

class GjsListLink {
 private:
    ObjectInstance *m_prev;
    ObjectInstance *m_next;

 public:
    ObjectInstance *prev(void) const { return m_prev; }
    ObjectInstance *next(void) const { return m_next; }

    void prepend(ObjectInstance *this_instance, ObjectInstance *head);
    void unlink(void);
    size_t size(void) const;
};

using ParamRef = std::unique_ptr<GParamSpec, decltype(&g_param_spec_unref)>;
using PropertyCache = JS::GCHashMap<JS::Heap<JSString *>, ParamRef,
                                    js::DefaultHasher<JSString *>,
                                    js::SystemAllocPolicy>;
using FieldCache = JS::GCHashMap<JS::Heap<JSString *>, GjsAutoInfo<GIFieldInfo>,
                                 js::DefaultHasher<JSString *>,
                                 js::SystemAllocPolicy>;

/* This tells the GCHashMap that the GC doesn't need to care about ParamRef */
namespace JS {
template<>
struct GCPolicy<ParamRef> : public IgnoreGCPolicy<ParamRef> {};
};

class ObjectInstance {
    GIObjectInfo *m_info;
    GObject *m_gobj;  /* nullptr if we are the prototype and not an instance */
    GjsMaybeOwned<JSObject *> m_wrapper;
    GType m_gtype;

    /* a list of all GClosures installed on this object (from
     * signals, trampolines and explicit GClosures), used when tracing */
    std::set<GClosure *> m_closures;

    /* the GObjectClass wrapped by this JS Object (only used for
       prototypes) */
    GTypeClass *m_class;

    GjsListLink m_instance_link;

    bool m_wrapper_finalized : 1;
    bool m_gobj_disposed : 1;

    /* True if this object has visible JS state, and thus its lifecycle is
     * managed using toggle references. False if this object just keeps a
     * hard ref on the underlying GObject, and may be finalized at will. */
    bool m_uses_toggle_ref : 1;

    /* Static methods to get an existing ObjectInstance */

 private:
    static ObjectInstance *for_js_prototype(JSContext *cx, JS::HandleObject obj);

 public:
    static ObjectInstance *for_gobject(GObject *gobj);
    static ObjectInstance *for_js(JSContext *cx, JS::HandleObject obj);
    static ObjectInstance *for_js_nogc(JSObject *obj);
    static ObjectInstance *for_gtype(GType gtype);  /* Prototype-only */

    /* Constructors */

 private:
    /* Constructor for instances */
    ObjectInstance(JSContext *cx, JS::HandleObject obj);

 public:
    /* Public constructor for instances (uses GSlice allocator) */
    static ObjectInstance *new_for_js_object(JSContext *cx, JS::HandleObject obj);

    /* Constructor for prototypes (only called from gjs_define_object_class) */
    ObjectInstance(JSObject *prototype, GIObjectInfo *info, GType gtype);

    /* Accessors */

 private:
    bool is_prototype(void) const { return !m_gobj; }
    bool is_custom_js_class(void) const { return !m_info; }
    bool has_wrapper(void) const { return !!m_wrapper; }
    const char *ns(void) const {
        return m_info ? g_base_info_get_namespace(m_info) : "";
    }
    const char *name(void) const {
        return m_info ? g_base_info_get_name(m_info) : type_name();
    }
    const char *type_name(void) const { return g_type_name(m_gtype); }
    PropertyCache *get_property_cache(void);  /* Prototype-only */
    FieldCache *get_field_cache(void);  /* Prototype-only */

 public:
    GType gtype(void) const { return m_gtype; }
    GObject *gobj(void) const { return m_gobj; }
    JSObject *wrapper(void) const { return m_wrapper; }

    /* Methods to manipulate the JS object wrapper */

 private:
    void discard_wrapper(void) { m_wrapper.reset(); }
    void switch_to_rooted(JSContext *cx) { m_wrapper.switch_to_rooted(cx); }
    void switch_to_unrooted(void) { m_wrapper.switch_to_unrooted(); }
    bool update_after_gc(void) { return m_wrapper.update_after_gc(); }

 public:
    bool wrapper_is_rooted(void) const { return m_wrapper.rooted(); }
    void release_native_object(void);
    void associate_js_gobject(JSContext *cx, JS::HandleObject obj, GObject *gobj);
    void disassociate_js_gobject(void);
    bool weak_pointer_was_finalized(void);
    void toggle_down(void);
    void toggle_up(void);

    /* Methods to manipulate the list of closures */

 private:
    void invalidate_all_closures(void);

 public:
    void associate_closure(JSContext *cx, GClosure *closure);
    void ref_closures(void) {
        for (GClosure *closure : m_closures)
            g_closure_ref(closure);
    }
    void unref_closures(void) {
        for (GClosure *closure : m_closures)
            g_closure_unref(closure);
    }

    /* Helper methods for both prototypes and instances */

 private:
    bool check_is_instance(JSContext *cx, const char *for_what) const {
        if (!is_prototype())
            return true;
        gjs_throw(cx, "Can't %s on %s.%s.prototype; only on instances",
                  for_what, ns(), name());
        return false;
    }

    /* Instance-only helper methods */

 private:
    void set_object_qdata(void);
    void unset_object_qdata(void);
    void check_js_object_finalized(void);

 public:
    void ensure_uses_toggle_ref(JSContext *cx);
    bool check_gobject_disposed(const char *for_what) const {
        if (!m_gobj_disposed)
            return true;

        g_critical("Object %s.%s (%p), has been already deallocated — "
                   "impossible to %s it. This might be caused by the object "
                   "having been destroyed from C code using something such as "
                   "destroy(), dispose(), or remove() vfuncs.",
                   ns(), name(), m_gobj, for_what);
        gjs_dumpstack();
        return false;
    }

    /* Prototype-only helper methods */

 private:
    GParamSpec *find_param_spec_from_id(JSContext *cx, JS::HandleString key);
    GIFieldInfo *find_field_info_from_id(JSContext *cx, JS::HandleString key);
    bool props_to_g_parameters(JSContext *cx, const JS::HandleValueArray& args,
                               std::vector<const char *> *names,
                               std::vector<GValue> *values);
    bool is_vfunc_unchanged(GIVFuncInfo *info);
    bool resolve_no_info(JSContext *cx, JS::HandleObject obj, bool *resolved,
                         const char *name);

    /* Methods to manipulate the linked list of instances */

 private:
    static ObjectInstance *wrapped_gobject_list;
    ObjectInstance *next(void) const { return m_instance_link.next(); }
    void unlink(void) {
        if (wrapped_gobject_list == this)
            wrapped_gobject_list = m_instance_link.next();
        m_instance_link.unlink();
    }
    void link(void) {
        if (wrapped_gobject_list)
            m_instance_link.prepend(this, wrapped_gobject_list);
        wrapped_gobject_list = this;
    }

 public:
    GjsListLink *get_link(void) { return &m_instance_link; }
    static size_t num_wrapped_gobjects(void) {
        return wrapped_gobject_list ?
            wrapped_gobject_list->m_instance_link.size() : 0;
    }
    using Action = std::function<void(ObjectInstance *)>;
    using Predicate = std::function<bool(ObjectInstance *)>;
    static void iterate_wrapped_gobjects(Action action);
    static void remove_wrapped_gobjects_if(Predicate predicate, Action action);

    /* JSClass operations */

 private:
    bool add_property_impl(JSContext *cx, JS::HandleObject obj, JS::HandleId id,
                           JS::HandleValue value);
    bool resolve_impl(JSContext *cx, JS::HandleObject obj, JS::HandleId id,
                      bool *resolved);
    void trace_impl(JSTracer *tracer);
    void finalize_impl(JSFreeOp *fop, JSObject *obj);

 public:
    static bool add_property(JSContext *cx, JS::HandleObject obj,
                             JS::HandleId id, JS::HandleValue value);
    static bool resolve(JSContext *cx, JS::HandleObject obj, JS::HandleId id,
                        bool *resolved);
    static void finalize(JSFreeOp *fop, JSObject *obj);
    static void trace(JSTracer *tracer, JSObject *obj);

    /* JS property getters/setters */

 private:
    bool prop_getter_impl(JSContext *cx, JS::HandleObject obj,
                          JS::HandleString name, JS::MutableHandleValue rval);
    bool field_getter_impl(JSContext *cx, JS::HandleObject obj,
                           JS::HandleString name, JS::MutableHandleValue rval);
    bool prop_setter_impl(JSContext *cx, JS::HandleObject obj,
                          JS::HandleString name, JS::HandleValue value);
    bool field_setter_impl(JSContext *cx, JS::HandleObject obj,
                           JS::HandleString name, JS::HandleValue value);
    static bool prop_getter(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool field_getter(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool prop_setter(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool field_setter(JSContext *cx, unsigned argc, JS::Value *vp);

    /* JS methods */

 private:
    bool connect_impl(JSContext *cx, const JS::CallArgs& args, bool after);
    bool emit_impl(JSContext *cx, const JS::CallArgs& args);
    bool to_string_impl(JSContext *cx, const JS::CallArgs& args);
    bool init_impl(JSContext *cx, const JS::CallArgs& args,
                   JS::MutableHandleObject obj);

 public:
    static bool connect(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool connect_after(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool emit(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool to_string(JSContext *cx, unsigned argc, JS::Value *vp);
    static bool init(JSContext *cx, unsigned argc, JS::Value *vp);

    /* Methods connected to "public" API */

    bool typecheck_object(JSContext *cx, GType expected_type, bool throw_error);
    bool hook_up_vfunc(JSContext *cx, JS::HandleObject wrapper, const char *name,
                       JS::HandleObject function);

    /* Notification callbacks */

    void gobj_dispose_notify(void);
    void context_dispose_notify(void);
    static void closure_invalidated_notify(void *data, GClosure *closure) {
        static_cast<ObjectInstance *>(data)->m_closures.erase(closure);
    }
};

static std::stack<JS::PersistentRootedObject> object_init_list;

using ParamRefArray = std::vector<ParamRef>;
static std::unordered_map<GType, ParamRefArray> class_init_properties;

static bool weak_pointer_callback = false;
ObjectInstance *ObjectInstance::wrapped_gobject_list = nullptr;

extern struct JSClass gjs_object_instance_class;
GJS_DEFINE_PRIV_FROM_JS(ObjectInstance, gjs_object_instance_class)

static GQuark
gjs_is_custom_type_quark (void)
{
    static GQuark val = 0;
    if (!val)
        val = g_quark_from_static_string ("gjs::custom-type");

    return val;
}

static GQuark
gjs_is_custom_property_quark (void)
{
    static GQuark val = 0;
    if (!val)
        val = g_quark_from_static_string ("gjs::custom-property");

    return val;
}

static GQuark
gjs_object_priv_quark (void)
{
    static GQuark val = 0;
    if (G_UNLIKELY (!val))
        val = g_quark_from_static_string ("gjs::private");

    return val;
}

static
G_DEFINE_QUARK(gjs::property-cache, gjs_property_cache);

static
G_DEFINE_QUARK(gjs::field-cache, gjs_field_cache);

/* Plain g_type_query fails and leaves @query uninitialized for
   dynamic types.
   See https://bugzilla.gnome.org/show_bug.cgi?id=687184 and
   https://bugzilla.gnome.org/show_bug.cgi?id=687211
*/
static void
g_type_query_dynamic_safe (GType       type,
                           GTypeQuery *query)
{
    while (g_type_get_qdata(type, gjs_is_custom_type_quark()))
        type = g_type_parent(type);

    g_type_query(type, query);
}

void
GjsListLink::prepend(ObjectInstance *this_instance,
                     ObjectInstance *head)
{
    GjsListLink *elem = head->get_link();

    g_assert(this_instance->get_link() == this);

    if (elem->m_prev) {
        GjsListLink *prev = elem->m_prev->get_link();
        prev->m_next = this_instance;
        this->m_prev = elem->m_prev;
    }

    elem->m_prev = this_instance;
    this->m_next = head;
}

void
GjsListLink::unlink(void)
{
    if (m_prev)
        m_prev->get_link()->m_next = m_next;
    if (m_next)
        m_next->get_link()->m_prev = m_prev;

    m_prev = m_next = nullptr;
}

size_t
GjsListLink::size(void) const
{
    const GjsListLink *elem = this;
    size_t count = 0;

    do {
        count++;
        if (!elem->m_next)
            break;
        elem = elem->m_next->get_link();
    } while (elem);

    return count;
}

static bool
throw_priv_is_null_error(JSContext *context)
{
    gjs_throw(context,
              "This JS object wrapper isn't wrapping a GObject."
              " If this is a custom subclass, are you sure you chained"
              " up to the parent _init properly?");
    return false;
}

ObjectInstance *
ObjectInstance::for_js(JSContext       *cx,
                       JS::HandleObject wrapper)
{
    return priv_from_js(cx, wrapper);
}

/* Use when you don't have a JSContext* available. This method cannot trigger
 * a GC, so it's safe to use from finalize() and trace(). */
ObjectInstance *
ObjectInstance::for_js_nogc(JSObject *wrapper)
{
    return static_cast<ObjectInstance *>(JS_GetPrivate(wrapper));
}

ObjectInstance *
ObjectInstance::for_gobject(GObject *gobj)
{
    auto priv = static_cast<ObjectInstance *>(g_object_get_qdata(gobj,
                                                                 gjs_object_priv_quark()));

    if (priv)
        priv->check_js_object_finalized();

    return priv;
}

void
ObjectInstance::check_js_object_finalized(void)
{
    if (!m_uses_toggle_ref)
        return;
    if (G_UNLIKELY(m_wrapper_finalized)) {
        g_critical("Object %p (a %s) resurfaced after the JS wrapper was finalized. "
                   "This is some library doing dubious memory management inside dispose()",
                   m_gobj, type_name());
        m_wrapper_finalized = false;
        g_assert(!m_wrapper);  /* should associate again with a new wrapper */
    }
}

/* Prototypes only. */
ObjectInstance *
ObjectInstance::for_gtype(GType gtype)
{
    return static_cast<ObjectInstance *>(g_type_get_qdata(gtype,
                                                          gjs_object_priv_quark()));
}

void
ObjectInstance::set_object_qdata(void)
{
    g_object_set_qdata(m_gobj, gjs_object_priv_quark(), this);
}

void
ObjectInstance::unset_object_qdata(void)
{
    g_object_set_qdata(m_gobj, gjs_object_priv_quark(), nullptr);
}

PropertyCache *
ObjectInstance::get_property_cache(void)
{
    void *data = g_type_get_qdata(m_gtype, gjs_property_cache_quark());
    return static_cast<PropertyCache *>(data);
}

FieldCache *
ObjectInstance::get_field_cache(void)
{
    void *data = g_type_get_qdata(m_gtype, gjs_field_cache_quark());
    return static_cast<FieldCache *>(data);
}

GParamSpec *
ObjectInstance::find_param_spec_from_id(JSContext       *cx,
                                        JS::HandleString key)
{
    char *gname;

    /* First check for the ID in the cache */
    PropertyCache *cache = get_property_cache();
    g_assert(cache);
    auto entry = cache->lookupForAdd(key);
    if (entry)
        return entry->value().get();

    GjsAutoJSChar js_prop_name;
    if (!gjs_string_to_utf8(cx, JS::StringValue(key), &js_prop_name))
        return nullptr;

    gname = gjs_hyphen_from_camel(js_prop_name);
    GObjectClass *gobj_class = G_OBJECT_CLASS(g_type_class_ref(m_gtype));
    ParamRef param_spec(g_object_class_find_property(gobj_class, gname),
                        g_param_spec_unref);
    g_type_class_unref(gobj_class);
    g_free(gname);

    if (!param_spec) {
        _gjs_proxy_throw_nonexistent_field(cx, m_gtype, js_prop_name);
        return nullptr;
    }

    if (!cache->add(entry, key, std::move(param_spec)))
        g_error("Out of memory adding param spec to cache");
    return entry->value().get();  /* owned by property cache */
}

/* Gets the ObjectInstance corresponding to obj.prototype */
ObjectInstance *
ObjectInstance::for_js_prototype(JSContext       *context,
                                 JS::HandleObject obj)
{
    JS::RootedObject proto(context);
    JS_GetPrototype(context, obj, &proto);
    return for_js(context, proto);
}

/* A hook on adding a property to an object. This is called during a set
 * property operation after all the resolve hooks on the prototype chain have
 * failed to resolve. We use this to mark an object as needing toggle refs when
 * custom state is set on it, because we need to keep the JS GObject wrapper
 * alive in order not to lose custom "expando" properties.
 */
bool
ObjectInstance::add_property(JSContext       *cx,
                             JS::HandleObject obj,
                             JS::HandleId     id,
                             JS::HandleValue  value)
{
    ObjectInstance *priv = for_js(cx, obj);

    /* priv is null during init: property is not being added from JS */
    if (!priv) {
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Add prop '%s' hook, obj %s, "
                         "no instance associated", gjs_debug_id(id).c_str(),
                         gjs_debug_object(obj).c_str());
        return true;
    }

    return priv->add_property_impl(cx, obj, id, value);
}

bool
ObjectInstance::add_property_impl(JSContext       *cx,
                                  JS::HandleObject obj,
                                  JS::HandleId     id,
                                  JS::HandleValue  value)
{
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Add prop '%s' hook, obj %s, priv %p, gobj %p %s",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str(),
                     this, m_gobj, type_name());

    if (is_prototype() || is_custom_js_class() || m_gobj_disposed)
        return true;

    ensure_uses_toggle_ref(cx);
    return true;
}

bool
ObjectInstance::prop_getter(JSContext *cx,
                            unsigned   argc,
                            JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Property getter '%s', obj %p priv %p",
                     gjs_debug_string(name).c_str(),
                     gjs_debug_object(obj).c_str(), priv);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->prop_getter_impl(cx, obj, name, args.rval());
}

bool
ObjectInstance::prop_getter_impl(JSContext             *cx,
                                 JS::HandleObject       obj,
                                 JS::HandleString       name,
                                 JS::MutableHandleValue rval)
{
    if (!check_gobject_disposed("get any property from"))
        return true;

    GValue gvalue = { 0, };

    auto *proto_priv = ObjectInstance::for_js_prototype(cx, obj);
    GParamSpec *param = proto_priv->find_param_spec_from_id(cx, name);

    /* This is guaranteed because we resolved the property before */
    g_assert(param);

    /* Do not fetch JS overridden properties from GObject, to avoid
     * infinite recursion. */
    if (g_param_spec_get_qdata(param, gjs_is_custom_property_quark()))
        return true;

    if ((param->flags & G_PARAM_READABLE) == 0)
        return true;

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Accessing GObject property %s",
                     param->name);

    g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(m_gobj, param->name, &gvalue);
    if (!gjs_value_from_g_value(cx, rval, &gvalue)) {
        g_value_unset(&gvalue);
        return false;
    }
    g_value_unset(&gvalue);

    return true;
}

static GIFieldInfo *
lookup_field_info(GIObjectInfo *info,
                  const char   *name)
{
    int n_fields = g_object_info_get_n_fields(info);
    int ix;
    GIFieldInfo *retval = NULL;

    for (ix = 0; ix < n_fields; ix++) {
        retval = g_object_info_get_field(info, ix);
        const char *field_name = g_base_info_get_name((GIBaseInfo *) retval);
        if (strcmp(name, field_name) == 0)
            break;
        g_clear_pointer(&retval, g_base_info_unref);
    }

    if (!retval)
        return nullptr;

    if (!(g_field_info_get_flags(retval) & GI_FIELD_IS_READABLE)) {
        g_base_info_unref(retval);
        return nullptr;
    }

    return retval;
}

GIFieldInfo *
ObjectInstance::find_field_info_from_id(JSContext       *cx,
                                        JS::HandleString key)
{
    /* First check for the ID in the cache */
    FieldCache *cache = get_field_cache();
    g_assert(cache);
    auto entry = cache->lookupForAdd(key);
    if (entry)
        return entry->value().get();

    GjsAutoJSChar js_prop_name;
    if (!gjs_string_to_utf8(cx, JS::StringValue(key), &js_prop_name))
        return nullptr;

    GjsAutoInfo<GIFieldInfo> field = lookup_field_info(m_info, js_prop_name);

    if (!field) {
        _gjs_proxy_throw_nonexistent_field(cx, m_gtype, js_prop_name);
        return nullptr;
    }

    if (!cache->add(entry, key, std::move(field)))
        g_error("Out of memory adding field info to cache");
    return entry->value().get();  /* owned by field cache */
}

bool
ObjectInstance::field_getter(JSContext *cx,
                             unsigned   argc,
                             JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Field getter '%s', obj %p priv %p",
                     gjs_debug_string(name).c_str(),
                     gjs_debug_object(obj).c_str(), priv);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    return priv->field_getter_impl(cx, obj, name, args.rval());
}

bool
ObjectInstance::field_getter_impl(JSContext             *cx,
                                  JS::HandleObject       obj,
                                  JS::HandleString       name,
                                  JS::MutableHandleValue rval)
{
    if (!check_gobject_disposed("get any property from"))
        return true;

    auto *proto_priv = ObjectInstance::for_js_prototype(cx, obj);
    GIFieldInfo *field = proto_priv->find_field_info_from_id(cx, name);
    /* This is guaranteed because we resolved the property before */
    g_assert(field);

    bool retval = true;
    GITypeInfo *type = NULL;
    GITypeTag tag;
    GIArgument arg = { 0 };

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Overriding %s with GObject field",
                     gjs_debug_string(name).c_str());

    type = g_field_info_get_type(field);
    tag = g_type_info_get_tag(type);
    if (tag == GI_TYPE_TAG_ARRAY ||
        tag == GI_TYPE_TAG_INTERFACE ||
        tag == GI_TYPE_TAG_GLIST ||
        tag == GI_TYPE_TAG_GSLIST ||
        tag == GI_TYPE_TAG_GHASH ||
        tag == GI_TYPE_TAG_ERROR) {
        gjs_throw(cx, "Can't get field %s; GObject introspection supports only "
                  "fields with simple types, not %s",
                  gjs_debug_string(name).c_str(), g_type_tag_to_string(tag));
        retval = false;
        goto out;
    }

    retval = g_field_info_get_field(field, m_gobj, &arg);
    if (!retval) {
        gjs_throw(cx, "Error getting field %s from object",
                  gjs_debug_string(name).c_str());
        goto out;
    }

    retval = gjs_value_from_g_argument(cx, rval, type, &arg, true);
    /* copy_structs is irrelevant because g_field_info_get_field() doesn't
     * handle boxed types */

out:
    if (type != NULL)
        g_base_info_unref((GIBaseInfo *) type);
    return retval;
}

/* Dynamic setter for GObject properties. Returns false on OOM/exception.
 * args.rval() becomes the "stored value" for the property. */
bool
ObjectInstance::prop_setter(JSContext *cx,
                            unsigned   argc,
                            JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Property setter '%s', obj %p priv %p",
                     gjs_debug_string(name).c_str(),
                     gjs_debug_object(obj).c_str(), priv);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* Clear the JS stored value, to avoid keeping additional references */
    args.rval().setUndefined();

    return priv->prop_setter_impl(cx, obj, name, args[0]);
}

bool
ObjectInstance::prop_setter_impl(JSContext       *cx,
                                 JS::HandleObject obj,
                                 JS::HandleString name,
                                 JS::HandleValue  value)
{
    if (!check_gobject_disposed("set any property on"))
        return true;

    auto *proto_priv = ObjectInstance::for_js_prototype(cx, obj);
    GParamSpec *param_spec = proto_priv->find_param_spec_from_id(cx, name);
    if (!param_spec)
        return false;

    /* Do not set JS overridden properties through GObject, to avoid
     * infinite recursion (unless constructing) */
    if (g_param_spec_get_qdata(param_spec, gjs_is_custom_property_quark()))
        return true;

    if (!(param_spec->flags & G_PARAM_WRITABLE))
        /* prevent setting the prop even in JS */
        return _gjs_proxy_throw_readonly_field(cx, m_gtype, param_spec->name);

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Setting GObject prop %s",
                     param_spec->name);

    GValue gvalue = G_VALUE_INIT;
    g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param_spec));
    if (!gjs_value_to_g_value(cx, value, &gvalue)) {
        g_value_unset(&gvalue);
        return false;
    }

    g_object_set_property(m_gobj, param_spec->name, &gvalue);
    g_value_unset(&gvalue);

    return true;
}

bool
ObjectInstance::field_setter(JSContext *cx,
                             unsigned   argc,
                             JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);

    JS::RootedString name(cx,
        gjs_dynamic_property_private_slot(&args.callee()).toString());

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Field setter '%s', obj %p priv %p",
                     gjs_debug_string(name).c_str(),
                     gjs_debug_object(obj).c_str(), priv);

    if (priv->is_prototype())
        return true;
        /* Ignore silently; note that this is different from what we do for
         * boxed types, for historical reasons */

    /* We have to update args.rval(), because JS caches it as the property's "stored
     * value" (https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/Stored_value)
     * and so subsequent gets would get the stored value instead of accessing
     * the field */
    args.rval().setUndefined();

    return priv->field_setter_impl(cx, obj, name, args[0]);
}

bool
ObjectInstance::field_setter_impl(JSContext       *cx,
                                  JS::HandleObject obj,
                                  JS::HandleString name,
                                  JS::HandleValue  value)
{
    if (!check_gobject_disposed("set any property on"))
        return true;

    auto *proto_priv = ObjectInstance::for_js_prototype(cx, obj);
    GIFieldInfo *field = proto_priv->find_field_info_from_id(cx, name);
    if (field == NULL)
        return false;

    /* As far as I know, GI never exposes GObject instance struct fields as
     * writable, so no need to implement this for the time being */
    if (g_field_info_get_flags(field) & GI_FIELD_IS_WRITABLE) {
        g_message("Field %s of a GObject is writable, but setting it is not "
                  "implemented", gjs_debug_string(name).c_str());
        return true;
    }

    return _gjs_proxy_throw_readonly_field(cx, m_gtype,
                                           g_base_info_get_name(field));
}

bool
ObjectInstance::is_vfunc_unchanged(GIVFuncInfo *info)
{
    GType ptype = g_type_parent(m_gtype);
    GError *error = NULL;
    gpointer addr1, addr2;

    addr1 = g_vfunc_info_get_address(info, m_gtype, &error);
    if (error) {
        g_clear_error(&error);
        return false;
    }

    addr2 = g_vfunc_info_get_address(info, ptype, &error);
    if (error) {
        g_clear_error(&error);
        return false;
    }

    return addr1 == addr2;
}

static GIVFuncInfo *
find_vfunc_on_parents(GIObjectInfo *info,
                      const char   *name,
                      bool         *out_defined_by_parent)
{
    GIVFuncInfo *vfunc = NULL;
    GIObjectInfo *parent;
    bool defined_by_parent = false;

    /* ref the first info so that we don't destroy
     * it when unrefing parents later */
    g_base_info_ref(info);
    parent = info;

    /* Since it isn't possible to override a vfunc on
     * an interface without reimplementing it, we don't need
     * to search the parent types when looking for a vfunc. */
    vfunc = g_object_info_find_vfunc_using_interfaces(parent, name, NULL);
    while (!vfunc && parent) {
        GIObjectInfo *tmp = parent;
        parent = g_object_info_get_parent(tmp);
        g_base_info_unref(tmp);
        if (parent)
            vfunc = g_object_info_find_vfunc(parent, name);

        defined_by_parent = true;
    }

    if (parent)
        g_base_info_unref(parent);

    if (out_defined_by_parent)
        *out_defined_by_parent = defined_by_parent;

    return vfunc;
}

bool
ObjectInstance::resolve_no_info(JSContext       *cx,
                                JS::HandleObject obj,
                                bool            *resolved,
                                const char      *name)
{
    GIFunctionInfo *method_info;
    guint n_interfaces;
    guint i;

    GType *interfaces = g_type_interfaces(m_gtype, &n_interfaces);
    for (i = 0; i < n_interfaces; i++) {
        GIBaseInfo *base_info;
        GIInterfaceInfo *iface_info;

        base_info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                                interfaces[i]);

        if (base_info == NULL)
            continue;

        /* An interface GType ought to have interface introspection info */
        g_assert (g_base_info_get_type(base_info) == GI_INFO_TYPE_INTERFACE);

        iface_info = (GIInterfaceInfo*) base_info;

        method_info = g_interface_info_find_method(iface_info, name);

        g_base_info_unref(base_info);


        if (method_info != NULL) {
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                if (!gjs_define_function(cx, obj, m_gtype, method_info)) {
                    g_base_info_unref((GIBaseInfo*) method_info);
                    g_free(interfaces);
                    return false;
                }

                g_base_info_unref((GIBaseInfo*) method_info);
                *resolved = true;
                g_free(interfaces);
                return true;
            }

            g_base_info_unref( (GIBaseInfo*) method_info);
        }
    }

    *resolved = false;
    g_free(interfaces);
    return true;
}

/* Taken from GLib */
static void
canonicalize_key(char *key)
{
    for (char *p = key; *p != 0; p++) {
        char c = *p;

        if (c != '-' &&
            (c < '0' || c > '9') &&
            (c < 'A' || c > 'Z') &&
            (c < 'a' || c > 'z'))
            *p = '-';
    }
}

static bool
is_gobject_property_name(GIObjectInfo *info,
                         const char   *name)
{
    int n_props = g_object_info_get_n_properties(info);
    int ix;
    GIPropertyInfo *prop_info = nullptr;

    char *canonical_name = gjs_hyphen_from_camel(name);
    canonicalize_key(canonical_name);

    for (ix = 0; ix < n_props; ix++) {
        prop_info = g_object_info_get_property(info, ix);
        const char *prop_name = g_base_info_get_name(prop_info);
        if (strcmp(canonical_name, prop_name) == 0)
            break;
        g_clear_pointer(&prop_info, g_base_info_unref);
    }

    g_free(canonical_name);

    if (!prop_info)
        return false;

    if (!(g_property_info_get_flags(prop_info) & G_PARAM_READABLE)) {
        g_base_info_unref(prop_info);
        return false;
    }

    g_base_info_unref(prop_info);
    return true;
}

static bool
is_gobject_field_name(GIObjectInfo *info,
                      const char   *name,
                      GIFieldInfo **field_info_out)
{
    GIFieldInfo *field_info = lookup_field_info(info, name);
    if (!field_info)
        return false;

    if (field_info_out)
        *field_info_out = field_info;
    else
        g_base_info_unref(field_info);
    return true;
}

bool
ObjectInstance::resolve(JSContext       *cx,
                        JS::HandleObject obj,
                        JS::HandleId     id,
                        bool            *resolved)
{
    auto *priv = ObjectInstance::for_js(cx, obj);

    if (!priv) {
        /* We won't have a private until the initializer is called, so
         * just defer to prototype chains in this case.
         *
         * This isn't too bad: either you get undefined if the field
         * doesn't exist on any of the prototype chains, or whatever code
         * will run afterwards will fail because of the "priv == NULL"
         * check there.
         */
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Resolve prop '%s' hook, obj %s, "
                         "no instance associated", gjs_debug_id(id).c_str(),
                         gjs_debug_object(obj).c_str());
        *resolved = false;
        return true;
    }

    return priv->resolve_impl(cx, obj, id, resolved);
}

bool
ObjectInstance::resolve_impl(JSContext       *context,
                             JS::HandleObject obj,
                             JS::HandleId     id,
                             bool            *resolved)
{
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                 "Resolve prop '%s' hook, obj %s, priv %p (%s.%s), gobj %p %s",
                 gjs_debug_id(id).c_str(),
                 gjs_debug_object(obj).c_str(),
                 this, ns(), name(), m_gobj, type_name());

    if (!is_prototype()) {
        *resolved = false;
        return true;
    }

    GjsAutoJSChar name;
    if (!gjs_get_string_id(context, id, &name)) {
        *resolved = false;
        return true;  /* not resolved, but no error */
    }

    /* If we have no GIRepository information (we're a JS GObject subclass),
     * we need to look at exposing interfaces. Look up our interfaces through
     * GType data, and then hope that *those* are introspectable. */
    if (is_custom_js_class())
        return resolve_no_info(context, obj, resolved, name);

    if (g_str_has_prefix (name, "vfunc_")) {
        /* The only time we find a vfunc info is when we're the base
         * class that defined the vfunc. If we let regular prototype
         * chaining resolve this, we'd have the implementation for the base's
         * vfunc on the base class, without any other "real" implementations
         * in the way. If we want to expose a "real" vfunc implementation,
         * we need to go down to the parent infos and look at their VFuncInfos.
         *
         * This is good, but it's memory-hungry -- we would define every
         * possible vfunc on every possible object, even if it's the same
         * "real" vfunc underneath. Instead, only expose vfuncs that are
         * different from their parent, and let prototype chaining do the
         * rest.
         */

        const char *name_without_vfunc_ = &(name[6]);  /* lifetime tied to name */
        bool defined_by_parent;
        GjsAutoInfo<GIVFuncInfo> vfunc = find_vfunc_on_parents(m_info,
                                                               name_without_vfunc_,
                                                               &defined_by_parent);
        if (vfunc != NULL) {

            /* In the event that the vfunc is unchanged, let regular
             * prototypal inheritance take over. */
            if (defined_by_parent && is_vfunc_unchanged(vfunc)) {
                *resolved = false;
                return true;
            }

            gjs_define_function(context, obj, m_gtype, vfunc);
            *resolved = true;
            return true;
        }

        /* If the vfunc wasn't found, fall through, back to normal
         * method resolution. */
    }

    /* If the name refers to a GObject property or field, lazily define the
     * property in JS, on the prototype. */
    if (is_gobject_property_name(m_info, name)) {
        JS::RootedObject proto(context);
        JS_GetPrototype(context, obj, &proto);

        bool found = false;
        if (!JS_AlreadyHasOwnPropertyById(context, proto, id, &found))
            return false;
        if (found) {
            /* Already defined, so *resolved = false because we didn't just
             * define it */
            *resolved = false;
            return true;
        }

        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "Defining lazy GObject property '%s' in prototype of %s",
                         gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str());

        JS::RootedValue private_id(context, JS::StringValue(JSID_TO_STRING(id)));
        if (!gjs_define_property_dynamic(context, proto, name, "gobject_prop",
                                         &ObjectInstance::prop_getter,
                                         &ObjectInstance::prop_setter,
                                         private_id, GJS_MODULE_PROP_FLAGS))
            return false;

        *resolved = true;
        return true;
    }

    GIFieldInfo *field_info;
    if (is_gobject_field_name(m_info, name, &field_info)) {
        JS::RootedObject proto(context);
        JS_GetPrototype(context, obj, &proto);

        bool found = false;
        if (!JS_AlreadyHasOwnPropertyById(context, proto, id, &found))
            return false;
        if (found) {
            *resolved = false;
            return true;
        }

        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "Defining lazy GObject field '%s' in prototype of %s",
                         gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str());

        unsigned flags = GJS_MODULE_PROP_FLAGS;
        if (!(g_field_info_get_flags(field_info) & GI_FIELD_IS_WRITABLE))
            flags |= JSPROP_READONLY;

        JS::RootedValue private_id(context, JS::StringValue(JSID_TO_STRING(id)));
        if (!gjs_define_property_dynamic(context, proto, name, "gobject_field",
                                         &ObjectInstance::field_getter,
                                         &ObjectInstance::field_setter,
                                         private_id, flags))
            return false;

        *resolved = true;
        return true;
    }

    /* find_method does not look at methods on parent classes,
     * we rely on javascript to walk up the __proto__ chain
     * and find those and define them in the right prototype.
     *
     * Note that if it isn't a method on the object, since JS
     * lacks multiple inheritance, we're sticking the iface
     * methods in the object prototype, which means there are many
     * copies of the iface methods (one per object class node that
     * introduces the iface)
     */

    GjsAutoInfo<GIFunctionInfo> method_info =
        g_object_info_find_method_using_interfaces(m_info, name, nullptr);

    /**
     * Search through any interfaces implemented by the GType;
     * this could be done better.  See
     * https://bugzilla.gnome.org/show_bug.cgi?id=632922
     */
    if (!method_info)
        return resolve_no_info(context, obj, resolved, name);

#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

    if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for %s (%s.%s)",
                  g_base_info_get_name( (GIBaseInfo*) method_info),
                  type_name(), ns(), this->name());

        if (!gjs_define_function(context, obj, m_gtype, method_info))
            return false;

        *resolved = true; /* we defined the prop in obj */
    } else {
        *resolved = false;
    }

    return true;
}

/* Set properties from args to constructor (argv[0] is supposed to be
 * a hash)
 * The GValue elements in the passed-in vector must be unset by the caller,
 * regardless of the return value of this function.
 */
bool
ObjectInstance::props_to_g_parameters(JSContext                  *context,
                                      const JS::HandleValueArray& args,
                                      std::vector<const char *>  *names,
                                      std::vector<GValue>        *values)
{
    size_t ix, length;

    if (args.length() == 0 || args[0].isUndefined())
        return true;

    if (!args[0].isObject()) {
        gjs_throw(context, "argument should be a hash with props to set");
        return false;
    }

    JS::RootedObject props(context, &args[0].toObject());
    JS::RootedId prop_id(context);
    JS::RootedValue value(context);
    JS::Rooted<JS::IdVector> ids(context, context);
    if (!JS_Enumerate(context, props, &ids)) {
        gjs_throw(context, "Failed to create property iterator for object props hash");
        return false;
    }

    for (ix = 0, length = ids.length(); ix < length; ix++) {
        GValue gvalue = G_VALUE_INIT;

        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        prop_id = ids[ix];

        if (!JSID_IS_STRING(prop_id))
            return _gjs_proxy_throw_nonexistent_field(context, m_gtype,
                                                      gjs_debug_id(prop_id).c_str());

        JS::RootedString js_prop_name(context, JSID_TO_STRING(prop_id));
        GParamSpec *param_spec = find_param_spec_from_id(context, js_prop_name);
        if (!param_spec)
            return false;

        if (!JS_GetPropertyById(context, props, prop_id, &value))
            return false;
        if (value.isUndefined()) {
            gjs_throw(context, "Invalid value 'undefined' for property %s in "
                      "object initializer.", param_spec->name);
            return false;
        }

        if (!(param_spec->flags & G_PARAM_WRITABLE))
            return _gjs_proxy_throw_readonly_field(context, m_gtype,
                                                   param_spec->name);
            /* prevent setting the prop even in JS */

        g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param_spec));
        if (!gjs_value_to_g_value(context, value, &gvalue)) {
            g_value_unset(&gvalue);
            return false;
        }

        names->push_back(param_spec->name);  /* owned by GParamSpec in cache */
        values->push_back(gvalue);
    }

    return true;
}

static void
wrapped_gobj_dispose_notify(gpointer      data,
                            GObject      *where_the_object_was)
{
    auto *priv = static_cast<ObjectInstance *>(data);
    priv->gobj_dispose_notify();
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Wrapped GObject %p disposed",
                        where_the_object_was);
}

void
ObjectInstance::gobj_dispose_notify(void)
{
    m_gobj_disposed = true;
    unlink();
}

void
ObjectInstance::iterate_wrapped_gobjects(ObjectInstance::Action action)
{
    ObjectInstance *link = ObjectInstance::wrapped_gobject_list;
    while (link) {
        ObjectInstance *next = link->next();
        action(link);
        link = next;
    }
}

void
ObjectInstance::remove_wrapped_gobjects_if(ObjectInstance::Predicate predicate,
                                           ObjectInstance::Action action)
{
    std::vector<ObjectInstance *> removed;
    ObjectInstance *link = ObjectInstance::wrapped_gobject_list;
    while (link) {
        ObjectInstance *next = link->next();
        if (predicate(link)) {
            removed.push_back(link);
            link->unlink();
        }

        link = next;
    }
    for (ObjectInstance *priv : removed)
        action(priv);
}

void
gjs_object_context_dispose_notify(void    *data,
                                  GObject *where_the_object_was)
{
    ObjectInstance::iterate_wrapped_gobjects(std::mem_fn(&ObjectInstance::context_dispose_notify));
}

void
ObjectInstance::context_dispose_notify(void)
{
    if (wrapper_is_rooted()) {
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "GObject wrapper %p for GObject "
                            "%p (%s) was rooted but is now unrooted due to "
                            "GjsContext dispose", m_wrapper.get(),
                            m_gobj, type_name());
        discard_wrapper();
        unlink();
    }
}

void
ObjectInstance::toggle_down(void)
{
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Toggle notify DOWN for GObject "
                        "%p (%s), JS obj %p", m_gobj, type_name(),
                        m_wrapper.get());

    /* Change to weak ref so the wrapper-wrappee pair can be
     * collected by the GC
     */
    if (wrapper_is_rooted()) {
        GjsContext *context;

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Unrooting object");
        switch_to_unrooted();

        /* During a GC, the collector asks each object which other
         * objects that it wants to hold on to so if there's an entire
         * section of the heap graph that's not connected to anything
         * else, and not reachable from the root set, then it can be
         * trashed all at once.
         *
         * GObjects, however, don't work like that, there's only a
         * reference count but no notion of who owns the reference so,
         * a JS object that's proxying a GObject is unconditionally held
         * alive as long as the GObject has >1 references.
         *
         * Since we cannot know how many more wrapped GObjects are going
         * be marked for garbage collection after the owner is destroyed,
         * always queue a garbage collection when a toggle reference goes
         * down.
         */
        context = gjs_context_get_current();
        if (!_gjs_context_destroying(context))
            _gjs_context_schedule_gc(context);
    }
}

void
ObjectInstance::toggle_up(void)
{
    /* We need to root the JSObject associated with the passed in GObject so it
     * doesn't get garbage collected (and lose any associated javascript state
     * such as custom properties).
     */
    if (!has_wrapper()) /* Object already GC'd */
        return;

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Toggle notify UP for GObject "
                        "%p (%s), JS obj %p", m_gobj, type_name(),
                        m_wrapper.get());

    /* Change to strong ref so the wrappee keeps the wrapper alive
     * in case the wrapper has data in it that the app cares about
     */
    if (!wrapper_is_rooted()) {
        /* FIXME: thread the context through somehow. Maybe by looking up
         * the compartment that obj belongs to. */
        GjsContext *context = gjs_context_get_current();
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Rooting object");
        auto cx = static_cast<JSContext *>(gjs_context_get_native_context(context));
        switch_to_rooted(cx);
    }
}

static void
toggle_handler(GObject               *gobj,
               ToggleQueue::Direction direction)
{
    switch (direction) {
        case ToggleQueue::UP:
            ObjectInstance::for_gobject(gobj)->toggle_up();
            break;
        case ToggleQueue::DOWN:
            ObjectInstance::for_gobject(gobj)->toggle_down();
            break;
        default:
            g_assert_not_reached();
    }
}

static void
wrapped_gobj_toggle_notify(gpointer      data,
                           GObject      *gobj,
                           gboolean      is_last_ref)
{
    bool is_main_thread;
    bool toggle_up_queued, toggle_down_queued;
    GjsContext *context;

    context = gjs_context_get_current();
    if (_gjs_context_destroying(context)) {
        /* Do nothing here - we're in the process of disassociating
         * the objects.
         */
        return;
    }

    /* We only want to touch javascript from one thread.
     * If we're not in that thread, then we need to defer processing
     * to it.
     * In case we're toggling up (and thus rooting the JS object) we
     * also need to take care if GC is running. The marking side
     * of it is taken care by JS::Heap, which we use in GjsMaybeOwned,
     * so we're safe. As for sweeping, it is too late: the JS object
     * is dead, and attempting to keep it alive would soon crash
     * the process. Plus, if we touch the JSAPI, libmozjs aborts in
     * the first BeginRequest.
     * Thus, we drain the toggle queue when GC starts, in order to
     * prevent this from happening.
     * In practice, a toggle up during JS finalize can only happen
     * for temporary refs/unrefs of objects that are garbage anyway,
     * because JS code is never invoked while the finalizers run
     * and C code needs to clean after itself before it returns
     * from dispose()/finalize().
     * On the other hand, toggling down is a lot simpler, because
     * we're creating more garbage. So we just unroot the object, make it a
     * weak pointer, and wait for the next GC cycle.
     *
     * Note that one would think that toggling up only happens
     * in the main thread (because toggling up is the result of
     * the JS object, previously visible only to JS code, becoming
     * visible to the refcounted C world), but because of weird
     * weak singletons like g_bus_get_sync() objects can see toggle-ups
     * from different threads too.
     */
    is_main_thread = _gjs_context_get_is_owner_thread(context);

    auto& toggle_queue = ToggleQueue::get_default();
    std::tie(toggle_down_queued, toggle_up_queued) = toggle_queue.is_queued(gobj);

    if (is_last_ref) {
        /* We've transitions from 2 -> 1 references,
         * The JSObject is rooted and we need to unroot it so it
         * can be garbage collected
         */
        if (is_main_thread) {
            if (G_UNLIKELY (toggle_up_queued || toggle_down_queued)) {
                g_error("toggling down object %s that's already queued to toggle %s\n",
                        G_OBJECT_TYPE_NAME(gobj),
                        toggle_up_queued && toggle_down_queued? "up and down" :
                        toggle_up_queued? "up" : "down");
            }

            ObjectInstance::for_gobject(gobj)->toggle_down();
        } else {
            toggle_queue.enqueue(gobj, ToggleQueue::DOWN, toggle_handler);
        }
    } else {
        /* We've transitioned from 1 -> 2 references.
         *
         * The JSObject associated with the gobject is not rooted,
         * but it needs to be. We'll root it.
         */
        if (is_main_thread && !toggle_down_queued) {
            if (G_UNLIKELY (toggle_up_queued)) {
                g_error("toggling up object %s that's already queued to toggle up\n",
                        G_OBJECT_TYPE_NAME(gobj));
            }
            ObjectInstance::for_gobject(gobj)->toggle_up();
        } else {
            toggle_queue.enqueue(gobj, ToggleQueue::UP, toggle_handler);
        }
    }
}

void
ObjectInstance::release_native_object(void)
{
    discard_wrapper();
    if (m_uses_toggle_ref)
        g_object_remove_toggle_ref(m_gobj, wrapped_gobj_toggle_notify, nullptr);
    else
        g_object_unref(m_gobj);
    m_gobj = nullptr;
}

/* At shutdown, we need to ensure we've cleared the context of any
 * pending toggle references.
 */
void
gjs_object_clear_toggles(void)
{
    auto& toggle_queue = ToggleQueue::get_default();
    while (toggle_queue.handle_toggle(toggle_handler))
        ;
}

void
gjs_object_shutdown_toggle_queue(void)
{
    auto& toggle_queue = ToggleQueue::get_default();
    toggle_queue.shutdown();
}

void
gjs_object_prepare_shutdown(void)
{
    /* We iterate over all of the objects, breaking the JS <-> C
     * association.  We avoid the potential recursion implied in:
     *   toggle ref removal -> gobj dispose -> toggle ref notify
     * by emptying the toggle queue earlier in the shutdown sequence. */
    ObjectInstance::remove_wrapped_gobjects_if(
        std::mem_fn(&ObjectInstance::wrapper_is_rooted),
        std::mem_fn(&ObjectInstance::release_native_object));
}

ObjectInstance *
ObjectInstance::new_for_js_object(JSContext       *cx,
                                  JS::HandleObject obj)
{
    ObjectInstance *priv = g_slice_new0(ObjectInstance);
    new (priv) ObjectInstance(cx, obj);
    return priv;
}

ObjectInstance::ObjectInstance(JSContext       *cx,
                               JS::HandleObject object)
    : m_wrapper_finalized(0),
      m_gobj_disposed(0),
      m_uses_toggle_ref(0)
{
    GJS_INC_COUNTER(object);

    g_assert(!ObjectInstance::for_js(cx, object));
    JS_SetPrivate(object, this);

    auto *proto_priv = ObjectInstance::for_js_prototype(cx, object);
    g_assert(proto_priv != NULL);

    m_gtype = proto_priv->m_gtype;
    m_info = proto_priv->m_info;
    if (m_info)
        g_base_info_ref(m_info);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Instance constructor of %s, "
                        "JS obj %p, priv %p", type_name(), object.get(), this);
}

ObjectInstance::ObjectInstance(JSObject     *prototype,
                               GIObjectInfo *info,
                               GType         gtype)
    : m_info(info),
      m_gtype(gtype),
      m_class(static_cast<GTypeClass *>(g_type_class_ref(gtype)))
{
    if (info)
        g_base_info_ref(info);

    auto *property_cache = new PropertyCache();
    if (!property_cache->init())
        g_error("Out of memory for property cache of %s", type_name());
    g_type_set_qdata(gtype, gjs_property_cache_quark(), property_cache);

    auto *field_cache = new FieldCache();
    if (!field_cache->init())
        g_error("Out of memory for field cache of %s", type_name());
    g_type_set_qdata(gtype, gjs_field_cache_quark(), field_cache);

    JS_SetPrivate(prototype, this);
}

static void
update_heap_wrapper_weak_pointers(JSContext     *cx,
                                  JSCompartment *compartment,
                                  gpointer       data)
{
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Weak pointer update callback, "
                        "%zu wrapped GObject(s) to examine",
                        ObjectInstance::num_wrapped_gobjects());

    ObjectInstance::remove_wrapped_gobjects_if(
        std::mem_fn(&ObjectInstance::weak_pointer_was_finalized),
        std::mem_fn(&ObjectInstance::disassociate_js_gobject));
}

bool
ObjectInstance::weak_pointer_was_finalized(void)
{
    if (has_wrapper() && !wrapper_is_rooted() && update_after_gc()) {
        /* Ouch, the JS object is dead already. Disassociate the
         * GObject and hope the GObject dies too. (Remove it from
         * the weak pointer list first, since the disassociation
         * may also cause it to be erased.)
         */
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Found GObject weak pointer "
                            "whose JS object %p is about to be finalized: "
                            "%p (%s)", m_wrapper.get(), m_gobj, type_name());
        return true;
    }
    return false;
}

static void
ensure_weak_pointer_callback(JSContext *cx)
{
    if (!weak_pointer_callback) {
        JS_AddWeakPointerCompartmentCallback(cx,
                                             update_heap_wrapper_weak_pointers,
                                             nullptr);
        weak_pointer_callback = true;
    }
}

void
ObjectInstance::associate_js_gobject(JSContext       *context,
                                     JS::HandleObject object,
                                     GObject         *gobj)
{
    g_assert(!wrapper_is_rooted());

    m_uses_toggle_ref = false;
    m_gobj = gobj;
    set_object_qdata();
    m_wrapper = object;

    ensure_weak_pointer_callback(context);
    link();

    g_object_weak_ref(gobj, wrapped_gobj_dispose_notify, this);
}

void
ObjectInstance::ensure_uses_toggle_ref(JSContext *cx)
{
    if (m_uses_toggle_ref)
        return;

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Switching object instance %p, gobj %p %s to toggle ref",
                     this, m_gobj, type_name());

    g_assert(!wrapper_is_rooted());

    /* OK, here is where things get complicated. We want the
     * wrapped gobj to keep the JSObject* wrapper alive, because
     * people might set properties on the JSObject* that they care
     * about. Therefore, whenever the refcount on the wrapped gobj
     * is >1, i.e. whenever something other than the wrapper is
     * referencing the wrapped gobj, the wrapped gobj has a strong
     * ref (gc-roots the wrapper). When the refcount on the
     * wrapped gobj is 1, then we change to a weak ref to allow
     * the wrapper to be garbage collected (and thus unref the
     * wrappee).
     */
    m_uses_toggle_ref = true;
    switch_to_rooted(cx);
    g_object_add_toggle_ref(m_gobj, wrapped_gobj_toggle_notify, nullptr);

    /* We now have both a ref and a toggle ref, we only want the toggle ref.
     * This may immediately remove the GC root we just added, since refcount
     * may drop to 1. */
    g_object_unref(m_gobj);
}

void
ObjectInstance::invalidate_all_closures(void)
{
    /* Can't loop directly through the items, since invalidating an item's
     * closure might have the effect of removing the item from the set in the
     * invalidate notifier */
    while (!m_closures.empty()) {
        /* This will also free cd, through the closure invalidation mechanism */
        GClosure *closure = *m_closures.begin();
        g_closure_invalidate(closure);
        /* Erase element if not already erased */
        m_closures.erase(closure);
    }
}

void
ObjectInstance::disassociate_js_gobject(void)
{
    bool had_toggle_down, had_toggle_up;

    if (!m_gobj_disposed)
        g_object_weak_unref(m_gobj, wrapped_gobj_dispose_notify, this);

    auto& toggle_queue = ToggleQueue::get_default();
    std::tie(had_toggle_down, had_toggle_up) = toggle_queue.cancel(m_gobj);
    if (had_toggle_down != had_toggle_up) {
        g_error("JS object wrapper for GObject %p (%s) is being released while "
                "toggle references are still pending.", m_gobj, type_name());
    }

    /* Fist, remove the wrapper pointer from the wrapped GObject */
    unset_object_qdata();

    /* Now release all the resources the current wrapper has */
    invalidate_all_closures();
    release_native_object();

    /* Mark that a JS object once existed, but it doesn't any more */
    m_wrapper_finalized = true;
    m_wrapper = nullptr;
}

static void
clear_g_values(const std::vector<GValue>& values)
{
    for (GValue value : values)
        g_value_unset(&value);
}

bool
ObjectInstance::init_impl(JSContext              *context,
                          const JS::CallArgs&     args,
                          JS::MutableHandleObject object)
{
    GTypeQuery query;

    g_assert(m_gtype != G_TYPE_NONE);

    std::vector<const char *> names;
    std::vector<GValue> values;
    auto *proto_priv = ObjectInstance::for_js_prototype(context, object);
    if (!proto_priv->props_to_g_parameters(context, args, &names, &values)) {
        clear_g_values(values);
        return false;
    }

    /* Mark this object in the construction stack, it
       will be popped in gjs_object_custom_init() later
       down.
    */
    if (g_type_get_qdata(m_gtype, gjs_is_custom_type_quark()))
        object_init_list.emplace(context, object);

    g_assert(names.size() == values.size());
    GObject *gobj = g_object_new_with_properties(m_gtype, values.size(),
                                                 names.data(), values.data());

    clear_g_values(values);

    ObjectInstance *other_priv = ObjectInstance::for_gobject(gobj);
    if (other_priv && other_priv->m_wrapper != object.get()) {
        /* g_object_new_with_properties() returned an object that's already
         * tracked by a JS object. Let's assume this is a singleton like
         * IBus.IBus and return the existing JS wrapper object.
         *
         * 'object' has a value that was originally created by
         * JS_NewObjectForConstructor in GJS_NATIVE_CONSTRUCTOR_PRELUDE, but
         * we're not actually using it, so just let it get collected. Avoiding
         * this would require a non-trivial amount of work.
         * */
        other_priv->ensure_uses_toggle_ref(context);
        object.set(other_priv->m_wrapper);
        g_object_unref(gobj); /* We already own a reference */
        gobj = NULL;
        return true;
    }

    g_type_query_dynamic_safe(m_gtype, &query);
    if (G_LIKELY (query.type))
        JS_updateMallocCounter(context, query.instance_size);

    if (G_IS_INITIALLY_UNOWNED(gobj) &&
        !g_object_is_floating(gobj)) {
        /* GtkWindow does not return a ref to caller of g_object_new.
         * Need a flag in gobject-introspection to tell us this.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Newly-created object is initially unowned but we did not get the "
                  "floating ref, probably GtkWindow, using hacky workaround");
        g_object_ref(gobj);
    } else if (g_object_is_floating(gobj)) {
        g_object_ref_sink(gobj);
    } else {
        /* we should already have a ref */
    }

    if (!m_gobj)
        associate_js_gobject(context, object, gobj);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "JSObject created with GObject %p (%s)",
                        m_gobj, type_name());

    TRACE(GJS_OBJECT_PROXY_NEW(this, m_gobj, ns(), name()));

    args.rval().setObject(*object);
    return true;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(object_instance)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(object_instance)
    bool ret;
    JS::RootedValue initer(context);

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(object_instance);

    /* Init the private variable before we do anything else. If a garbage
     * collection happens when calling the init function then this object
     * might be traced and we will end up dereferencing a null pointer */
    (void) ObjectInstance::new_for_js_object(context, object);

    if (!gjs_object_require_property(context, object, "GObject instance",
                                     GJS_STRING_GOBJECT_INIT, &initer))
        return false;

    argv.rval().setUndefined();
    ret = gjs_call_function_value(context, object, initer, argv, argv.rval());

    if (argv.rval().isUndefined())
        argv.rval().setObject(*object);

    return ret;
}

void
ObjectInstance::trace(JSTracer *tracer,
                      JSObject *obj)
{
    auto *priv = ObjectInstance::for_js_nogc(obj);
    if (priv == NULL)
        return;

    priv->trace_impl(tracer);
}

void
ObjectInstance::trace_impl(JSTracer *tracer)
{
    for (GClosure *closure : m_closures)
        gjs_closure_trace(closure, tracer);

    PropertyCache *property_cache = get_property_cache();
    if (property_cache)
        property_cache->trace(tracer);
    FieldCache *field_cache = get_field_cache();
    if (field_cache)
        field_cache->trace(tracer);
}

void
ObjectInstance::finalize(JSFreeOp *fop,
                         JSObject *obj)
{
    auto *priv = ObjectInstance::for_js_nogc(obj);
    g_assert (priv != NULL);

    priv->finalize_impl(fop, obj);

    priv->~ObjectInstance();
    g_slice_free(ObjectInstance, priv);
}

void
ObjectInstance::finalize_impl(JSFreeOp  *fop,
                              JSObject  *obj)
{
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "Finalizing %s, JS obj %p, priv %p, GObject %p",
                        type_name(), obj, this, m_gobj);

    TRACE(GJS_OBJECT_PROXY_FINALIZE(priv, m_gobj, ns(), name()));

    /* Prototype only, although if priv->gobj is null it could also be an
     * instance struct with a freed gobject. This is annoying. It means we have
     * to always do this, and have an extra null check. */
    PropertyCache *property_cache = get_property_cache();
    if (property_cache) {
        delete property_cache;
        g_type_set_qdata(m_gtype, gjs_property_cache_quark(), nullptr);
    }
    FieldCache *field_cache = get_field_cache();
    if (field_cache) {
        delete field_cache;
        g_type_set_qdata(m_gtype, gjs_field_cache_quark(), nullptr);
    }

    /* This applies only to instances, not prototypes, but it's possible that
     * an instance's GObject is already freed at this point. */
    invalidate_all_closures();

    /* Object is instance, not prototype, AND GObject is not already freed */
    if (!is_prototype()) {
        bool had_toggle_up;
        bool had_toggle_down;

        if (G_UNLIKELY (m_gobj->ref_count <= 0)) {
            g_error("Finalizing proxy for an already freed object of type: %s.%s\n",
                    ns(), name());
        }

        auto& toggle_queue = ToggleQueue::get_default();
        std::tie(had_toggle_down, had_toggle_up) = toggle_queue.cancel(m_gobj);

        if (!had_toggle_up && had_toggle_down) {
            g_error("Finalizing proxy for an object that's scheduled to be unrooted: %s.%s\n",
                    ns(), name());
        }

        if (!m_gobj_disposed)
            g_object_weak_unref(m_gobj, wrapped_gobj_dispose_notify, this);
        release_native_object();
    }

    if (wrapper_is_rooted()) {
        /* This happens when the refcount on the object is still >1,
         * for example with global objects GDK never frees like GdkDisplay,
         * when we close down the JS runtime.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Wrapper was finalized despite being kept alive, has refcount >1");

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Unrooting object");

        discard_wrapper();
    }
    unlink();

    g_clear_pointer(&m_info, g_base_info_unref);
    g_clear_pointer(&m_class, g_type_class_unref);

    /* Remove the ObjectInstance pointer from the JSObject */
    JS_SetPrivate(obj, nullptr);

    GJS_DEC_COUNTER(object);
}

static JSObject *
gjs_lookup_object_constructor_from_info(JSContext    *context,
                                        GIObjectInfo *info,
                                        GType         gtype)
{
    JS::RootedObject in_object(context);
    const char *constructor_name;

    if (info) {
        in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);
        constructor_name = g_base_info_get_name((GIBaseInfo*) info);
    } else {
        in_object = gjs_lookup_private_namespace(context);
        constructor_name = g_type_name(gtype);
    }

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    JS::RootedObject constructor(context);
    if (value.isUndefined()) {
        /* In case we're looking for a private type, and we don't find it,
           we need to define it first.
        */
        JS::RootedObject ignored(context);
        gjs_define_object_class(context, in_object, NULL, gtype, &constructor,
                                &ignored);
    } else {
        if (G_UNLIKELY (!value.isObject()))
            return NULL;

        constructor = &value.toObject();
    }

    g_assert(constructor);

    return constructor;
}

static JSObject *
gjs_lookup_object_prototype_from_info(JSContext    *context,
                                      GIObjectInfo *info,
                                      GType         gtype)
{
    JS::RootedObject constructor(context,
        gjs_lookup_object_constructor_from_info(context, info, gtype));

    if (G_UNLIKELY(!constructor))
        return NULL;

    JS::RootedValue value(context);
    if (!gjs_object_get_property(context, constructor,
                                 GJS_STRING_PROTOTYPE, &value))
        return NULL;

    if (G_UNLIKELY (!value.isObjectOrNull()))
        return NULL;

    return value.toObjectOrNull();
}

static JSObject *
gjs_lookup_object_prototype(JSContext *context,
                            GType      gtype)
{
    GIObjectInfo *info;
    JSObject *proto;

    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), gtype);
    proto = gjs_lookup_object_prototype_from_info(context, info, gtype);
    if (info)
        g_base_info_unref((GIBaseInfo*)info);

    return proto;
}

void
ObjectInstance::associate_closure(JSContext *cx,
                                  GClosure  *closure)
{
    /* FIXME: Should be changed to g_assert(!is_prototype()) but due to the
     * structs being the same, this could still be an instance, the GObject of
     * which has been dissociated */
    if (!is_prototype())
        ensure_uses_toggle_ref(cx);

    /* This is a weak reference, and will be cleared when the closure is
     * invalidated */
    m_closures.insert(closure);
    g_closure_add_invalidate_notifier(closure, this,
                                      &ObjectInstance::closure_invalidated_notify);
}

bool
ObjectInstance::connect(JSContext *cx,
                        unsigned   argc,
                        JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);
    if (!priv)
        return throw_priv_is_null_error(cx); /* wrong class passed in */
    return priv->connect_impl(cx, args, false);
}

bool
ObjectInstance::connect_after(JSContext *cx,
                              unsigned   argc,
                              JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);
    if (!priv)
        return throw_priv_is_null_error(cx); /* wrong class passed in */
    return priv->connect_impl(cx, args, true);
}

bool
ObjectInstance::connect_impl(JSContext          *context,
                             const JS::CallArgs& argv,
                             bool                after)
{
    GClosure *closure;
    gulong id;
    guint signal_id;
    GQuark signal_detail;

    gjs_debug_gsignal("connect obj %p priv %p argc %d", m_wrapper.get(), this,
                      argv.length());

    if (!check_is_instance(context, "connect to signals"))
        return false;

    if (!check_gobject_disposed("connect to any signal on"))
        return true;

    if (argv.length() != 2 || !argv[0].isString() || !JS::IsCallable(&argv[1].toObject())) {
        gjs_throw(context, "connect() takes two args, the signal name and the callback");
        return false;
    }

    JS::RootedString signal_str(context, argv[0].toString());
    GjsAutoJSChar signal_name = JS_EncodeStringToUTF8(context, signal_str);
    if (!signal_name)
        return false;

    if (!g_signal_parse_name(signal_name, m_gtype, &signal_id, &signal_detail,
                             true)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(), type_name());
        return false;
    }

    closure = gjs_closure_new_for_signal(context, &argv[1].toObject(), "signal callback", signal_id);
    if (closure == NULL)
        return false;
    associate_closure(context, closure);

    id = g_signal_connect_closure_by_id(m_gobj,
                                        signal_id,
                                        signal_detail,
                                        closure,
                                        after);

    argv.rval().setDouble(id);

    return true;
}

bool
ObjectInstance::emit(JSContext *cx,
                     unsigned   argc,
                     JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);
    if (!priv)
        return throw_priv_is_null_error(cx); /* wrong class passed in */
    return priv->emit_impl(cx, args);
}

bool
ObjectInstance::emit_impl(JSContext          *context,
                          const JS::CallArgs& argv)
{
    guint signal_id;
    GQuark signal_detail;
    GSignalQuery signal_query;
    GValue *instance_and_args;
    GValue rvalue = G_VALUE_INIT;
    unsigned int i;
    bool failed;

    gjs_debug_gsignal("emit obj %p priv %p argc %d", m_wrapper.get(), this,
                      argv.length());

    if (!check_is_instance(context, "emit signal"))
        return false;

    if (!check_gobject_disposed("emit any signal on"))
        return true;

    if (argv.length() < 1 || !argv[0].isString()) {
        gjs_throw(context, "emit() first arg is the signal name");
        return false;
    }

    JS::RootedString signal_str(context, argv[0].toString());
    GjsAutoJSChar signal_name = JS_EncodeStringToUTF8(context, signal_str);
    if (!signal_name)
        return false;

    if (!g_signal_parse_name(signal_name, m_gtype, &signal_id, &signal_detail,
                             false)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(), type_name());
        return false;
    }

    g_signal_query(signal_id, &signal_query);

    if ((argv.length() - 1) != signal_query.n_params) {
        gjs_throw(context, "Signal '%s' on %s requires %d args got %d",
                  signal_name.get(), type_name(), signal_query.n_params,
                  argv.length() - 1);
        return false;
    }

    if (signal_query.return_type != G_TYPE_NONE) {
        g_value_init(&rvalue, signal_query.return_type & ~G_SIGNAL_TYPE_STATIC_SCOPE);
    }

    instance_and_args = g_newa(GValue, signal_query.n_params + 1);
    memset(instance_and_args, 0, sizeof(GValue) * (signal_query.n_params + 1));

    g_value_init(&instance_and_args[0], G_TYPE_FROM_INSTANCE(m_gobj));
    g_value_set_instance(&instance_and_args[0], m_gobj);

    failed = false;
    for (i = 0; i < signal_query.n_params; ++i) {
        GValue *value;
        value = &instance_and_args[i + 1];

        g_value_init(value, signal_query.param_types[i] & ~G_SIGNAL_TYPE_STATIC_SCOPE);
        if ((signal_query.param_types[i] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0)
            failed = !gjs_value_to_g_value_no_copy(context, argv[i + 1], value);
        else
            failed = !gjs_value_to_g_value(context, argv[i + 1], value);

        if (failed)
            break;
    }

    if (!failed) {
        g_signal_emitv(instance_and_args, signal_id, signal_detail,
                       &rvalue);
    }

    if (signal_query.return_type != G_TYPE_NONE) {
        if (!gjs_value_from_g_value(context, argv.rval(), &rvalue))
            failed = true;

        g_value_unset(&rvalue);
    } else {
        argv.rval().setUndefined();
    }

    for (i = 0; i < (signal_query.n_params + 1); ++i) {
        g_value_unset(&instance_and_args[i]);
    }

    return !failed;
}

bool
ObjectInstance::to_string(JSContext *cx,
                          unsigned   argc,
                          JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, ObjectInstance, priv);
    if (!priv)
        return throw_priv_is_null_error(cx);  /* wrong class passed in */
    return priv->to_string_impl(cx, args);
}

bool
ObjectInstance::to_string_impl(JSContext          *cx,
                               const JS::CallArgs& args)
{
    return _gjs_proxy_to_string_func(cx, m_wrapper,
                                     m_gobj_disposed ?
                                        "object (FINALIZED)" : "object",
                                     m_info, m_gtype,
                                     m_gobj, args.rval());
}

static const struct JSClassOps gjs_object_class_ops = {
    &ObjectInstance::add_property,
    NULL,  /* deleteProperty */
    nullptr,  /* getProperty */
    nullptr,  /* setProperty */
    NULL,  /* enumerate */
    &ObjectInstance::resolve,
    nullptr,  /* mayResolve */
    &ObjectInstance::finalize,
    NULL,
    NULL,
    NULL,
    &ObjectInstance::trace,
};

struct JSClass gjs_object_instance_class = {
    "GObject_Object",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &gjs_object_class_ops
};

bool
ObjectInstance::init(JSContext *context,
                     unsigned   argc,
                     JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);

    if (!do_base_typecheck(context, obj, true))
        return false;

    ObjectInstance *priv = ObjectInstance::for_js_nogc(obj);
    return priv->init_impl(context, argv, &obj);
}

JSPropertySpec gjs_object_instance_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_object_instance_proto_funcs[] = {
    JS_FS("_init", &ObjectInstance::init, 0, 0),
    JS_FS("connect", &ObjectInstance::connect, 0, 0),
    JS_FS("connect_after", &ObjectInstance::connect_after, 0, 0),
    JS_FS("emit", &ObjectInstance::emit, 0, 0),
    JS_FS("toString", &ObjectInstance::to_string, 0, 0),
    JS_FS_END
};

void
gjs_object_define_static_methods(JSContext       *context,
                                 JS::HandleObject constructor,
                                 GType            gtype,
                                 GIObjectInfo    *object_info)
{
    GIStructInfo *gtype_struct;
    int i;
    int n_methods;

    n_methods = g_object_info_get_n_methods(object_info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfo *meth_info;
        GIFunctionInfoFlags flags;

        meth_info = g_object_info_get_method(object_info, i);
        flags = g_function_info_get_flags (meth_info);

        /* Anything that isn't a method we put on the prototype of the
         * constructor.  This includes <constructor> introspection
         * methods, as well as the forthcoming "static methods"
         * support.  We may want to change this to use
         * GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the
         * like in the near future.
         */
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            if (!gjs_define_function(context, constructor, gtype,
                                     (GICallableInfo *) meth_info))
                gjs_log_exception(context);
        }

        g_base_info_unref((GIBaseInfo*) meth_info);
    }

    gtype_struct = g_object_info_get_class_struct(object_info);

    if (gtype_struct == NULL)
        return;

    n_methods = g_struct_info_get_n_methods(gtype_struct);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfo *meth_info;

        meth_info = g_struct_info_get_method(gtype_struct, i);

        if (!gjs_define_function(context, constructor, gtype,
                                 (GICallableInfo *) meth_info))
            gjs_log_exception(context);

        g_base_info_unref((GIBaseInfo*) meth_info);
    }

    g_base_info_unref((GIBaseInfo*) gtype_struct);
}

void
gjs_define_object_class(JSContext              *context,
                        JS::HandleObject        in_object,
                        GIObjectInfo           *info,
                        GType                   gtype,
                        JS::MutableHandleObject constructor,
                        JS::MutableHandleObject prototype)
{
    const char *constructor_name;
    JS::RootedObject parent_proto(context);

    ObjectInstance *priv;
    const char *ns;
    GType parent_type;

    g_assert(in_object);
    g_assert(gtype != G_TYPE_INVALID);

    /*   http://egachine.berlios.de/embedding-sm-best-practice/apa.html
     *   http://www.sitepoint.com/blogs/2006/01/17/javascript-inheritance/
     *   http://www.cs.rit.edu/~atk/JavaScript/manuals/jsobj/
     *
     * What we want is:
     *
     * repoobj.Gtk.Window is constructor for a GtkWindow wrapper JSObject
     *   (gjs_define_object_constructor() is supposed to define Window in Gtk)
     *
     * Window.prototype contains the methods on Window, e.g. set_default_size()
     * mywindow.__proto__ is Window.prototype
     * mywindow.__proto__.__proto__ is Bin.prototype
     * mywindow.__proto__.__proto__.__proto__ is Container.prototype
     *
     * Because Window.prototype is an instance of Window in a sense,
     * Window.prototype.__proto__ is Window.prototype, just as
     * mywindow.__proto__ is Window.prototype
     *
     * If we do "mywindow = new Window()" then we should get:
     *     mywindow.__proto__ == Window.prototype
     * which means "mywindow instanceof Window" is true.
     *
     * Remember "Window.prototype" is "the __proto__ of stuff
     * constructed with new Window()"
     *
     * __proto__ is used to search for properties if you do "this.foo"
     * while __parent__ defines the scope to search if you just have
     * "foo".
     *
     * __proto__ is used to look up properties, while .prototype is only
     * relevant for constructors and is used to set __proto__ on new'd
     * objects. So .prototype only makes sense on constructors.
     *
     * JS_SetPrototype() and JS_GetPrototype() are for __proto__.
     * To set/get .prototype, just use the normal property accessors,
     * or JS_InitClass() sets it up automatically.
     *
     * JavaScript is SO AWESOME
     */

    parent_type = g_type_parent(gtype);
    if (parent_type != G_TYPE_INVALID)
       parent_proto = gjs_lookup_object_prototype(context, parent_type);

    ns = gjs_get_names_from_gtype_and_gi_info(gtype, (GIBaseInfo *) info,
                                              &constructor_name);

    if (!gjs_init_class_dynamic(context, in_object,
                                parent_proto,
                                ns, constructor_name,
                                &gjs_object_instance_class,
                                gjs_object_instance_constructor, 0,
                                /* props of prototype */
                                parent_proto ? NULL : &gjs_object_instance_proto_props[0],
                                /* funcs of prototype */
                                parent_proto ? NULL : &gjs_object_instance_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                NULL,
                                prototype,
                                constructor)) {
        g_error("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(object);
    priv = g_slice_new0(ObjectInstance);
    new (priv) ObjectInstance(prototype, info, gtype);

    gjs_debug(GJS_DEBUG_GOBJECT, "Defined class for %s (%s), prototype %p, "
              "JSClass %p, in object %p", constructor_name, g_type_name(gtype),
              prototype.get(), JS_GetClass(prototype), in_object.get());

    if (info)
        gjs_object_define_static_methods(context, constructor, gtype, info);

    JS::RootedObject gtype_obj(context,
        gjs_gtype_create_gtype_wrapper(context, gtype));
    JS_DefineProperty(context, constructor, "$gtype", gtype_obj,
                      JSPROP_PERMANENT);
}

JSObject*
gjs_object_from_g_object(JSContext    *context,
                         GObject      *gobj)
{
    if (gobj == NULL)
        return NULL;

    ObjectInstance *priv = ObjectInstance::for_gobject(gobj);

    if (!priv) {
        /* We have to create a wrapper */
        GType gtype;

        gjs_debug_marshal(GJS_DEBUG_GOBJECT,
                          "Wrapping %s with JSObject",
                          g_type_name_from_instance((GTypeInstance*) gobj));

        gtype = G_TYPE_FROM_INSTANCE(gobj);

        JS::RootedObject proto(context,
            gjs_lookup_object_prototype(context, gtype));
        if (!proto)
            return nullptr;

        JS::RootedObject obj(context,
            JS_NewObjectWithGivenProto(context, JS_GetClass(proto), proto));
        if (!obj)
            return nullptr;

        priv = ObjectInstance::new_for_js_object(context, obj);

        g_object_ref_sink(gobj);
        priv->associate_js_gobject(context, obj, gobj);

        g_assert(priv->wrapper() == obj.get());
    }

    return priv->wrapper();
}

GObject*
gjs_g_object_from_object(JSContext       *cx,
                         JS::HandleObject obj)
{
    if (!obj)
        return NULL;

    auto *priv = ObjectInstance::for_js(cx, obj);

    if (!priv->check_gobject_disposed("access"))
        return nullptr;

    return priv->gobj();
}

bool
gjs_typecheck_is_object(JSContext       *context,
                        JS::HandleObject object,
                        bool             throw_error)
{
    return do_base_typecheck(context, object, throw_error);
}

bool
gjs_typecheck_object(JSContext       *context,
                     JS::HandleObject object,
                     GType            expected_type,
                     bool             throw_error)
{
    if (!do_base_typecheck(context, object, throw_error))
        return false;

    auto *priv = ObjectInstance::for_js(context, object);

    if (priv == NULL) {
        if (throw_error)
            return throw_priv_is_null_error(context);

        return false;
    }

    return priv->typecheck_object(context, expected_type, throw_error);
}

bool
ObjectInstance::typecheck_object(JSContext *context,
                                 GType      expected_type,
                                 bool       throw_error)
{
    if ((throw_error && !check_is_instance(context, "convert to GObject*")) ||
        is_prototype())
        return false;

    g_assert(m_gobj_disposed || m_gtype == G_OBJECT_TYPE(m_gobj));

    bool result;
    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a(m_gtype, expected_type);
    else
        result = true;

    if (!result && throw_error) {
        if (!is_custom_js_class()) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s.%s - cannot convert to %s",
                             ns(), name(),
                             g_type_name(expected_type));
        } else {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s - cannot convert to %s",
                             type_name(),
                             g_type_name(expected_type));
        }
    }

    return result;
}


static void
find_vfunc_info (JSContext *context,
                 GType implementor_gtype,
                 GIBaseInfo *vfunc_info,
                 const char   *vfunc_name,
                 gpointer *implementor_vtable_ret,
                 GIFieldInfo **field_info_ret)
{
    GType ancestor_gtype;
    int length, i;
    GIBaseInfo *ancestor_info;
    GIStructInfo *struct_info;
    gpointer implementor_class;
    bool is_interface;

    *field_info_ret = NULL;
    *implementor_vtable_ret = NULL;

    ancestor_info = g_base_info_get_container(vfunc_info);
    ancestor_gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)ancestor_info);

    is_interface = g_base_info_get_type(ancestor_info) == GI_INFO_TYPE_INTERFACE;

    implementor_class = g_type_class_ref(implementor_gtype);
    if (is_interface) {
        GTypeInstance *implementor_iface_class;
        implementor_iface_class = (GTypeInstance*) g_type_interface_peek(implementor_class,
                                                        ancestor_gtype);
        if (implementor_iface_class == NULL) {
            g_type_class_unref(implementor_class);
            gjs_throw (context, "Couldn't find GType of implementor of interface %s.",
                       g_type_name(ancestor_gtype));
            return;
        }

        *implementor_vtable_ret = implementor_iface_class;

        struct_info = g_interface_info_get_iface_struct((GIInterfaceInfo*)ancestor_info);
    } else {
        struct_info = g_object_info_get_class_struct((GIObjectInfo*)ancestor_info);
        *implementor_vtable_ret = implementor_class;
    }

    g_type_class_unref(implementor_class);

    length = g_struct_info_get_n_fields(struct_info);
    for (i = 0; i < length; i++) {
        GIFieldInfo *field_info;
        GITypeInfo *type_info;

        field_info = g_struct_info_get_field(struct_info, i);

        if (strcmp(g_base_info_get_name((GIBaseInfo*)field_info), vfunc_name) != 0) {
            g_base_info_unref(field_info);
            continue;
        }

        type_info = g_field_info_get_type(field_info);
        if (g_type_info_get_tag(type_info) != GI_TYPE_TAG_INTERFACE) {
            /* We have a field with the same name, but it's not a callback.
             * There's no hope of being another field with a correct name,
             * so just abort early. */
            g_base_info_unref(type_info);
            g_base_info_unref(field_info);
            break;
        } else {
            g_base_info_unref(type_info);
            *field_info_ret = field_info;
            break;
        }
    }

    g_base_info_unref(struct_info);
}

static bool
gjs_hook_up_vfunc(JSContext *cx,
                  unsigned   argc,
                  JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoJSChar name;
    JS::RootedObject object(cx), function(cx);

    if (!gjs_parse_call_args(cx, "hook_up_vfunc", argv, "oso",
                             "object", &object,
                             "name", &name,
                             "function", &function))
        return false;

    if (!do_base_typecheck(cx, object, true))
        return false;

    argv.rval().setUndefined();

    auto *priv = ObjectInstance::for_js(cx, object);
    return priv->hook_up_vfunc(cx, object, name, function);
}

bool
ObjectInstance::hook_up_vfunc(JSContext       *cx,
                              JS::HandleObject wrapper,
                              const char      *name,
                              JS::HandleObject function)
{
    /* find the first class that actually has repository information */
    GIObjectInfo *info = m_info;
    GType info_gtype = m_gtype;
    while (!info && info_gtype != G_TYPE_OBJECT) {
        info_gtype = g_type_parent(info_gtype);

        info = g_irepository_find_by_gtype(g_irepository_get_default(), info_gtype);
    }

    /* If we don't have 'info', we don't have the base class (GObject).
     * This is awful, so abort now. */
    g_assert(info != NULL);

    GjsAutoInfo<GIVFuncInfo> vfunc = find_vfunc_on_parents(info, name, nullptr);

    if (!vfunc) {
        guint i, n_interfaces;
        GType *interface_list;
        GIInterfaceInfo *interface;

        interface_list = g_type_interfaces(m_gtype, &n_interfaces);

        for (i = 0; i < n_interfaces; i++) {
            interface = (GIInterfaceInfo*)g_irepository_find_by_gtype(g_irepository_get_default(),
                                                                      interface_list[i]);

            /* The interface doesn't have to exist -- it could be private
             * or dynamic. */
            if (interface) {
                vfunc = g_interface_info_find_vfunc(interface, name);

                g_base_info_unref((GIBaseInfo*)interface);

                if (vfunc)
                    break;
            }
        }

        g_free(interface_list);
    }

    if (!vfunc) {
        gjs_throw(cx, "Could not find definition of virtual function %s", name);
        return false;
    }

    void *implementor_vtable;
    GIFieldInfo *field_info;
    find_vfunc_info(cx, m_gtype, vfunc, name, &implementor_vtable, &field_info);
    if (field_info != NULL) {
        gint offset;
        gpointer method_ptr;
        GjsCallbackTrampoline *trampoline;

        offset = g_field_info_get_offset(field_info);
        method_ptr = G_STRUCT_MEMBER_P(implementor_vtable, offset);

        JS::RootedValue v_function(cx, JS::ObjectValue(*function));
        trampoline = gjs_callback_trampoline_new(cx, v_function, vfunc,
                                                 GI_SCOPE_TYPE_NOTIFIED,
                                                 wrapper, true);

        *((ffi_closure **)method_ptr) = trampoline->closure;

        g_base_info_unref(field_info);
    }

    return true;
}

static gchar *
hyphen_to_underscore (gchar *string)
{
    gchar *str, *s;
    str = s = g_strdup(string);
    while (*(str++) != '\0') {
        if (*str == '-')
            *str = '_';
    }
    return s;
}

static void
gjs_object_get_gproperty (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    GjsContext *gjs_context;
    JSContext *context;
    gchar *underscore_name;
    auto *priv = ObjectInstance::for_gobject(object);

    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JS::RootedObject js_obj(context, priv->wrapper());
    JS::RootedValue jsvalue(context);

    underscore_name = hyphen_to_underscore((gchar *)pspec->name);
    if (!JS_GetProperty(context, js_obj, underscore_name, &jsvalue) ||
        !gjs_value_to_g_value(context, jsvalue, value))
        gjs_log_exception(context);
    g_free (underscore_name);
}

static void
jsobj_set_gproperty(JSContext       *context,
                    JS::HandleObject object,
                    const GValue    *value,
                    GParamSpec      *pspec)
{
    JS::RootedValue jsvalue(context);
    gchar *underscore_name;

    if (!gjs_value_from_g_value(context, &jsvalue, value))
        return;

    underscore_name = hyphen_to_underscore((gchar *)pspec->name);
    if (!JS_SetProperty(context, object, underscore_name, jsvalue))
        gjs_log_exception(context);
    g_free (underscore_name);
}

static GObject *
gjs_object_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
    if (!object_init_list.empty()) {
        GType parent_type = g_type_parent(type);

        /* The object is being constructed from JS:
         * Simply chain up to the first non-gjs constructor
         */
        while (G_OBJECT_CLASS(g_type_class_peek(parent_type))->constructor == gjs_object_constructor)
            parent_type = g_type_parent(parent_type);

        return G_OBJECT_CLASS(g_type_class_peek(parent_type))->constructor(type, n_construct_properties, construct_properties);
    }

    GjsContext *gjs_context;
    JSContext *context;
    JSObject *object;

    /* The object is being constructed from native code (e.g. GtkBuilder):
     * Construct the JS object from the constructor, then use the GObject
     * that was associated in gjs_object_custom_init()
     */
    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JSAutoRequest ar(context);
    JSAutoCompartment ac(context, gjs_get_import_global(context));

    JS::RootedObject constructor(context,
        gjs_lookup_object_constructor_from_info(context, NULL, type));
    if (!constructor)
        return NULL;

    if (n_construct_properties) {
        guint i;

        JS::RootedObject props_hash(context, JS_NewPlainObject(context));

        for (i = 0; i < n_construct_properties; i++)
            jsobj_set_gproperty(context, props_hash,
                                construct_properties[i].value,
                                construct_properties[i].pspec);

        JS::AutoValueArray<1> args(context);
        args[0].set(JS::ObjectValue(*props_hash));
        object = JS_New(context, constructor, args);
    } else {
        object = JS_New(context, constructor, JS::HandleValueArray::empty());
    }

    if (!object)
        return NULL;

    auto *priv = ObjectInstance::for_js_nogc(object);
    /* We only hold a toggle ref at this point, add back a ref that the
     * native code can own.
     */
    return G_OBJECT(g_object_ref(priv->gobj()));
}

static void
gjs_object_set_gproperty (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    GjsContext *gjs_context;
    JSContext *context;
    auto *priv = ObjectInstance::for_gobject(object);

    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JS::RootedObject js_obj(context, priv->wrapper());
    jsobj_set_gproperty(context, js_obj, value, pspec);
}

static bool
gjs_override_property(JSContext *cx,
                      unsigned   argc,
                      JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsAutoJSChar name;
    JS::RootedObject type(cx);
    GParamSpec *pspec;
    GParamSpec *new_pspec;
    GType gtype;

    if (!gjs_parse_call_args(cx, "override_property", args, "so",
                             "name", &name,
                             "type", &type))
        return false;

    if ((gtype = gjs_gtype_get_actual_gtype(cx, type)) == G_TYPE_INVALID) {
        gjs_throw(cx, "Invalid parameter type was not a GType");
        return false;
    }

    if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
        GTypeInterface *interface_type =
            (GTypeInterface *) g_type_default_interface_ref(gtype);
        pspec = g_object_interface_find_property(interface_type, name);
        g_type_default_interface_unref(interface_type);
    } else {
        GTypeClass *class_type = (GTypeClass *) g_type_class_ref(gtype);
        pspec = g_object_class_find_property(G_OBJECT_CLASS(class_type), name);
        g_type_class_unref(class_type);
    }

    if (pspec == NULL) {
        gjs_throw(cx, "No such property '%s' to override on type '%s'",
                  name.get(), g_type_name(gtype));
        return false;
    }

    new_pspec = g_param_spec_override(name, pspec);

    g_param_spec_set_qdata(new_pspec, gjs_is_custom_property_quark(), GINT_TO_POINTER(1));

    args.rval().setObject(*gjs_param_from_g_param(cx, new_pspec));
    g_param_spec_unref(new_pspec);

    return true;
}

static void
gjs_interface_init(GTypeInterface *g_iface,
                   gpointer        iface_data)
{
    GType gtype = G_TYPE_FROM_INTERFACE(g_iface);

    auto found = class_init_properties.find(gtype);
    if (found == class_init_properties.end())
        return;

    ParamRefArray& properties = found->second;
    for (ParamRef& pspec : properties) {
        g_param_spec_set_qdata(pspec.get(), gjs_is_custom_property_quark(),
                               GINT_TO_POINTER(1));
        g_object_interface_install_property(g_iface, pspec.get());
    }

    class_init_properties.erase(found);
}

static void
gjs_object_class_init(GObjectClass *klass,
                      gpointer      user_data)
{
    GType gtype = G_OBJECT_CLASS_TYPE(klass);

    klass->constructor = gjs_object_constructor;
    klass->set_property = gjs_object_set_gproperty;
    klass->get_property = gjs_object_get_gproperty;

    auto found = class_init_properties.find(gtype);
    if (found == class_init_properties.end())
        return;

    ParamRefArray& properties = found->second;
    unsigned i = 0;
    for (ParamRef& pspec : properties) {
        g_param_spec_set_qdata(pspec.get(), gjs_is_custom_property_quark(),
                               GINT_TO_POINTER(1));
        g_object_class_install_property(klass, ++i, pspec.get());
    }

    class_init_properties.erase(found);
}

static void
gjs_object_custom_init(GTypeInstance *instance,
                       gpointer       klass)
{
    GjsContext *gjs_context;
    JSContext *context;
    ObjectInstance *priv;

    if (object_init_list.empty())
      return;

    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JS::RootedObject object(context, object_init_list.top().get());
    priv = ObjectInstance::for_js_nogc(object);

    if (priv->gtype() != G_TYPE_FROM_INSTANCE(instance)) {
        /* This is not the most derived instance_init function,
           do nothing.
         */
        return;
    }

    object_init_list.pop();

    priv->associate_js_gobject(context, object, G_OBJECT(instance));

    /* Custom JS objects will most likely have visible state, so
     * just do this from the start */
    priv->ensure_uses_toggle_ref(context);

    JS::RootedValue v(context);
    if (!gjs_object_get_property(context, object,
                                 GJS_STRING_INSTANCE_INIT, &v)) {
        gjs_log_exception(context);
        return;
    }

    if (!v.isObject())
        return;

    JS::RootedValue r(context);
    if (!JS_CallFunctionValue(context, object, v,
                              JS::HandleValueArray::empty(), &r))
        gjs_log_exception(context);
}

static inline void
gjs_add_interface(GType instance_type,
                  GType interface_type)
{
    static GInterfaceInfo interface_vtable = { NULL, NULL, NULL };

    g_type_add_interface_static(instance_type,
                                interface_type,
                                &interface_vtable);
}

static bool
validate_interfaces_and_properties_args(JSContext       *cx,
                                        JS::HandleObject interfaces,
                                        JS::HandleObject properties,
                                        uint32_t        *n_interfaces,
                                        uint32_t        *n_properties)
{
    guint32 n_int, n_prop;
    bool is_array;

    if (!JS_IsArrayObject(cx, interfaces, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter interfaces (expected Array)");
        return false;
    }

    if (!JS_GetArrayLength(cx, interfaces, &n_int))
        return false;

    if (!JS_IsArrayObject(cx, properties, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(cx, "Invalid parameter properties (expected Array)");
        return false;
    }

    if (!JS_GetArrayLength(cx, properties, &n_prop))
        return false;

    if (n_interfaces != NULL)
        *n_interfaces = n_int;
    if (n_properties != NULL)
        *n_properties = n_prop;
    return true;
}

static bool
get_interface_gtypes(JSContext       *cx,
                     JS::HandleObject interfaces,
                     uint32_t         n_interfaces,
                     GType           *iface_types)
{
    guint32 i;

    for (i = 0; i < n_interfaces; i++) {
        JS::RootedValue iface_val(cx);
        GType iface_type;

        if (!JS_GetElement(cx, interfaces, i, &iface_val))
            return false;

        if (!iface_val.isObject()) {
            gjs_throw (cx, "Invalid parameter interfaces (element %d was not a GType)", i);
            return false;
        }

        JS::RootedObject iface(cx, &iface_val.toObject());
        iface_type = gjs_gtype_get_actual_gtype(cx, iface);
        if (iface_type == G_TYPE_INVALID) {
            gjs_throw (cx, "Invalid parameter interfaces (element %d was not a GType)", i);
            return false;
        }

        iface_types[i] = iface_type;
    }
    return true;
}

static bool
save_properties_for_class_init(JSContext       *cx,
                               JS::HandleObject properties,
                               uint32_t         n_properties,
                               GType            gtype)
{
    ParamRefArray properties_native;
    JS::RootedValue prop_val(cx);
    JS::RootedObject prop_obj(cx);
    for (uint32_t i = 0; i < n_properties; i++) {
        if (!JS_GetElement(cx, properties, i, &prop_val))
            return false;

        if (!prop_val.isObject()) {
            gjs_throw(cx, "Invalid parameter, expected object");
            return false;
        }

        prop_obj = &prop_val.toObject();
        if (!gjs_typecheck_param(cx, prop_obj, G_TYPE_NONE, true))
            return false;

        properties_native.emplace_back(g_param_spec_ref(gjs_g_param_from_param(cx, prop_obj)),
                                       g_param_spec_unref);
    }
    class_init_properties[gtype] = std::move(properties_native);
    return true;
}

static bool
gjs_register_interface(JSContext *cx,
                       unsigned   argc,
                       JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    GjsAutoJSChar name;
    guint32 i, n_interfaces, n_properties;
    GType *iface_types;
    GType interface_type;
    GTypeInfo type_info = {
        sizeof (GTypeInterface), /* class_size */

        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,

        (GClassInitFunc) gjs_interface_init,
        (GClassFinalizeFunc) NULL,
        NULL, /* class_data */

        0,    /* instance_size */
        0,    /* n_preallocs */
        NULL, /* instance_init */
    };

    JS::RootedObject interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_interface", args, "soo",
                             "name", &name,
                             "interfaces", &interfaces,
                             "properties", &properties))
        return false;

    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    iface_types = (GType *) g_alloca(sizeof(GType) * n_interfaces);

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name.get());
        return false;
    }

    interface_type = g_type_register_static(G_TYPE_INTERFACE, name, &type_info,
                                            (GTypeFlags) 0);

    g_type_set_qdata(interface_type, gjs_is_custom_type_quark(), GINT_TO_POINTER(1));

    if (!save_properties_for_class_init(cx, properties, n_properties, interface_type))
        return false;

    for (i = 0; i < n_interfaces; i++)
        g_type_interface_add_prerequisite(interface_type, iface_types[i]);

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    if (!module)
        return false;  /* error will have been thrown already */

    JS::RootedObject constructor(cx);
    gjs_define_interface_class(cx, module, NULL, interface_type, &constructor);

    args.rval().setObject(*constructor);
    return true;
}

static void
gjs_object_base_init(void *klass)
{
    auto *priv = ObjectInstance::for_gtype(G_OBJECT_CLASS_TYPE(klass));
    if (priv)
        priv->ref_closures();
}

static void
gjs_object_base_finalize(void *klass)
{
    auto *priv = ObjectInstance::for_gtype(G_OBJECT_CLASS_TYPE(klass));
    if (priv)
        priv->unref_closures();
}

static bool
gjs_register_type(JSContext *cx,
                  unsigned   argc,
                  JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoJSChar name;
    GType instance_type, parent_type;
    GTypeQuery query;
    ObjectInstance *parent_priv;
    GTypeInfo type_info = {
        0, /* class_size */

        gjs_object_base_init,
        gjs_object_base_finalize,

	(GClassInitFunc) gjs_object_class_init,
	(GClassFinalizeFunc) NULL,
	NULL, /* class_data */

	0,    /* instance_size */
	0,    /* n_preallocs */
	gjs_object_custom_init,
    };
    guint32 i, n_interfaces, n_properties;
    GType *iface_types;

    JSAutoRequest ar(cx);

    JS::RootedObject parent(cx), interfaces(cx), properties(cx);
    if (!gjs_parse_call_args(cx, "register_type", argv, "osoo",
                             "parent", &parent,
                             "name", &name,
                             "interfaces", &interfaces,
                             "properties", &properties))
        return false;

    if (!parent)
        return false;

    if (!do_base_typecheck(cx, parent, true))
        return false;

    if (!validate_interfaces_and_properties_args(cx, interfaces, properties,
                                                 &n_interfaces, &n_properties))
        return false;

    iface_types = (GType*) g_alloca(sizeof(GType) * n_interfaces);

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    if (!get_interface_gtypes(cx, interfaces, n_interfaces, iface_types))
        return false;

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw(cx, "Type name %s is already registered", name.get());
        return false;
    }

    parent_priv = ObjectInstance::for_js(cx, parent);

    /* We checked parent above, in do_base_typecheck() */
    g_assert(parent_priv != NULL);

    parent_type = parent_priv->gtype();

    g_type_query_dynamic_safe(parent_type, &query);
    if (G_UNLIKELY (query.type == 0)) {
        gjs_throw (cx, "Cannot inherit from a non-gjs dynamic type [bug 687184]");
        return false;
    }

    type_info.class_size = query.class_size;
    type_info.instance_size = query.instance_size;

    instance_type = g_type_register_static(parent_type, name, &type_info,
                                           (GTypeFlags) 0);

    g_type_set_qdata (instance_type, gjs_is_custom_type_quark(), GINT_TO_POINTER (1));

    if (!save_properties_for_class_init(cx, properties, n_properties, instance_type))
        return false;

    for (i = 0; i < n_interfaces; i++)
        gjs_add_interface(instance_type, iface_types[i]);

    /* create a custom JSClass */
    JS::RootedObject module(cx, gjs_lookup_private_namespace(cx));
    JS::RootedObject constructor(cx), prototype(cx);
    gjs_define_object_class(cx, module, nullptr, instance_type, &constructor,
                            &prototype);

    ObjectInstance *priv = priv_from_js(cx, prototype);
    g_type_set_qdata(instance_type, gjs_object_priv_quark(), priv);

    argv.rval().setObject(*constructor);

    return true;
}

static bool
gjs_signal_new(JSContext *cx,
               unsigned   argc,
               JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GType gtype;
    GjsAutoJSChar signal_name;
    GSignalAccumulator accumulator;
    gint signal_id;
    guint i, n_parameters;
    GType *params, return_type;

    if (argc != 6)
        return false;

    JSAutoRequest ar(cx);

    if (!gjs_string_to_utf8(cx, argv[1], &signal_name))
        return false;

    JS::RootedObject obj(cx, &argv[0].toObject());
    if (!gjs_typecheck_gtype(cx, obj, true))
        return false;

    /* we only support standard accumulators for now */
    switch (argv[3].toInt32()) {
    case 1:
        accumulator = g_signal_accumulator_first_wins;
        break;
    case 2:
        accumulator = g_signal_accumulator_true_handled;
        break;
    case 0:
    default:
        accumulator = NULL;
    }

    JS::RootedObject gtype_obj(cx, &argv[4].toObject());
    return_type = gjs_gtype_get_actual_gtype(cx, gtype_obj);

    if (accumulator == g_signal_accumulator_true_handled && return_type != G_TYPE_BOOLEAN) {
        gjs_throw (cx, "GObject.SignalAccumulator.TRUE_HANDLED can only be used with boolean signals");
        return false;
    }

    JS::RootedObject params_obj(cx, &argv[5].toObject());
    if (!JS_GetArrayLength(cx, params_obj, &n_parameters))
        return false;

    params = g_newa(GType, n_parameters);
    JS::RootedValue gtype_val(cx);
    for (i = 0; i < n_parameters; i++) {
        if (!JS_GetElement(cx, params_obj, i, &gtype_val) ||
            !gtype_val.isObject()) {
            gjs_throw(cx, "Invalid signal parameter number %d", i);
            return false;
        }

        JS::RootedObject gjs_gtype(cx, &gtype_val.toObject());
        params[i] = gjs_gtype_get_actual_gtype(cx, gjs_gtype);
    }

    gtype = gjs_gtype_get_actual_gtype(cx, obj);

    signal_id = g_signal_newv(signal_name,
                              gtype,
                              (GSignalFlags) argv[2].toInt32(), /* signal_flags */
                              NULL, /* class closure */
                              accumulator,
                              NULL, /* accu_data */
                              g_cclosure_marshal_generic,
                              return_type, /* return type */
                              n_parameters,
                              params);

    argv.rval().setInt32(signal_id);
    return true;
}

static JSFunctionSpec module_funcs[] = {
    JS_FS("override_property", gjs_override_property, 2, GJS_MODULE_PROP_FLAGS),
    JS_FS("register_interface", gjs_register_interface, 3, GJS_MODULE_PROP_FLAGS),
    JS_FS("register_type", gjs_register_type, 4, GJS_MODULE_PROP_FLAGS),
    JS_FS("hook_up_vfunc", gjs_hook_up_vfunc, 3, GJS_MODULE_PROP_FLAGS),
    JS_FS("signal_new", gjs_signal_new, 6, GJS_MODULE_PROP_FLAGS),
    JS_FS_END,
};

bool
gjs_define_private_gi_stuff(JSContext              *cx,
                            JS::MutableHandleObject module)
{
    module.set(JS_NewPlainObject(cx));
    return JS_DefineFunctions(cx, module, &module_funcs[0]);
}

bool
gjs_lookup_object_constructor(JSContext             *context,
                              GType                  gtype,
                              JS::MutableHandleValue value_p)
{
    JSObject *constructor;
    GIObjectInfo *object_info;

    object_info = (GIObjectInfo*)g_irepository_find_by_gtype(NULL, gtype);

    g_assert(object_info == NULL ||
             g_base_info_get_type((GIBaseInfo*)object_info) ==
             GI_INFO_TYPE_OBJECT);

    constructor = gjs_lookup_object_constructor_from_info(context, object_info, gtype);

    if (G_UNLIKELY (constructor == NULL))
        return false;

    if (object_info)
        g_base_info_unref((GIBaseInfo*)object_info);

    value_p.setObject(*constructor);
    return true;
}

bool
gjs_object_associate_closure(JSContext       *cx,
                             JS::HandleObject object,
                             GClosure        *closure)
{
    ObjectInstance *priv = priv_from_js(cx, object);
    if (!priv)
        return false;

    priv->associate_closure(cx, closure);
    return true;
}
