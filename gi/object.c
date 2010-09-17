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

#include "object.h"
#include "arg.h"
#include "repo.h"
#include "function.h"
#include "value.h"
#include "keep-alive.h"
#include "gjs_gi_trace.h"

#include <gjs/gjs.h>
#include <gjs/compat.h>

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>

typedef struct {
    GIObjectInfo *info;
    GObject *gobj; /* NULL if we are the prototype and not an instance */
    JSObject *keep_alive; /* NULL if we are not added to it */
    GType gtype;
} ObjectInstance;

static ObjectInstance unthreadsafe_template_for_constructor = { NULL, NULL };

static struct JSClass gjs_object_instance_class;

GJS_DEFINE_DYNAMIC_PRIV_FROM_JS(ObjectInstance, gjs_object_instance_class)

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

static ValueFromPropertyResult
init_g_param_from_property(JSContext  *context,
                           const char *js_prop_name,
                           jsval       js_value,
                           GType       gtype,
                           GParameter *parameter)
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

/* a hook on getting a property; set value_p to override property's value.
 * Return value is JS_FALSE on OOM/exception.
 */
static JSBool
object_instance_get_prop(JSContext *context,
                         JSObject  *obj,
                         jsval      id,
                         jsval     *value_p)
{
    ObjectInstance *priv;
    const char *name;
    char *gname;
    GParamSpec *param;
    GValue gvalue = { 0, };

    if (!gjs_get_string_id(id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Get prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->gobj == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    gname = gjs_hyphen_from_camel(name);
    param = g_object_class_find_property(G_OBJECT_GET_CLASS(priv->gobj),
                                         gname);
    g_free(gname);

    if (param == NULL) {
        /* leave value_p as it was */
        return JS_TRUE;
    }

    if ((param->flags & G_PARAM_READABLE) == 0) {
        return JS_TRUE;
    }

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Overriding %s with GObject prop %s",
                     name, param->name);

    g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE(param));
    g_object_get_property(priv->gobj, param->name,
                          &gvalue);
    if (!gjs_value_from_g_value(context, value_p, &gvalue)) {
        g_value_unset(&gvalue);
        return JS_FALSE;
    }
    g_value_unset(&gvalue);

    return JS_TRUE;
}

/* a hook on setting a property; set value_p to override property value to
 * be set. Return value is JS_FALSE on OOM/exception.
 */
static JSBool
object_instance_set_prop(JSContext *context,
                         JSObject  *obj,
                         jsval      id,
                         jsval     *value_p)
{
    ObjectInstance *priv;
    const char *name;
    GParameter param = { NULL, { 0, }};

    if (!gjs_get_string_id(id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Set prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE;  /* wrong class passed in */
    if (priv->gobj == NULL)
        return JS_TRUE; /* prototype, not an instance. */

    switch (init_g_param_from_property(context, name,
                                       *value_p,
                                       G_TYPE_FROM_INSTANCE(priv->gobj),
                                       &param)) {
    case SOME_ERROR_OCCURRED:
        return JS_FALSE;
    case NO_SUCH_G_PROPERTY:
        return JS_TRUE;
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

    return JS_TRUE;
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
                            JSObject  *obj,
                            jsval      id,
                            uintN      flags,
                            JSObject **objp)
{
    ObjectInstance *priv;
    const char *name;

    *objp = NULL;

    if (!gjs_get_string_id(id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_GOBJECT,
                     "Resolve prop '%s' hook obj %p priv %p gobj %p %s",
                     name, obj, priv, priv ? priv->gobj : NULL,
                     (priv && priv->gobj) ?
                     g_type_name_from_instance((GTypeInstance*) priv->gobj) : "(type unknown)");

    if (priv == NULL)
        return JS_FALSE; /* we are the wrong class */

    if (priv->gobj == NULL) {
        /* We are the prototype, so look for methods and other class properties */
        GIFunctionInfo *method_info;

        /* find_method does not look at methods on parent classes,
         * we rely on javascript to walk up the __proto__ chain
         * and find those and define them in the right prototype.
         */
        method_info = g_object_info_find_method(priv->info,
                                                name);

        /* If it isn't a method on the object, see if it's one on an
         * iface the object implements. Note that since JS lacks
         * multiple inheritance, we stick the iface methods in the
         * object prototype, which means there are many copies of the
         * iface methods (one per object class node that introduces
         * the iface)
         */
        if (method_info == NULL) {
            int n_interfaces;
            int i;

            n_interfaces = g_object_info_get_n_interfaces(priv->info);

            for (i = 0; i < n_interfaces; ++i) {
                GIInterfaceInfo *iface_info;

                iface_info = g_object_info_get_interface(priv->info, i);

                method_info = g_interface_info_find_method(iface_info, name);

                g_base_info_unref( (GIBaseInfo*) iface_info);

                if (method_info != NULL) {
                    gjs_debug(GJS_DEBUG_GOBJECT,
                              "Found method %s in interface %d implemented by object",
                              name, i);
                    break;
                }
            }
        }

        if (method_info == NULL) {
            GType *interfaces;
            guint n_interfaces;
            guint i;

            interfaces = g_type_interfaces (priv->gtype, &n_interfaces);
            for (i = 0; i < n_interfaces; i++) {
                GIBaseInfo *base_info;
                GIInterfaceInfo *iface_info;

                base_info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                                        interfaces[i]);
                if (!base_info)
                    continue;

                if (g_base_info_get_type(base_info) != GI_INFO_TYPE_INTERFACE) {
                    g_base_info_unref(base_info);
                    continue;
                }

                iface_info = (GIInterfaceInfo*) base_info;

                method_info = g_interface_info_find_method(iface_info, name);

                g_base_info_unref(base_info);

                if (method_info != NULL) {
                    gjs_debug(GJS_DEBUG_GOBJECT,
                              "Found method %s in native interface %s",
                              name, g_type_name(interfaces[i]));
                    break;
                }
            }
            g_free(interfaces);
        }

        if (method_info != NULL) {
            const char *method_name;

#if GJS_VERBOSE_ENABLE_GI_USAGE
            _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

            method_name = g_base_info_get_name( (GIBaseInfo*) method_info);

            /* we do not define deprecated methods in the prototype */
            if (g_base_info_is_deprecated( (GIBaseInfo*) method_info)) {
                gjs_debug(GJS_DEBUG_GOBJECT,
                          "Ignoring definition of deprecated method %s in prototype for %s (%s.%s)",
                          method_name,
                          g_type_name(priv->gtype),
                          g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                          g_base_info_get_name( (GIBaseInfo*) priv->info));
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_TRUE;
            }

            gjs_debug(GJS_DEBUG_GOBJECT,
                      "Defining method %s in prototype for %s (%s.%s)",
                      method_name,
                      g_type_name(priv->gtype),
                      g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                      g_base_info_get_name( (GIBaseInfo*) priv->info));

            if (gjs_define_function(context, obj, method_info) == NULL) {
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_FALSE;
            }

            *objp = obj; /* we defined the prop in obj */

            g_base_info_unref( (GIBaseInfo*) method_info);
        }
    } else {
        /* We are an instance, not a prototype, so look for per-instance props that
         * we want to define on the JSObject. Generally we do not want to cache
         * these in JS, we want to always pull them from the GObject, or
         * JS would not see any changes made from C. So we use the get/set prop hooks,
         * not this resolve hook.
         */

        JSObject *proto;
        ObjectInstance *proto_priv;

        proto = JS_GetPrototype(context, obj);
        proto_priv = priv_from_js(context, proto);
        if (proto_priv->gtype == G_TYPE_INVALID) {
            gjs_debug(GJS_DEBUG_GOBJECT,
                      "storing gtype %s (%d) to prototype %p",
                      G_OBJECT_TYPE_NAME(priv->gobj),
                      (int) G_OBJECT_TYPE(priv->gobj),
                      proto);
            proto_priv->gtype = G_OBJECT_TYPE(priv->gobj);
        } else if (proto_priv->gtype != G_OBJECT_TYPE(priv->gobj)) {
            gjs_fatal("conflicting gtypes for prototype %s (%d) (was %s (%d))",
                      G_OBJECT_TYPE_NAME(priv->gobj),
                      (int) G_OBJECT_TYPE(priv->gobj),
                      g_type_name(proto_priv->gtype),
                      (int) proto_priv->gtype);
        }
    }

    return JS_TRUE;
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
                                      uintN        argc,
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

    if (argc == 0)
        return JS_TRUE;

    if (!JSVAL_IS_OBJECT(argv[0])) {
        gjs_throw(context, "argument should be a hash with props to set");
        return JS_FALSE;
    }

    props = JSVAL_TO_OBJECT(argv[0]);

    iter = JS_NewPropertyIterator(context, props);
    if (iter == NULL) {
        gjs_throw(context, "Failed to create property iterator for object props hash");
        return JS_FALSE;
    }

    prop_id = JSVAL_VOID;
    if (!JS_NextProperty(context, iter, &prop_id))
        return JS_FALSE;

    if (prop_id != JSVAL_VOID) {
        gparams = g_array_new(/* nul term */ FALSE, /* clear */ TRUE,
                              sizeof(GParameter));
    } else {
        return JS_TRUE;
    }

    while (prop_id != JSVAL_VOID) {
        jsval nameval;
        const char *name;
        jsval value;
        GParameter gparam = { NULL, { 0, }};

        if (!JS_IdToValue(context, prop_id, &nameval))
            goto free_array_and_fail;

        if (!gjs_get_string_id(nameval, &name))
            goto free_array_and_fail;

        if (!gjs_object_require_property(context, props, "property list", name, &value))
            goto free_array_and_fail;

        switch (init_g_param_from_property(context, name,
                                           value,
                                           gtype,
                                           &gparam)) {
        case SOME_ERROR_OCCURRED:
            goto free_array_and_fail;
        case NO_SUCH_G_PROPERTY:
            gjs_throw(context, "No property %s on this GObject %s",
                         name, g_type_name(gtype));
            goto free_array_and_fail;
        case VALUE_WAS_SET:
            break;
        }

        g_array_append_val(gparams, gparam);

        prop_id = JSVAL_VOID;
        if (!JS_NextProperty(context, iter, &prop_id))
            goto free_array_and_fail;
    }

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

    /* The JSContext will be gone if runtime is being destroyed.
     * In that case we effectively already converted to a weak ref without
     * doing anything since the keep alive will be collected.
     * Or if !is_last_ref, we do not want to convert to a strong
     * ref since we want everything collected on runtime destroy.
     */
    context = gjs_runtime_peek_load_context(runtime);
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
            priv->keep_alive = gjs_keep_alive_get_for_load_context(runtime);
            gjs_keep_alive_add_child(context, priv->keep_alive,
                                     gobj_no_longer_kept_alive_func,
                                     obj,
                                     priv);
        }
    }
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype.
 */
static JSBool
object_instance_constructor(JSContext *context,
                            JSObject  *obj,
                            uintN      argc,
                            jsval     *argv,
                            jsval     *retval)
{
    ObjectInstance *priv;
    ObjectInstance *proto_priv;
    JSObject *proto;
    gboolean is_proto;
    JSClass *obj_class;
    JSClass *proto_class;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    priv = g_slice_new0(ObjectInstance);

    GJS_INC_COUNTER(object);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "obj instance constructor, obj %p priv %p retval %p", obj, priv,
                        JSVAL_IS_OBJECT(*retval) ?
                        JSVAL_TO_OBJECT(*retval) : NULL);

    proto = JS_GetPrototype(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT, "obj instance __proto__ is %p", proto);

    /* If we're constructing the prototype, its __proto__ is not the same
     * class as us, but if we're constructing an instance, the prototype
     * has the same class.
     */
    obj_class = JS_GET_CLASS(context, obj);
    proto_class = JS_GET_CLASS(context, proto);

    is_proto = (obj_class != proto_class);

    gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                        "obj instance constructing proto %d, obj class %s proto class %s",
                        is_proto, obj_class->name, proto_class->name);

    if (!is_proto) {
        GType gtype;

        /* If we're the prototype, then post-construct we'll fill in priv->info.
         * If we are not the prototype, though, then we'll get ->info from the
         * prototype and then create a GObject if we don't have one already.
         */
        proto_priv = priv_from_js(context, proto);
        if (proto_priv == NULL) {
            gjs_debug(GJS_DEBUG_GOBJECT,
                      "Bad prototype set on object? Must match JSClass of object. JS error should have been reported.");
            return JS_FALSE;
        }

        priv->info = proto_priv->info;
        g_base_info_ref( (GIBaseInfo*) priv->info);

        /* Since gobject-introspection is always creating new info
         * objects, == is not meaningful on them, only comparison of
         * their names. We prefer to use the info that is already ref'd
         * by the prototype for the class.
         */
        g_assert(unthreadsafe_template_for_constructor.info == NULL ||
                 strcmp(g_base_info_get_name( (GIBaseInfo*) priv->info),
                        g_base_info_get_name( (GIBaseInfo*) unthreadsafe_template_for_constructor.info))
                 == 0);
        unthreadsafe_template_for_constructor.info = NULL;

        if (unthreadsafe_template_for_constructor.gobj == NULL) {
            GParameter *params;
            int n_params;

            gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);
            if (gtype == G_TYPE_NONE) {
                gjs_throw(context,
                          "No GType for object '%s'???",
                          g_base_info_get_name( (GIBaseInfo*) priv->info));
                return JS_FALSE;
            }

            if (!object_instance_props_to_g_parameters(context, obj, argc, argv,
                                                       gtype,
                                                       &params, &n_params)) {
                return JS_FALSE;
            }

            priv->gobj = g_object_newv(gtype, n_params, params);
            free_g_params(params, n_params);

            if (G_IS_INITIALLY_UNOWNED(priv->gobj) &&
                !g_object_is_floating(priv->gobj)) {
                /* GtkWindow does not return a ref to caller of g_object_new.
                 * Need a flag in gobject-introspection to tell us this.
                 */
                gjs_debug(GJS_DEBUG_GOBJECT,
                          "Newly-created object is initially unowned but we did not get the "
                          "floating ref, probably GtkWindow, using hacky workaround");
                g_object_ref(priv->gobj);
            } else if (g_object_is_floating(priv->gobj)) {
                g_object_ref_sink(priv->gobj);
            } else {
                /* we should already have a ref */
            }
        } else {
            priv->gobj = unthreadsafe_template_for_constructor.gobj;
            unthreadsafe_template_for_constructor.gobj = NULL;

            g_object_ref_sink(priv->gobj);
        }

        g_assert(peek_js_obj(context, priv->gobj) == NULL);
        set_js_obj(context, priv->gobj, obj);

#if DEBUG_DISPOSE
        g_object_weak_ref(priv->gobj, wrapped_gobj_dispose_notify, obj);
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
        priv->keep_alive = gjs_keep_alive_get_for_load_context(JS_GetRuntime(context));
        gjs_keep_alive_add_child(context,
                                 priv->keep_alive,
                                 gobj_no_longer_kept_alive_func,
                                 obj,
                                 priv);

        g_object_add_toggle_ref(priv->gobj,
                                wrapped_gobj_toggle_notify,
                                JS_GetRuntime(context));

        /* We now have both a ref and a toggle ref, we only want the
         * toggle ref. This may immediately remove the GC root
         * we just added, since refcount may drop to 1.
         */
        g_object_unref(priv->gobj);

        gjs_debug_lifecycle(GJS_DEBUG_GOBJECT,
                            "JSObject created with GObject %p %s",
                            priv->gobj, g_type_name_from_instance((GTypeInstance*) priv->gobj));

        TRACE(GJS_OBJECT_PROXY_NEW(priv, priv->gobj, g_base_info_get_namespace ( (GIBaseInfo*) priv->info),
                                    g_base_info_get_name ( (GIBaseInfo*) priv->info) ));
    }

    return JS_TRUE;
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
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    TRACE(GJS_OBJECT_PROXY_FINALIZE(priv, priv->gobj, g_base_info_get_namespace ( (GIBaseInfo*) priv->info),
                                    g_base_info_get_name ( (GIBaseInfo*) priv->info) ));

    if (priv->gobj) {
        g_assert(priv->gobj->ref_count > 0);
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

    GJS_DEC_COUNTER(object);
    g_slice_free(ObjectInstance, priv);
}

JSObject*
gjs_lookup_object_prototype(JSContext    *context,
                            GType         gtype)
{
    JSObject *proto;

    if (!gjs_define_object_class(context, NULL, gtype, NULL, &proto, NULL))
        return NULL;
    return proto;
}

static JSBool
real_connect_func(JSContext *context,
                  JSObject  *obj,
                  uintN      argc,
                  jsval     *argv,
                  jsval     *retval,
                  gboolean  after)
{
    ObjectInstance *priv;
    GClosure *closure;
    gulong id;
    guint signal_id;
    const char *signal_name;
    GQuark signal_detail;

    *retval = INT_TO_JSVAL(0);

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("connect obj %p priv %p argc %d", obj, priv, argc);
    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */
    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't connect to signals on %s.%s.prototype; only on instances",
                     g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                     g_base_info_get_name( (GIBaseInfo*) priv->info));
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

    signal_name = gjs_string_get_ascii_checked(context, argv[0]);
    if (signal_name == NULL) {
        return JS_FALSE;
    }

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             FALSE)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)));
        return JS_FALSE;
    }

    closure = gjs_closure_new_for_signal(context, JSVAL_TO_OBJECT(argv[1]), "signal callback", signal_id);
    if (closure == NULL)
        return JS_FALSE;

    id = g_signal_connect_closure(priv->gobj,
                                  signal_name,
                                  closure,
                                  after);

    if (!JS_NewNumberValue(context, id, retval)) {
        g_signal_handler_disconnect(priv->gobj, id);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
connect_after_func(JSContext *context,
                   JSObject  *obj,
                   uintN      argc,
                   jsval     *argv,
                   jsval     *retval)
{
    return real_connect_func(context, obj, argc, argv, retval, TRUE);
}

static JSBool
connect_func(JSContext *context,
             JSObject  *obj,
             uintN      argc,
             jsval     *argv,
             jsval     *retval)
{
    return real_connect_func(context, obj, argc, argv, retval, FALSE);
}

static JSBool
disconnect_func(JSContext *context,
                JSObject  *obj,
                uintN      argc,
                jsval     *argv,
                jsval     *retval)
{
    ObjectInstance *priv;
    gulong id;

    *retval = JSVAL_VOID;

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("disconnect obj %p priv %p argc %d", obj, priv, argc);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */

    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't disconnect signal on %s.%s.prototype; only on instances",
                     g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                     g_base_info_get_name( (GIBaseInfo*) priv->info));
        return JS_FALSE;
    }

    if (argc != 1 ||
        !JSVAL_IS_INT(argv[0])) {
        gjs_throw(context, "disconnect() takes one arg, the signal handler id");
        return JS_FALSE;
    }

    id = JSVAL_TO_INT(argv[0]);

    g_signal_handler_disconnect(priv->gobj, id);

    return JS_TRUE;
}

static JSBool
emit_func(JSContext *context,
          JSObject  *obj,
          uintN      argc,
          jsval     *argv,
          jsval     *retval)
{
    ObjectInstance *priv;
    guint signal_id;
    GQuark signal_detail;
    GSignalQuery signal_query;
    const char *signal_name;
    GValue *instance_and_args;
    GValue rvalue;
    unsigned int i;
    gboolean failed;

    *retval = JSVAL_VOID;

    priv = priv_from_js(context, obj);
    gjs_debug_gsignal("emit obj %p priv %p argc %d", obj, priv, argc);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */

    if (priv->gobj == NULL) {
        /* prototype, not an instance. */
        gjs_throw(context, "Can't emit signal on %s.%s.prototype; only on instances",
                     g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                     g_base_info_get_name( (GIBaseInfo*) priv->info));
        return JS_FALSE;
    }

    if (argc < 1 ||
        !JSVAL_IS_STRING(argv[0])) {
        gjs_throw(context, "emit() first arg is the signal name");
        return JS_FALSE;
    }

    signal_name = gjs_string_get_ascii_checked(context,
                                                  argv[0]);
    if (signal_name == NULL)
        return JS_FALSE;

    if (!g_signal_parse_name(signal_name,
                             G_OBJECT_TYPE(priv->gobj),
                             &signal_id,
                             &signal_detail,
                             FALSE)) {
        gjs_throw(context, "No signal '%s' on object '%s'",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)));
        return JS_FALSE;
    }

    g_signal_query(signal_id, &signal_query);

    if ((argc - 1) != signal_query.n_params) {
        gjs_throw(context, "Signal '%s' on %s requires %d args got %d",
                     signal_name,
                     g_type_name(G_OBJECT_TYPE(priv->gobj)),
                     signal_query.n_params,
                     argc - 1);
        return JS_FALSE;
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
                                       retval,
                                       &rvalue))
            failed = TRUE;

        g_value_unset(&rvalue);
    }

    for (i = 0; i < (signal_query.n_params + 1); ++i) {
        g_value_unset(&instance_and_args[i]);
    }

    return !failed;
}

/* Default spidermonkey toString is worthless.  Replace it
 * with something that gives us both the introspection name
 * and a memory address.
 */
static JSBool
to_string_func(JSContext *context,
               JSObject  *obj,
               uintN      argc,
               jsval     *argv,
               jsval     *retval)
{
    ObjectInstance *priv;
    char *strval;
    JSBool ret;
    const char *namespace;
    const char *name;

    *retval = JSVAL_VOID;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return JS_FALSE; /* wrong class passed in */

    namespace = g_base_info_get_namespace( (GIBaseInfo*) priv->info);
    name = g_base_info_get_name( (GIBaseInfo*) priv->info);

    if (priv->gobj == NULL) {
        strval = g_strdup_printf ("[object prototype of GIName:%s.%s jsobj@%p]", namespace, name, obj);
    } else {
        strval = g_strdup_printf ("[object instance proxy GIName:%s.%s jsobj@%p native@%p]", namespace, name, obj, priv->gobj);
    }

    ret = gjs_string_from_utf8 (context, strval, -1, retval);
    g_free (strval);
    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_object_instance_class = {
    NULL, /* We copy this class struct with multiple names */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START |
    JSCLASS_CONSTRUCT_PROTOTYPE,
    JS_PropertyStub,
    JS_PropertyStub,
    object_instance_get_prop,
    object_instance_set_prop,
    JS_EnumerateStub,
    (JSResolveOp) object_instance_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    object_instance_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static JSPropertySpec gjs_object_instance_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_object_instance_proto_funcs[] = {
    { "connect", connect_func, 0, 0 },
    { "connect_after", connect_after_func, 0, 0 },
    { "disconnect", disconnect_func, 0, 0 },
    { "emit", emit_func, 0, 0 },
    { "toString", to_string_func, 0, 0 },
    { NULL }
};

static JSBool
gjs_define_static_methods(JSContext    *context,
                          JSObject     *constructor,
                          GIObjectInfo *object_info)
{
    int i;
    const char *obj_name;
    int n_methods;

    obj_name = g_base_info_get_name ((GIBaseInfo*)object_info);
    n_methods = g_object_info_get_n_methods(object_info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfo *meth_info;
        GIFunctionInfoFlags flags;
        const char *name;

        meth_info = g_object_info_get_method(object_info, i);
        name = g_base_info_get_name((GIBaseInfo*)meth_info);
        flags = g_function_info_get_flags (meth_info);

        /* Anything that isn't a method we put on the prototype of the
         * constructor.  This includes <constructor> introspection
         * methods, as well as the forthcoming "static methods"
         * support.  We may want to change this to use
         * GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the
         * like in the near future.
         */
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            gjs_define_function(context, constructor, meth_info);
        }

        g_base_info_unref((GIBaseInfo*) meth_info);
    }
    return JS_TRUE;
}

static GIObjectInfo*
get_base_info(JSContext *context,
              GType      gtype)
{
    GIBaseInfo *info = NULL;

    while (TRUE) {
        info = g_irepository_find_by_gtype(g_irepository_get_default(),
                                           gtype);
        if (info != NULL)
            break;
         if (gtype == G_TYPE_OBJECT)
            gjs_fatal("No introspection data on GObject - pretty much screwed");

        gjs_debug(GJS_DEBUG_GOBJECT,
                  "No introspection data on '%s' so trying parent type '%s'",
                  g_type_name(gtype), g_type_name(g_type_parent(gtype)));

        gtype = g_type_parent(gtype);
    }
    return (GIObjectInfo*)info;
}

JSBool
gjs_define_object_class(JSContext     *context,
                        JSObject      *in_object,
                        GType          gtype,
                        JSObject     **constructor_p,
                        JSObject     **prototype_p,
                        GIObjectInfo **class_info_p)
{
    const char *constructor_name;
    JSObject *prototype;
    JSObject *constructor;
    JSObject *parent_proto;
    jsval value;
    ObjectInstance *priv;
    GIObjectInfo *info = NULL;
    gboolean has_own_info = TRUE;

    g_assert(gtype != G_TYPE_INVALID);

    info = (GIObjectInfo*)g_irepository_find_by_gtype(g_irepository_get_default(), gtype);
    if (!info) {
        has_own_info = FALSE;
        info = get_base_info(context, gtype);
    }

    if (!in_object) {
        in_object = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

        if (!in_object) {
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
    if (!has_own_info) {
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

            if (class_info_p)
                *class_info_p = info;
            else
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

    prototype = gjs_init_class_dynamic(context, in_object,
                                       /* parent prototype JSObject* for
                                        * prototype; NULL for
                                        * Object.prototype
                                        */
                                       parent_proto,
                                       g_base_info_get_namespace( (GIBaseInfo*) info),
                                       constructor_name,
                                       &gjs_object_instance_class,
                                       /* constructor for instances (NULL for
                                        * none - just name the prototype like
                                        * Math - rarely correct)
                                        */
                                       object_instance_constructor,
                                       /* number of constructor args */
                                       0,
                                       /* props of prototype */
                                       parent_proto ? NULL : &gjs_object_instance_proto_props[0],
                                       /* funcs of prototype */
                                       parent_proto ? NULL : &gjs_object_instance_proto_funcs[0],
                                       /* props of constructor, MyConstructor.myprop */
                                       NULL,
                                       /* funcs of constructor, MyConstructor.myfunc() */
                                       NULL);
    if (prototype == NULL)
        gjs_fatal("Can't init class %s", constructor_name);

    g_assert(gjs_object_has_property(context, in_object, constructor_name));

    /* Put the info in the prototype */
    priv = priv_from_js(context, prototype);
    g_assert(priv != NULL);
    g_assert(priv->info == NULL);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = gtype;

    gjs_debug(GJS_DEBUG_GOBJECT, "Defined class %s prototype %p class %p in object %p",
              constructor_name, prototype, JS_GET_CLASS(context, prototype), in_object);

    /* Now get the constructor we defined in
     * gjs_init_class_dynamic
     */
    gjs_object_get_property(context, in_object, constructor_name, &value);
    constructor = NULL;
    if (value != JSVAL_VOID) {
       if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Property '%s' does not look like a constructor",
                      constructor_name);
            g_base_info_unref((GIBaseInfo*)info);
            return FALSE;
       }

       constructor = JSVAL_TO_OBJECT(value);
       gjs_define_static_methods(context, constructor, info);
    }

    if (prototype_p)
        *prototype_p = prototype;

    if (constructor_p)
        *constructor_p = constructor;

    if (class_info_p)
        *class_info_p = info;
    else
        g_base_info_unref((GIBaseInfo*)info);
    return TRUE;
}

/* multiple JSRuntime could have a proxy to the same GObject, in theory
 */
#define OBJ_KEY_PREFIX_LEN 3
#define OBJ_KEY_LEN (OBJ_KEY_PREFIX_LEN+sizeof(void*)*2)
static void
get_obj_key(JSRuntime *runtime,
            char      *buf)
{
    /* not thread safe, but that's fine for now - just nuke the
     * cache thingy if we ever need thread safety
     */
    static char cached_buf[OBJ_KEY_LEN+1];
    static JSRuntime *cached_for = NULL;

    if (cached_for != runtime) {
        unsigned int i;
        union {
            const unsigned char bytes[sizeof(void*)];
            void *ptr;
        } d;
        g_assert(sizeof(d) == sizeof(void*));

        buf[0] = 'j';
        buf[1] = 's';
        buf[2] = '-';
        d.ptr = runtime;
        for (i = 0; i < sizeof(void*); i++) {
                int offset = OBJ_KEY_PREFIX_LEN+(i*2);
                buf[offset] = 'a' + ((d.bytes[i] & 0xf0) >> 4);
                buf[offset+1] = 'a' + (d.bytes[i] & 0x0f);
        }
        buf[OBJ_KEY_LEN] = '\0';
        strcpy(cached_buf, buf);
        cached_for = runtime;
        g_assert(strlen(buf) == OBJ_KEY_LEN);
    } else {
        strcpy(buf, cached_buf);
    }
}

static JSObject*
peek_js_obj(JSContext *context,
            GObject   *gobj)
{
    char buf[OBJ_KEY_LEN+1];

    get_obj_key(JS_GetRuntime(context), buf);

    return g_object_get_data(gobj, buf);
}

static void
set_js_obj(JSContext *context,
           GObject   *gobj,
           JSObject  *obj)
{
    char buf[OBJ_KEY_LEN+1];

    get_obj_key(JS_GetRuntime(context), buf);

    g_object_set_data(gobj, buf, obj);
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
        GIObjectInfo *info;

        gjs_debug_marshal(GJS_DEBUG_GOBJECT,
                          "Wrapping %s with JSObject",
                          g_type_name_from_instance((GTypeInstance*) gobj));


        if (!gjs_define_object_class(context, NULL, G_TYPE_FROM_INSTANCE(gobj), NULL, &proto, &info))
            return NULL;
        /* can't come up with a better approach... */
        unthreadsafe_template_for_constructor.info = (GIObjectInfo*) info;
        unthreadsafe_template_for_constructor.gobj = gobj;

        obj = gjs_construct_object_dynamic(context, proto,
                                              0, NULL);

        g_base_info_unref( (GIBaseInfo*) info);

        g_assert(peek_js_obj(context, gobj) == obj);
    }

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

    if (priv == NULL)
        return NULL;

    if (priv->gobj == NULL) {
        gjs_throw(context,
                  "Object is %s.%s.prototype, not an object instance - cannot convert to GObject*",
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));
        return NULL;
    }

    return priv->gobj;
}
