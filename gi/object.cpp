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
#include "gtype.h"
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

typedef class GjsListLink GjsListLink;
typedef struct ObjectInstance ObjectInstance;

static GjsListLink* object_instance_get_link(ObjectInstance *priv);

class GjsListLink {
 private:
    ObjectInstance *m_prev;
    ObjectInstance *m_next;

 public:
    ObjectInstance* prev() {
        return m_prev;
    }

    ObjectInstance* next() {
        return m_next;
    }

    void prepend(ObjectInstance *this_instance,
                 ObjectInstance *head) {
        GjsListLink *elem = object_instance_get_link(head);

        g_assert(object_instance_get_link(this_instance) == this);

        if (elem->m_prev) {
            GjsListLink *prev = object_instance_get_link(elem->m_prev);
            prev->m_next = this_instance;
            this->m_prev = elem->m_prev;
        }

        elem->m_prev = this_instance;
        this->m_next = head;
    }

    void unlink() {
        if (m_prev)
            object_instance_get_link(m_prev)->m_next = m_next;
        if (m_next)
            object_instance_get_link(m_next)->m_prev = m_prev;

        m_prev = m_next = NULL;
    }

    int size() {
        GjsListLink *elem = this;
        int count = 0;

        do {
            count++;
            if (!elem->m_next)
                break;
            elem = object_instance_get_link(elem->m_next);
        } while (elem);

        return count;
    }
};

struct ObjectInstance {
    GIObjectInfo *info;
    GObject *gobj; /* NULL if we are the prototype and not an instance */
    GjsMaybeOwned<JSObject *> keep_alive;
    GType gtype;

    /* a list of all GClosures installed on this object (from
     * signals, trampolines and explicit GClosures), used when tracing */
    std::set<GClosure *> closures;

    /* the GObjectClass wrapped by this JS Object (only used for
       prototypes) */
    GTypeClass *klass;

    GjsListLink instance_link;

    unsigned js_object_finalized : 1;
    unsigned g_object_finalized  : 1;

    /* True if this object has visible JS state, and thus its lifecycle is
     * managed using toggle references. False if this object just keeps a
     * hard ref on the underlying GObject, and may be finalized at will. */
    bool uses_toggle_ref : 1;
};

static std::stack<JS::PersistentRootedObject> object_init_list;

using ParamRef = std::unique_ptr<GParamSpec, decltype(&g_param_spec_unref)>;
using ParamRefArray = std::vector<ParamRef>;
static std::unordered_map<GType, ParamRefArray> class_init_properties;

static bool context_weak_pointer_callback = false;
static bool weak_pointer_callback = false;
ObjectInstance *wrapped_gobject_list;

extern struct JSClass gjs_object_instance_class;
GJS_DEFINE_PRIV_FROM_JS(ObjectInstance, gjs_object_instance_class)

static void            disassociate_js_gobject (GObject *gobj);
static void ensure_uses_toggle_ref(JSContext *cx, ObjectInstance *priv);

typedef enum {
    SOME_ERROR_OCCURRED = false,
    NO_SUCH_G_PROPERTY,
    VALUE_WAS_SET
} ValueFromPropertyResult;

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

static void
throw_priv_is_null_error(JSContext *context)
{
    gjs_throw(context,
              "This JS object wrapper isn't wrapping a GObject."
              " If this is a custom subclass, are you sure you chained"
              " up to the parent _init properly?");
}

static ObjectInstance *
get_object_qdata(GObject *gobj)
{
    auto priv = static_cast<ObjectInstance *>(g_object_get_qdata(gobj,
                                                                 gjs_object_priv_quark()));

    if (priv && priv->uses_toggle_ref && G_UNLIKELY(priv->js_object_finalized)) {
        g_critical("Object %p (a %s) resurfaced after the JS wrapper was finalized. "
                   "This is some library doing dubious memory management inside dispose()",
                   gobj, g_type_name(G_TYPE_FROM_INSTANCE(gobj)));
        priv->js_object_finalized = false;
        g_assert(!priv->keep_alive);  /* should associate again with a new wrapper */
    }

    return priv;
}

static void
set_object_qdata(GObject        *gobj,
                 ObjectInstance *priv)
{
    g_object_set_qdata(gobj, gjs_object_priv_quark(), priv);
}

static ValueFromPropertyResult
init_g_param_from_property(JSContext      *context,
                           const char     *js_prop_name,
                           JS::HandleValue value,
                           GType           gtype,
                           GParameter     *parameter,
                           bool            constructing)
{
    char *gname;
    GParamSpec *param_spec;
    void *klass;

    gname = gjs_hyphen_from_camel(js_prop_name);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Hyphen name %s on %s", gname, g_type_name(gtype));

    klass = g_type_class_ref(gtype);
    param_spec = g_object_class_find_property(G_OBJECT_CLASS(klass),
                                              gname);
    g_type_class_unref(klass);
    g_free(gname);

    if (param_spec == NULL) {
        /* not a GObject prop, so nothing else to do */
        return NO_SUCH_G_PROPERTY;
    }

    /* Do not set JS overridden properties through GObject, to avoid
     * infinite recursion (but set them when constructing) */
    if (!constructing &&
        g_param_spec_get_qdata(param_spec, gjs_is_custom_property_quark()))
        return NO_SUCH_G_PROPERTY;


    if ((param_spec->flags & G_PARAM_WRITABLE) == 0) {
        /* prevent setting the prop even in JS */
        gjs_throw(context, "Property %s (GObject %s) is not writable",
                     js_prop_name, param_spec->name);
        return SOME_ERROR_OCCURRED;
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Syncing %s to GObject prop %s",
                     js_prop_name, param_spec->name);

    g_value_init(&parameter->value, G_PARAM_SPEC_VALUE_TYPE(param_spec));
    if (!gjs_value_to_g_value(context, value, &parameter->value)) {
        g_value_unset(&parameter->value);
        return SOME_ERROR_OCCURRED;
    }

    parameter->name = param_spec->name;

    return VALUE_WAS_SET;
}

static inline ObjectInstance *
proto_priv_from_js(JSContext       *context,
                   JS::HandleObject obj)
{
    JS::RootedObject proto(context);
    JS_GetPrototype(context, obj, &proto);
    return priv_from_js(context, proto);
}

static bool
get_prop_from_g_param(JSContext             *context,
                      JS::HandleObject       obj,
                      ObjectInstance        *priv,
                      const char            *name,
                      JS::MutableHandleValue value_p)
{
    char *gname;
    GParamSpec *param;
    GValue gvalue = { 0, };

    gname = gjs_hyphen_from_camel(name);
    param = g_object_class_find_property(G_OBJECT_GET_CLASS(priv->gobj),
                                         gname);
    g_free(gname);

    if (param == NULL) {
        /* leave value_p as it was */
        return true;
    }

    /* Do not fetch JS overridden properties from GObject, to avoid
     * infinite recursion. */
    if (g_param_spec_get_qdata(param, gjs_is_custom_property_quark()))
        return true;

    if ((param->flags & G_PARAM_READABLE) == 0)
        return true;

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Overriding %s with GObject prop %s",
                     name, param->name);

    g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(priv->gobj, param->name,
                          &gvalue);
    if (!gjs_value_from_g_value(context, value_p, &gvalue)) {
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

static bool
get_prop_from_field(JSContext             *cx,
                    JS::HandleObject       obj,
                    ObjectInstance        *priv,
                    const char            *name,
                    JS::MutableHandleValue value_p)
{
    if (priv->info == NULL)
        return true;  /* Not resolved, but no error; leave value_p untouched */

    GIFieldInfo *field = lookup_field_info(priv->info, name);

    if (field == NULL)
        return true;

    bool retval = true;
    GITypeInfo *type = NULL;
    GITypeTag tag;
    GIArgument arg = { 0 };

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT, "Overriding %s with GObject field",
                     name);

    type = g_field_info_get_type(field);
    tag = g_type_info_get_tag(type);
    if (tag == GI_TYPE_TAG_ARRAY ||
        tag == GI_TYPE_TAG_INTERFACE ||
        tag == GI_TYPE_TAG_GLIST ||
        tag == GI_TYPE_TAG_GSLIST ||
        tag == GI_TYPE_TAG_GHASH ||
        tag == GI_TYPE_TAG_ERROR) {
        gjs_throw(cx, "Can't get field %s; GObject introspection supports only "
                  "fields with simple types, not %s", name,
                  g_type_tag_to_string(tag));
        retval = false;
        goto out;
    }

    retval = g_field_info_get_field(field, priv->gobj, &arg);
    if (!retval) {
        gjs_throw(cx, "Error getting field %s from object", name);
        goto out;
    }

    retval = gjs_value_from_g_argument(cx, value_p, type, &arg, true);
    /* copy_structs is irrelevant because g_field_info_get_field() doesn't
     * handle boxed types */

out:
    if (type != NULL)
        g_base_info_unref((GIBaseInfo *) type);
    g_base_info_unref((GIBaseInfo *) field);
    return retval;
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is false on OOM/exception.
 */
static bool
object_instance_get_prop(JSContext              *context,
                         JS::HandleObject        obj,
                         JS::HandleId            id,
                         JS::MutableHandleValue  value_p)
{
    ObjectInstance *priv;
    GjsAutoJSChar name;

    if (!gjs_get_string_id(context, id, &name))
        return true; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Get prop '%s' hook obj %p priv %p",
                     name.get(), obj.get(), priv);

    if (priv == nullptr)
        /* If we reach this point, either object_instance_new_resolve
         * did not throw (so name == "_init"), or the property actually
         * exists and it's not something we should be concerned with */
        return true;

    if (priv->gobj == NULL) /* prototype, not an instance. */
        return true;

    if (priv->g_object_finalized) {
        g_critical("Object %s.%s (%p), has been already finalized. "
                   "Impossible to get any property from it.",
                   priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                   priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype),
                   priv->gobj);
        gjs_dumpstack();
        return true;
    }

    if (!get_prop_from_g_param(context, obj, priv, name, value_p))
        return false;

    if (!value_p.isUndefined())
        return true;

    /* Fall back to fields */
    return get_prop_from_field(context, obj, priv, name, value_p);
}

static bool
set_g_param_from_prop(JSContext          *context,
                      ObjectInstance     *priv,
                      const char         *name,
                      bool&               was_set,
                      JS::HandleValue     value_p,
                      JS::ObjectOpResult& result)
{
    GParameter param = { NULL, { 0, }};
    was_set = false;

    switch (init_g_param_from_property(context, name,
                                       value_p,
                                       G_TYPE_FROM_INSTANCE(priv->gobj),
                                       &param,
                                       false /* constructing */)) {
    case SOME_ERROR_OCCURRED:
        return false;
    case NO_SUCH_G_PROPERTY:
        /* We need to keep the wrapper alive in order not to lose custom
         * "expando" properties */
        ensure_uses_toggle_ref(context, priv);
        return result.succeed();
    case VALUE_WAS_SET:
    default:
        break;
    }

    g_object_set_property(priv->gobj, param.name,
                          &param.value);

    g_value_unset(&param.value);
    was_set = true;
    return result.succeed();
}

static bool
check_set_field_from_prop(JSContext             *cx,
                          ObjectInstance        *priv,
                          const char            *name,
                          JS::MutableHandleValue value_p,
                          JS::ObjectOpResult&    result)
{
    if (priv->info == NULL)
        return result.succeed();

    GIFieldInfo *field = lookup_field_info(priv->info, name);
    if (field == NULL)
        return result.succeed();

    bool retval = true;

    /* As far as I know, GI never exposes GObject instance struct fields as
     * writable, so no need to implement this for the time being */
    if (g_field_info_get_flags(field) & GI_FIELD_IS_WRITABLE) {
        g_message("Field %s of a GObject is writable, but setting it is not "
                  "implemented", name);
        result.succeed();
        goto out;
    }

    result.failReadOnly();  /* still return true; error only in strict mode */

    /* We have to update value_p because JS caches it as the property's "stored
     * value" (https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/Stored_value)
     * and so subsequent gets would get the stored value instead of accessing
     * the field */
    value_p.setUndefined();
out:
    g_base_info_unref((GIBaseInfo *) field);
    return retval;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is false on OOM/exception.
 */
static bool
object_instance_set_prop(JSContext              *context,
                         JS::HandleObject        obj,
                         JS::HandleId            id,
                         JS::MutableHandleValue  value_p,
                         JS::ObjectOpResult&     result)
{
    ObjectInstance *priv;
    GjsAutoJSChar name;
    bool ret = true;
    bool g_param_was_set = false;

    priv = priv_from_js(context, obj);
    if (priv == nullptr)
        /* see the comment in object_instance_get_prop() on this */
        return result.succeed();

    if (priv->gobj == NULL) /* prototype, not an instance. */
        return result.succeed();

    if (priv->g_object_finalized) {
        g_critical("Object %s.%s (%p), has been already finalized. "
                   "Impossible to set any property to it.",
                   priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                   priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype),
                   priv->gobj);
        gjs_dumpstack();
        return result.succeed();
    }

    if (!gjs_get_string_id(context, id, &name)) {
        /* We need to keep the wrapper alive in order not to lose custom
         * "expando" properties. In this case if gjs_get_string_id() is false
         * then a number or symbol property was probably set. */
        ensure_uses_toggle_ref(context, priv);
        return result.succeed();  /* not resolved, but no error */
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Set prop '%s' hook obj %p priv %p",
                     name.get(), obj.get(), priv);

    ret = set_g_param_from_prop(context, priv, name, g_param_was_set, value_p, result);
    if (g_param_was_set || !ret)
        return ret;

    /* note that the prop will also have been set in JS, which I think
     * is OK, since we hook get and set so will always override that
     * value. We could also use JS_DefineProperty though and specify a
     * getter/setter maybe, don't know if that is better.
     */
    return check_set_field_from_prop(context, priv, name, value_p, result);
}

static bool
is_vfunc_unchanged(GIVFuncInfo *info,
                   GType        gtype)
{
    GType ptype = g_type_parent(gtype);
    GError *error = NULL;
    gpointer addr1, addr2;

    addr1 = g_vfunc_info_get_address(info, gtype, &error);
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

static bool
object_instance_resolve_no_info(JSContext       *context,
                                JS::HandleObject obj,
                                bool            *resolved,
                                ObjectInstance  *priv,
                                const char      *name)
{
    GIFunctionInfo *method_info;
    guint n_interfaces;
    guint i;

    GType *interfaces = g_type_interfaces(priv->gtype, &n_interfaces);
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
                if (!gjs_define_function(context, obj, priv->gtype,
                                        (GICallableInfo *)method_info)) {
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
                      const char   *name)
{
    GIFieldInfo *field_info = lookup_field_info(info, name);
    if (!field_info)
        return false;
    g_base_info_unref(field_info);
    return true;
}

/* The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved. */
static bool
object_instance_resolve(JSContext       *context,
                        JS::HandleObject obj,
                        JS::HandleId     id,
                        bool            *resolved)
{
    GIFunctionInfo *method_info;
    ObjectInstance *priv;
    GjsAutoJSChar name;

    if (!gjs_get_string_id(context, id, &name)) {
        *resolved = false;
        return true; /* not resolved, but no error */
    }

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Resolve prop '%s' hook obj %p priv %p (%s.%s) gobj %p %s",
                     name.get(),
                     obj.get(),
                     priv,
                     priv && priv->info ? g_base_info_get_namespace (priv->info) : "",
                     priv && priv->info ? g_base_info_get_name (priv->info) : "",
                     priv ? priv->gobj : NULL,
                     (priv && priv->gobj) ? g_type_name_from_instance((GTypeInstance*) priv->gobj) : "(type unknown)");

    if (priv == NULL) {
        /* We won't have a private until the initializer is called, so
         * just defer to prototype chains in this case.
         *
         * This isn't too bad: either you get undefined if the field
         * doesn't exist on any of the prototype chains, or whatever code
         * will run afterwards will fail because of the "priv == NULL"
         * check there.
         */
        *resolved = false;
        return true;
    }

    if (priv->gobj != NULL) {
        *resolved = false;
        return true;
    }

    /* If we have no GIRepository information (we're a JS GObject subclass),
     * we need to look at exposing interfaces. Look up our interfaces through
     * GType data, and then hope that *those* are introspectable. */
    if (priv->info == NULL) {
        bool status = object_instance_resolve_no_info(context, obj, resolved, priv, name);
        return status;
    }

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
        GIVFuncInfo *vfunc;
        bool defined_by_parent;

        vfunc = find_vfunc_on_parents(priv->info, name_without_vfunc_, &defined_by_parent);
        if (vfunc != NULL) {

            /* In the event that the vfunc is unchanged, let regular
             * prototypal inheritance take over. */
            if (defined_by_parent && is_vfunc_unchanged(vfunc, priv->gtype)) {
                g_base_info_unref((GIBaseInfo *)vfunc);
                *resolved = false;
                return true;
            }

            gjs_define_function(context, obj, priv->gtype, vfunc);
            *resolved = true;
            g_base_info_unref((GIBaseInfo *)vfunc);
            return true;
        }

        /* If the vfunc wasn't found, fall through, back to normal
         * method resolution. */
    }

    /* If the name refers to a GObject property or field, don't resolve.
     * Instead, let the getProperty hook handle fetching the property from
     * GObject. */
    if (is_gobject_property_name(priv->info, name) ||
        is_gobject_field_name(priv->info, name)) {
        gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                         "Breaking out of %p resolve, '%s' is a GObject prop",
                         obj.get(), name.get());
        *resolved = false;
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

    method_info = g_object_info_find_method_using_interfaces(priv->info,
                                                             name,
                                                             NULL);

    /**
     * Search through any interfaces implemented by the GType;
     * this could be done better.  See
     * https://bugzilla.gnome.org/show_bug.cgi?id=632922
     */
    if (method_info == NULL) {
        bool retval = object_instance_resolve_no_info(context, obj, resolved, priv, name);
        return retval;
    }


#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

    if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for %s (%s.%s)",
                  g_base_info_get_name( (GIBaseInfo*) method_info),
                  g_type_name(priv->gtype),
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));

        if (gjs_define_function(context, obj, priv->gtype, method_info) == NULL) {
            g_base_info_unref( (GIBaseInfo*) method_info);
            return false;
        }

        *resolved = true; /* we defined the prop in obj */
    } else {
        *resolved = false;
    }

    g_base_info_unref( (GIBaseInfo*) method_info);
    return true;
}

/* Set properties from args to constructor (argv[0] is supposed to be
 * a hash)
 * The GParameter elements in the passed-in vector must be unset by the caller,
 * regardless of the return value of this function.
 */
static bool
object_instance_props_to_g_parameters(JSContext                  *context,
                                      JSObject                   *obj,
                                      const JS::HandleValueArray& args,
                                      GType                       gtype,
                                      std::vector<GParameter>&    gparams)
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
        GjsAutoJSChar name;
        GParameter gparam = { NULL, { 0, }};

        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        prop_id = ids[ix];

        if (!gjs_object_require_property(context, props, "property list",
                                         prop_id, &value) ||
            !gjs_get_string_id(context, prop_id, &name))
            return false;

        switch (init_g_param_from_property(context, name,
                                           value,
                                           gtype,
                                           &gparam,
                                           true /* constructing */)) {
        case NO_SUCH_G_PROPERTY:
            gjs_throw(context, "No property %s on this GObject %s",
                      name.get(), g_type_name(gtype));
            /* fallthrough */
        case SOME_ERROR_OCCURRED:
            return false;
        case VALUE_WAS_SET:
        default:
            break;
        }

        gparams.push_back(gparam);
    }

    return true;
}

static GjsListLink *
object_instance_get_link(ObjectInstance *priv)
{
    return &priv->instance_link;
}

static void
object_instance_unlink(ObjectInstance *priv)
{
    if (wrapped_gobject_list == priv)
        wrapped_gobject_list = priv->instance_link.next();
    priv->instance_link.unlink();
}

static void
object_instance_link(ObjectInstance *priv)
{
    if (wrapped_gobject_list)
        priv->instance_link.prepend(priv, wrapped_gobject_list);
    wrapped_gobject_list = priv;
}

static void
wrapped_gobj_dispose_notify(gpointer      data,
                            GObject      *where_the_object_was)
{
    auto *priv = static_cast<ObjectInstance *>(data);

    priv->g_object_finalized = true;
    object_instance_unlink(priv);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Wrapped GObject %p disposed",
                        where_the_object_was);
}

void
gjs_object_context_dispose_notify(void    *data,
                                  GObject *where_the_object_was)
{
    ObjectInstance *priv = wrapped_gobject_list;
    while (priv) {
        ObjectInstance *next = priv->instance_link.next();

        if (priv->keep_alive.rooted()) {
            gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "GObject wrapper %p for GObject "
                                "%p (%s) was rooted but is now unrooted due to "
                                "GjsContext dispose", priv->keep_alive.get(),
                                priv->gobj, G_OBJECT_TYPE_NAME(priv->gobj));
            priv->keep_alive.reset();
            object_instance_unlink(priv);
        }

        priv = next;
    }
}

static void
handle_toggle_down(GObject *gobj)
{
    ObjectInstance *priv = get_object_qdata(gobj);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Toggle notify DOWN for GObject "
                        "%p (%s), JS obj %p", gobj, G_OBJECT_TYPE_NAME(gobj),
                        priv->keep_alive.get());

    /* Change to weak ref so the wrapper-wrappee pair can be
     * collected by the GC
     */
    if (priv->keep_alive.rooted()) {
        GjsContext *context;

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Unrooting object");
        priv->keep_alive.switch_to_unrooted();

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

static void
handle_toggle_up(GObject   *gobj)
{
    ObjectInstance *priv = get_object_qdata(gobj);

    /* We need to root the JSObject associated with the passed in GObject so it
     * doesn't get garbage collected (and lose any associated javascript state
     * such as custom properties).
     */
    if (!priv->keep_alive) /* Object already GC'd */
        return;

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Toggle notify UP for GObject "
                        "%p (%s), JS obj %p", gobj, G_OBJECT_TYPE_NAME(gobj),
                        priv->keep_alive.get());

    /* Change to strong ref so the wrappee keeps the wrapper alive
     * in case the wrapper has data in it that the app cares about
     */
    if (!priv->keep_alive.rooted()) {
        /* FIXME: thread the context through somehow. Maybe by looking up
         * the compartment that obj belongs to. */
        GjsContext *context = gjs_context_get_current();
        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Rooting object");
        auto cx = static_cast<JSContext *>(gjs_context_get_native_context(context));
        priv->keep_alive.switch_to_rooted(cx);
    }
}

static void
toggle_handler(GObject               *gobj,
               ToggleQueue::Direction direction)
{
    switch (direction) {
        case ToggleQueue::UP:
            handle_toggle_up(gobj);
            break;
        case ToggleQueue::DOWN:
            handle_toggle_down(gobj);
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

            handle_toggle_down(gobj);
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
            handle_toggle_up(gobj);
        } else {
            toggle_queue.enqueue(gobj, ToggleQueue::UP, toggle_handler);
        }
    }
}

static void
release_native_object (ObjectInstance *priv)
{
    priv->keep_alive.reset();
    if (priv->uses_toggle_ref)
        g_object_remove_toggle_ref(priv->gobj, wrapped_gobj_toggle_notify, nullptr);
    else
        g_object_unref(priv->gobj);
    priv->gobj = NULL;
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
    std::vector<ObjectInstance *> to_be_released;
    ObjectInstance *link = wrapped_gobject_list;
    while (link) {
        ObjectInstance *next = link->instance_link.next();
        if (link->keep_alive.rooted()) {
            to_be_released.push_back(link);
            object_instance_unlink(link);
        }

        link = next;
    }
    for (ObjectInstance *priv : to_be_released)
        release_native_object(priv);
}

static ObjectInstance *
init_object_private (JSContext       *context,
                     JS::HandleObject object)
{
    ObjectInstance *proto_priv;
    ObjectInstance *priv;

    JS_BeginRequest(context);

    priv = g_slice_new0(ObjectInstance);
    new (priv) ObjectInstance();

    GJS_INC_COUNTER(object);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    proto_priv = proto_priv_from_js(context, object);
    g_assert(proto_priv != NULL);

    priv->gtype = proto_priv->gtype;
    priv->info = proto_priv->info;
    if (priv->info)
        g_base_info_ref( (GIBaseInfo*) priv->info);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Instance constructor of %s, "
                        "JS obj %p, priv %p", g_type_name(priv->gtype),
                        object.get(), priv);

    JS_EndRequest(context);
    return priv;
}

static void
update_heap_wrapper_weak_pointers(JSContext     *cx,
                                  JSCompartment *compartment,
                                  gpointer       data)
{
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Weak pointer update callback, "
                        "%zu wrapped GObject(s) to examine",
                        wrapped_gobject_list ?
                            wrapped_gobject_list->instance_link.size() : 0);

    std::vector<GObject *> to_be_disassociated;
    ObjectInstance *priv = wrapped_gobject_list;

    while (priv) {
        ObjectInstance *next = priv->instance_link.next();

        if (!priv->keep_alive.rooted() &&
            priv->keep_alive != nullptr &&
            priv->keep_alive.update_after_gc()) {
            /* Ouch, the JS object is dead already. Disassociate the
             * GObject and hope the GObject dies too. (Remove it from
             * the weak pointer list first, since the disassociation
             * may also cause it to be erased.)
             */
            gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Found GObject weak pointer "
                                "whose JS object %p is about to be finalized: "
                                "%p (%s)", priv->keep_alive.get(), priv->gobj,
                                G_OBJECT_TYPE_NAME(priv->gobj));
            to_be_disassociated.push_back(priv->gobj);
            object_instance_unlink(priv);
        }

        priv = next;
    }

    for (GObject *gobj : to_be_disassociated)
        disassociate_js_gobject(gobj);
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

static void
associate_js_gobject (JSContext       *context,
                      JS::HandleObject object,
                      GObject         *gobj)
{
    ObjectInstance *priv;

    priv = priv_from_js(context, object);
    priv->uses_toggle_ref = false;
    priv->gobj = gobj;

    g_assert(!priv->keep_alive.rooted());

    set_object_qdata(gobj, priv);

    priv->keep_alive = object;
    ensure_weak_pointer_callback(context);
    object_instance_link(priv);

    g_object_weak_ref(gobj, wrapped_gobj_dispose_notify, priv);
}

static void
ensure_uses_toggle_ref(JSContext      *cx,
                       ObjectInstance *priv)
{
    if (priv->uses_toggle_ref)
        return;

    g_assert(!priv->keep_alive.rooted());

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
    priv->uses_toggle_ref = true;
    priv->keep_alive.switch_to_rooted(cx);
    g_object_add_toggle_ref(priv->gobj, wrapped_gobj_toggle_notify, nullptr);

    /* We now have both a ref and a toggle ref, we only want the toggle ref.
     * This may immediately remove the GC root we just added, since refcount
     * may drop to 1. */
    g_object_unref(priv->gobj);
}

static void
invalidate_all_closures(ObjectInstance *priv)
{
    /* Can't loop directly through the items, since invalidating an item's
     * closure might have the effect of removing the item from the set in the
     * invalidate notifier */
    while (!priv->closures.empty()) {
        /* This will also free cd, through the closure invalidation mechanism */
        GClosure *closure = g_closure_ref (*priv->closures.begin());
        g_closure_invalidate(closure);
        /* Erase element if not already erased */
        priv->closures.erase(closure);
    }
}

static void
disassociate_js_gobject(GObject *gobj)
{
    ObjectInstance *priv = get_object_qdata(gobj);
    bool had_toggle_down, had_toggle_up;

    if (!priv->g_object_finalized)
        g_object_weak_unref(gobj, wrapped_gobj_dispose_notify, priv);

    /* FIXME: this check fails when JS code runs after the main loop ends,
     * because the idle functions are not dispatched without a main loop.
     * The only situation I'm aware of where this happens is during the
     * dbus_unregister stage in GApplication. Ideally this check should be an
     * assertion.
     * https://bugzilla.gnome.org/show_bug.cgi?id=778862
     */
    auto& toggle_queue = ToggleQueue::get_default();
    std::tie(had_toggle_down, had_toggle_up) = toggle_queue.cancel(gobj);
    if (had_toggle_down != had_toggle_up) {
        g_critical("JS object wrapper for GObject %p (%s) is being released "
                   "while toggle references are still pending. This may happen "
                   "on exit in Gio.Application.vfunc_dbus_unregister(). If you "
                   "encounter it another situation, please report a GJS bug.",
                   gobj, G_OBJECT_TYPE_NAME(gobj));
    }

    /* Fist, remove the wrapper pointer from the wrapped GObject */
    set_object_qdata(gobj, nullptr);

    /* Now release all the resources the current wrapper has */
    invalidate_all_closures(priv);
    release_native_object(priv);

    /* Mark that a JS object once existed, but it doesn't any more */
    priv->js_object_finalized = true;
    priv->keep_alive = nullptr;
}

static void
clear_g_params(std::vector<GParameter>& params)
{
    for (GParameter param : params)
        g_value_unset(&param.value);
}

static bool
object_instance_init (JSContext                  *context,
                      JS::MutableHandleObject     object,
                      const JS::HandleValueArray& args)
{
    ObjectInstance *priv;
    GType gtype;
    std::vector<GParameter> params;
    GTypeQuery query;
    GObject *gobj;

    priv = (ObjectInstance *) JS_GetPrivate(object);

    gtype = priv->gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!object_instance_props_to_g_parameters(context, object, args,
                                               gtype, params)) {
        clear_g_params(params);
        return false;
    }

    /* Mark this object in the construction stack, it
       will be popped in gjs_object_custom_init() later
       down.
    */
    if (g_type_get_qdata(gtype, gjs_is_custom_type_quark())) {
        object_init_list.emplace(context, object);
    }

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gobj = (GObject*) g_object_newv(gtype, params.size(), params.data());
G_GNUC_END_IGNORE_DEPRECATIONS

    clear_g_params(params);

    ObjectInstance *other_priv = get_object_qdata(gobj);
    if (other_priv && other_priv->keep_alive != object.get()) {
        /* g_object_newv returned an object that's already tracked by a JS
         * object. Let's assume this is a singleton like IBus.IBus and return
         * the existing JS wrapper object.
         *
         * 'object' has a value that was originally created by
         * JS_NewObjectForConstructor in GJS_NATIVE_CONSTRUCTOR_PRELUDE, but
         * we're not actually using it, so just let it get collected. Avoiding
         * this would require a non-trivial amount of work.
         * */
        ensure_uses_toggle_ref(context, other_priv);
        object.set(other_priv->keep_alive);
        g_object_unref(gobj); /* We already own a reference */
        gobj = NULL;
        goto out;
    }

    g_type_query_dynamic_safe(gtype, &query);
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

    if (priv->gobj == NULL)
        associate_js_gobject(context, object, gobj);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "JSObject created with GObject %p (%s)",
                        priv->gobj, G_OBJECT_TYPE_NAME(priv->gobj));

    TRACE(GJS_OBJECT_PROXY_NEW(priv, priv->gobj,
                               priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "_gjs_private",
                               priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(gtype)));

 out:
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
    init_object_private(context, object);

    if (!gjs_object_require_property(context, object, "GObject instance",
                                     GJS_STRING_GOBJECT_INIT, &initer))
        return false;

    argv.rval().setUndefined();
    ret = gjs_call_function_value(context, object, initer, argv, argv.rval());

    if (argv.rval().isUndefined())
        argv.rval().setObject(*object);

    return ret;
}

static void
object_instance_trace(JSTracer *tracer,
                      JSObject *obj)
{
    ObjectInstance *priv;

    priv = (ObjectInstance *) JS_GetPrivate(obj);
    if (priv == NULL)
        return;

    for (GClosure *closure : priv->closures)
        gjs_closure_trace(closure, tracer);
}

static void
closure_invalidated(void     *data,
                    GClosure *closure)
{
    auto priv = static_cast<ObjectInstance *>(data);
    priv->closures.erase(closure);
}

static void
object_instance_finalize(JSFreeOp  *fop,
                         JSObject  *obj)
{
    ObjectInstance *priv;

    priv = (ObjectInstance *) JS_GetPrivate(obj);
    g_assert (priv != NULL);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "Finalizing %s, JS obj %p, priv %p, GObject %p",
                        g_type_name(priv->gtype), obj, priv, priv->gobj);

    TRACE(GJS_OBJECT_PROXY_FINALIZE(priv, priv->gobj,
                                    priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "_gjs_private",
                                    priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(priv->gtype)));

    /* This applies only to instances, not prototypes, but it's possible that
     * an instance's GObject is already freed at this point. */
    invalidate_all_closures(priv);

    /* Object is instance, not prototype, AND GObject is not already freed */
    if (priv->gobj) {
        bool had_toggle_up;
        bool had_toggle_down;

        if (G_UNLIKELY (priv->gobj->ref_count <= 0)) {
            g_error("Finalizing proxy for an already freed object of type: %s.%s\n",
                    priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "",
                    priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        }

        auto& toggle_queue = ToggleQueue::get_default();
        std::tie(had_toggle_down, had_toggle_up) = toggle_queue.cancel(priv->gobj);

        if (!had_toggle_up && had_toggle_down) {
            g_error("Finalizing proxy for an object that's scheduled to be unrooted: %s.%s\n",
                    priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "",
                    priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        }

        if (!priv->g_object_finalized)
            g_object_weak_unref(priv->gobj, wrapped_gobj_dispose_notify, priv);
        release_native_object(priv);
    }

    if (priv->keep_alive.rooted()) {
        /* This happens when the refcount on the object is still >1,
         * for example with global objects GDK never frees like GdkDisplay,
         * when we close down the JS runtime.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Wrapper was finalized despite being kept alive, has refcount >1");

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Unrooting object");

        priv->keep_alive.reset();
    }
    object_instance_unlink(priv);

    if (priv->info) {
        g_base_info_unref( (GIBaseInfo*) priv->info);
        priv->info = NULL;
    }

    if (priv->klass) {
        g_type_class_unref (priv->klass);
        priv->klass = NULL;
    }

    GJS_DEC_COUNTER(object);
    priv->~ObjectInstance();
    g_slice_free(ObjectInstance, priv);

    /* Remove the ObjectInstance pointer from the JSObject */
    JS_SetPrivate(obj, nullptr);
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

static void
do_associate_closure(ObjectInstance *priv,
                     GClosure       *closure)
{
    /* This is a weak reference, and will be cleared when the closure is
     * invalidated */
    priv->closures.insert(closure);
    g_closure_add_invalidate_notifier(closure, priv, closure_invalidated);
}

static bool
real_connect_func(JSContext *context,
                  unsigned   argc,
                  JS::Value *vp,
                  bool       after)
{
    GJS_GET_PRIV(context, argc, vp, argv, obj, ObjectInstance, priv);
    GClosure *closure;
    gulong id;
    guint signal_id;
    GQuark signal_detail;

    gjs_debug_gsignal("connect obj %p priv %p argc %d", obj.get(), priv, argc);
    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return false; /* wrong class passed in */
    }
    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't connect to signals on %s.%s.prototype; only on instances",
                  priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                  priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        return false;
    }
    if (priv->g_object_finalized) {
        g_critical("Object %s.%s (%p), has been already deallocated - impossible to connect to signal. "
                   "This might be caused by the fact that the object has been destroyed from C "
                   "code using something such as destroy(), dispose(), or remove() vfuncs",
                   priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                   priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype),
                   priv->gobj);
        gjs_dumpstack();
        return true;
    }

    ensure_uses_toggle_ref(context, priv);

    if (argc != 2 || !argv[0].isString() || !JS::IsCallable(&argv[1].toObject())) {
        gjs_throw(context, "connect() takes two args, the signal name and the callback");
        return false;
    }

    JS::RootedString signal_str(context, argv[0].toString());
    GjsAutoJSChar signal_name = JS_EncodeStringToUTF8(context, signal_str);
    if (!signal_name)
        return false;

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             true)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(),
                  g_type_name(G_OBJECT_TYPE(priv->gobj)));
        return false;
    }

    closure = gjs_closure_new_for_signal(context, &argv[1].toObject(), "signal callback", signal_id);
    if (closure == NULL)
        return false;
    do_associate_closure(priv, closure);

    id = g_signal_connect_closure_by_id(priv->gobj,
                                        signal_id,
                                        signal_detail,
                                        closure,
                                        after);

    argv.rval().setDouble(id);

    return true;
}

static bool
connect_after_func(JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    return real_connect_func(context, argc, vp, true);
}

static bool
connect_func(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    return real_connect_func(context, argc, vp, false);
}

static bool
emit_func(JSContext *context,
          unsigned   argc,
          JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, argv, obj, ObjectInstance, priv);
    guint signal_id;
    GQuark signal_detail;
    GSignalQuery signal_query;
    GValue *instance_and_args;
    GValue rvalue = G_VALUE_INIT;
    unsigned int i;
    bool failed;

    gjs_debug_gsignal("emit obj %p priv %p argc %d", obj.get(), priv, argc);

    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return false; /* wrong class passed in */
    }

    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't emit signal on %s.%s.prototype; only on instances",
                  priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                  priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        return false;
    }

    if (priv->g_object_finalized) {
        g_critical("Object %s.%s (%p), has been already deallocated - impossible to emit signal. "
                   "This might be caused by the fact that the object has been destroyed from C "
                   "code using something such as destroy(), dispose(), or remove() vfuncs",
                   priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                   priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype),
                   priv->gobj);
        gjs_dumpstack();
        return true;
    }

    if (argc < 1 || !argv[0].isString()) {
        gjs_throw(context, "emit() first arg is the signal name");
        return false;
    }

    JS::RootedString signal_str(context, argv[0].toString());
    GjsAutoJSChar signal_name = JS_EncodeStringToUTF8(context, signal_str);
    if (!signal_name)
        return false;

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             false)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                  signal_name.get(),
                  g_type_name(G_OBJECT_TYPE(priv->gobj)));
        return false;
    }

    g_signal_query(signal_id, &signal_query);

    if ((argc - 1) != signal_query.n_params) {
        gjs_throw(context, "Signal '%s' on %s requires %d args got %d",
                  signal_name.get(),
                  g_type_name(G_OBJECT_TYPE(priv->gobj)),
                  signal_query.n_params,
                  argc - 1);
        return false;
    }

    if (signal_query.return_type != G_TYPE_NONE) {
        g_value_init(&rvalue, signal_query.return_type & ~G_SIGNAL_TYPE_STATIC_SCOPE);
    }

    instance_and_args = g_newa(GValue, signal_query.n_params + 1);
    memset(instance_and_args, 0, sizeof(GValue) * (signal_query.n_params + 1));

    g_value_init(&instance_and_args[0], G_TYPE_FROM_INSTANCE(priv->gobj));
    g_value_set_instance(&instance_and_args[0], priv->gobj);

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

static bool
to_string_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, obj, ObjectInstance, priv);

    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return false;  /* wrong class passed in */
    }

    return _gjs_proxy_to_string_func(context, obj,
                                     (priv->g_object_finalized) ?
                                      "object (FINALIZED)" : "object",
                                     (GIBaseInfo*)priv->info, priv->gtype,
                                     priv->gobj, rec.rval());
}

static const struct JSClassOps gjs_object_class_ops = {
    NULL,  /* addProperty */
    NULL,  /* deleteProperty */
    object_instance_get_prop,
    object_instance_set_prop,
    NULL,  /* enumerate */
    object_instance_resolve,
    nullptr,  /* mayResolve */
    object_instance_finalize,
    NULL,
    NULL,
    NULL,
    object_instance_trace,
};

struct JSClass gjs_object_instance_class = {
    "GObject_Object",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &gjs_object_class_ops
};

static bool
init_func (JSContext *context,
           unsigned   argc,
           JS::Value *vp)
{
    GJS_GET_THIS(context, argc, vp, argv, obj);
    bool ret;

    if (!do_base_typecheck(context, obj, true))
        return false;

    ret = object_instance_init(context, &obj, argv);

    if (ret)
        argv.rval().setObject(*obj);

    return ret;
}

JSPropertySpec gjs_object_instance_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_object_instance_proto_funcs[] = {
    JS_FS("_init", init_func, 0, 0),
    JS_FS("connect", connect_func, 0, 0),
    JS_FS("connect_after", connect_after_func, 0, 0),
    JS_FS("emit", emit_func, 0, 0),
    JS_FS("toString", to_string_func, 0, 0),
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
    new (priv) ObjectInstance();
    priv->info = info;
    if (info)
        g_base_info_ref((GIBaseInfo*) info);
    priv->gtype = gtype;
    priv->klass = (GTypeClass*) g_type_class_ref (gtype);
    JS_SetPrivate(prototype, priv);

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

    ObjectInstance *priv = get_object_qdata(gobj);

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

        priv = init_object_private(context, obj);

        g_object_ref_sink(gobj);
        associate_js_gobject(context, obj, gobj);

        g_assert(priv->keep_alive == obj.get());
    }

    return priv->keep_alive;
}

GObject*
gjs_g_object_from_object(JSContext       *context,
                         JS::HandleObject obj)
{
    ObjectInstance *priv;

    if (!obj)
        return NULL;

    priv = priv_from_js(context, obj);

    if (priv->g_object_finalized) {
        g_critical("Object %s.%s (%p), has been already deallocated - "
                   "impossible to access it. This might be caused by the "
                   "object having been destroyed from C code using something "
                   "such as destroy(), dispose(), or remove() vfuncs",
                   priv->info ? g_base_info_get_namespace(priv->info) : "",
                   priv->info ? g_base_info_get_name(priv->info) : g_type_name(priv->gtype),
                   priv->gobj);
        gjs_dumpstack();
        return nullptr;
    }

    return priv->gobj;
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
    ObjectInstance *priv;
    bool result;

    if (!do_base_typecheck(context, object, throw_error))
        return false;

    priv = priv_from_js(context, object);

    if (priv == NULL) {
        if (throw_error) {
            gjs_throw(context,
                      "Object instance or prototype has not been properly initialized yet. "
                      "Did you forget to chain-up from _init()?");
        }

        return false;
    }

    if (priv->gobj == NULL) {
        if (throw_error) {
            gjs_throw(context,
                      "Object is %s.%s.prototype, not an object instance - cannot convert to GObject*",
                      priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                      priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        }

        return false;
    }

    g_assert(priv->g_object_finalized || priv->gtype == G_OBJECT_TYPE(priv->gobj));

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (priv->gtype, expected_type);
    else
        result = true;

    if (!result && throw_error) {
        if (priv->info) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_type_name(expected_type));
        } else {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s - cannot convert to %s",
                             g_type_name(priv->gtype),
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

static void
free_trampoline (GjsCallbackTrampoline *trampoline,
                 GClosure *closure)
{
    gjs_callback_trampoline_unref(trampoline);
    g_closure_unref (closure);
}

static bool
gjs_hook_up_vfunc(JSContext *cx,
                  unsigned   argc,
                  JS::Value *vp)
{
    JS::CallArgs argv = JS::CallArgsFromVp (argc, vp);
    GjsAutoJSChar name;
    JS::RootedObject object(cx), function(cx);
    ObjectInstance *priv;
    GType gtype, info_gtype;
    GIObjectInfo *info;
    GIVFuncInfo *vfunc;
    gpointer implementor_vtable;
    GIFieldInfo *field_info;

    if (!gjs_parse_call_args(cx, "hook_up_vfunc", argv, "oso",
                             "object", &object,
                             "name", &name,
                             "function", &function))
        return false;

    if (!do_base_typecheck(cx, object, true))
        return false;

    priv = priv_from_js(cx, object);
    gtype = priv->gtype;
    info = priv->info;

    /* find the first class that actually has repository information */
    info_gtype = gtype;
    while (!info && info_gtype != G_TYPE_OBJECT) {
        info_gtype = g_type_parent(info_gtype);

        info = g_irepository_find_by_gtype(g_irepository_get_default(), info_gtype);
    }

    /* If we don't have 'info', we don't have the base class (GObject).
     * This is awful, so abort now. */
    g_assert(info != NULL);

    argv.rval().setUndefined();

    vfunc = find_vfunc_on_parents(info, name, NULL);

    if (!vfunc) {
        guint i, n_interfaces;
        GType *interface_list;
        GIInterfaceInfo *interface;

        interface_list = g_type_interfaces(gtype, &n_interfaces);

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
        gjs_throw(cx, "Could not find definition of virtual function %s",
                  name.get());
        return false;
    }

    find_vfunc_info(cx, gtype, vfunc, name, &implementor_vtable, &field_info);
    if (field_info != NULL) {
        gint offset;
        gpointer method_ptr;
        GjsCallbackTrampoline *trampoline;

        offset = g_field_info_get_offset(field_info);
        method_ptr = G_STRUCT_MEMBER_P(implementor_vtable, offset);

        JS::RootedValue v_function(cx, JS::ObjectValue(*function));
        trampoline = gjs_callback_trampoline_new(cx, v_function, vfunc,
                                                 GI_SCOPE_TYPE_NOTIFIED,
                                                 object, true);
        g_closure_add_invalidate_notifier (trampoline->js_function, trampoline, (GClosureNotify) free_trampoline);

        *((ffi_closure **)method_ptr) = trampoline->closure;

        g_base_info_unref(field_info);
    }

    g_base_info_unref(vfunc);
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
    ObjectInstance *priv = get_object_qdata(object);

    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JS::RootedObject js_obj(context, priv->keep_alive);
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
    ObjectInstance *priv;

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

    priv = (ObjectInstance*) JS_GetPrivate(object);
    /* We only hold a toggle ref at this point, add back a ref that the
     * native code can own.
     */
    return G_OBJECT(g_object_ref(priv->gobj));
}

static void
gjs_object_set_gproperty (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    GjsContext *gjs_context;
    JSContext *context;
    ObjectInstance *priv = get_object_qdata(object);

    gjs_context = gjs_context_get_current();
    context = (JSContext*) gjs_context_get_native_context(gjs_context);

    JS::RootedObject js_obj(context, priv->keep_alive);
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
    priv = (ObjectInstance*) JS_GetPrivate(object);

    if (priv->gtype != G_TYPE_FROM_INSTANCE (instance)) {
        /* This is not the most derived instance_init function,
           do nothing.
         */
        return;
    }

    object_init_list.pop();

    associate_js_gobject(context, object, G_OBJECT (instance));

    /* Custom JS objects will most likely have visible state, so
     * just do this from the start */
    ensure_uses_toggle_ref(context, priv);

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
    auto priv = static_cast<ObjectInstance *>(g_type_get_qdata(G_OBJECT_CLASS_TYPE(klass),
                                              gjs_object_priv_quark()));

    if (priv) {
        for (GClosure *closure : priv->closures)
            g_closure_ref(closure);
    }
}

static void
gjs_object_base_finalize(void *klass)
{
    auto priv = static_cast<ObjectInstance *>(g_type_get_qdata(G_OBJECT_CLASS_TYPE(klass),
                                              gjs_object_priv_quark()));

    if (priv) {
        for (GClosure *closure : priv->closures)
            g_closure_unref(closure);
    }
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

    parent_priv = priv_from_js(cx, parent);

    /* We checked parent above, in do_base_typecheck() */
    g_assert(parent_priv != NULL);

    parent_type = parent_priv->gtype;

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

    if (priv->gobj)
        ensure_uses_toggle_ref(cx, priv);

    do_associate_closure(priv, closure);
    return true;
}
