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

#include "repo.h"
#include "ns.h"
#include "function.h"
#include "object.h"
#include "param.h"
#include "boxed.h"
#include "union.h"
#include "enumeration.h"
#include "arg.h"
#include "foreign.h"
#include "fundamental.h"
#include "interface.h"
#include "gerror.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/jsapi-private.h"
#include "gjs/mem.h"

#include <util/misc.h>

#include <girepository.h>
#include <string.h>

typedef struct {
    void *dummy;

} Repo;

extern struct JSClass gjs_repo_class;

GJS_DEFINE_PRIV_FROM_JS(Repo, gjs_repo_class)

static JSObject *lookup_override_function(JSContext *, JS::HandleId);

static bool
get_version_for_ns (JSContext       *context,
                    JS::HandleObject repo_obj,
                    JS::HandleId     ns_id,
                    char           **version)
{
    JS::RootedObject versions(context);
    JS::RootedValue version_val(context);
    JS::RootedId versions_name(context,
        gjs_context_get_const_string(context, GJS_STRING_GI_VERSIONS));

    if (!gjs_object_require_property_value(context, repo_obj,
                                           "GI repository object", versions_name,
                                           &versions))
        return false;

    if (!gjs_object_require_property_value(context, versions, NULL, ns_id, version)) {
        /* Property not actually required, so clear an exception */
        JS_ClearPendingException(context);
        *version = NULL;
    }

    return true;
}

static bool
resolve_namespace_object(JSContext       *context,
                         JS::HandleObject repo_obj,
                         JS::HandleId     ns_id,
                         const char      *ns_name)
{
    GIRepository *repo;
    GError *error;
    char *version;

    JSAutoRequest ar(context);

    if (!get_version_for_ns(context, repo_obj, ns_id, &version))
        return false;

    repo = g_irepository_get_default();

    error = NULL;
    g_irepository_require(repo, ns_name, version, (GIRepositoryLoadFlags) 0, &error);
    if (error != NULL) {
        gjs_throw(context,
                  "Requiring %s, version %s: %s",
                  ns_name, version?version:"none", error->message);

        g_error_free(error);
        g_free(version);
        return false;
    }

    g_free(version);

    /* Defines a property on "obj" (the javascript repo object)
     * with the given namespace name, pointing to that namespace
     * in the repo.
     */
    JS::RootedObject gi_namespace(context, gjs_create_ns(context, ns_name));

    /* Define the property early, to avoid reentrancy issues if
       the override module looks for namespaces that import this */
    if (!JS_DefineProperty(context, repo_obj, ns_name, gi_namespace,
                           GJS_MODULE_PROP_FLAGS))
        g_error("no memory to define ns property");

    JS::RootedValue override(context,
        JS::ObjectOrNullValue(lookup_override_function(context, ns_id)));
    JS::RootedValue result(context);
    if (!override.isNull() &&
        !JS_CallFunctionValue (context, gi_namespace, /* thisp */
                               override, /* callee */
                               JS::HandleValueArray::empty(), &result))
        return false;

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Defined namespace '%s' %p in GIRepository %p", ns_name,
              gi_namespace.get(), repo_obj.get());

    gjs_schedule_gc_if_needed(context);
    return true;
}

/*
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static bool
repo_new_resolve(JSContext *context,
                 JS::HandleObject obj,
                 JS::HandleId id,
                 JS::MutableHandleObject objp)
{
    Repo *priv;
    char *name;
    bool ret = true;

    if (!gjs_get_string_id(context, id, &name))
        return true; /* not resolved, but no error */

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0)
        goto out;

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GREPO, "Resolve prop '%s' hook obj %p priv %p",
                     name, obj.get(), priv);

    if (priv == NULL) /* we are the prototype, or have the wrong class */
        goto out;

    if (!resolve_namespace_object(context, obj, id, name)) {
        ret = false;
    } else {
        objp.set(obj); /* store the object we defined the prop in */
    }

 out:
    g_free(name);
    return ret;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(repo)

static void
repo_finalize(JSFreeOp *fop,
              JSObject *obj)
{
    Repo *priv;

    priv = (Repo*) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GREPO,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance */

    GJS_DEC_COUNTER(repo);
    g_slice_free(Repo, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_repo_class = {
    "GIRepository", /* means "new GIRepository()" works */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) repo_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    repo_finalize
};

JSPropertySpec gjs_repo_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_repo_proto_funcs[] = {
    JS_FS_END
};

static JSObject*
repo_new(JSContext *context)
{
    Repo *priv;
    JSObject *versions;
    JSObject *private_ns;
    bool found;
    jsid versions_name, private_ns_name;

    JS::RootedObject global(context, gjs_get_import_global(context));

    if (!JS_HasProperty(context, global, gjs_repo_class.name, &found))
        return NULL;
    if (!found) {
        JSObject *prototype;
        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 JS::NullPtr(),
                                 &gjs_repo_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 gjs_repo_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_repo_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_repo_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            g_error("Can't init class %s", gjs_repo_class.name);

        gjs_debug(GJS_DEBUG_GREPO, "Initialized class %s prototype %p",
                  gjs_repo_class.name, prototype);
    }

    JS::RootedObject repo(context,
        JS_NewObject(context, &gjs_repo_class, JS::NullPtr(), global));
    if (repo == NULL) {
        gjs_throw(context, "No memory to create repo object");
        return NULL;
    }

    priv = g_slice_new0(Repo);

    GJS_INC_COUNTER(repo);

    g_assert(priv_from_js(context, repo) == NULL);
    JS_SetPrivate(repo, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GREPO,
                        "repo constructor, obj %p priv %p", repo.get(), priv);

    versions = JS_NewObject(context, NULL, JS::NullPtr(), global);
    versions_name = gjs_context_get_const_string(context, GJS_STRING_GI_VERSIONS);
    JS_DefinePropertyById(context, repo,
                          versions_name,
                          JS::ObjectValue(*versions),
                          NULL, NULL,
                          JSPROP_PERMANENT);

    private_ns = JS_NewObject(context, NULL, JS::NullPtr(), global);
    private_ns_name = gjs_context_get_const_string(context, GJS_STRING_PRIVATE_NS_MARKER);
    JS_DefinePropertyById(context, repo,
                          private_ns_name,
                          JS::ObjectValue(*private_ns),
                          NULL, NULL, JSPROP_PERMANENT);

    /* FIXME - hack to make namespaces load, since
     * gobject-introspection does not yet search a path properly.
     */
    {
        JS::RootedValue value(context);
        JS_GetProperty(context, repo, "GLib", &value);
    }

    return repo;
}

bool
gjs_define_repo(JSContext              *cx,
                JS::MutableHandleObject repo,
                const char             *name)
{
    repo.set(repo_new(cx));
    return true;
}

static bool
gjs_define_constant(JSContext       *context,
                    JS::HandleObject in_object,
                    GIConstantInfo  *info)
{
    JS::RootedValue value(context);
    GArgument garg = { 0, };
    GITypeInfo *type_info;
    const char *name;
    bool ret = false;

    type_info = g_constant_info_get_type(info);
    g_constant_info_get_value(info, &garg);

    if (!gjs_value_from_g_argument(context, &value, type_info, &garg, true))
        goto out;

    name = g_base_info_get_name((GIBaseInfo*) info);

    if (JS_DefineProperty(context, in_object,
                          name, value,
                          GJS_MODULE_PROP_FLAGS))
        ret = true;

 out:
    g_constant_info_free_value (info, &garg);
    g_base_info_unref((GIBaseInfo*) type_info);
    return ret;
}

#if GJS_VERBOSE_ENABLE_GI_USAGE
void
_gjs_log_info_usage(GIBaseInfo *info)
{
#define DIRECTION_STRING(d) ( ((d) == GI_DIRECTION_IN) ? "IN" : ((d) == GI_DIRECTION_OUT) ? "OUT" : "INOUT" )
#define TRANSFER_STRING(t) ( ((t) == GI_TRANSFER_NOTHING) ? "NOTHING" : ((t) == GI_TRANSFER_CONTAINER) ? "CONTAINER" : "EVERYTHING" )

    {
        char *details;
        GIInfoType info_type;
        GIBaseInfo *container;

        info_type = g_base_info_get_type(info);

        if (info_type == GI_INFO_TYPE_FUNCTION) {
            GString *args;
            int n_args;
            int i;
            GITransfer retval_transfer;

            args = g_string_new("{ ");

            n_args = g_callable_info_get_n_args((GICallableInfo*) info);
            for (i = 0; i < n_args; ++i) {
                GIArgInfo *arg;
                GIDirection direction;
                GITransfer transfer;

                arg = g_callable_info_get_arg((GICallableInfo*)info, i);
                direction = g_arg_info_get_direction(arg);
                transfer = g_arg_info_get_ownership_transfer(arg);

                g_string_append_printf(args,
                                       "{ GI_DIRECTION_%s, GI_TRANSFER_%s }, ",
                                       DIRECTION_STRING(direction),
                                       TRANSFER_STRING(transfer));

                g_base_info_unref((GIBaseInfo*) arg);
            }
            if (args->len > 2)
                g_string_truncate(args, args->len - 2); /* chop comma */

            g_string_append(args, " }");

            retval_transfer = g_callable_info_get_caller_owns((GICallableInfo*) info);

            details = g_strdup_printf(".details = { .func = { .retval_transfer = GI_TRANSFER_%s, .n_args = %d, .args = %s } }",
                                      TRANSFER_STRING(retval_transfer), n_args, args->str);
            g_string_free(args, true);
        } else {
            details = g_strdup_printf(".details = { .nothing = {} }");
        }

        container = g_base_info_get_container(info);

        gjs_debug_gi_usage("{ GI_INFO_TYPE_%s, \"%s\", \"%s\", \"%s\", %s },",
                           gjs_info_type_name(info_type),
                           g_base_info_get_namespace(info),
                           container ? g_base_info_get_name(container) : "",
                           g_base_info_get_name(info),
                           details);
        g_free(details);
    }
}
#endif /* GJS_VERBOSE_ENABLE_GI_USAGE */

bool
gjs_define_info(JSContext       *context,
                JS::HandleObject in_object,
                GIBaseInfo      *info,
                bool            *defined)
{
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(info);
#endif

    *defined = true;

    switch (g_base_info_get_type(info)) {
    case GI_INFO_TYPE_FUNCTION:
        {
            JSObject *f;
            f = gjs_define_function(context, in_object, 0, (GICallableInfo*) info);
            if (f == NULL)
                return false;
        }
        break;
    case GI_INFO_TYPE_OBJECT:
        {
            GType gtype;
            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);

            if (g_type_is_a (gtype, G_TYPE_PARAM)) {
                gjs_define_param_class(context, in_object);
            } else if (g_type_is_a (gtype, G_TYPE_OBJECT)) {
                JS::RootedObject ignored(context);
                gjs_define_object_class(context, in_object,
                                        (GIObjectInfo *) info, gtype, &ignored);
            } else if (G_TYPE_IS_INSTANTIATABLE(gtype)) {
                JS::RootedObject ignored1(context), ignored2(context);
                if (!gjs_define_fundamental_class(context, in_object,
                                                  (GIObjectInfo*)info,
                                                  &ignored1, &ignored2)) {
                    gjs_throw (context,
                               "Unsupported fundamental class creation for type %s",
                               g_type_name(gtype));
                    return false;
                }
            } else {
                gjs_throw (context,
                           "Unsupported type %s, deriving from fundamental %s",
                           g_type_name(gtype), g_type_name(g_type_fundamental(gtype)));
                return false;
            }
        }
        break;
    case GI_INFO_TYPE_STRUCT:
        /* We don't want GType structures in the namespace,
           we expose their fields as vfuncs and their methods
           as static methods
        */
        if (g_struct_info_is_gtype_struct((GIStructInfo*) info)) {
            *defined = false;
            break;
        }
        /* Fall through */

    case GI_INFO_TYPE_BOXED:
        gjs_define_boxed_class(context, in_object, (GIBoxedInfo*) info);
        break;
    case GI_INFO_TYPE_UNION:
        if (!gjs_define_union_class(context, in_object, (GIUnionInfo*) info))
            return false;
        break;
    case GI_INFO_TYPE_ENUM:
        if (g_enum_info_get_error_domain((GIEnumInfo*) info)) {
            /* define as GError subclass */
            gjs_define_error_class(context, in_object, (GIEnumInfo*) info);
            break;
        }
        /* fall through */

    case GI_INFO_TYPE_FLAGS:
        if (!gjs_define_enumeration(context, in_object, (GIEnumInfo*) info))
            return false;
        break;
    case GI_INFO_TYPE_CONSTANT:
        if (!gjs_define_constant(context, in_object, (GIConstantInfo*) info))
            return false;
        break;
    case GI_INFO_TYPE_INTERFACE:
        {
            JS::RootedObject ignored(context);
            gjs_define_interface_class(context, in_object,
                                       (GIInterfaceInfo *) info,
                                       g_registered_type_info_get_g_type((GIRegisteredTypeInfo *) info),
                                       &ignored);
        }
        break;
    case GI_INFO_TYPE_INVALID:
    case GI_INFO_TYPE_INVALID_0:
    case GI_INFO_TYPE_CALLBACK:
    case GI_INFO_TYPE_VALUE:
    case GI_INFO_TYPE_SIGNAL:
    case GI_INFO_TYPE_VFUNC:
    case GI_INFO_TYPE_PROPERTY:
    case GI_INFO_TYPE_FIELD:
    case GI_INFO_TYPE_ARG:
    case GI_INFO_TYPE_TYPE:
    case GI_INFO_TYPE_UNRESOLVED:
    default:
        gjs_throw(context, "API of type %s not implemented, cannot define %s.%s",
                  gjs_info_type_name(g_base_info_get_type(info)),
                  g_base_info_get_namespace(info),
                  g_base_info_get_name(info));
        return false;
    }

    return true;
}

/* Get the "unknown namespace", which should be used for unnamespaced types */
JSObject*
gjs_lookup_private_namespace(JSContext *context)
{
    JS::RootedId ns_name(context,
        gjs_context_get_const_string(context, GJS_STRING_PRIVATE_NS_MARKER));
    return gjs_lookup_namespace_object_by_name(context, ns_name);
}

/* Get the namespace object that the GIBaseInfo should be inside */
JSObject*
gjs_lookup_namespace_object(JSContext  *context,
                            GIBaseInfo *info)
{
    const char *ns;

    ns = g_base_info_get_namespace(info);
    if (ns == NULL) {
        gjs_throw(context, "%s '%s' does not have a namespace",
                     gjs_info_type_name(g_base_info_get_type(info)),
                     g_base_info_get_name(info));

        return NULL;
    }

    JS::RootedId ns_name(context, gjs_intern_string_to_id(context, ns));
    return gjs_lookup_namespace_object_by_name(context, ns_name);
}

static JSObject*
lookup_override_function(JSContext   *cx,
                         JS::HandleId ns_name)
{
    JSAutoRequest ar(cx);

    JS::RootedValue importer(cx, gjs_get_global_slot(cx, GJS_GLOBAL_SLOT_IMPORTS));
    g_assert(importer.isObject());

    JS::RootedValue function(cx);
    JS::RootedId overrides_name(cx,
        gjs_context_get_const_string(cx, GJS_STRING_GI_OVERRIDES));
    JS::RootedId object_init_name(cx,
        gjs_context_get_const_string(cx, GJS_STRING_GOBJECT_INIT));
    JS::RootedObject overridespkg(cx), module(cx);
    JS::RootedObject importer_obj(cx, &importer.toObject());

    if (!gjs_object_require_property_value(cx, importer_obj, "importer",
                                           overrides_name, &overridespkg))
        goto fail;

    if (!gjs_object_require_property_value(cx, overridespkg, "GI repository object",
                                           ns_name, &module))
        goto fail;

    if (!gjs_object_require_property(cx, module, "override module",
                                     object_init_name, &function) ||
        !function.isObjectOrNull())
        goto fail;

    return function.toObjectOrNull();

 fail:
    JS_ClearPendingException(cx);
    return NULL;
}

JSObject*
gjs_lookup_namespace_object_by_name(JSContext      *context,
                                    JS::HandleId    ns_name)
{
    JSAutoRequest ar(context);

    JS::RootedValue importer(context,
        gjs_get_global_slot(context, GJS_GLOBAL_SLOT_IMPORTS));
    g_assert(importer.isObject());

    JS::RootedId gi_name(context,
        gjs_context_get_const_string(context, GJS_STRING_GI_MODULE));
    JS::RootedObject repo(context), importer_obj(context, &importer.toObject());

    if (!gjs_object_require_property_value(context, importer_obj, "importer",
                                           gi_name, &repo)) {
        gjs_log_exception(context);
        gjs_throw(context, "No gi property in importer");
        return NULL;
    }

    JS::RootedObject retval(context);
    if (!gjs_object_require_property_value(context, repo, "GI repository object",
                                           ns_name, &retval))
        return NULL;

    return retval;
}

const char*
gjs_info_type_name(GIInfoType type)
{
    switch (type) {
    case GI_INFO_TYPE_INVALID:
        return "INVALID";
    case GI_INFO_TYPE_FUNCTION:
        return "FUNCTION";
    case GI_INFO_TYPE_CALLBACK:
        return "CALLBACK";
    case GI_INFO_TYPE_STRUCT:
        return "STRUCT";
    case GI_INFO_TYPE_BOXED:
        return "BOXED";
    case GI_INFO_TYPE_ENUM:
        return "ENUM";
    case GI_INFO_TYPE_FLAGS:
        return "FLAGS";
    case GI_INFO_TYPE_OBJECT:
        return "OBJECT";
    case GI_INFO_TYPE_INTERFACE:
        return "INTERFACE";
    case GI_INFO_TYPE_CONSTANT:
        return "CONSTANT";
    case GI_INFO_TYPE_UNION:
        return "UNION";
    case GI_INFO_TYPE_VALUE:
        return "VALUE";
    case GI_INFO_TYPE_SIGNAL:
        return "SIGNAL";
    case GI_INFO_TYPE_VFUNC:
        return "VFUNC";
    case GI_INFO_TYPE_PROPERTY:
        return "PROPERTY";
    case GI_INFO_TYPE_FIELD:
        return "FIELD";
    case GI_INFO_TYPE_ARG:
        return "ARG";
    case GI_INFO_TYPE_TYPE:
        return "TYPE";
    case GI_INFO_TYPE_UNRESOLVED:
        return "UNRESOLVED";
    case GI_INFO_TYPE_INVALID_0:
        g_assert_not_reached();
        break;
    default:
      return "???";
    }
}

char*
gjs_camel_from_hyphen(const char *hyphen_name)
{
    GString *s;
    const char *p;
    bool next_upper;

    s = g_string_sized_new(strlen(hyphen_name) + 1);

    next_upper = false;
    for (p = hyphen_name; *p; ++p) {
        if (*p == '-' || *p == '_') {
            next_upper = true;
        } else {
            if (next_upper) {
                g_string_append_c(s, g_ascii_toupper(*p));
                next_upper = false;
            } else {
                g_string_append_c(s, *p);
            }
        }
    }

    return g_string_free(s, false);
}

char*
gjs_hyphen_from_camel(const char *camel_name)
{
    GString *s;
    const char *p;

    /* four hyphens should be reasonable guess */
    s = g_string_sized_new(strlen(camel_name) + 4 + 1);

    for (p = camel_name; *p; ++p) {
        if (g_ascii_isupper(*p)) {
            g_string_append_c(s, '-');
            g_string_append_c(s, g_ascii_tolower(*p));
        } else {
            g_string_append_c(s, *p);
        }
    }

    return g_string_free(s, false);
}

JSObject *
gjs_lookup_generic_constructor(JSContext  *context,
                               GIBaseInfo *info)
{
    const char *constructor_name;

    JS::RootedObject in_object(context,
        gjs_lookup_namespace_object(context, (GIBaseInfo*) info));
    constructor_name = g_base_info_get_name((GIBaseInfo*) info);

    if (G_UNLIKELY (!in_object))
        return NULL;

    JS::RootedValue value(context);
    if (!JS_GetProperty(context, in_object, constructor_name, &value))
        return NULL;

    if (G_UNLIKELY (!value.isObject()))
        return NULL;

    return &value.toObject();
}

JSObject *
gjs_lookup_generic_prototype(JSContext  *context,
                             GIBaseInfo *info)
{
    JS::RootedObject constructor(context,
                                 gjs_lookup_generic_constructor(context, info));
    if (G_UNLIKELY (constructor == NULL))
        return NULL;

    JS::RootedValue value(context);
    if (!gjs_object_get_property_const(context, constructor,
                                       GJS_STRING_PROTOTYPE, &value))
        return NULL;

    if (G_UNLIKELY (!value.isObjectOrNull()))
        return NULL;

    return value.toObjectOrNull();
}
