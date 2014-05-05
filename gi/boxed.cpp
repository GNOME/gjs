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
#include "proxyutils.h"
#include "function.h"
#include "gtype.h"

#include <util/log.h>

#include <girepository.h>

typedef struct {
    /* prototype info */
    GIBoxedInfo *info;
    GType gtype;
    gint zero_args_constructor; /* -1 if none */
    jsid zero_args_constructor_name;
    gint default_constructor; /* -1 if none */
    jsid default_constructor_name;

    /* instance info */
    void *gboxed; /* NULL if we are the prototype and not an instance */

    guint can_allocate_directly : 1;
    guint allocated_directly : 1;
    guint not_owning_gboxed : 1; /* if set, the JS wrapper does not own
                                    the reference to the C gboxed */
} Boxed;

static gboolean struct_is_simple(GIStructInfo *info);

static JSBool boxed_set_field_from_value(JSContext   *context,
                                         Boxed       *priv,
                                         GIFieldInfo *field_info,
                                         jsval        value);

extern struct JSClass gjs_boxed_class;

GJS_DEFINE_PRIV_FROM_JS(Boxed, gjs_boxed_class)

static JSBool
gjs_define_static_methods(JSContext    *context,
                          JSObject     *constructor,
                          GType         gtype,
                          GIStructInfo *boxed_info)
{
    int i;
    int n_methods;

    n_methods = g_struct_info_get_n_methods(boxed_info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfo *meth_info;
        GIFunctionInfoFlags flags;

        meth_info = g_struct_info_get_method (boxed_info, i);
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
                  JS::HandleObject obj,
                  JS::HandleId id,
                  unsigned flags,
                  JS::MutableHandleObject objp)
{
    Boxed *priv;
    char *name;
    JSBool ret = JS_FALSE;

    if (!gjs_get_string_id(context, id, &name))
        return JS_TRUE; /* not resolved, but no error */

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GBOXED, "Resolve prop '%s' hook obj %p priv %p", name, *obj, priv);

    if (priv == NULL)
        goto out; /* wrong class */

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
            if (g_function_info_get_flags (method_info) & GI_FUNCTION_IS_METHOD) {
                method_name = g_base_info_get_name( (GIBaseInfo*) method_info);

                gjs_debug(GJS_DEBUG_GBOXED,
                          "Defining method %s in prototype for %s.%s",
                          method_name,
                          g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                          g_base_info_get_name( (GIBaseInfo*) priv->info));

                boxed_proto = obj;

                if (gjs_define_function(context, boxed_proto, priv->gtype,
                                        (GICallableInfo *)method_info) == NULL) {
                    g_base_info_unref( (GIBaseInfo*) method_info);
                    goto out;
                }

                objp.set(boxed_proto); /* we defined the prop in object_proto */
            }

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
    ret = JS_TRUE;

 out:
    g_free(name);
    return ret;
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

    if (!g_base_info_equal((GIBaseInfo*) priv->info, (GIBaseInfo*) source_priv->info))
        return JS_FALSE;

    *source_priv_out = source_priv;

    return JS_TRUE;
}

static void
boxed_new_direct(Boxed       *priv)
{
    g_assert(priv->can_allocate_directly);

    priv->gboxed = g_slice_alloc0(g_struct_info_get_size (priv->info));
    priv->allocated_directly = TRUE;

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "JSObject created by directly allocating %s",
                        g_base_info_get_name ((GIBaseInfo *)priv->info));
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
        char *name;
        jsval value;

        if (!gjs_get_string_id(context, prop_id, &name))
            goto out;

        field_info = (GIFieldInfo *) g_hash_table_lookup(field_map, name);
        if (field_info == NULL) {
            gjs_throw(context, "No field %s on boxed type %s",
                      name, g_base_info_get_name((GIBaseInfo *)priv->info));
            g_free(name);
            goto out;
        }

        if (!gjs_object_require_property(context, props, "property list", prop_id, &value)) {
            g_free(name);
            goto out;
        }
        g_free(name);

        if (!boxed_set_field_from_value(context, priv, field_info, value))
            goto out;

        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, iter, &prop_id))
            goto out;
    }

    success = TRUE;

 out:
    g_hash_table_destroy(field_map);

    return success;
}

static JSBool
boxed_invoke_constructor(JSContext   *context,
                         JSObject    *obj,
                         jsid         constructor_name,
                         unsigned     argc,
                         jsval       *argv,
                         jsval       *rval)
{
    jsval js_constructor, js_constructor_func;
    jsid constructor_const;

    constructor_const = gjs_context_get_const_string(context, GJS_STRING_CONSTRUCTOR);
    if (!gjs_object_require_property(context, obj, NULL, constructor_const, &js_constructor))
        return JS_FALSE;

    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(js_constructor), NULL,
                                     constructor_name, &js_constructor_func))
        return JS_FALSE;

    return gjs_call_function_value(context, NULL, js_constructor_func, argc, argv, rval);
}

static JSBool
boxed_new(JSContext   *context,
          JSObject    *obj, /* "this" for constructor */
          Boxed       *priv,
          unsigned     argc,
          jsval       *argv,
          jsval       *rval)
{
    if (priv->gtype == G_TYPE_VARIANT) {
        jsid constructor_name;

        /* Short-circuit construction for GVariants by calling into the JS packing
           function */
        constructor_name = gjs_context_get_const_string(context, GJS_STRING_NEW_INTERNAL);
        return boxed_invoke_constructor (context, obj, constructor_name, argc, argv, rval);
    }

    /* If the structure is registered as a boxed, we can create a new instance by
     * looking for a zero-args constructor and calling it.
     * Constructors don't really make sense for non-boxed types, since there is no
     * memory management for the return value, and zero_args_constructor and
     * default_constructor are always -1 for them.
     *
     * For backward compatibility, we choose the zero args constructor if one
     * exists, otherwise we choose the internal slice allocator if possible;
     * finally, we fallback on the default constructor */
    if (priv->zero_args_constructor >= 0) {
        GIFunctionInfo *func_info = g_struct_info_get_method (priv->info, priv->zero_args_constructor);

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
                            priv->gboxed, g_type_name(priv->gtype));

    } else if (priv->can_allocate_directly) {
        boxed_new_direct(priv);
    } else if (priv->default_constructor >= 0) {
        JSBool retval;

        /* for simplicity, we simply delegate all the work to the actual JS constructor
           function (which we retrieve from the JS constructor, that is, Namespace.BoxedType,
           or object.constructor, given that object was created with the right prototype */
        retval = boxed_invoke_constructor(context, obj, priv->default_constructor_name, argc, argv, rval);
        return retval;
    } else {
        gjs_throw(context, "Unable to construct struct type %s since it has no default constructor and cannot be allocated directly",
                  g_base_info_get_name((GIBaseInfo*) priv->info));
        return JS_FALSE;
    }

    /* If we reach this code, we need to init from a map of fields */

    if (argc == 0)
        return JS_TRUE;

    if (argc > 1) {
        gjs_throw(context, "Constructor with multiple arguments not supported for %s",
                  g_base_info_get_name((GIBaseInfo *)priv->info));
        return JS_FALSE;
    }

    return boxed_init_from_props (context, obj, priv, argv[0]);
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(boxed)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(boxed)
    Boxed *priv;
    Boxed *proto_priv;
    JSObject *proto;
    Boxed *source_priv;
    jsval actual_rval;
    JSBool retval;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(boxed);

    priv = g_slice_new0(Boxed);

    GJS_INC_COUNTER(boxed);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "boxed constructor, obj %p priv %p",
                        object, priv);

    JS_GetPrototype(context, object, &proto);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED, "boxed instance __proto__ is %p", proto);
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

    *priv = *proto_priv;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    /* Short-circuit copy-construction in the case where we can use g_boxed_copy or memcpy */
    if (argc == 1 &&
        boxed_get_copy_source(context, priv, argv[0], &source_priv)) {

        if (g_type_is_a (priv->gtype, G_TYPE_BOXED)) {
            priv->gboxed = g_boxed_copy(priv->gtype, source_priv->gboxed);

            GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
            return JS_TRUE;
        } else if (priv->can_allocate_directly) {
            boxed_new_direct (priv);
            memcpy(priv->gboxed, source_priv->gboxed,
                   g_struct_info_get_size (priv->info));

            GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
            return JS_TRUE;
        }
    }

    /* we may need to return a value different from object
       (for example because we delegate to another constructor)
       prepare the location for that
    */

    actual_rval = JSVAL_VOID;
    JS_AddValueRoot(context, &actual_rval);

    retval = boxed_new(context, object, priv, argc, argv, &actual_rval);

    if (retval) {
        if (!JSVAL_IS_VOID (actual_rval))
            JS_SET_RVAL(context, vp, actual_rval);
        else
            GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
    }

    JS_RemoveValueRoot(context, &actual_rval);

    return retval;
}

static void
boxed_finalize(JSFreeOp *fop,
               JSObject *obj)
{
    Boxed *priv;

    priv = (Boxed *) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* wrong class? */

    if (priv->gboxed && !priv->not_owning_gboxed) {
        if (priv->allocated_directly) {
            g_slice_free1(g_struct_info_get_size (priv->info), priv->gboxed);
        } else {
            if (g_type_is_a (priv->gtype, G_TYPE_BOXED))
                g_boxed_free (priv->gtype,  priv->gboxed);
            else if (g_type_is_a (priv->gtype, G_TYPE_VARIANT))
                g_variant_unref ((GVariant *) priv->gboxed);
            else
                g_assert_not_reached ();
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
    Boxed *priv;
    Boxed *proto_priv;

    if (!struct_is_simple ((GIStructInfo *)interface_info)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)parent_priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));

        return JS_FALSE;
    }

    proto = gjs_lookup_generic_prototype(context, (GIBoxedInfo*) interface_info);
    proto_priv = priv_from_js(context, proto);

    offset = g_field_info_get_offset (field_info);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    if (obj == NULL)
        return JS_FALSE;

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);
    JS_SetPrivate(obj, priv);
    priv->info = (GIBoxedInfo*) interface_info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo*) interface_info);
    priv->can_allocate_directly = proto_priv->can_allocate_directly;

    /* A structure nested inside a parent object; doesn't have an independent allocation */
    priv->gboxed = ((char *)parent_priv->gboxed) + offset;
    priv->not_owning_gboxed = TRUE;

    /* We never actually read the reserved slot, but we put the parent object
     * into it to hold onto the parent object.
     */
    JS_SetReservedSlot(obj, 0,
                       OBJECT_TO_JSVAL (parent_obj));

    *value = OBJECT_TO_JSVAL(obj);
    return JS_TRUE;
}

static JSBool
boxed_field_getter (JSContext              *context,
                    JS::HandleObject        obj,
                    JS::HandleId            id,
                    JS::MutableHandleValue  value)
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
                                                   value.address());

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

    if (!gjs_value_from_g_argument (context, value.address(),
                                    type_info,
                                    &arg,
                                    TRUE))
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

    proto = gjs_lookup_generic_prototype(context, (GIBoxedInfo*) interface_info);
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
boxed_field_setter (JSContext              *context,
                    JS::HandleObject        obj,
                    JS::HandleId            id,
                    JSBool                  strict,
                    JS::MutableHandleValue  value)
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

    success = boxed_set_field_from_value (context, priv, field_info, value);

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
        g_warning("Only defining the first 256 fields in boxed type '%s'",
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

static JSBool
to_string_func(JSContext *context,
               unsigned   argc,
               jsval     *vp)
{
    JSObject *obj = JS_THIS_OBJECT(context, vp);
    Boxed *priv;
    JSBool ret = JS_FALSE;
    jsval retval;

    if (!priv_from_js_with_typecheck(context, obj, &priv))
        goto out;
    
    if (!_gjs_proxy_to_string_func(context, obj, "boxed", (GIBaseInfo*)priv->info,
                                   priv->gtype, priv->gboxed, &retval))
        goto out;

    ret = JS_TRUE;
    JS_SET_RVAL(context, vp, retval);
 out:
    return ret;
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * We allocate 1 reserved slot; this is typically unused, but if the
 * boxed is for a nested structure inside a parent structure, the
 * reserved slot is used to hold onto the parent Javascript object and
 * make sure it doesn't get freed.
 */
struct JSClass gjs_boxed_class = {
    "GObject_Boxed",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_HAS_RESERVED_SLOTS(1),
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) boxed_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    boxed_finalize,
    NULL,
    NULL,
    NULL, NULL, NULL
};

JSPropertySpec gjs_boxed_proto_props[] = {
    { NULL }
};

JSFunctionSpec gjs_boxed_proto_funcs[] = {
    { "toString", JSOP_WRAPPER((JSNative)to_string_func), 0, 0 },
    { NULL }
};

static gboolean
type_can_be_allocated_directly(GITypeInfo *type_info)
{
    gboolean is_simple = TRUE;

    if (g_type_info_is_pointer(type_info)) {
        if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_ARRAY &&
            g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            GITypeInfo *param_info;

            param_info = g_type_info_get_param_type(type_info, 0);
            is_simple = type_can_be_allocated_directly(param_info);

            g_base_info_unref((GIBaseInfo*)param_info);
        } else {
            is_simple = FALSE;
        }
    } else {
        switch (g_type_info_get_tag(type_info)) {
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
        case GI_TYPE_TAG_UNICHAR:
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
                GIBaseInfo *interface = g_type_info_get_interface(type_info);
                switch (g_base_info_get_type(interface)) {
                case GI_INFO_TYPE_BOXED:
                case GI_INFO_TYPE_STRUCT:
                    if (!struct_is_simple((GIStructInfo *)interface))
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
                case GI_INFO_TYPE_VALUE:
                case GI_INFO_TYPE_SIGNAL:
                case GI_INFO_TYPE_PROPERTY:
                case GI_INFO_TYPE_FIELD:
                case GI_INFO_TYPE_ARG:
                case GI_INFO_TYPE_TYPE:
                case GI_INFO_TYPE_UNRESOLVED:
                    is_simple = FALSE;
                    break;
                case GI_INFO_TYPE_INVALID_0:
                    g_assert_not_reached();
                    break;
                }

                g_base_info_unref(interface);
                break;
            }
        }
    }
    return is_simple;
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
static gboolean
struct_is_simple(GIStructInfo *info)
{
    int n_fields = g_struct_info_get_n_fields(info);
    gboolean is_simple = TRUE;
    int i;

    /* If it's opaque, it's not simple */
    if (n_fields == 0)
        return FALSE;

    for (i = 0; i < n_fields && is_simple; i++) {
        GIFieldInfo *field_info = g_struct_info_get_field(info, i);
        GITypeInfo *type_info = g_field_info_get_type(field_info);

        is_simple = type_can_be_allocated_directly(type_info);

        g_base_info_unref((GIBaseInfo *)field_info);
        g_base_info_unref((GIBaseInfo *)type_info);
    }

    return is_simple;
}

static void
boxed_fill_prototype_info(JSContext *context,
                          Boxed     *priv)
{
    int i, n_methods;
    int first_constructor = -1;
    jsid first_constructor_name = JSID_VOID;

    priv->gtype = g_registered_type_info_get_g_type( (GIRegisteredTypeInfo*) priv->info);
    priv->zero_args_constructor = -1;
    priv->default_constructor = -1;

    if (priv->gtype != G_TYPE_NONE) {
        /* If the structure is registered as a boxed, we can create a new instance by
         * looking for a zero-args constructor and calling it; constructors don't
         * really make sense for non-boxed types, since there is no memory management
         * for the return value.
         */
        n_methods = g_struct_info_get_n_methods(priv->info);

        for (i = 0; i < n_methods; ++i) {
            GIFunctionInfo *func_info;
            GIFunctionInfoFlags flags;

            func_info = g_struct_info_get_method(priv->info, i);

            flags = g_function_info_get_flags(func_info);
            if ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0) {
                if (first_constructor < 0) {
                    const char *name;

                    name = g_base_info_get_name((GIBaseInfo*) func_info);
                    first_constructor = i;
                    first_constructor_name = gjs_intern_string_to_id(context, name);
                }

                if (priv->zero_args_constructor < 0 &&
                    g_callable_info_get_n_args((GICallableInfo*) func_info) == 0) {
                    const char *name;

                    name = g_base_info_get_name((GIBaseInfo*) func_info);
                    priv->zero_args_constructor = i;
                    priv->zero_args_constructor_name = gjs_intern_string_to_id(context, name);
                }

                if (priv->default_constructor < 0 &&
                    strcmp(g_base_info_get_name ((GIBaseInfo*) func_info), "new") == 0) {
                    priv->default_constructor = i;
                    priv->default_constructor_name = gjs_context_get_const_string(context, GJS_STRING_NEW);
                }
            }

            g_base_info_unref((GIBaseInfo*) func_info);
        }

        if (priv->default_constructor < 0) {
            priv->default_constructor = priv->zero_args_constructor;
            priv->default_constructor_name = priv->zero_args_constructor_name;
        }
        if (priv->default_constructor < 0) {
            priv->default_constructor = first_constructor;
            priv->default_constructor_name = first_constructor_name;
        }
    }
}

void
gjs_define_boxed_class(JSContext    *context,
                       JSObject     *in_object,
                       GIBoxedInfo  *info)
{
    const char *constructor_name;
    JSObject *prototype;
    JSObject *constructor;
    jsval value;
    Boxed *priv;

    /* See the comment in gjs_define_object_class() for an
     * explanation of how this all works; Boxed is pretty much the
     * same as Object.
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    if (!gjs_init_class_dynamic(context, in_object,
                                NULL, /* parent prototype */
                                g_base_info_get_namespace( (GIBaseInfo*) info),
                                constructor_name,
                                &gjs_boxed_class,
                                gjs_boxed_constructor, 1,
                                /* props of prototype */
                                &gjs_boxed_proto_props[0],
                                /* funcs of prototype */
                                &gjs_boxed_proto_funcs[0],
                                /* props of constructor, MyConstructor.myprop */
                                NULL,
                                /* funcs of constructor, MyConstructor.myfunc() */
                                NULL,
                                &prototype,
                                &constructor)) {
        gjs_log_exception(context);
        g_error("Can't init class %s", constructor_name);
    }

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);
    priv->info = info;
    boxed_fill_prototype_info(context, priv);

    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo*) priv->info);
    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype, JS_GetClass(prototype), in_object);

    priv->can_allocate_directly = struct_is_simple (priv->info);

    define_boxed_class_fields (context, priv, prototype);
    gjs_define_static_methods (context, constructor, priv->gtype, priv->info);

    value = OBJECT_TO_JSVAL(gjs_gtype_create_gtype_wrapper(context, priv->gtype));
    JS_DefineProperty(context, constructor, "$gtype", value,
                      NULL, NULL, JSPROP_PERMANENT);
}

JSObject*
gjs_boxed_from_c_struct(JSContext             *context,
                        GIStructInfo          *info,
                        void                  *gboxed,
                        GjsBoxedCreationFlags  flags)
{
    JSObject *obj;
    JSObject *proto;
    Boxed *priv;
    Boxed *proto_priv;

    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    proto = gjs_lookup_generic_prototype(context, info);
    proto_priv = priv_from_js(context, proto);

    obj = JS_NewObjectWithGivenProto(context,
                                     JS_GetClass(proto), proto,
                                     gjs_get_import_global (context));

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);

    *priv = *proto_priv;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    JS_SetPrivate(obj, priv);

    if ((flags & GJS_BOXED_CREATION_NO_COPY) != 0) {
        /* we need to create a JS Boxed which references the
         * original C struct, not a copy of it. Used for
         * G_SIGNAL_TYPE_STATIC_SCOPE
         */
        priv->gboxed = gboxed;
        priv->not_owning_gboxed = TRUE;
    } else {
        if (priv->gtype != G_TYPE_NONE && g_type_is_a (priv->gtype, G_TYPE_BOXED)) {
            priv->gboxed = g_boxed_copy(priv->gtype, gboxed);
        } else if (priv->gtype == G_TYPE_VARIANT) {
            priv->gboxed = g_variant_ref_sink ((GVariant *) gboxed);
        } else if (priv->can_allocate_directly) {
            boxed_new_direct(priv);
            memcpy(priv->gboxed, gboxed, g_struct_info_get_size (priv->info));
        } else {
            gjs_throw(context,
                      "Can't create a Javascript object for %s; no way to copy",
                      g_base_info_get_name( (GIBaseInfo*) priv->info));
        }
    }

    return obj;
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

    return priv->gboxed;
}

JSBool
gjs_typecheck_boxed(JSContext     *context,
                    JSObject      *object,
                    GIStructInfo  *expected_info,
                    GType          expected_type,
                    JSBool         throw_error)
{
    Boxed *priv;
    JSBool result;

    if (!do_base_typecheck(context, object, throw_error))
        return JS_FALSE;

    priv = priv_from_js(context, object);

    if (priv->gboxed == NULL) {
        if (throw_error) {
            gjs_throw_custom(context, "TypeError",
                             "Object is %s.%s.prototype, not an object instance - cannot convert to a boxed instance",
                             g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                             g_base_info_get_name( (GIBaseInfo*) priv->info));
        }

        return JS_FALSE;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (priv->gtype, expected_type);
    else if (expected_info != NULL)
        result = g_base_info_equal((GIBaseInfo*) priv->info, (GIBaseInfo*) expected_info);
    else
        result = JS_TRUE;

    if (!result && throw_error) {
        if (expected_info != NULL) {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s.%s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_base_info_get_namespace((GIBaseInfo*) expected_info),
                             g_base_info_get_name((GIBaseInfo*) expected_info));
        } else {
            gjs_throw_custom(context, "TypeError",
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_type_name(expected_type));
        }
    }

    return result;
}
