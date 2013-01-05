/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <string.h>

#include <gjs/gi.h>
#include "object.h"
#include "gtype.h"
#include "arg.h"
#include "repo.h"
#include "function.h"
#include "proxyutils.h"
#include "param.h"
#include "value.h"
#include "keep-alive.h"
#include "closure.h"
#include "gjs_gi_trace.h"

#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include <gjs/type-module.h>

#include <util/log.h>
#include <girepository.h>

typedef struct {
    GIObjectInfo *info;
    GObject *gobj; /* NULL if we are the prototype and not an instance */
    JSObject *keep_alive; /* NULL if we are not added to it */
    GType gtype;

    /* a list of all signal connections, used when tracing */
    GList *signals;

    /* the GObjectClass wrapped by this JS Object (only used for
       prototypes) */
    GTypeClass *klass;
} ObjectInstance;

typedef struct {
    ObjectInstance *obj;
    GList *link;
    GClosure *closure;
} ConnectData;

enum {
    PROP_0,
    PROP_JS_CONTEXT,
    PROP_JS_OBJECT,
    PROP_JS_HANDLED,
};

static struct JSClass gjs_object_instance_class;

GJS_DEFINE_PRIV_FROM_JS(ObjectInstance, gjs_object_instance_class)

static JSObject*       peek_js_obj  (JSContext *context,
                                     GObject   *gobj);
static void            set_js_obj   (JSContext *context,
                                     GObject   *gobj,
                                     JSObject  *obj);

typedef enum {
    SOME_ERROR_OCCURRED = JS_FALSE,
    NO_SUCH_G_PROPERTY,
    VALUE_WAS_SET
} ValueFromPropertyResult;

static GQuark
gjs_context_quark(void)
{
    static GQuark val = 0;
    if (!val)
        val = g_quark_from_static_string ("gjs::context");

    return val;
}

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

static ValueFromPropertyResult
init_g_param_from_property(JSContext  *context,
                           const char *js_prop_name,
                           jsval       js_value,
                           GType       gtype,
                           GParameter *parameter,
                           gboolean    constructing)
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
    if (!gjs_value_to_g_value(context, js_value, &parameter->value)) {
        g_value_unset(&parameter->value);
        return SOME_ERROR_OCCURRED;
    }

    parameter->name = param_spec->name;

    return VALUE_WAS_SET;
}

static inline ObjectInstance *
proto_priv_from_js(JSContext *context,
                   JSObject  *obj)
{
    return priv_from_js(context, JS_GetPrototype(obj));
}

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
object_instance_get_prop(JSContext *context,
                         JSObject **obj,
                         jsid      *id,
                         jsval     *value_p)
{
    ObjectInstance *priv;
    char *name;
    char *gname;
    GParamSpec *param;
    GValue gvalue = { 0, };
    JSBool ret = JS_TRUE;

    if (!gjs_get_string_id(context, *id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, *obj);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Get prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL) {
        /* If we reach this point, either object_instance_new_resolve
         * did not throw (so name == "_init"), or the property actually
         * exists and it's not something we should be concerned with */
        goto out;
    }
    if (priv->gobj == NULL) /* prototype, not an instance. */
        goto out;

    gname = gjs_hyphen_from_camel(name);
    param = g_object_class_find_property(G_OBJECT_GET_CLASS(priv->gobj),
                                         gname);
    g_free(gname);

    if (param == NULL) {
        /* leave value_p as it was */
        goto out;
    }

    /* Do not fetch JS overridden properties from GObject, to avoid
     * infinite recursion. */
    if (g_param_spec_get_qdata(param, gjs_is_custom_property_quark()))
        goto out;

    if ((param->flags & G_PARAM_READABLE) == 0)
        goto out;

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Overriding %s with GObject prop %s",
                     name, param->name);

    g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(priv->gobj, param->name,
                          &gvalue);
    if (!gjs_value_from_g_value(context, value_p, &gvalue)) {
        g_value_unset(&gvalue);
        ret = JS_FALSE;
        goto out;
    }
    g_value_unset(&gvalue);

 out:
    g_free(name);
    return ret;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is JS_FALSE on OOM/exception.
 */
static JSBool
object_instance_set_prop(JSContext *context,
                         JSObject **obj,
                         jsid      *id,
                         JSBool     strict,
                         jsval     *value_p)
{
    ObjectInstance *priv;
    char *name;
    GParameter param = { NULL, { 0, }};
    JSBool ret = JS_TRUE;

    if (!gjs_get_string_id(context, *id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, *obj);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Set prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL) {
        /* see the comment in object_instance_get_prop() on this */
        goto out;
    }
    if (priv->gobj == NULL) /* prototype, not an instance. */
        goto out;

    switch (init_g_param_from_property(context, name,
                                       *value_p,
                                       G_TYPE_FROM_INSTANCE(priv->gobj),
                                       &param,
                                       FALSE /* constructing */)) {
    case SOME_ERROR_OCCURRED:
        ret = JS_FALSE;
    case NO_SUCH_G_PROPERTY:
        goto out;
    case VALUE_WAS_SET:
        break;
    }

    g_object_set_property(priv->gobj, param.name,
                          &param.value);

    g_value_unset(&param.value);

    /* note that the prop will also have been set in JS, which I think
     * is OK, since we hook get and set so will always override that
     * value. We could also use JS_DefineProperty though and specify a
     * getter/setter maybe, don't know if that is better.
     */

 out:
    g_free(name);
    return ret;
}

static gboolean
is_vfunc_unchanged(GIVFuncInfo *info,
                   GType        gtype)
{
    GType ptype = g_type_parent(gtype);
    GError *error = NULL;
    gpointer addr1, addr2;

    addr1 = g_vfunc_info_get_address(info, gtype, &error);
    if (error) {
        g_clear_error(&error);
        return FALSE;
    }

    addr2 = g_vfunc_info_get_address(info, ptype, &error);
    if (error) {
        g_clear_error(&error);
        return FALSE;
    }

    return addr1 == addr2;
}

static GIVFuncInfo *
find_vfunc_on_parent(GIObjectInfo *info,
                     gchar        *name)
{
    GIVFuncInfo *vfunc = NULL;
    GIObjectInfo *parent;

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
    }

    if (parent)
        g_base_info_unref(parent);

    return vfunc;
}

static JSBool
object_instance_new_resolve_no_info(JSContext       *context,
                                    JSObject        *obj,
                                    JSObject       **objp,
                                    ObjectInstance  *priv,
                                    char            *name)
{
    GIFunctionInfo *method_info;
    JSBool ret;
    GType *interfaces;
    guint n_interfaces;
    guint i;

    ret = JS_TRUE;
    interfaces = g_type_interfaces(priv->gtype, &n_interfaces);
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
            if (gjs_define_function(context, obj, priv->gtype,
                                    (GICallableInfo *)method_info)) {
                *objp = obj;
            } else {
                ret = JS_FALSE;
            }

            g_base_info_unref( (GIBaseInfo*) method_info);
        }
    }

    g_free(interfaces);
    return ret;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or object prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
object_instance_new_resolve(JSContext *context,
                            JSObject **obj,
                            jsid      *id,
                            unsigned   flags,
                            JSObject **objp)
{
    GIFunctionInfo *method_info;
    ObjectInstance *priv;
    char *name;
    JSBool ret = JS_FALSE;

    *objp = NULL;

    if (!gjs_get_string_id(context, *id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, *obj);

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Resolve prop '%s' hook obj %p priv %p (%s.%s) gobj %p %s",
                     name,
                     *obj,
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
        ret = JS_TRUE;
        goto out;
    }

    if (priv->gobj != NULL) {
        ret = JS_TRUE;
        goto out;
    }

    /* If we have no GIRepository information (we're a JS GObject subclass),
     * we need to look at exposing interfaces. Look up our interfaces through
     * GType data, and then hope that *those* are introspectable. */
    if (priv->info == NULL) {
        ret = object_instance_new_resolve_no_info(context, *obj, objp, priv, name);
        goto out;
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

        gchar *name_without_vfunc_ = &name[6];
        GIVFuncInfo *vfunc;

        vfunc = find_vfunc_on_parent(priv->info, name_without_vfunc_);
        if (vfunc != NULL) {
            /* In the event that the vfunc is unchanged, let regular
             * prototypal inheritance take over. */
            if (is_vfunc_unchanged(vfunc, priv->gtype)) {
                g_base_info_unref((GIBaseInfo *)vfunc);
                ret = JS_TRUE;
                goto out;
            }

            gjs_define_function(context, *obj, priv->gtype, vfunc);
            *objp = *obj;
            g_base_info_unref((GIBaseInfo *)vfunc);
            ret = JS_TRUE;
            goto out;
        }

        /* If the vfunc wasn't found, fall through, back to normal
         * method resolution. */
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
        ret = object_instance_new_resolve_no_info(context, *obj, objp,
                                                  priv, name);
        goto out;
    } else {
#if GJS_VERBOSE_ENABLE_GI_USAGE
        _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Defining method %s in prototype for %s (%s.%s)",
                  g_base_info_get_name( (GIBaseInfo*) method_info),
                  g_type_name(priv->gtype),
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));

        if (gjs_define_function(context, *obj, priv->gtype, method_info) == NULL) {
            g_base_info_unref( (GIBaseInfo*) method_info);
            goto out;
        }

        *objp = *obj; /* we defined the prop in obj */

        g_base_info_unref( (GIBaseInfo*) method_info);
    }

    ret = JS_TRUE;
 out:
    g_free(name);
    return ret;
}

static void
free_g_params(GParameter *params,
              int         n_params)
{
    int i;

    for (i = 0; i < n_params; ++i) {
        g_value_unset(&params[i].value);
    }
    g_free(params);
}

/* Set properties from args to constructor (argv[0] is supposed to be
 * a hash)
 */
static JSBool
object_instance_props_to_g_parameters(JSContext   *context,
                                      JSObject    *obj,
                                      unsigned     argc,
                                      jsval       *argv,
                                      GType        gtype,
                                      GParameter **gparams_p,
                                      int         *n_gparams_p)
{
    JSObject *props;
    JSObject *iter;
    jsid prop_id;
    GArray *gparams;

    if (gparams_p)
        *gparams_p = NULL;
    if (n_gparams_p)
        *n_gparams_p = 0;

    gparams = g_array_new(/* nul term */ FALSE, /* clear */ TRUE,
                          sizeof(GParameter));

    /* For custom types we register, we need to set additional
       properties for the JS context and JS object, so that we can retrieve
       them inside the constructor, when handling construct properties
       There is no other way to set those, as we need them before
       g_object_newv returns.
       We also need to ensure that these are the first properties set
       (luckily g_object_newv preserves the order)
    */
    if (g_type_get_qdata(gtype, gjs_is_custom_type_quark())) {
        GParameter gparam = { "js-context", { 0, } };

        g_value_init(&gparam.value, G_TYPE_POINTER);
        g_value_set_pointer(&gparam.value, context);

        g_array_append_val(gparams, gparam);

        gparam.name = "js-object";
        g_value_set_pointer(&gparam.value, obj);

        g_array_append_val(gparams, gparam);
    }

    if (argc == 0 || JSVAL_IS_VOID(argv[0]))
        goto out;

    if (!JSVAL_IS_OBJECT(argv[0])) {
        gjs_throw(context, "argument should be a hash with props to set");
        goto free_array_and_fail;
    }

    props = JSVAL_TO_OBJECT(argv[0]);

    iter = JS_NewPropertyIterator(context, props);
    if (iter == NULL) {
        gjs_throw(context, "Failed to create property iterator for object props hash");
        goto free_array_and_fail;
    }

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, iter, &prop_id))
        goto free_array_and_fail;

    while (!JSID_IS_VOID(prop_id)) {
        char *name;
        jsval value;
        GParameter gparam = { NULL, { 0, }};

        if (!gjs_get_string_id(context, prop_id, &name))
            goto free_array_and_fail;

        if (!gjs_object_require_property(context, props, "property list", name, &value)) {
            g_free(name);
            goto free_array_and_fail;
        }

        switch (init_g_param_from_property(context, name,
                                           value,
                                           gtype,
                                           &gparam,
                                           TRUE /* constructing */)) {
        case NO_SUCH_G_PROPERTY:
            gjs_throw(context, "No property %s on this GObject %s",
                         name, g_type_name(gtype));
        case SOME_ERROR_OCCURRED:
            g_free(name);
            goto free_array_and_fail;
        case VALUE_WAS_SET:
            break;
        }

        g_free(name);

        g_array_append_val(gparams, gparam);

        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, iter, &prop_id))
            goto free_array_and_fail;
    }

 out:
    if (n_gparams_p)
        *n_gparams_p = gparams->len;
    if (gparams_p)
        *gparams_p = (void*) g_array_free(gparams, FALSE);

    return JS_TRUE;

 free_array_and_fail:
    {
        GParameter *to_free;
        int count;
        count = gparams->len;
        to_free = (void*) g_array_free(gparams, FALSE);
        free_g_params(to_free, count);
    }
    return JS_FALSE;
}

#define DEBUG_DISPOSE 0
#if DEBUG_DISPOSE
static void
wrapped_gobj_dispose_notify(gpointer      data,
                            GObject      *where_the_object_was)
{
    gjs_debug(GJS_DEBUG_GOBJECT, "JSObject %p GObject %p disposed", data, where_the_object_was);
}
#endif

static void
gobj_no_longer_kept_alive_func(JSObject *obj,
                               void     *data)
{
    ObjectInstance *priv;

    priv = data;

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "GObject wrapper %p will no longer be kept alive, eligible for collection",
                        obj);

    priv->keep_alive = NULL;
}

static void
wrapped_gobj_toggle_notify(gpointer      data,
                           GObject      *gobj,
                           gboolean      is_last_ref)
{
    JSRuntime *runtime;
    JSContext *context;
    JSObject *obj;
    ObjectInstance *priv;

    runtime = data;

    /* During teardown, this can return NULL if runtime is being destroyed.
     * In that case we effectively already converted to a weak ref without
     * doing anything since the keep alive will be collected.
     * Or if !is_last_ref, we do not want to convert to a strong
     * ref since we want everything collected on runtime destroy.
     */
    context = gjs_runtime_get_context(runtime);
    if (!context)
        return;

    obj = peek_js_obj(context, gobj);

    g_assert(obj != NULL);

    priv = priv_from_js(context, obj);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "Toggle notify gobj %p obj %p is_last_ref %d keep-alive %p",
                        gobj, obj, is_last_ref, priv->keep_alive);

    if (is_last_ref) {
        /* Change to weak ref so the wrapper-wrappee pair can be
         * collected by the GC
         */
        if (priv->keep_alive != NULL) {
            gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Removing object from keep alive");
            gjs_keep_alive_remove_child(context, priv->keep_alive,
                                        gobj_no_longer_kept_alive_func,
                                        obj,
                                        priv);
            priv->keep_alive = NULL;
        }
    } else {
        /* Change to strong ref so the wrappee keeps the wrapper alive
         * in case the wrapper has data in it that the app cares about
         */
        if (priv->keep_alive == NULL) {
            gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "Adding object to keep alive");
            priv->keep_alive = gjs_keep_alive_get_for_import_global(context);
            gjs_keep_alive_add_child(context, priv->keep_alive,
                                     gobj_no_longer_kept_alive_func,
                                     obj,
                                     priv);
        }
    }
}

static ObjectInstance *
init_object_private (JSContext *context,
                     JSObject  *object)
{
    ObjectInstance *proto_priv;
    ObjectInstance *priv;

    JS_BeginRequest(context);

    priv = g_slice_new0(ObjectInstance);

    GJS_INC_COUNTER(object);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "obj instance constructor, obj %p priv %p", object, priv);

    proto_priv = proto_priv_from_js(context, object);
    g_assert(proto_priv != NULL);

    priv->gtype = proto_priv->gtype;
    priv->info = proto_priv->info;
    if (priv->info)
        g_base_info_ref( (GIBaseInfo*) priv->info);

    JS_EndRequest(context);
    return priv;
}

static void
associate_js_gobject (JSContext      *context,
                      JSObject       *object,
                      GObject        *gobj)
{
    ObjectInstance *priv;

    priv = priv_from_js(context, object);
    priv->gobj = gobj;

    g_assert(peek_js_obj(context, gobj) == NULL);
    set_js_obj(context, gobj, object);

#if DEBUG_DISPOSE
    g_object_weak_ref(gobj, wrapped_gobj_dispose_notify, object);
#endif

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
    priv->keep_alive = gjs_keep_alive_get_for_import_global(context);
    gjs_keep_alive_add_child(context,
                             priv->keep_alive,
                             gobj_no_longer_kept_alive_func,
                             object,
                             priv);

    g_object_add_toggle_ref(gobj,
                            wrapped_gobj_toggle_notify,
                            JS_GetRuntime(context));
}

static JSBool
object_instance_init (JSContext *context,
                      JSObject **object,
                      unsigned   argc,
                      jsval     *argv)
{
    ObjectInstance *priv;
    GType gtype;
    GParameter *params;
    int n_params;
    GTypeQuery query;
    JSObject *old_jsobj;
    GObject *gobj;

    priv = init_object_private(context, *object);

    gtype = priv->gtype;
    g_assert(gtype != G_TYPE_NONE);

    if (!object_instance_props_to_g_parameters(context, *object, argc, argv,
                                               gtype,
                                               &params, &n_params)) {
        return JS_FALSE;
    }

    gobj = g_object_newv(gtype, n_params, params);

    free_g_params(params, n_params);

    old_jsobj = peek_js_obj(context, gobj);
    if (old_jsobj != NULL && old_jsobj != *object) {
        /* g_object_newv returned an object that's already tracked by a JS
         * object. Let's assume this is a singleton like IBus.IBus and return
         * the existing JS wrapper object.
         *
         * 'object' has a value that was originally created by
         * JS_NewObjectForConstructor in GJS_NATIVE_CONSTRUCTOR_PRELUDE, but
         * we're not actually using it, so just let it get collected. Avoiding
         * this would require a non-trivial amount of work.
         * */
        *object = old_jsobj;
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
        associate_js_gobject(context, *object, gobj);
    /* We now have both a ref and a toggle ref, we only want the
     * toggle ref. This may immediately remove the GC root
     * we just added, since refcount may drop to 1.
     */
    g_object_unref(gobj);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "JSObject created with GObject %p %s",
                        priv->gobj, g_type_name_from_instance((GTypeInstance*) priv->gobj));

    TRACE(GJS_OBJECT_PROXY_NEW(priv, priv->gobj,
                               priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "_gjs_private",
                               priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(gtype)));

 out:
    return JS_TRUE;
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(object_instance)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(object_instance)
    JSBool ret;
    jsval initer;
    jsval rval;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(object_instance);

    if (!gjs_object_require_property(context, object, "GObject instance", "_init", &initer))
        return JS_FALSE;

    rval = JSVAL_VOID;
    ret = gjs_call_function_value(context, object, initer, argc, argv, &rval);

    if (JSVAL_IS_VOID(rval))
        rval = OBJECT_TO_JSVAL(object);

    JS_SET_RVAL(context, vp, rval);
    return ret;
}

static void
invalidate_all_signals(ObjectInstance *priv)
{
    GList *iter, *next;

    for (iter = priv->signals; iter; ) {
        ConnectData *cd = iter->data;
        next = iter->next;

        /* This will also free cd and iter, through
           the closure invalidation mechanism */
        g_closure_invalidate(cd->closure);

        iter = next;
    }
}

static void
object_instance_trace(JSTracer *tracer,
                      JSObject *obj)
{
    ObjectInstance *priv;
    GList *iter;

    priv = JS_GetPrivate(obj);

    for (iter = priv->signals; iter; iter = iter->next) {
        ConnectData *cd = iter->data;

        gjs_closure_trace(cd->closure, tracer);
    }
}

static void
object_instance_finalize(JSContext *context,
                         JSObject  *obj)
{
    ObjectInstance *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "finalize obj %p priv %p gtype %s gobj %p", obj, priv,
                        (priv && priv->gobj) ?
                        g_type_name_from_instance( (GTypeInstance*) priv->gobj) :
                        "<no gobject>",
                        priv ? priv->gobj : NULL);
    g_assert (priv != NULL);

    TRACE(GJS_OBJECT_PROXY_FINALIZE(priv, priv->gobj,
                                    priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "_gjs_private",
                                    priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(priv->gtype)));

    if (priv->gobj) {
        invalidate_all_signals (priv);

        if (G_UNLIKELY (priv->gobj->ref_count <= 0)) {
            g_error("Finalizing proxy for an already freed object of type: %s.%s\n",
                    priv->info ? g_base_info_get_namespace((GIBaseInfo*) priv->info) : "",
                    priv->info ? g_base_info_get_name((GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        }
        set_js_obj(context, priv->gobj, NULL);
        g_object_remove_toggle_ref(priv->gobj, wrapped_gobj_toggle_notify,
                                   JS_GetRuntime(context));
        priv->gobj = NULL;
    }

    if (priv->keep_alive != NULL) {
        /* This happens when the refcount on the object is still >1,
         * for example with global objects GDK never frees like GdkDisplay,
         * when we close down the JS runtime.
         */
        gjs_debug(GJS_DEBUG_GOBJECT,
                  "Wrapper was finalized despite being kept alive, has refcount >1");

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                            "Removing from keep alive");

        /* We're in a finalizer while the runtime is about to be
         * destroyed. This is not the safest time to be calling back
         * into jsapi, but we have to do this or the keep alive could
         * be finalized later and call gobj_no_longer_kept_alive_func.
         */
        gjs_keep_alive_remove_child(context, priv->keep_alive,
                                    gobj_no_longer_kept_alive_func,
                                    obj,
                                    priv);
    }

    if (priv->info) {
        g_base_info_unref( (GIBaseInfo*) priv->info);
        priv->info = NULL;
    }

    if (priv->klass) {
        g_type_class_unref (priv->klass);
        priv->klass = NULL;
    }

    GJS_DEC_COUNTER(object);
    g_slice_free(ObjectInstance, priv);
}

JSObject*
gjs_lookup_object_prototype(JSContext    *context,
                            GType         gtype)
{
    JSObject *proto;

    if (!gjs_define_object_class(context, NULL, gtype, NULL, &proto))
        return NULL;
    return proto;
}

static void
signal_connection_invalidated (gpointer  user_data,
                               GClosure *closure)
{
    ConnectData *connect_data = user_data;

    connect_data->obj->signals = g_list_delete_link(connect_data->obj->signals,
                                                    connect_data->link);
    g_slice_free(ConnectData, connect_data);
}

static JSBool
real_connect_func(JSContext *context,
                  unsigned   argc,
                  jsval     *vp,
                  gboolean  after)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    jsval *argv = JS_ARGV(context, vp);
    ObjectInstance *priv;
    GClosure *closure;
    gulong id;
    guint signal_id;
    char *signal_name;
    GQuark signal_detail;
    jsval retval;
    ConnectData *connect_data;
    JSBool ret = JS_FALSE;

    if (!do_base_typecheck(context, obj, JS_TRUE))
        return JS_FALSE;

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("connect obj %p priv %p argc %d", obj, priv, argc);
    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return JS_FALSE; /* wrong class passed in */
    }
    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't connect to signals on %s.%s.prototype; only on instances",
                  priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                  priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        return JS_FALSE;
    }

    /* Best I can tell, there is no way to know if argv[1] is really
     * callable other than to just try it. Checking whether it's a
     * function will not detect native objects that provide
     * JSClass::call, for example.
     */

    if (argc != 2 ||
        !JSVAL_IS_STRING(argv[0]) ||
        !JSVAL_IS_OBJECT(argv[1])) {
        gjs_throw(context, "connect() takes two args, the signal name and the callback");
        return JS_FALSE;
    }

    if (!gjs_string_to_utf8(context, argv[0], &signal_name)) {
        return JS_FALSE;
    }

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             TRUE)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)));
        goto out;
    }

    closure = gjs_closure_new_for_signal(context, JSVAL_TO_OBJECT(argv[1]), "signal callback", signal_id);
    if (closure == NULL)
        goto out;

    connect_data = g_slice_new(ConnectData);
    priv->signals = g_list_prepend(priv->signals, connect_data);
    connect_data->obj = priv;
    connect_data->link = priv->signals;
    /* This is a weak reference, and will be cleared when the closure is invalidated */
    connect_data->closure = closure;
    g_closure_add_invalidate_notifier(closure, connect_data, signal_connection_invalidated);

    id = g_signal_connect_closure_by_id(priv->gobj,
                                        signal_id,
                                        signal_detail,
                                        closure,
                                        after);

    if (!JS_NewNumberValue(context, id, &retval)) {
        g_signal_handler_disconnect(priv->gobj, id);
        goto out;
    }
    
    JS_SET_RVAL(context, vp, retval);

    ret = JS_TRUE;
 out:
    g_free(signal_name);
    return ret;
}

static JSBool
connect_after_func(JSContext *context,
                   unsigned   argc,
                   jsval     *vp)
{
    return real_connect_func(context, argc, vp, TRUE);
}

static JSBool
connect_func(JSContext *context,
             unsigned   argc,
             jsval     *vp)
{
    return real_connect_func(context, argc, vp, FALSE);
}

static JSBool
disconnect_func(JSContext *context,
                unsigned   argc,
                jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    ObjectInstance *priv;
    gulong id;

    if (!do_base_typecheck(context, obj, JS_TRUE))
        return JS_FALSE;

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("disconnect obj %p priv %p argc %d", obj, priv, argc);

    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return JS_FALSE; /* wrong class passed in */
    }

    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't disconnect signal on %s.%s.prototype; only on instances",
                  priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                  priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        return JS_FALSE;
    }

    if (argc != 1 ||
        !JSVAL_IS_INT(argv[0])) {
        gjs_throw(context, "disconnect() takes one arg, the signal handler id");
        return JS_FALSE;
    }

    id = JSVAL_TO_INT(argv[0]);

    g_signal_handler_disconnect(priv->gobj, id);
    
    JS_SET_RVAL(context, vp, JSVAL_VOID);

    return JS_TRUE;
}

static JSBool
emit_func(JSContext *context,
          unsigned   argc,
          jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    ObjectInstance *priv;
    guint signal_id;
    GQuark signal_detail;
    GSignalQuery signal_query;
    char *signal_name;
    GValue *instance_and_args;
    GValue rvalue = G_VALUE_INIT;
    unsigned int i;
    gboolean failed;
    jsval retval;
    JSBool ret = JS_FALSE;

    if (!do_base_typecheck(context, obj, JS_TRUE))
        return JS_FALSE;

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("emit obj %p priv %p argc %d", obj, priv, argc);

    if (priv == NULL) {
        throw_priv_is_null_error(context);
        return JS_FALSE; /* wrong class passed in */
    }

    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't emit signal on %s.%s.prototype; only on instances",
                  priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                  priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        return JS_FALSE;
    }

    if (argc < 1 ||
        !JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context, "emit() first arg is the signal name");
        return JS_FALSE;
    }

    if (!gjs_string_to_utf8(context, argv[0], &signal_name))
        return JS_FALSE;

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             FALSE)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)));
        goto out;
    }

    g_signal_query(signal_id, &signal_query);

    if ((argc - 1) != signal_query.n_params) {
        gjs_throw(context, "Signal '%s' on %s requires %d args got %d",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)),
                     signal_query.n_params,
                     argc - 1);
        goto out;
    }

    if (signal_query.return_type != G_TYPE_NONE) {
        g_value_init(&rvalue, signal_query.return_type & ~G_SIGNAL_TYPE_STATIC_SCOPE);
    }

    instance_and_args = g_newa(GValue, signal_query.n_params + 1);
    memset(instance_and_args, 0, sizeof(GValue) * (signal_query.n_params + 1));

    g_value_init(&instance_and_args[0], G_TYPE_FROM_INSTANCE(priv->gobj));
    g_value_set_instance(&instance_and_args[0], priv->gobj);

    failed = FALSE;
    for (i = 0; i < signal_query.n_params; ++i) {
        GValue *value;
        value = &instance_and_args[i + 1];

        g_value_init(value, signal_query.param_types[i] & ~G_SIGNAL_TYPE_STATIC_SCOPE);
        if ((signal_query.param_types[i] & G_SIGNAL_TYPE_STATIC_SCOPE) != 0)
            failed = !gjs_value_to_g_value_no_copy(context, argv[i+1], value);
        else
            failed = !gjs_value_to_g_value(context, argv[i+1], value);

        if (failed)
            break;
    }

    if (!failed) {
        g_signal_emitv(instance_and_args, signal_id, signal_detail,
                       &rvalue);
    }

    if (signal_query.return_type != G_TYPE_NONE) {
        if (!gjs_value_from_g_value(context,
                                    &retval,
                                    &rvalue))
            failed = TRUE;

        g_value_unset(&rvalue);
    } else {
        retval = JSVAL_VOID;
    }

    for (i = 0; i < (signal_query.n_params + 1); ++i) {
        g_value_unset(&instance_and_args[i]);
    }

    if (!failed)
        JS_SET_RVAL(context, vp, retval);

    ret = !failed;
 out:
    g_free(signal_name);
    return ret;
}

static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    ObjectInstance *priv;
    JSBool ret = JS_FALSE;
    jsval retval;

    if (!do_base_typecheck(context, obj, JS_TRUE))
        goto out;

    priv = priv_from_js(context, obj);

    if (priv == NULL) {
        throw_priv_is_null_error(context);
        goto out;  /* wrong class passed in */
    }
    
    if (!_gjs_proxy_to_string_func(context, obj, "object", (GIBaseInfo*)priv->info,
                                   priv->gtype, priv->gobj, &retval))
        goto out;

    ret = JS_TRUE;
    JS_SET_RVAL(context, vp, retval);
 out:
    return ret;
}

static struct JSClass gjs_object_instance_class = {
    "GObject_Object",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_MARK_IS_TRACE,
    JS_PropertyStub,
    JS_PropertyStub,
    object_instance_get_prop,
    object_instance_set_prop,
    JS_EnumerateStub,
    (JSResolveOp) object_instance_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    object_instance_finalize,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    JS_CLASS_TRACE(object_instance_trace),
    NULL,
};

static JSBool
init_func (JSContext *context,
           unsigned   argc,
           jsval     *vp)
{
    jsval *argv = JS_ARGV(context, vp);
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    JSBool ret;

    if (!do_base_typecheck(context, obj, TRUE))
        return FALSE;

    ret = object_instance_init(context, &obj, argc, argv);

    if (ret)
        JS_SET_RVAL(context, vp, OBJECT_TO_JSVAL(obj));

    return ret;
}

static JSPropertySpec gjs_object_instance_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_object_instance_proto_funcs[] = {
    { "_init", (JSNative)init_func, 0, 0 },
    { "connect", (JSNative)connect_func, 0, 0 },
    { "connect_after", (JSNative)connect_after_func, 0, 0 },
    { "disconnect", (JSNative)disconnect_func, 0, 0 },
    { "emit", (JSNative)emit_func, 0, 0 },
    { "toString", (JSNative)to_string_func, 0, 0 },
    { NULL }
};

static JSBool
gjs_define_static_methods(JSContext    *context,
                          JSObject     *constructor,
                          GType         gtype,
                          GIObjectInfo *object_info)
{
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
            gjs_define_function(context, constructor, gtype,
                                (GICallableInfo *)meth_info);
        }

        g_base_info_unref((GIBaseInfo*) meth_info);
    }
    return JS_TRUE;
}

JSBool
gjs_define_object_class(JSContext     *context,
                        JSObject      *in_object,
                        GType          gtype,
                        JSObject     **constructor_p,
                        JSObject     **prototype_p)
{
    const char *constructor_name;
    JSObject *prototype;
    JSObject *constructor;
    JSObject *parent_proto;
    jsval value;
    ObjectInstance *priv;
    GIObjectInfo *info = NULL;
    const char *ns;

    g_assert(gtype != G_TYPE_INVALID);

    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), gtype);

    if (!in_object) {
        if (info)
            in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);
        else
            in_object = gjs_lookup_private_namespace(context);

        if (!in_object) {
            if (info)
                g_base_info_unref((GIBaseInfo*)info);
            return FALSE;
        }
    }
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

    /* 'gtype' is the GType of a concrete class (if any) which may or may not
     * be defined in the GIRepository. 'info' corresponds to the first known
     * ancestor of 'gtype' (or the gtype itself.)
     *
     * For example:
     * gtype=GtkWindow  info=Gtk.Window     (defined)
     * gtype=GLocalFile info=GLib.Object    (not defined)
     * gtype=GHalMount  info=GLib.Object    (not defined)
     *
     * Each GType needs to have distinct JS class, otherwise the JS class for
     * first common parent in GIRepository gets used with conflicting gtypes
     * when resolving GTypeInterface methods.
     *
     * In case 'gtype' is not defined in GIRepository use the type name as
     * constructor assuming it is unique enough instead of sharing
     * 'Object' (or whatever the first known ancestor is)
     *
     */
    if (!info) {
        constructor_name = g_type_name(gtype);
    } else {
        constructor_name = g_base_info_get_name((GIBaseInfo*) info);
    }

    if (gjs_object_get_property(context, in_object, constructor_name, &value)) {

        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Existing property '%s' does not look like a constructor",
                         constructor_name);
            g_base_info_unref((GIBaseInfo*)info);
            return FALSE;
        }

        constructor = JSVAL_TO_OBJECT(value);

        gjs_object_get_property(context, constructor, "prototype", &value);
        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "prototype property does not appear to exist or has wrong type");
            g_base_info_unref((GIBaseInfo*)info);
            return FALSE;
        } else {
            if (prototype_p)
                *prototype_p = JSVAL_TO_OBJECT(value);
            if (constructor_p)
                *constructor_p = constructor;

            if (info)
                g_base_info_unref((GIBaseInfo*)info);
            return TRUE;
        }
    }

    parent_proto = NULL;
    if (g_type_parent(gtype) != G_TYPE_INVALID) {
       GType parent_gtype;

       parent_gtype = g_type_parent(gtype);
       parent_proto = gjs_lookup_object_prototype(context, parent_gtype);
    }

    /* This is only used to disambiguate classes in the import global.
     * We can safely set "unknown" if there is no info, as in that case
     * the name is globally unique (it's a GType name). */
    if (info)
        ns = g_base_info_get_namespace((GIBaseInfo*) info);
    else
        ns = "unknown";
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
                                &prototype,
                                &constructor)) {
        gjs_fatal("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(object);
    priv = g_slice_new0(ObjectInstance);
    priv->info = info;
    if (info)
        g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = gtype;
    priv->klass = g_type_class_ref (gtype);
    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GOBJECT, "Defined class %s prototype %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

    if (info)
        gjs_define_static_methods(context, constructor, gtype, info);

    value = OBJECT_TO_JSVAL(gjs_gtype_create_gtype_wrapper(context, gtype));
    JS_DefineProperty(context, constructor, "$gtype", value,
                      NULL, NULL, JSPROP_PERMANENT);

    if (prototype_p)
        *prototype_p = prototype;

    if (constructor_p)
        *constructor_p = constructor;

    if (info)
        g_base_info_unref((GIBaseInfo*)info);
    return TRUE;
}

static JSObject*
peek_js_obj(JSContext *context,
            GObject   *gobj)
{
    return g_object_get_qdata(gobj, gjs_object_priv_quark());
}

static void
set_js_obj(JSContext *context,
           GObject   *gobj,
           JSObject  *obj)
{
    g_object_set_qdata(gobj, gjs_object_priv_quark(), obj);
}

JSObject*
gjs_object_from_g_object(JSContext    *context,
                         GObject      *gobj)
{
    JSObject *obj;

    if (gobj == NULL)
        return NULL;

    obj = peek_js_obj(context, gobj);

    if (obj == NULL) {
        /* We have to create a wrapper */
        JSObject *proto;

        gjs_debug_marshal(GJS_DEBUG_GOBJECT,
                          "Wrapping %s with JSObject",
                          g_type_name_from_instance((GTypeInstance*) gobj));


        if (!gjs_define_object_class(context, NULL, G_TYPE_FROM_INSTANCE(gobj), NULL, &proto))
            return NULL;

        JS_BeginRequest(context);

        obj = JS_NewObjectWithGivenProto(context,
                                         JS_GetClass(proto), proto,
                                         gjs_get_import_global (context));

        JS_EndRequest(context);

        if (obj == NULL)
            goto out;

        init_object_private(context, obj);

        g_object_ref_sink(gobj);
        associate_js_gobject(context, obj, gobj);

        /* see the comment in init_object_instance() for this */
        g_object_unref(gobj);

        g_assert(peek_js_obj(context, gobj) == obj);
    }

 out:
    return obj;
}

GObject*
gjs_g_object_from_object(JSContext    *context,
                         JSObject     *obj)
{
    ObjectInstance *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);
    return priv->gobj;
}

JSBool
gjs_typecheck_object(JSContext     *context,
                     JSObject      *object,
                     GType          expected_type,
                     JSBool         throw)
{
    ObjectInstance *priv;
    JSBool result;

    if (!do_base_typecheck(context, object, throw))
        return JS_FALSE;

    priv = priv_from_js(context, object);

    if (priv == NULL) {
        if (throw) {
            gjs_throw(context,
                      "Object instance or prototype has not been properly initialized yet. "
                      "Did you forget to chain-up from _init()?");
        }

        return JS_FALSE;
    }

    if (priv->gobj == NULL) {
        if (throw) {
            gjs_throw(context,
                      "Object is %s.%s.prototype, not an object instance - cannot convert to GObject*",
                      priv->info ? g_base_info_get_namespace( (GIBaseInfo*) priv->info) : "",
                      priv->info ? g_base_info_get_name( (GIBaseInfo*) priv->info) : g_type_name(priv->gtype));
        }

        return JS_FALSE;
    }

    g_assert(priv->gtype == G_OBJECT_TYPE(priv->gobj));

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (priv->gtype, expected_type);
    else
        result = JS_TRUE;

    if (!result && throw) {
        if (priv->info) {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_type_name(expected_type));
        } else {
            gjs_throw_custom(context, "TypeError",
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
                 gchar *vfunc_name,
                 gpointer *implementor_vtable_ret,
                 GIFieldInfo **field_info_ret)
{
    GType ancestor_gtype;
    int length, i;
    GIBaseInfo *ancestor_info;
    GIStructInfo *struct_info;
    gpointer implementor_class;
    gboolean is_interface;

    *field_info_ret = NULL;
    *implementor_vtable_ret = NULL;

    ancestor_info = g_base_info_get_container(vfunc_info);
    ancestor_gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)ancestor_info);

    is_interface = g_base_info_get_type(ancestor_info) == GI_INFO_TYPE_INTERFACE;

    implementor_class = g_type_class_ref(implementor_gtype);
    if (is_interface) {
        GTypeInstance *implementor_iface_class;
        implementor_iface_class = g_type_interface_peek(implementor_class,
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

static JSBool
gjs_hook_up_vfunc(JSContext *cx,
                  unsigned   argc,
                  jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    gchar *name;
    JSObject *object;
    JSObject *function;
    ObjectInstance *priv;
    GType gtype, info_gtype;
    GIObjectInfo *info;
    GIVFuncInfo *vfunc;
    gpointer implementor_vtable;
    GIFieldInfo *field_info;

    if (!gjs_parse_args(cx, "hook_up_vfunc",
                        "oso", argc, argv,
                        "object", &object,
                        "name", &name,
                        "function", &function))
        return JS_FALSE;

    if (!do_base_typecheck(cx, object, JS_TRUE))
        return JS_FALSE;

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

    JS_SET_RVAL(cx, vp, JSVAL_VOID);

    vfunc = find_vfunc_on_parent(info, name);

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
            if (interface)
                vfunc = g_interface_info_find_vfunc(interface, name);

            g_base_info_unref((GIBaseInfo*)interface);
            if (vfunc)
                break;
        }

        g_free(interface_list);
    }

    if (!vfunc) {
        gjs_throw(cx, "Could not find definition of virtual function %s", name);

        g_free(name);
        return JS_FALSE;
    }

    find_vfunc_info(cx, gtype, vfunc, name, &implementor_vtable, &field_info);
    if (field_info != NULL) {
        GITypeInfo *type_info;
        GIBaseInfo *interface_info;
        GICallbackInfo *callback_info;
        gint offset;
        gpointer method_ptr;
        GjsCallbackTrampoline *trampoline;

        type_info = g_field_info_get_type(field_info);

        interface_info = g_type_info_get_interface(type_info);

        callback_info = (GICallbackInfo*)interface_info;
        offset = g_field_info_get_offset(field_info);
        method_ptr = G_STRUCT_MEMBER_P(implementor_vtable, offset);

        trampoline = gjs_callback_trampoline_new(cx, OBJECT_TO_JSVAL(function), callback_info,
                                                 GI_SCOPE_TYPE_NOTIFIED, TRUE);

        *((ffi_closure **)method_ptr) = trampoline->closure;

        g_base_info_unref(interface_info);
        g_base_info_unref(type_info);
        g_base_info_unref(field_info);
    }

    g_base_info_unref(vfunc);
    g_free(name);
    return JS_TRUE;
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
    JSContext *context;
    JSObject *js_obj;
    jsval jsvalue;
    gchar *underscore_name;

    if (property_id != PROP_JS_HANDLED) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        return;
    }

    context = g_object_get_qdata(object, gjs_context_quark());
    js_obj = peek_js_obj(context, object);

    underscore_name = hyphen_to_underscore((gchar *)pspec->name);
    JS_GetProperty(context, js_obj, underscore_name, &jsvalue);
    g_free (underscore_name);

    if (!gjs_value_to_g_value(context, jsvalue, value))
        return;
}

static void
gjs_object_set_gproperty (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    JSContext *context;
    JSObject *js_obj;
    jsval jsvalue;
    gchar *underscore_name;

    if (property_id == PROP_JS_CONTEXT) {
        context = g_value_get_pointer (value);
        g_object_set_qdata(object, gjs_context_quark(), context);
        return;
    }

    context = g_object_get_qdata(object, gjs_context_quark());

    if (property_id == PROP_JS_OBJECT) {
        js_obj = g_value_get_pointer (value);
        associate_js_gobject(context, js_obj, object);
        return;
    }

    if (property_id != PROP_JS_HANDLED) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        return;
    }

    js_obj = peek_js_obj(context, object);

    if (!gjs_value_from_g_value(context, &jsvalue, value))
        return;

    underscore_name = hyphen_to_underscore((gchar *)pspec->name);
    JS_SetProperty(context, js_obj, underscore_name, &jsvalue);
    g_free (underscore_name);
}

static void
gjs_object_class_init(GObjectClass *class,
                      gpointer      user_data)
{
    class->set_property = gjs_object_set_gproperty;
    class->get_property = gjs_object_get_gproperty;

    g_object_class_install_property (class, PROP_JS_CONTEXT,
                                     g_param_spec_pointer ("js-context",
                                                           "JSContext",
                                                           "The JSContext this object was created for",
                                                           G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (class, PROP_JS_OBJECT,
                                     g_param_spec_pointer ("js-object",
                                                           "JSObject",
                                                           "The JSObject wrapping this GObject",
                                                           G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
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

static JSBool
gjs_register_type(JSContext *cx,
                  unsigned   argc,
                  jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    gchar *name;
    JSObject *parent, *constructor, *interfaces;
    GType instance_type, parent_type;
    GTypeQuery query;
    GTypeModule *type_module;
    ObjectInstance *parent_priv;
    GTypeInfo type_info = {
        0, /* class_size */

	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,

	(GClassInitFunc) gjs_object_class_init,
	(GClassFinalizeFunc) NULL,
	NULL, /* class_data */

	0,    /* instance_size */
	0,    /* n_preallocs */
	(GInstanceInitFunc) NULL,
    };
    guint32 i, n_interfaces;
    GType *iface_types;

    JS_BeginRequest(cx);

    if (!gjs_parse_args(cx, "register_type",
                        "oso", argc, argv,
                        "parent", &parent,
                        "name", &name,
                        "interfaces", &interfaces))
        return JS_FALSE;

    if (!parent)
        return JS_FALSE;

    if (!do_base_typecheck(cx, parent, JS_TRUE))
        return JS_FALSE;

    if (!JS_IsArrayObject(cx, interfaces)) {
        gjs_throw(cx, "Invalid parameter interfaces (expected Array)");
        return JS_FALSE;
    }

    if (!JS_GetArrayLength(cx, interfaces, &n_interfaces))
        return JS_FALSE;

    iface_types = g_alloca(sizeof(GType) * n_interfaces);

    /* We do interface addition in two passes so that any failure
       is caught early, before registering the GType (which we can't undo) */
    for (i = 0; i < n_interfaces; i++) {
        jsval iface_val;
        GType iface_type;

        if (!JS_GetElement(cx, interfaces, i, &iface_val))
            return JS_FALSE;

        if (!JSVAL_IS_OBJECT(iface_val) ||
            ((iface_type = gjs_gtype_get_actual_gtype(cx, JSVAL_TO_OBJECT(iface_val)))
             == G_TYPE_INVALID)) {
            gjs_throw(cx, "Invalid parameter interfaces (element %d was not a GType)", i);
            return JS_FALSE;
        }

        iface_types[i] = iface_type;
    }

    if (g_type_from_name(name) != G_TYPE_INVALID) {
        gjs_throw (cx, "Type name %s is already registered", name);
        return JS_FALSE;
    }

    parent_priv = priv_from_js(cx, parent);

    /* We checked parent above, in do_base_typecheck() */
    g_assert(parent_priv != NULL);

    parent_type = parent_priv->gtype;

    g_type_query_dynamic_safe(parent_type, &query);
    if (G_UNLIKELY (query.type == 0)) {
        gjs_throw (cx, "Cannot inherit from a non-gjs dynamic type [bug 687184]");
        return JS_FALSE;
    }

    type_info.class_size = query.class_size;
    type_info.instance_size = query.instance_size;

    type_module = G_TYPE_MODULE (gjs_type_module_get());
    instance_type = g_type_module_register_type(type_module,
                                                parent_type,
                                                name,
                                                &type_info,
                                                0);

    g_free(name);

    g_type_set_qdata (instance_type, gjs_is_custom_type_quark(), GINT_TO_POINTER (1));

    for (i = 0; i < n_interfaces; i++)
        gjs_add_interface(instance_type, iface_types[i]);

    /* create a custom JSClass */
    if (!gjs_define_object_class(cx, NULL, instance_type, &constructor, NULL))
        return JS_FALSE;

    JS_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(constructor));

    JS_EndRequest(cx);

    return JS_TRUE;
}

static JSBool
gjs_register_property(JSContext *cx,
                      unsigned   argc,
                      jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    JSObject *obj;
    JSObject *pspec_js;
    GParamSpec *pspec;
    ObjectInstance *priv;

    if (argc != 2)
        return JS_FALSE;

    if (!JSVAL_IS_OBJECT(argv[0]) ||
        !JSVAL_IS_OBJECT(argv[1]))
        return JS_FALSE;

    obj = JSVAL_TO_OBJECT(argv[0]);
    pspec_js = JSVAL_TO_OBJECT(argv[1]);

    if (!do_base_typecheck(cx, obj, JS_TRUE))
        return JS_FALSE;
    if (!gjs_typecheck_param(cx, pspec_js, G_TYPE_NONE, JS_TRUE))
        return JS_FALSE;

    priv = priv_from_js(cx, obj);
    pspec = gjs_g_param_from_param(cx, pspec_js);

    g_param_spec_set_qdata(pspec, gjs_is_custom_property_quark(), GINT_TO_POINTER(1));

    g_object_class_install_property(G_OBJECT_CLASS (priv->klass), PROP_JS_HANDLED, pspec);

    JS_SET_RVAL(cx, vp, JSVAL_VOID);
    return JS_TRUE;
}

static JSBool
gjs_signal_new(JSContext *cx,
               unsigned   argc,
               jsval     *vp)
{
    jsval *argv = JS_ARGV(cx, vp);
    JSObject *obj;
    ObjectInstance *priv;
    gchar *signal_name = NULL;
    GSignalAccumulator accumulator;
    gint signal_id;
    guint i, n_parameters;
    GType *params;
    JSBool ret;

    if (argc != 6)
        return JS_FALSE;

    JS_BeginRequest(cx);

    if (!gjs_string_to_utf8(cx, argv[1], &signal_name)) {
        ret = JS_FALSE;
        goto out;
    }

    obj = JSVAL_TO_OBJECT(argv[0]);
    if (!do_base_typecheck(cx, obj, JS_TRUE)) {
        ret = JS_FALSE;
        goto out;
    }

    priv = priv_from_js(cx, obj);

    /* we only support standard accumulators for now */
    switch (JSVAL_TO_INT(argv[3])) {
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

    if (accumulator == g_signal_accumulator_true_handled &&
        JSVAL_TO_INT(argv[4]) != G_TYPE_BOOLEAN) {
        gjs_throw (cx, "GObject.SignalAccumulator.TRUE_HANDLED can only be used with boolean signals");
        ret = JS_FALSE;
        goto out;
    }

    if (!JS_GetArrayLength(cx, JSVAL_TO_OBJECT(argv[5]), &n_parameters)) {
        ret = JS_FALSE;
        goto out;
    }
    params = g_newa(GType, n_parameters);
    for (i = 0; i < n_parameters; i++) {
        jsval gtype_val;
        if (!JS_GetElement(cx, JSVAL_TO_OBJECT(argv[5]), i, &gtype_val) ||
            !JSVAL_IS_OBJECT(gtype_val)) {
            gjs_throw(cx, "Invalid signal parameter number %d", i);
            ret = JS_FALSE;
            goto out;
        }

        params[i] = gjs_gtype_get_actual_gtype(cx, JSVAL_TO_OBJECT(gtype_val));
    }

    signal_id = g_signal_newv(signal_name,
                              priv->gtype,
                              JSVAL_TO_INT(argv[2]), /* signal_flags */
                              NULL, /* class closure */
                              accumulator,
                              NULL, /* accu_data */
                              g_cclosure_marshal_generic,
                              gjs_gtype_get_actual_gtype(cx, JSVAL_TO_OBJECT(argv[4])), /* return type */
                              n_parameters,
                              params);

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(signal_id));
    ret = JS_TRUE;

 out:
    JS_EndRequest(cx);

    free (signal_name);
    return ret;
}

JSBool
gjs_define_private_gi_stuff(JSContext *context,
                            JSObject  *module_obj)
{
    if (!JS_DefineFunction(context, module_obj,
                           "register_type",
                           (JSNative)gjs_register_type,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "add_interface",
                           (JSNative)gjs_add_interface,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "hook_up_vfunc",
                           (JSNative)gjs_hook_up_vfunc,
                           3, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "register_property",
                           (JSNative)gjs_register_property,
                           2, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    if (!JS_DefineFunction(context, module_obj,
                           "signal_new",
                           (JSNative)gjs_signal_new,
                           6, GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}
