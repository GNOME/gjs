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

#include "boxed.h"
#include "arg.h"
#include "object.h"
#include <gjs/gjs-module.h>
#include <gjs/compat.h>
#include "repo.h"
#include "function.h"

#include <util/log.h>

#include <jsapi.h>

#include <girepository.h>

typedef struct {
    GIBoxedInfo *info;
    void *gboxed; /* NULL if we are the prototype and not an instance */
    guint can_allocate_directly : 1;
    guint allocated_directly : 1;
    guint not_owning_gboxed : 1; /* if set, the JS wrapper does not own
                                    the reference to the C gboxed */
} Boxed;

typedef struct {
    GIBoxedInfo *info;
    void *gboxed;
    jsval parent_jsval;
    gboolean no_copy;
} BoxedConstructInfo;

static gboolean struct_is_simple(GIStructInfo *info);

static JSBool boxed_set_field_from_value(JSContext   *context,
                                         Boxed       *priv,
                                         GIFieldInfo *field_info,
                                         jsval        value);

static BoxedConstructInfo unthreadsafe_template_for_constructor = { NULL, NULL, JSVAL_NULL, FALSE };

static struct JSClass gjs_boxed_class;

GJS_DEFINE_DYNAMIC_PRIV_FROM_JS(Boxed, gjs_boxed_class)

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or boxed prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
boxed_new_resolve(JSContext *context,
                  JSObject  *obj,
                  jsid       id,
                  uintN      flags,
                  JSObject **objp)
{
    Boxed *priv;
    const char *name;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GBOXED, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL)
        return JS_FALSE; /* wrong class */

    if (priv->gboxed == NULL) {
        /* We are the prototype, so look for methods and other class properties */
        GIFunctionInfo *method_info;

        method_info = g_struct_info_find_method((GIStructInfo*) priv->info,
                                                name);

        if (method_info != NULL) {
            JSObject *boxed_proto;
            const char *method_name;

#if GJS_VERBOSE_ENABLE_GI_USAGE
            _gjs_log_info_usage((GIBaseInfo*) method_info);
#endif

            method_name = g_base_info_get_name( (GIBaseInfo*) method_info);

            /* we do not define deprecated methods in the prototype */
            if (g_base_info_is_deprecated( (GIBaseInfo*) method_info)) {
                gjs_debug(GJS_DEBUG_GBOXED,
                          "Ignoring definition of deprecated method %s in prototype %s.%s",
                          method_name,
                          g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                          g_base_info_get_name( (GIBaseInfo*) priv->info));
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_TRUE;
            }

            gjs_debug(GJS_DEBUG_GBOXED,
                      "Defining method %s in prototype for %s.%s",
                      method_name,
                      g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                      g_base_info_get_name( (GIBaseInfo*) priv->info));

            boxed_proto = obj;

            if (gjs_define_function(context, boxed_proto, method_info) == NULL) {
                g_base_info_unref( (GIBaseInfo*) method_info);
                return JS_FALSE;
            }

            *objp = boxed_proto; /* we defined the prop in object_proto */

            g_base_info_unref( (GIBaseInfo*) method_info);
        }
    } else {
        /* We are an instance, not a prototype, so look for
         * per-instance props that we want to define on the
         * JSObject. Generally we do not want to cache these in JS, we
         * want to always pull them from the C object, or JS would not
         * see any changes made from C. So we use the get/set prop
         * hooks, not this resolve hook.
         */
    }

    return JS_TRUE;
}

/* Check to see if jsval passed in is another Boxed object of the same,
 * and if so, retrieves the Boxed private structure for it.
 */
static JSBool
boxed_get_copy_source(JSContext *context,
                      Boxed     *priv,
                      jsval      value,
                      Boxed    **source_priv_out)
{
    Boxed *source_priv;

    if (!JSVAL_IS_OBJECT(value))
        return JS_FALSE;

    if (!priv_from_js_with_typecheck(context, JSVAL_TO_OBJECT(value), &source_priv))
        return JS_FALSE;

    if (strcmp(g_base_info_get_name((GIBaseInfo*) priv->info),
               g_base_info_get_name((GIBaseInfo*) source_priv->info)) != 0)
        return JS_FALSE;

    *source_priv_out = source_priv;

    return JS_TRUE;
}

static JSBool
boxed_new_direct(JSContext   *context,
                 JSObject    *obj, /* "this" for constructor */
                 Boxed       *priv)
{
    g_assert(priv->can_allocate_directly);

    priv->gboxed = g_slice_alloc0(g_struct_info_get_size (priv->info));
    priv->allocated_directly = TRUE;

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "JSObject created by directly allocating %s",
                        g_base_info_get_name ((GIBaseInfo *)priv->info));

    return JS_TRUE;
}

static JSBool
boxed_new(JSContext   *context,
          JSObject    *obj, /* "this" for constructor */
          Boxed       *priv)
{
    GType gtype;
    int n_methods;
    int i;

    gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);

    if (gtype != G_TYPE_NONE) {
        /* If the structure is registered as a boxed, we can create a new instance by
         * looking for a zero-args constructor and calling it; constructors don't
         * really make sense for non-boxed types, since there is no memory management
         * for the return value; those are handled below along with simple boxed
         * structures without constructor.
         */
        n_methods = g_struct_info_get_n_methods(priv->info);

        for (i = 0; i < n_methods; ++i) {
            GIFunctionInfo *func_info;
            GIFunctionInfoFlags flags;

            func_info = g_struct_info_get_method(priv->info, i);

            flags = g_function_info_get_flags(func_info);
            if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0 &&
                g_callable_info_get_n_args((GICallableInfo*) func_info) == 0) {

                GIArgument rval;
                GError *error = NULL;

                if (!g_function_info_invoke(func_info, NULL, 0, NULL, 0, &rval, &error)) {
                    gjs_throw(context, "Failed to invoke boxed constructor: %s", error->message);
                    g_clear_error(&error);
                    g_base_info_unref((GIBaseInfo*) func_info);
                    return JS_FALSE;
                }

                g_base_info_unref((GIBaseInfo*) func_info);

                priv->gboxed = rval.v_pointer;

                gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                                    "JSObject created with boxed instance %p type %s",
                                    priv->gboxed, g_type_name(gtype));

                return JS_TRUE;
            }

            g_base_info_unref((GIBaseInfo*) func_info);
        }
    }

    if (priv->can_allocate_directly)
        return boxed_new_direct(context, obj, priv);

    gjs_throw(context, "Unable to construct boxed type %s since it has no zero-args <constructor>, can only wrap an existing one",
              g_base_info_get_name((GIBaseInfo*) priv->info));

    return JS_FALSE;
}

/* When initializing a boxed object from a hash of properties, we don't want
 * to do n O(n) lookups, so put temporarily put the fields into a hash table
 * for fast lookup. We could also do this ahead of time and store it on proto->priv.
 */
static GHashTable *
get_field_map(GIStructInfo *struct_info)
{
    GHashTable *result;
    int n_fields;
    int i;

    result = g_hash_table_new_full(g_str_hash, g_str_equal,
                                   NULL, (GDestroyNotify)g_base_info_unref);
    n_fields = g_struct_info_get_n_fields(struct_info);

    for (i = 0; i < n_fields; i++) {
        GIFieldInfo *field_info = g_struct_info_get_field(struct_info, i);
        g_hash_table_insert(result, (char *)g_base_info_get_name((GIBaseInfo *)field_info), field_info);
    }

    return result;
}

/* Initialize a newly created Boxed from an object that is a "hash" of
 * properties to set as fieds of the object. We don't require that every field
 * of the object be set.
 */
static JSBool
boxed_init_from_props(JSContext   *context,
                      JSObject    *obj,
                      Boxed       *priv,
                      jsval        props_value)
{
    JSObject *props;
    JSObject *iter;
    jsid prop_id;
    GHashTable *field_map;
    gboolean success;

    success = FALSE;

    if (!JSVAL_IS_OBJECT(props_value)) {
        gjs_throw(context, "argument should be a hash with fields to set");
        return JS_FALSE;
    }

    props = JSVAL_TO_OBJECT(props_value);

    iter = JS_NewPropertyIterator(context, props);
    if (iter == NULL) {
        gjs_throw(context, "Failed to create property iterator for fields hash");
        return JS_FALSE;
    }

    field_map = get_field_map(priv->info);

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, iter, &prop_id))
        goto out;

    while (!JSID_IS_VOID(prop_id)) {
        GIFieldInfo *field_info;
        const char *name;
        jsval value;

        if (!gjs_get_string_id(context, prop_id, &name))
            goto out;

        field_info = g_hash_table_lookup(field_map, name);
        if (field_info == NULL) {
            gjs_throw(context, "No field %s on boxed type %s",
                      name, g_base_info_get_name((GIBaseInfo *)priv->info));
            goto out;
        }

        if (!gjs_object_require_property(context, props, "property list", name, &value))
            goto out;

        if (!boxed_set_field_from_value(context, priv, field_info, value)) {
            goto out;
        }

        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, iter, &prop_id))
            goto out;
    }

    success = TRUE;

out:
    g_hash_table_destroy(field_map);

    return success;
}

/* Do any initialization of a newly constructed object from the arguments passed
 * in from Javascript.
 */
static JSBool
boxed_init(JSContext   *context,
           JSObject    *obj, /* "this" for constructor */
           Boxed       *priv,
           uintN        argc,
           jsval       *argv)
{
    if (argc == 0)
        return JS_TRUE;

    if (argc == 1) {
        Boxed *source_priv;

        /* Short-cut to memcpy when possible */
        if (priv->can_allocate_directly &&
            boxed_get_copy_source (context, priv, argv[0], &source_priv)) {

            memcpy(priv->gboxed, source_priv->gboxed,
                   g_struct_info_get_size (priv->info));

            return JS_TRUE;
        }

        return boxed_init_from_props(context, obj, priv, argv[0]);
    }

    gjs_throw(context, "Constructor with multiple arguments not supported for %s",
              g_base_info_get_name((GIBaseInfo *)priv->info));

    return JS_FALSE;
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(boxed)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(boxed)
    Boxed *priv;
    Boxed *proto_priv;
    JSClass *obj_class;
    JSClass *proto_class;
    JSObject *proto;
    gboolean is_proto;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(boxed);

    priv = g_slice_new0(Boxed);

    GJS_INC_COUNTER(boxed);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "boxed constructor, obj %p priv %p",
                        object, priv);

    proto = JS_GetPrototype(context, object);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED, "boxed instance __proto__ is %p", proto);

    /* If we're constructing the prototype, its __proto__ is not the same
     * class as us, but if we're constructing an instance, the prototype
     * has the same class.
     */
    obj_class = JS_GET_CLASS(context, object);
    proto_class = JS_GET_CLASS(context, proto);

    is_proto = (obj_class != proto_class);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "boxed instance constructing proto %d, obj class %s proto class %s",
                        is_proto, obj_class->name, proto_class->name);

    if (!is_proto) {
        /* If we're the prototype, then post-construct we'll fill in priv->info.
         * If we are not the prototype, though, then we'll get ->info from the
         * prototype and then create a GObject if we don't have one already.
         */
        proto_priv = priv_from_js(context, proto);
        if (proto_priv == NULL) {
            gjs_debug(GJS_DEBUG_GBOXED,
                      "Bad prototype set on boxed? Must match JSClass of object. JS error should have been reported.");
            return JS_FALSE;
        }

        priv->info = proto_priv->info;
        priv->can_allocate_directly = proto_priv->can_allocate_directly;
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

        if (unthreadsafe_template_for_constructor.gboxed == NULL) {
            Boxed *source_priv;

            /* Short-circuit copy-construction in the case where we can use g_boxed_copy */
            if (argc == 1 &&
                boxed_get_copy_source(context, priv, argv[0], &source_priv)) {

                GType gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);
                if (gtype != G_TYPE_NONE) {
                    priv->gboxed = g_boxed_copy(gtype, source_priv->gboxed);
                    GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
                    return JS_TRUE;
                }
            }

            if (!boxed_new(context, object, priv))
                return JS_FALSE;

            if (!boxed_init(context, object, priv, argc, argv))
                return JS_FALSE;

        } else if (!JSVAL_IS_NULL(unthreadsafe_template_for_constructor.parent_jsval)) {
            /* A structure nested inside a parent object; doens't have an independent allocation */

            priv->gboxed = unthreadsafe_template_for_constructor.gboxed;
            priv->not_owning_gboxed = TRUE;

            /* We never actually read the reserved slot, but we put the parent object
             * into it to hold onto the parent object.
             */
            JS_SetReservedSlot(context, object, 0,
                               unthreadsafe_template_for_constructor.parent_jsval);

            unthreadsafe_template_for_constructor.parent_jsval = JSVAL_NULL;
            unthreadsafe_template_for_constructor.gboxed = NULL;
        } else if (unthreadsafe_template_for_constructor.no_copy) {
            /* we need to create a JS Boxed which references the
             * original C struct, not a copy of it. Used for
             * G_SIGNAL_TYPE_STATIC_SCOPE
             */
            priv->gboxed = unthreadsafe_template_for_constructor.gboxed;
            priv->not_owning_gboxed = TRUE;
            unthreadsafe_template_for_constructor.gboxed = NULL;
        } else {
            GType gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);
            JSBool retval;
            
            if (gtype != G_TYPE_NONE) {
                priv->gboxed = g_boxed_copy(gtype,
                                            unthreadsafe_template_for_constructor.gboxed);
            } else if (priv->can_allocate_directly) {
                if (!boxed_new_direct(context, object, priv))
                    return JS_FALSE;

                memcpy(priv->gboxed,
                       unthreadsafe_template_for_constructor.gboxed,
                       g_struct_info_get_size (priv->info));
            } else {
                gjs_throw(context,
                          "Can't create a Javascript object for %s; no way to copy",
                          g_base_info_get_name( (GIBaseInfo*) priv->info));
            }

            unthreadsafe_template_for_constructor.gboxed = NULL;

            retval = priv->gboxed != NULL;
            if (retval)
                GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
            return retval;
        }
    }

    GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);

    return JS_TRUE;
}

static void
boxed_finalize(JSContext *context,
               JSObject  *obj)
{
    Boxed *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    if (priv->gboxed && !priv->not_owning_gboxed) {
        if (priv->allocated_directly) {
            g_slice_free1(g_struct_info_get_size (priv->info), priv->gboxed);
        } else {
            GType gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);
            g_assert(gtype != G_TYPE_NONE);

            g_boxed_free(gtype,  priv->gboxed);
        }

        priv->gboxed = NULL;
    }

    if (priv->info) {
        g_base_info_unref( (GIBaseInfo*) priv->info);
        priv->info = NULL;
    }

    GJS_DEC_COUNTER(boxed);
    g_slice_free(Boxed, priv);
}

static GIFieldInfo *
get_field_info (JSContext *context,
                Boxed     *priv,
                jsid       id)
{
    int field_index;
    jsval id_val;

    if (!JS_IdToValue(context, id, &id_val))
        return JS_FALSE;

    if (!JSVAL_IS_INT (id_val)) {
        gjs_throw(context, "Field index for %s is not an integer",
                  g_base_info_get_name ((GIBaseInfo *)priv->info));
        return NULL;
    }

    field_index = JSVAL_TO_INT(id_val);
    if (field_index < 0 || field_index >= g_struct_info_get_n_fields (priv->info)) {
        gjs_throw(context, "Bad field index %d for %s", field_index,
                  g_base_info_get_name ((GIBaseInfo *)priv->info));
        return NULL;
    }

    return g_struct_info_get_field (priv->info, field_index);
}

static JSBool
get_nested_interface_object (JSContext   *context,
                             JSObject    *parent_obj,
                             Boxed       *parent_priv,
                             GIFieldInfo *field_info,
                             GITypeInfo  *type_info,
                             GIBaseInfo  *interface_info,
                             jsval       *value)
{
    JSObject *obj;
    JSObject *proto;
    int offset;

    if (!struct_is_simple ((GIStructInfo *)interface_info)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)parent_priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));

        return JS_FALSE;
    }

    proto = gjs_lookup_boxed_prototype(context, (GIBoxedInfo*) interface_info);

    offset = g_field_info_get_offset (field_info);
    unthreadsafe_template_for_constructor.info = (GIBoxedInfo*) interface_info;
    unthreadsafe_template_for_constructor.gboxed = ((char *)parent_priv->gboxed) + offset;

    /* Rooting the object here is a little paranoid; the JSObject has to be kept
     * alive anyways by our caller; so this would matter only if there was an
     * aggressive GC that moved rooted objects */
    JS_AddValueRoot(context, &unthreadsafe_template_for_constructor.parent_jsval);
    unthreadsafe_template_for_constructor.parent_jsval = OBJECT_TO_JSVAL(parent_obj);

    obj = gjs_construct_object_dynamic(context, proto,
                                       0, NULL);

    JS_RemoveValueRoot(context, &unthreadsafe_template_for_constructor.parent_jsval);

    if (obj != NULL) {
        *value = OBJECT_TO_JSVAL(obj);
        return JS_TRUE;
    } else {
        return JS_FALSE;
    }
}

static JSBool
boxed_field_getter (JSContext *context,
                    JSObject  *obj,
                    jsid       id,
                    jsval     *value)
{
    Boxed *priv;
    GIFieldInfo *field_info;
    GITypeInfo *type_info;
    GArgument arg;
    gboolean success = FALSE;

    priv = priv_from_js(context, obj);
    if (!priv)
        return JS_FALSE;

    field_info = get_field_info(context, priv, id);
    if (!field_info)
        return JS_FALSE;

    type_info = g_field_info_get_type (field_info);

    if (priv->gboxed == NULL) { /* direct access to proto field */
        gjs_throw(context, "Can't get field %s.%s from a prototype",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    if (!g_type_info_is_pointer (type_info) &&
        g_type_info_get_tag (type_info) == GI_TYPE_TAG_INTERFACE) {

        GIBaseInfo *interface_info = g_type_info_get_interface(type_info);

        if (g_base_info_get_type (interface_info) == GI_INFO_TYPE_STRUCT ||
            g_base_info_get_type (interface_info) == GI_INFO_TYPE_BOXED) {

            success = get_nested_interface_object (context, obj, priv,
                                                   field_info, type_info, interface_info,
                                                   value);

            g_base_info_unref ((GIBaseInfo *)interface_info);

            goto out;
        }

        g_base_info_unref ((GIBaseInfo *)interface_info);
    }

    if (!g_field_info_get_field (field_info, priv->gboxed, &arg)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    if (!gjs_value_from_g_argument (context, value,
                                    type_info,
                                    &arg))
        goto out;

    success = TRUE;

out:
    g_base_info_unref ((GIBaseInfo *)field_info);
    g_base_info_unref ((GIBaseInfo *)type_info);

    return success;
}

static JSBool
set_nested_interface_object (JSContext   *context,
                             Boxed       *parent_priv,
                             GIFieldInfo *field_info,
                             GITypeInfo  *type_info,
                             GIBaseInfo  *interface_info,
                             jsval        value)
{
    JSObject *proto;
    int offset;
    Boxed *proto_priv;
    Boxed *source_priv;

    if (!struct_is_simple ((GIStructInfo *)interface_info)) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)parent_priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));

        return JS_FALSE;
    }

    proto = gjs_lookup_boxed_prototype(context, (GIBoxedInfo*) interface_info);
    proto_priv = priv_from_js(context, proto);

    /* If we can't directly copy from the source object we need
     * to construct a new temporary object.
     */
    if (!boxed_get_copy_source(context, proto_priv, value, &source_priv)) {
        JSObject *tmp_object = gjs_construct_object_dynamic(context, proto, 1, &value);
        if (!tmp_object)
            return JS_FALSE;

        source_priv = priv_from_js(context, tmp_object);
        if (!source_priv)
            return JS_FALSE;
    }

    offset = g_field_info_get_offset (field_info);
    memcpy(((char *)parent_priv->gboxed) + offset,
           source_priv->gboxed,
           g_struct_info_get_size (source_priv->info));

    return JS_TRUE;
}

static JSBool
boxed_set_field_from_value(JSContext   *context,
                           Boxed       *priv,
                           GIFieldInfo *field_info,
                           jsval        value)
{
    GITypeInfo *type_info;
    GArgument arg;
    gboolean success = FALSE;
    gboolean need_release = FALSE;

    type_info = g_field_info_get_type (field_info);

    if (!g_type_info_is_pointer (type_info) &&
        g_type_info_get_tag (type_info) == GI_TYPE_TAG_INTERFACE) {

        GIBaseInfo *interface_info = g_type_info_get_interface(type_info);

        if (g_base_info_get_type (interface_info) == GI_INFO_TYPE_STRUCT ||
            g_base_info_get_type (interface_info) == GI_INFO_TYPE_BOXED) {

            success = set_nested_interface_object (context, priv,
                                                   field_info, type_info,
                                                   interface_info, value);

            g_base_info_unref ((GIBaseInfo *)interface_info);

            goto out;
        }

        g_base_info_unref ((GIBaseInfo *)interface_info);

    }

    if (!gjs_value_to_g_argument(context, value,
                                 type_info,
                                 g_base_info_get_name ((GIBaseInfo *)field_info),
                                 GJS_ARGUMENT_FIELD,
                                 GI_TRANSFER_NOTHING,
                                 TRUE, &arg))
        goto out;

    need_release = TRUE;

    if (!g_field_info_set_field (field_info, priv->gboxed, &arg)) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    success = TRUE;

out:
    if (need_release)
        gjs_g_argument_release (context, GI_TRANSFER_NOTHING,
                                type_info,
                                &arg);

    g_base_info_unref ((GIBaseInfo *)type_info);

    return success;
}

static JSBool
boxed_field_setter (JSContext *context,
                    JSObject  *obj,
                    jsid       id,
                    jsval     *value)
{
    Boxed *priv;
    GIFieldInfo *field_info;
    gboolean success = FALSE;

    priv = priv_from_js(context, obj);
    if (!priv)
        return JS_FALSE;

    field_info = get_field_info(context, priv, id);
    if (!field_info)
        return JS_FALSE;

    if (priv->gboxed == NULL) { /* direct access to proto field */
        gjs_throw(context, "Can't set field %s.%s on prototype",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    success = boxed_set_field_from_value (context, priv, field_info, *value);

out:
    g_base_info_unref ((GIBaseInfo *)field_info);

    return success;
}

static JSBool
define_boxed_class_fields (JSContext *context,
                           Boxed     *priv,
                           JSObject  *proto)
{
    int n_fields = g_struct_info_get_n_fields (priv->info);
    int i;

    /* We identify properties with a 'TinyId': a 8-bit numeric value
     * that can be retrieved in the property getter/setter. Using it
     * allows us to avoid a hash-table lookup or linear search.
     * It does restrict us to a maximum of 256 fields per type.
     *
     * We define all fields as read/write so that the user gets an
     * error message. If we omitted fields or defined them read-only
     * we'd:
     *
     *  - Storing a new property for a non-accessible field
     *  - Silently do nothing when writing a read-only field
     *
     * Which is pretty confusing if the only reason a field isn't
     * writable is language binding or memory-management restrictions.
     *
     * We just go ahead and define the fields immediately for the
     * class; doing it lazily in boxed_new_resolve() would be possible
     * as well if doing it ahead of time caused to much start-up
     * memory overhead.
     */
    if (n_fields > 256) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Only defining the first 256 fields in boxed type '%s'",
                  g_base_info_get_name ((GIBaseInfo *)priv->info));
        n_fields = 256;
    }

    for (i = 0; i < n_fields; i++) {
        GIFieldInfo *field = g_struct_info_get_field (priv->info, i);
        const char *field_name = g_base_info_get_name ((GIBaseInfo *)field);
        gboolean result;

        result = JS_DefinePropertyWithTinyId(context, proto, field_name, i,
                                             JSVAL_NULL,
                                             boxed_field_getter, boxed_field_setter,
                                             JSPROP_PERMANENT | JSPROP_SHARED);

        g_base_info_unref ((GIBaseInfo *)field);

        if (!result)
            return JS_FALSE;
    }

    return JS_TRUE;
}


/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 *
 * We allocate 1 reserved slot; this is typically unused, but if the
 * boxed is for a nested structure inside a parent structure, the
 * reserved slot is used to hold onto the parent Javascript object and
 * make sure it doesn't get freed.
 */
static struct JSClass gjs_boxed_class = {
    NULL, /* dynamic class, no name here */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START |
    JSCLASS_CONSTRUCT_PROTOTYPE |
    JSCLASS_HAS_RESERVED_SLOTS(1),
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) boxed_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    boxed_finalize,
    NULL,
    NULL,
    NULL,
    NULL, NULL, NULL, NULL, NULL
};

static JSPropertySpec gjs_boxed_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_boxed_proto_funcs[] = {
    { NULL }
};

JSObject*
gjs_lookup_boxed_constructor(JSContext    *context,
                                GIBoxedInfo  *info)
{
    JSObject *ns;
    JSObject *constructor;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    constructor = NULL;
    if (gjs_define_boxed_class(context, ns, info,
                                  &constructor, NULL))
        return constructor;
    else
        return NULL;
}

JSObject*
gjs_lookup_boxed_prototype(JSContext    *context,
                              GIBoxedInfo  *info)
{
    JSObject *ns;
    JSObject *proto;

    ns = gjs_lookup_namespace_object(context, (GIBaseInfo*) info);

    if (ns == NULL)
        return NULL;

    proto = NULL;
    if (gjs_define_boxed_class(context, ns, info, NULL, &proto))
        return proto;
    else
        return NULL;
}

JSClass*
gjs_lookup_boxed_class(JSContext    *context,
                          GIBoxedInfo  *info)
{
    JSObject *prototype;

    prototype = gjs_lookup_boxed_prototype(context, info);

    return JS_GET_CLASS(context, prototype);
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
static gboolean
struct_is_simple(GIStructInfo *info)
{
    int n_fields = g_struct_info_get_n_fields (info);
    gboolean is_simple = TRUE;
    int i;

    for (i = 0; i < n_fields && is_simple; i++) {
        GIFieldInfo *field_info = g_struct_info_get_field (info, i);
        GITypeInfo *type_info = g_field_info_get_type (field_info);

        if (g_type_info_is_pointer (type_info)) {
            is_simple = FALSE;
        } else {
            switch (g_type_info_get_tag (type_info)) {
            case GI_TYPE_TAG_BOOLEAN:
            case GI_TYPE_TAG_INT8:
            case GI_TYPE_TAG_UINT8:
            case GI_TYPE_TAG_INT16:
            case GI_TYPE_TAG_UINT16:
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_UINT32:
            case GI_TYPE_TAG_INT64:
            case GI_TYPE_TAG_UINT64:
            case GI_TYPE_TAG_FLOAT:
            case GI_TYPE_TAG_DOUBLE:
                break;
            case GI_TYPE_TAG_VOID:
            case GI_TYPE_TAG_GTYPE:
            case GI_TYPE_TAG_ERROR:
            case GI_TYPE_TAG_UTF8:
            case GI_TYPE_TAG_FILENAME:
            case GI_TYPE_TAG_ARRAY:
            case GI_TYPE_TAG_GLIST:
            case GI_TYPE_TAG_GSLIST:
            case GI_TYPE_TAG_GHASH:
                break;
            case GI_TYPE_TAG_INTERFACE:
            {
                GIBaseInfo *interface = g_type_info_get_interface (type_info);
                switch (g_base_info_get_type (interface)) {
                case GI_INFO_TYPE_BOXED:
                case GI_INFO_TYPE_STRUCT:
                    if (!struct_is_simple ((GIStructInfo *)interface))
                        is_simple = FALSE;
                    break;
                case GI_INFO_TYPE_UNION:
                    /* FIXME: Need to implement */
                    is_simple = FALSE;
                    break;
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                    break;
                case GI_INFO_TYPE_OBJECT:
                case GI_INFO_TYPE_VFUNC:
                case GI_INFO_TYPE_CALLBACK:
                case GI_INFO_TYPE_INVALID:
                case GI_INFO_TYPE_INTERFACE:
                case GI_INFO_TYPE_FUNCTION:
                case GI_INFO_TYPE_CONSTANT:
                case GI_INFO_TYPE_ERROR_DOMAIN:
                case GI_INFO_TYPE_VALUE:
                case GI_INFO_TYPE_SIGNAL:
                case GI_INFO_TYPE_PROPERTY:
                case GI_INFO_TYPE_FIELD:
                case GI_INFO_TYPE_ARG:
                case GI_INFO_TYPE_TYPE:
                case GI_INFO_TYPE_UNRESOLVED:
                    is_simple = FALSE;
                    break;
                }

                g_base_info_unref (interface);
                break;
            }
            }
        }

        g_base_info_unref ((GIBaseInfo *)field_info);
        g_base_info_unref ((GIBaseInfo *)type_info);
    }

    return is_simple;
}

JSBool
gjs_define_boxed_class(JSContext    *context,
                          JSObject     *in_object,
                          GIBoxedInfo  *info,
                          JSObject    **constructor_p,
                          JSObject    **prototype_p)
{
    const char *constructor_name;
    JSObject *prototype;
    jsval value;
    Boxed *priv;

    /* See the comment in gjs_define_object_class() for an
     * explanation of how this all works; Boxed is pretty much the
     * same as Object.
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    if (gjs_object_get_property(context, in_object, constructor_name, &value)) {
        JSObject *constructor;

        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "Existing property '%s' does not look like a constructor",
                         constructor_name);
            return JS_FALSE;
        }

        constructor = JSVAL_TO_OBJECT(value);

        gjs_object_get_property(context, constructor, "prototype", &value);
        if (!JSVAL_IS_OBJECT(value)) {
            gjs_throw(context, "boxed %s prototype property does not appear to exist or has wrong type", constructor_name);
            return JS_FALSE;
        } else {
            if (prototype_p)
                *prototype_p = JSVAL_TO_OBJECT(value);
            if (constructor_p)
                *constructor_p = constructor;

            return JS_TRUE;
        }
    }

    prototype = gjs_init_class_dynamic(context, in_object,
                                          /* parent prototype JSObject* for
                                           * prototype; NULL for
                                           * Object.prototype
                                           */
                                          NULL,
                                          g_base_info_get_namespace( (GIBaseInfo*) info),
                                          constructor_name,
                                          &gjs_boxed_class,
                                          /* constructor for instances (NULL for
                                           * none - just name the prototype like
                                           * Math - rarely correct)
                                           */
                                          gjs_boxed_constructor,
                                          /* number of constructor args (less can be passed) */
                                          1,
                                          /* props of prototype */
                                          &gjs_boxed_proto_props[0],
                                          /* funcs of prototype */
                                          &gjs_boxed_proto_funcs[0],
                                          /* props of constructor, MyConstructor.myprop */
                                          NULL,
                                          /* funcs of constructor, MyConstructor.myfunc() */
                                          NULL);
    if (prototype == NULL) {
        gjs_log_exception(context, NULL);
        gjs_fatal("Can't init class %s", constructor_name);
    }

    g_assert(gjs_object_has_property(context, in_object, constructor_name));

    /* Put the info in the prototype */
    priv = priv_from_js(context, prototype);
    g_assert(priv != NULL);
    g_assert(priv->info == NULL);
    priv->info = info;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GET_CLASS(context, prototype), in_object);

    priv->can_allocate_directly = struct_is_simple (priv->info);

    define_boxed_class_fields (context, priv, prototype);

    if (constructor_p) {
        *constructor_p = NULL;
        gjs_object_get_property(context, in_object, constructor_name, &value);
        if (value != JSVAL_VOID) {
            if (!JSVAL_IS_OBJECT(value)) {
                gjs_throw(context, "Property '%s' does not look like a constructor",
                          constructor_name);
                return JS_FALSE;
            }
        }

        *constructor_p = JSVAL_TO_OBJECT(value);
    }

    if (prototype_p)
        *prototype_p = prototype;

    return JS_TRUE;
}

JSObject*
gjs_boxed_from_c_struct(JSContext             *context,
                        GIStructInfo          *info,
                        void                  *gboxed,
                        GjsBoxedCreationFlags  flags)
{
    JSObject *proto;

    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    proto = gjs_lookup_boxed_prototype(context, info);

    /* can't come up with a better approach... */
    unthreadsafe_template_for_constructor.info = info;
    unthreadsafe_template_for_constructor.gboxed = gboxed;
    unthreadsafe_template_for_constructor.parent_jsval = JSVAL_NULL;
    unthreadsafe_template_for_constructor.no_copy = (flags & GJS_BOXED_CREATION_NO_COPY) != 0;

    return gjs_construct_object_dynamic(context, proto,
                                        0, NULL);
}

void*
gjs_c_struct_from_boxed(JSContext    *context,
                        JSObject     *obj)
{
    Boxed *priv;

    if (obj == NULL)
        return NULL;

    priv = priv_from_js(context, obj);

    if (priv == NULL)
        return NULL;

    if (priv->gboxed == NULL) {
        gjs_throw(context,
                  "Object is %s.%s.prototype, not an object instance - cannot convert to a boxed instance",
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));
        return NULL;
    }

    return priv->gboxed;
}
