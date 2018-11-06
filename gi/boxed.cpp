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

#include <string.h>

#include "gjs/jsapi-wrapper.h"
#include "js/GCHashTable.h"

#include "arg.h"
#include "boxed.h"
#include "function.h"
#include "gi/gerror.h"
#include "gjs/jsapi-class.h"
#include "gjs/mem.h"
#include "gtype.h"
#include "object.h"
#include "proxyutils.h"
#include "repo.h"

#include <util/log.h>

#include <girepository.h>

struct Boxed {
    using FieldMap =
        JS::GCHashMap<JS::Heap<JSString*>, GjsAutoFieldInfo,
                      js::DefaultHasher<JSString*>, js::SystemAllocPolicy>;

    /* prototype info */
    GIStructInfo* info;
    GType gtype;
    gint zero_args_constructor; /* -1 if none */
    JS::Heap<jsid> zero_args_constructor_name;
    gint default_constructor; /* -1 if none */
    JS::Heap<jsid> default_constructor_name;

    /* instance info */
    void *gboxed; /* NULL if we are the prototype and not an instance */
    FieldMap* field_map;

    guint can_allocate_directly : 1;
    guint allocated_directly : 1;
    guint not_owning_gboxed : 1; /* if set, the JS wrapper does not own
                                    the reference to the C gboxed */
};

GJS_USE
static bool struct_is_simple(GIStructInfo *info);

GJS_JSAPI_RETURN_CONVENTION
static bool boxed_set_field_from_value(JSContext      *context,
                                       Boxed          *priv,
                                       GIFieldInfo    *field_info,
                                       JS::HandleValue value);

extern struct JSClass gjs_boxed_class;

GJS_DEFINE_PRIV_FROM_JS(Boxed, gjs_boxed_class)

GJS_JSAPI_RETURN_CONVENTION
static bool
gjs_define_static_methods(JSContext       *context,
                          JS::HandleObject constructor,
                          GType            gtype,
                          GIStructInfo    *boxed_info)
{
    int i;
    int n_methods;

    n_methods = g_struct_info_get_n_methods(boxed_info);

    for (i = 0; i < n_methods; i++) {
        GIFunctionInfoFlags flags;

        GjsAutoFunctionInfo meth_info = g_struct_info_get_method(boxed_info, i);
        flags = g_function_info_get_flags (meth_info);

        /* Anything that isn't a method we put on the prototype of the
         * constructor.  This includes <constructor> introspection
         * methods, as well as the forthcoming "static methods"
         * support.  We may want to change this to use
         * GI_FUNCTION_IS_CONSTRUCTOR and GI_FUNCTION_IS_STATIC or the
         * like in the near future.
         */
        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            if (!gjs_define_function(context, constructor, gtype, meth_info))
                return false;
        }
    }
    return true;
}

/* The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved. */
GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_resolve(JSContext       *context,
              JS::HandleObject obj,
              JS::HandleId     id,
              bool            *resolved)
{
    Boxed *priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GBOXED, "Resolve prop '%s' hook, obj %s, priv %p",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str(),
                     priv);

    if (priv == nullptr)
        return false; /* wrong class */

    if (priv->gboxed) {
        /* We are an instance, not a prototype, so look for
         * per-instance props that we want to define on the
         * JSObject. Generally we do not want to cache these in JS, we
         * want to always pull them from the C object, or JS would not
         * see any changes made from C. So we use the get/set prop
         * hooks, not this resolve hook.
         */
        *resolved = false;
        return true;
    }

    JS::UniqueChars name;
    if (!gjs_get_string_id(context, id, &name))
        return false;
    if (!name) {
        *resolved = false;
        return true;
    }

    /* We are the prototype, so look for methods and other class properties */
    GjsAutoFunctionInfo method_info =
        g_struct_info_find_method(priv->info, name.get());
    if (!method_info) {
        *resolved = false;
        return true;
    }
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(method_info);
#endif

    if (g_function_info_get_flags(method_info) & GI_FUNCTION_IS_METHOD) {
        gjs_debug(GJS_DEBUG_GBOXED,
                  "Defining method %s in prototype for %s.%s",
                  method_info.name(),
                  g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                  g_base_info_get_name( (GIBaseInfo*) priv->info));

        /* obj is the Boxed prototype */
        if (!gjs_define_function(context, obj, priv->gtype, method_info))
            return false;

        *resolved = true;
    } else {
        *resolved = false;
    }

    return true;
}

/* Check to see if JS::Value passed in is another Boxed object of the same,
 * and if so, retrieves the Boxed private structure for it.
 */
GJS_USE
static bool
boxed_get_copy_source(JSContext *context,
                      Boxed     *priv,
                      JS::Value  value,
                      Boxed    **source_priv_out)
{
    Boxed *source_priv;

    if (!value.isObject())
        return false;

    JS::RootedObject object(context, &value.toObject());
    if (!priv_from_js_with_typecheck(context, object, &source_priv))
        return false;

    if (!g_base_info_equal((GIBaseInfo*) priv->info, (GIBaseInfo*) source_priv->info))
        return false;

    *source_priv_out = source_priv;

    return true;
}

static void
boxed_new_direct(Boxed       *priv)
{
    g_assert(priv->can_allocate_directly);

    priv->gboxed = g_slice_alloc0(g_struct_info_get_size (priv->info));
    priv->allocated_directly = true;

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "JSObject created by directly allocating %s",
                        g_base_info_get_name ((GIBaseInfo *)priv->info));
}

/* When initializing a boxed object from a hash of properties, we don't want
 * to do n O(n) lookups, so put put the fields into a hash table and store it on proto->priv
 * for fast lookup. 
 */
GJS_JSAPI_RETURN_CONVENTION
static Boxed::FieldMap* get_field_map(JSContext* cx,
                                      GIStructInfo* struct_info) {
    int n_fields;
    int i;

    auto* result = new Boxed::FieldMap();
    n_fields = g_struct_info_get_n_fields(struct_info);
    if (!result->init(n_fields)) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }

    for (i = 0; i < n_fields; i++) {
        GjsAutoFieldInfo field_info = g_struct_info_get_field(struct_info, i);

        // We get the string as a jsid later, which is interned. We intern the
        // string here as well, so it will be the same string pointer
        JS::RootedString name(cx, JS_NewStringCopyZ(cx, field_info.name()));
        JSString* atom = JS_AtomizeAndPinJSString(cx, name);

        result->putNewInfallible(atom, std::move(field_info));
    }

    return result;
}

/* Initialize a newly created Boxed from an object that is a "hash" of
 * properties to set as fieds of the object. We don't require that every field
 * of the object be set.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_init_from_props(JSContext   *context,
                      JSObject    *obj,
                      Boxed       *priv,
                      JS::Value    props_value)
{
    size_t ix, length;

    if (!props_value.isObject()) {
        gjs_throw(context, "argument should be a hash with fields to set");
        return false;
    }

    JS::RootedObject props(context, &props_value.toObject());
    JS::Rooted<JS::IdVector> ids(context, context);
    if (!JS_Enumerate(context, props, &ids)) {
        gjs_throw(context, "Failed to enumerate fields hash");
        return false;
    }

    if (!priv->field_map)
        priv->field_map = get_field_map(context, priv->info);
    if (!priv->field_map)
        return false;

    JS::RootedValue value(context);
    for (ix = 0, length = ids.length(); ix < length; ix++) {
        if (!JSID_IS_STRING(ids[ix])) {
            gjs_throw(context, "Fields hash contained a non-string field");
            return false;
        }

        auto entry = priv->field_map->lookup(JSID_TO_STRING(ids[ix]));
        if (!entry) {
            gjs_throw(context, "No field %s on boxed type %s",
                      gjs_debug_id(ids[ix]).c_str(),
                      g_base_info_get_name(priv->info));
            return false;
        }
        GIFieldInfo* field_info = entry->value().get();

        /* ids[ix] is reachable because props is rooted, but require_property
         * doesn't know that */
        if (!gjs_object_require_property(context, props, "property list",
                                         JS::HandleId::fromMarkedLocation(ids[ix].address()),
                                         &value))
            return false;

        if (!boxed_set_field_from_value(context, priv, field_info, value))
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_invoke_constructor(JSContext             *context,
                         JS::HandleObject       obj,
                         JS::HandleId           constructor_name,
                         JS::CallArgs&          args)
{
    JS::RootedObject js_constructor(context);

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (!gjs_object_require_property(context, obj, NULL, atoms.constructor(),
                                     &js_constructor))
        return false;

    JS::RootedValue js_constructor_func(context);
    if (!gjs_object_require_property(context, js_constructor, NULL,
                                     constructor_name, &js_constructor_func))
        return false;

    return gjs_call_function_value(context, nullptr, js_constructor_func,
                                   args, args.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_new(JSContext             *context,
          JS::HandleObject       obj, /* "this" for constructor */
          Boxed                 *priv,
          JS::CallArgs&          args)
{
    if (priv->gtype == G_TYPE_VARIANT) {
        /* Short-circuit construction for GVariants by calling into the JS packing
           function */
        const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
        return boxed_invoke_constructor(context, obj, atoms.new_internal(),
                                        args);
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

        GIArgument rval_arg;
        GError *error = NULL;

        if (!g_function_info_invoke(func_info, NULL, 0, NULL, 0, &rval_arg, &error)) {
            gjs_throw(context, "Failed to invoke boxed constructor: %s", error->message);
            g_clear_error(&error);
            g_base_info_unref((GIBaseInfo*) func_info);
            return false;
        }

        g_base_info_unref((GIBaseInfo*) func_info);

        priv->gboxed = rval_arg.v_pointer;

        gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                            "JSObject created with boxed instance %p type %s",
                            priv->gboxed, g_type_name(priv->gtype));

    } else if (priv->can_allocate_directly) {
        boxed_new_direct(priv);
    } else if (priv->default_constructor >= 0) {
        /* for simplicity, we simply delegate all the work to the actual JS
         * constructor function (which we retrieve from the JS constructor,
         * that is, Namespace.BoxedType, or object.constructor, given that
         * object was created with the right prototype. The ID is traced from
         * the object, so it's OK to create a handle from it. */
        JS::HandleId constructor_name = JS::HandleId::fromMarkedLocation(
            priv->default_constructor_name.address());
        if (!boxed_invoke_constructor(context, obj, constructor_name, args))
            return false;

        // Define the expected Error properties
        if (priv->gtype == G_TYPE_ERROR) {
            JS::RootedObject gerror(context, &args.rval().toObject());
            if (!gjs_define_error_properties(context, gerror))
                return false;
        }

        return true;
    } else {
        gjs_throw(context, "Unable to construct struct type %s since it has no default constructor and cannot be allocated directly",
                  g_base_info_get_name((GIBaseInfo*) priv->info));
        return false;
    }

    /* If we reach this code, we need to init from a map of fields */

    if (args.length() == 0)
        return true;

    if (args.length() > 1) {
        gjs_throw(context, "Constructor with multiple arguments not supported for %s",
                  g_base_info_get_name((GIBaseInfo *)priv->info));
        return false;
    }

    return boxed_init_from_props(context, obj, priv, args[0]);
}

GJS_NATIVE_CONSTRUCTOR_DECLARE(boxed)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(boxed)
    Boxed *priv;
    Boxed *proto_priv;
    JS::RootedObject proto(context);
    Boxed *source_priv;
    bool retval;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(boxed);

    priv = g_slice_new0(Boxed);
    new (priv) Boxed();

    GJS_INC_COUNTER(boxed);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GBOXED,
                        "boxed constructor, obj %p priv %p",
                        object.get(), priv);

    JS_GetPrototype(context, object, &proto);
    gjs_debug_lifecycle(GJS_DEBUG_GBOXED, "boxed instance __proto__ is %p",
                        proto.get());
    /* If we're the prototype, then post-construct we'll fill in priv->info.
     * If we are not the prototype, though, then we'll get ->info from the
     * prototype and then create a GObject if we don't have one already.
     */
    proto_priv = priv_from_js(context, proto);
    if (proto_priv == NULL) {
        gjs_debug(GJS_DEBUG_GBOXED,
                  "Bad prototype set on boxed? Must match JSClass of object. JS error should have been reported.");
        return false;
    }

    *priv = *proto_priv;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    /* Short-circuit copy-construction in the case where we can use g_boxed_copy or memcpy */
    if (argc == 1 &&
        boxed_get_copy_source(context, priv, argv[0], &source_priv)) {

        if (g_type_is_a (priv->gtype, G_TYPE_BOXED)) {
            priv->gboxed = g_boxed_copy(priv->gtype, source_priv->gboxed);

            GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
            return true;
        } else if (priv->can_allocate_directly) {
            boxed_new_direct (priv);
            memcpy(priv->gboxed, source_priv->gboxed,
                   g_struct_info_get_size (priv->info));

            GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);
            return true;
        }
    }

    /* we may need to return a value different from object
       (for example because we delegate to another constructor)
    */

    argv.rval().setUndefined();
    retval = boxed_new(context, object, priv, argv);

    if (argv.rval().isUndefined())
        GJS_NATIVE_CONSTRUCTOR_FINISH(boxed);

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

    if (priv->field_map)
        delete priv->field_map;

    GJS_DEC_COUNTER(boxed);
    priv->~Boxed();
    g_slice_free(Boxed, priv);
}

GJS_JSAPI_RETURN_CONVENTION
static GIFieldInfo *
get_field_info(JSContext *cx,
               Boxed     *priv,
               uint32_t   id)
{
    GIFieldInfo *field_info = g_struct_info_get_field(priv->info, id);
    if (field_info == NULL) {
        gjs_throw(cx, "No field %d on boxed type %s",
                  id, g_base_info_get_name((GIBaseInfo *)priv->info));
        return NULL;
    }

    return field_info;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
get_nested_interface_object(JSContext             *context,
                            JSObject              *parent_obj,
                            Boxed                 *parent_priv,
                            GIFieldInfo           *field_info,
                            GITypeInfo            *type_info,
                            GIBaseInfo            *interface_info,
                            JS::MutableHandleValue value)
{
    JSObject *obj;
    int offset;
    Boxed *priv;
    Boxed *proto_priv;

    if (!struct_is_simple ((GIStructInfo *)interface_info)) {
        gjs_throw(context, "Reading field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)parent_priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));

        return false;
    }

    JS::RootedObject proto(
        context, gjs_lookup_generic_prototype(context, interface_info));

    if (!proto)
        return false;
    proto_priv = priv_from_js(context, proto);

    offset = g_field_info_get_offset (field_info);

    obj = JS_NewObjectWithGivenProto(context, JS_GetClass(proto), proto);

    if (!obj)
        return false;

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);
    new (priv) Boxed();
    JS_SetPrivate(obj, priv);
    priv->info = interface_info;
    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo*) interface_info);
    priv->can_allocate_directly = proto_priv->can_allocate_directly;

    /* A structure nested inside a parent object; doesn't have an independent allocation */
    priv->gboxed = ((char *)parent_priv->gboxed) + offset;
    priv->not_owning_gboxed = true;

    /* We never actually read the reserved slot, but we put the parent object
     * into it to hold onto the parent object.
     */
    JS_SetReservedSlot(obj, 0, JS::ObjectValue(*parent_obj));

    value.setObject(*obj);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_field_getter(JSContext *context,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, obj, Boxed, priv);
    GIFieldInfo *field_info;
    GITypeInfo *type_info;
    GArgument arg;
    bool success = false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    field_info = get_field_info(context, priv, field_ix);
    if (!field_info)
        return false;

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
                                                   args.rval());

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

    if (!gjs_value_from_g_argument(context, args.rval(), type_info,
                                   &arg, true))
        goto out;

    success = true;

out:
    g_base_info_unref ((GIBaseInfo *)field_info);
    g_base_info_unref ((GIBaseInfo *)type_info);

    return success;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
set_nested_interface_object (JSContext      *context,
                             Boxed          *parent_priv,
                             GIFieldInfo    *field_info,
                             GITypeInfo     *type_info,
                             GIBaseInfo     *interface_info,
                             JS::HandleValue value)
{
    int offset;
    Boxed *proto_priv;
    Boxed *source_priv;

    if (!struct_is_simple ((GIStructInfo *)interface_info)) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)parent_priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));

        return false;
    }

    JS::RootedObject proto(
        context, gjs_lookup_generic_prototype(context, interface_info));

    if (!proto)
        return false;
    proto_priv = priv_from_js(context, proto);

    /* If we can't directly copy from the source object we need
     * to construct a new temporary object.
     */
    if (!boxed_get_copy_source(context, proto_priv, value, &source_priv)) {
        JS::AutoValueArray<1> args(context);
        args[0].set(value);
        JS::RootedObject tmp_object(context,
            gjs_construct_object_dynamic(context, proto, args));
        if (!tmp_object)
            return false;

        source_priv = priv_from_js(context, tmp_object);
        if (!source_priv)
            return false;
    }

    offset = g_field_info_get_offset (field_info);
    memcpy(((char *)parent_priv->gboxed) + offset,
           source_priv->gboxed,
           g_struct_info_get_size (source_priv->info));

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_set_field_from_value(JSContext      *context,
                           Boxed          *priv,
                           GIFieldInfo    *field_info,
                           JS::HandleValue value)
{
    GITypeInfo *type_info;
    GArgument arg;
    bool success = false;
    bool need_release = false;

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
                                 true, &arg))
        goto out;

    need_release = true;

    if (!g_field_info_set_field (field_info, priv->gboxed, &arg)) {
        gjs_throw(context, "Writing field %s.%s is not supported",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    success = true;

out:
    if (need_release) {
        JS::AutoSaveExceptionState saved_exc(context);
        if (!gjs_g_argument_release(context, GI_TRANSFER_NOTHING, type_info,
                                    &arg))
            gjs_log_exception(context);
        saved_exc.restore();
    }

    g_base_info_unref ((GIBaseInfo *)type_info);

    return success;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
boxed_field_setter(JSContext *cx,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, args, obj, Boxed, priv);
    GIFieldInfo *field_info;
    bool success = false;

    uint32_t field_ix = gjs_dynamic_property_private_slot(&args.callee())
        .toPrivateUint32();
    field_info = get_field_info(cx, priv, field_ix);
    if (!field_info)
        return false;

    if (priv->gboxed == NULL) { /* direct access to proto field */
        gjs_throw(cx, "Can't set field %s.%s on prototype",
                  g_base_info_get_name ((GIBaseInfo *)priv->info),
                  g_base_info_get_name ((GIBaseInfo *)field_info));
        goto out;
    }

    if (!boxed_set_field_from_value(cx, priv, field_info, args[0]))
        goto out;

    args.rval().setUndefined();  /* No stored value */
    success = true;

out:
    g_base_info_unref ((GIBaseInfo *)field_info);

    return success;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
define_boxed_class_fields(JSContext       *cx,
                          Boxed           *priv,
                          JS::HandleObject proto)
{
    int n_fields = g_struct_info_get_n_fields (priv->info);
    int i;

    /* We define all fields as read/write so that the user gets an
     * error message. If we omitted fields or defined them read-only
     * we'd:
     *
     *  - Store a new property for a non-accessible field
     *  - Silently do nothing when writing a read-only field
     *
     * Which is pretty confusing if the only reason a field isn't
     * writable is language binding or memory-management restrictions.
     *
     * We just go ahead and define the fields immediately for the
     * class; doing it lazily in boxed_resolve() would be possible
     * as well if doing it ahead of time caused to much start-up
     * memory overhead.
     */
    for (i = 0; i < n_fields; i++) {
        GIFieldInfo *field = g_struct_info_get_field (priv->info, i);
        const char *field_name = g_base_info_get_name ((GIBaseInfo *)field);

        JS::RootedValue private_id(cx, JS::PrivateUint32Value(i));
        bool ok = gjs_define_property_dynamic(cx, proto, field_name,
                                              "boxed_field", boxed_field_getter,
                                              boxed_field_setter, private_id,
                                              GJS_MODULE_PROP_FLAGS);
        g_base_info_unref(field);
        if (!ok)
            return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
to_string_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, obj, Boxed, priv);
    return _gjs_proxy_to_string_func(context, obj, "boxed",
                                     (GIBaseInfo*)priv->info, priv->gtype,
                                     priv->gboxed, rec.rval());
}

static void
boxed_trace(JSTracer *tracer,
            JSObject *obj)
{
    Boxed *priv = reinterpret_cast<Boxed *>(JS_GetPrivate(obj));
    if (priv == NULL)
        return;

    JS::TraceEdge<jsid>(tracer, &priv->zero_args_constructor_name,
                        "Boxed::zero_args_constructor_name");
    JS::TraceEdge<jsid>(tracer, &priv->default_constructor_name,
                        "Boxed::default_constructor_name");
    if (priv->field_map)
        priv->field_map->trace(tracer);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_boxed_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    boxed_resolve,
    nullptr,  // mayResolve
    boxed_finalize,
    nullptr,  // call
    nullptr,  // hasInstance
    nullptr,  // construct
    boxed_trace};

/* We allocate 1 reserved slot; this is typically unused, but if the
 * boxed is for a nested structure inside a parent structure, the
 * reserved slot is used to hold onto the parent Javascript object and
 * make sure it doesn't get freed.
 */
struct JSClass gjs_boxed_class = {
    "GObject_Boxed",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE |
        JSCLASS_HAS_RESERVED_SLOTS(1),
    &gjs_boxed_class_ops
};

JSPropertySpec gjs_boxed_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_boxed_proto_funcs[] = {
    JS_FN("toString", to_string_func, 0, 0),
    JS_FS_END};

GJS_USE
static bool
type_can_be_allocated_directly(GITypeInfo *type_info)
{
    bool is_simple = true;

    if (g_type_info_is_pointer(type_info)) {
        if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_ARRAY &&
            g_type_info_get_array_type(type_info) == GI_ARRAY_TYPE_C) {
            GITypeInfo *param_info;

            param_info = g_type_info_get_param_type(type_info, 0);
            is_simple = type_can_be_allocated_directly(param_info);

            g_base_info_unref((GIBaseInfo*)param_info);
        } else {
            is_simple = false;
        }
    } else {
        switch (g_type_info_get_tag(type_info)) {
        case GI_TYPE_TAG_INTERFACE:
            {
                GIBaseInfo *interface = g_type_info_get_interface(type_info);
                switch (g_base_info_get_type(interface)) {
                case GI_INFO_TYPE_BOXED:
                case GI_INFO_TYPE_STRUCT:
                    if (!struct_is_simple((GIStructInfo *)interface))
                        is_simple = false;
                    break;
                case GI_INFO_TYPE_UNION:
                    /* FIXME: Need to implement */
                    is_simple = false;
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
                    is_simple = false;
                    break;
                case GI_INFO_TYPE_INVALID_0:
                    g_assert_not_reached();
                    break;
                case GI_INFO_TYPE_ENUM:
                case GI_INFO_TYPE_FLAGS:
                default:
                    break;
                }

                g_base_info_unref(interface);
                break;
            }
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
        case GI_TYPE_TAG_VOID:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_ERROR:
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
        default:
            break;
        }
    }
    return is_simple;
}

/* Check if the type of the boxed is "simple" - every field is a non-pointer
 * type that we know how to assign to. If so, then we can allocate and free
 * instances without needing a constructor.
 */
GJS_USE
static bool
struct_is_simple(GIStructInfo *info)
{
    int n_fields = g_struct_info_get_n_fields(info);
    bool is_simple = true;
    int i;

    /* If it's opaque, it's not simple */
    if (n_fields == 0)
        return false;

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
    priv->zero_args_constructor_name = JSID_VOID;
    priv->default_constructor = -1;
    priv->default_constructor_name = JSID_VOID;

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
                    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
                    priv->default_constructor_name = atoms.new_();
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

bool gjs_define_boxed_class(JSContext* context, JS::HandleObject in_object,
                            GIStructInfo* info) {
    const char *constructor_name;
    JS::RootedObject prototype(context), constructor(context);
    Boxed *priv;

    /* See the comment in gjs_define_object_class() for an
     * explanation of how this all works; Boxed is pretty much the
     * same as Object.
     */

    constructor_name = g_base_info_get_name( (GIBaseInfo*) info);

    if (!gjs_init_class_dynamic(context, in_object,
                                nullptr, /* parent prototype */
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
        return false;
    }

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);
    new (priv) Boxed();
    priv->info = info;
    boxed_fill_prototype_info(context, priv);

    g_base_info_ref( (GIBaseInfo*) priv->info);
    priv->gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo*) priv->info);
    JS_SetPrivate(prototype, priv);

    gjs_debug(GJS_DEBUG_GBOXED, "Defined class %s prototype is %p class %p in object %p",
              constructor_name, prototype.get(), JS_GetClass(prototype),
              in_object.get());

    priv->can_allocate_directly = struct_is_simple (priv->info);

    if (!define_boxed_class_fields(context, priv, prototype) ||
        !gjs_define_static_methods(context, constructor, priv->gtype,
                                   priv->info))
        return false;

    if (priv->gtype == G_TYPE_ERROR &&
        !JS_DefineFunction(context, prototype, "toString", gjs_gerror_to_string,
                           0, GJS_MODULE_PROP_FLAGS))
        return false;

    JS::RootedObject gtype_obj(context,
        gjs_gtype_create_gtype_wrapper(context, priv->gtype));
    if (!gtype_obj)
        return false;

    return JS_DefineProperty(context, constructor, "$gtype", gtype_obj,
                             JSPROP_PERMANENT);
}

JSObject* gjs_boxed_from_c_struct(JSContext* cx, GIStructInfo* info,
                                  void* gboxed, GjsBoxedCreationFlags flags) {
    Boxed *priv;
    Boxed *proto_priv;

    if (gboxed == NULL)
        return NULL;

    gjs_debug_marshal(GJS_DEBUG_GBOXED,
                      "Wrapping struct %s %p with JSObject",
                      g_base_info_get_name((GIBaseInfo *)info), gboxed);

    JS::RootedObject proto(cx, gjs_lookup_generic_prototype(cx, info));
    if (!proto)
        return nullptr;
    proto_priv = priv_from_js(cx, proto);

    JS::RootedObject obj(
        cx, JS_NewObjectWithGivenProto(cx, JS_GetClass(proto), proto));
    if (!obj)
        return nullptr;

    GJS_INC_COUNTER(boxed);
    priv = g_slice_new0(Boxed);
    new (priv) Boxed();

    *priv = *proto_priv;
    g_base_info_ref( (GIBaseInfo*) priv->info);

    JS_SetPrivate(obj, priv);

    if ((flags & GJS_BOXED_CREATION_NO_COPY) != 0) {
        /* we need to create a JS Boxed which references the
         * original C struct, not a copy of it. Used for
         * G_SIGNAL_TYPE_STATIC_SCOPE
         */
        priv->gboxed = gboxed;
        priv->not_owning_gboxed = true;
    } else {
        if (priv->gtype != G_TYPE_NONE && g_type_is_a (priv->gtype, G_TYPE_BOXED)) {
            priv->gboxed = g_boxed_copy(priv->gtype, gboxed);
        } else if (priv->gtype == G_TYPE_VARIANT) {
            priv->gboxed = g_variant_ref_sink ((GVariant *) gboxed);
        } else if (priv->can_allocate_directly) {
            boxed_new_direct(priv);
            memcpy(priv->gboxed, gboxed, g_struct_info_get_size (priv->info));
        } else {
            gjs_throw(cx,
                      "Can't create a Javascript object for %s; no way to copy",
                      g_base_info_get_name(priv->info));
        }
    }

    if (priv->gtype == G_TYPE_ERROR && !gjs_define_error_properties(cx, obj))
        return nullptr;

    return obj;
}

void*
gjs_c_struct_from_boxed(JSContext       *context,
                        JS::HandleObject obj)
{
    Boxed *priv;

    if (!obj)
        return NULL;

    priv = priv_from_js(context, obj);
    if (priv == NULL)
        return NULL;

    return priv->gboxed;
}

bool
gjs_typecheck_boxed(JSContext       *context,
                    JS::HandleObject object,
                    GIStructInfo    *expected_info,
                    GType            expected_type,
                    bool             throw_error)
{
    Boxed *priv;
    bool result;

    if (!do_base_typecheck(context, object, throw_error))
        return false;

    priv = priv_from_js(context, object);

    if (priv->gboxed == NULL) {
        if (throw_error) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is %s.%s.prototype, not an object instance - cannot convert to a boxed instance",
                             g_base_info_get_namespace( (GIBaseInfo*) priv->info),
                             g_base_info_get_name( (GIBaseInfo*) priv->info));
        }

        return false;
    }

    if (expected_type != G_TYPE_NONE)
        result = g_type_is_a (priv->gtype, expected_type);
    else if (expected_info != NULL)
        result = g_base_info_equal((GIBaseInfo*) priv->info, (GIBaseInfo*) expected_info);
    else
        result = true;

    if (!result && throw_error) {
        if (expected_info != NULL) {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s.%s - cannot convert to %s.%s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_base_info_get_namespace((GIBaseInfo*) expected_info),
                             g_base_info_get_name((GIBaseInfo*) expected_info));
        } else {
            gjs_throw_custom(context, JSProto_TypeError, nullptr,
                             "Object is of type %s.%s - cannot convert to %s",
                             g_base_info_get_namespace((GIBaseInfo*) priv->info),
                             g_base_info_get_name((GIBaseInfo*) priv->info),
                             g_type_name(expected_type));
        }
    }

    return result;
}
