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
#include "interface.h"
#include "gerror.h"

#include <gjs/compat.h>

#include <util/log.h>
#include <util/misc.h>

#include <girepository.h>
#include <string.h>

#define DUMPBIN "_gjs_private"

typedef struct {
    void *dummy;

} Repo;

static struct JSClass gjs_repo_class;

GJS_DEFINE_PRIV_FROM_JS(Repo, gjs_repo_class)

static JSObject * lookup_override_function(JSContext *, const char *);

static JSObject*
resolve_namespace_object(JSContext  *context,
                         JSObject   *repo_obj,
                         const char *ns_name)
{
    GIRepository *repo;
    GError *error;
    jsval versions_val;
    JSObject *versions;
    jsval version_val;
    char *version;
    JSObject *namespace;
    JSObject *override;
    jsval result;

    JS_BeginRequest(context);

    if (!gjs_object_require_property(context, repo_obj, "GI repository object", "versions", &versions_val) ||
        !JSVAL_IS_OBJECT(versions_val)) {
        gjs_throw(context, "No 'versions' property in GI repository object");

        JS_EndRequest(context);
        return NULL;
    }

    versions = JSVAL_TO_OBJECT(versions_val);

    version = NULL;
    if (JS_GetProperty(context, versions, ns_name, &version_val) &&
        JSVAL_IS_STRING(version_val)) {
        gjs_string_to_utf8(context, version_val, &version);
    }

    repo = g_irepository_get_default();

    error = NULL;
    g_irepository_require(repo, ns_name, version, 0, &error);
    if (error != NULL) {
        gjs_throw(context,
                  "Requiring %s, version %s: %s",
                  ns_name, version?version:"none", error->message);
        g_error_free(error);
        g_free(version);
        JS_EndRequest(context);
        return NULL;
    }

    g_free(version);

    /* Defines a property on "obj" (the javascript repo object)
     * with the given namespace name, pointing to that namespace
     * in the repo.
     */
    namespace = gjs_create_ns(context, ns_name, repo);
    JS_AddObjectRoot(context, &namespace);

    /* Define the property early, to avoid reentrancy issues if
       the override module looks for namespaces that import this */
    if (!JS_DefineProperty(context, repo_obj,
                           ns_name, OBJECT_TO_JSVAL(namespace),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        gjs_fatal("no memory to define ns property");

    override = lookup_override_function(context, ns_name);
    if (override && !JS_CallFunctionValue (context,
                                           namespace, /* thisp */
                                           OBJECT_TO_JSVAL(override), /* callee */
                                           0, /* argc */
                                           NULL, /* argv */
                                           &result)) {
        JS_RemoveObjectRoot(context, &namespace);
        JS_EndRequest(context);
        return NULL;
    }

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Defined namespace '%s' %p in GIRepository %p", ns_name, namespace, repo_obj);

    JS_RemoveObjectRoot(context, &namespace);
    JS_EndRequest(context);
    return namespace;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
repo_new_resolve(JSContext *context,
                 JSObject **obj,
                 jsid      *id,
                 unsigned   flags,
                 JSObject **objp)
{
    Repo *priv;
    char *name;
    JSBool ret = JS_TRUE;

    *objp = NULL;

    if (!gjs_get_string_id(context, *id, &name))
        return JS_TRUE; /* not resolved, but no error */

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0)
        goto out;

    priv = priv_from_js(context, *obj);
    gjs_debug_jsprop(GJS_DEBUG_GREPO, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL) /* we are the prototype, or have the wrong class */
        goto out;

    JS_BeginRequest(context);
    if (resolve_namespace_object(context, *obj, name) == NULL) {
        ret = JS_FALSE;
    } else {
        *objp = *obj; /* store the object we defined the prop in */
    }
    JS_EndRequest(context);

 out:
    g_free(name);
    return ret;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(repo)

static void
repo_finalize(JSContext *context,
              JSObject  *obj)
{
    Repo *priv;

    priv = priv_from_js(context, obj);
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
static struct JSClass gjs_repo_class = {
    "GIRepository", /* means "new GIRepository()" works */
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    (JSResolveOp) repo_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    repo_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static JSPropertySpec gjs_repo_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_repo_proto_funcs[] = {
    { NULL }
};

static JSObject*
repo_new(JSContext *context)
{
    Repo *priv;
    JSObject *repo;
    JSObject *global;
    JSObject *versions;

    global = gjs_get_import_global(context);

    if (!gjs_object_has_property(context, global, gjs_repo_class.name)) {
        JSObject *prototype;
        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 NULL,
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
            gjs_fatal("Can't init class %s", gjs_repo_class.name);

        g_assert(gjs_object_has_property(context, global, gjs_repo_class.name));

        gjs_debug(GJS_DEBUG_GREPO, "Initialized class %s prototype %p",
                  gjs_repo_class.name, prototype);
    }

    repo = JS_NewObject(context, &gjs_repo_class, NULL, global);
    if (repo == NULL) {
        gjs_throw(context, "No memory to create repo object");
        return JS_FALSE;
    }

    priv = g_slice_new0(Repo);

    GJS_INC_COUNTER(repo);

    g_assert(priv_from_js(context, repo) == NULL);
    JS_SetPrivate(repo, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GREPO,
                        "repo constructor, obj %p priv %p", repo, priv);

    versions = JS_NewObject(context, NULL, NULL, global);

    JS_DefineProperty(context, repo,
                      "versions",
                      OBJECT_TO_JSVAL(versions),
                      NULL, NULL,
                      JSPROP_PERMANENT);

    g_assert(gjs_object_has_property(context, repo, "versions"));

    JS_DefineObject(context, repo, DUMPBIN, NULL, NULL, JSPROP_PERMANENT);

    /* FIXME - hack to make namespaces load, since
     * gobject-introspection does not yet search a path properly.
     */
    {
        jsval value;
        JS_GetProperty(context, repo, "GLib", &value);
    }

    return repo;
}

JSBool
gjs_define_repo(JSContext  *context,
                JSObject   *module_obj,
                const char *name)
{
    JSObject *repo;

    repo = repo_new(context);

    if (!JS_DefineProperty(context, module_obj,
                           name, OBJECT_TO_JSVAL(repo),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
gjs_define_constant(JSContext      *context,
                    JSObject       *in_object,
                    GIConstantInfo *info)
{
    jsval value;
    GArgument garg = { 0, };
    GITypeInfo *type_info;
    const char *name;
    JSBool ret = JS_FALSE;

    type_info = g_constant_info_get_type(info);
    g_constant_info_get_value(info, &garg);

    if (!gjs_value_from_g_argument(context, &value, type_info, &garg, TRUE))
        goto out;

    name = g_base_info_get_name((GIBaseInfo*) info);

    if (JS_DefineProperty(context, in_object,
                          name, value,
                          NULL, NULL,
                          GJS_MODULE_PROP_FLAGS))
        ret = JS_TRUE;

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
            g_string_free(args, TRUE);
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

JSBool
gjs_define_info(JSContext  *context,
                JSObject   *in_object,
                GIBaseInfo *info)
{
#if GJS_VERBOSE_ENABLE_GI_USAGE
    _gjs_log_info_usage(info);
#endif
    switch (g_base_info_get_type(info)) {
    case GI_INFO_TYPE_FUNCTION:
        {
            JSObject *f;
            f = gjs_define_function(context, in_object, 0, (GICallableInfo*) info);
            if (f == NULL)
                return JS_FALSE;
        }
        break;
    case GI_INFO_TYPE_OBJECT:
        {
            GType gtype;
            gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);

            if (g_type_is_a (gtype, G_TYPE_PARAM)) {
                if (!gjs_define_param_class(context, in_object, NULL))
                    return JS_FALSE;
            } else if (g_type_is_a (gtype, G_TYPE_OBJECT)) {
                if (!gjs_define_object_class(context, in_object, gtype, NULL, NULL))
                    return JS_FALSE;
            } else {
                gjs_throw (context,
                           "Unsupported type %s, deriving from fundamental %s",
                           g_type_name(gtype), g_type_name(g_type_fundamental(gtype)));
                return JS_FALSE;
            }
        }
        break;
    case GI_INFO_TYPE_STRUCT:
    case GI_INFO_TYPE_BOXED:
        if (!gjs_define_boxed_class(context, in_object, (GIBoxedInfo*) info, NULL, NULL))
            return JS_FALSE;
        break;
    case GI_INFO_TYPE_UNION:
        if (!gjs_define_union_class(context, in_object, (GIUnionInfo*) info, NULL, NULL))
            return JS_FALSE;
        break;
    case GI_INFO_TYPE_ENUM:
        if (g_enum_info_get_error_domain((GIEnumInfo*) info)) {
            /* define as GError subclass */
            if (!gjs_define_error_class(context, in_object, (GIEnumInfo*) info, NULL, NULL))
                return JS_FALSE;
        }
        /* fall through */

    case GI_INFO_TYPE_FLAGS:
        if (!gjs_define_enumeration(context, in_object, (GIEnumInfo*) info, NULL))
            return JS_FALSE;
        break;
    case GI_INFO_TYPE_CONSTANT:
        if (!gjs_define_constant(context, in_object, (GIConstantInfo*) info))
            return JS_FALSE;
        break;
    case GI_INFO_TYPE_INTERFACE:
        if (!gjs_define_interface_class(context, in_object, (GIInterfaceInfo*) info, NULL))
            return JS_FALSE;
        break;
    default:
        gjs_throw(context, "API of type %s not implemented, cannot define %s.%s",
                  gjs_info_type_name(g_base_info_get_type(info)),
                  g_base_info_get_namespace(info),
                  g_base_info_get_name(info));
        return JS_FALSE;
    }

    return JS_TRUE;
}

/* Get the "unknown namespace", which should be used for unnamespaced types */
JSObject*
gjs_lookup_private_namespace(JSContext *context)
{
    return gjs_lookup_namespace_object_by_name(context, DUMPBIN);
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

    return gjs_lookup_namespace_object_by_name(context, ns);
}

static JSObject*
lookup_override_function(JSContext  *context,
                         const char *ns)
{
    JSObject *global;
    jsval importer;
    jsval overridespkg;
    jsval module;
    jsval function;

    JS_BeginRequest(context);
    global = gjs_get_import_global(context);

    importer = JSVAL_VOID;
    if (!gjs_object_require_property(context, global, "global object", "imports", &importer) ||
        !JSVAL_IS_OBJECT(importer))
        goto fail;

    overridespkg = JSVAL_VOID;
    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(importer), "importer",
                                        "overrides", &overridespkg) ||
        !JSVAL_IS_OBJECT(overridespkg))
        goto fail;

    module = JSVAL_VOID;
    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(overridespkg), "GI repository object", ns, &module)
        || !JSVAL_IS_OBJECT(module))
        goto fail;

    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(module), "override module",
                                     "_init", &function) ||
        !JSVAL_IS_OBJECT(function))
        goto fail;

    JS_EndRequest(context);
    return JSVAL_TO_OBJECT(function);

 fail:
    JS_ClearPendingException(context);
    JS_EndRequest(context);
    return NULL;
}

JSObject*
gjs_lookup_namespace_object_by_name(JSContext      *context,
                                    const char     *ns)
{
    JSObject *global;
    JSObject *repo_obj;
    jsval importer;
    jsval girepository;
    jsval ns_obj;

    /* This is a little bit of a hack, we hardcode an assumption that
     * the only repo object that exists is called "imports.gi" and is
     * is stored in the "import global" for the runtime.
     */

    JS_BeginRequest(context);
    global = gjs_get_import_global(context);

    importer = JSVAL_VOID;
    if (!gjs_object_require_property(context, global, "global object", "imports", &importer) ||
        !JSVAL_IS_OBJECT(importer)) {
        gjs_log_exception(context, NULL);
        gjs_throw(context, "No imports property in global object");
        goto fail;
    }

    girepository = JSVAL_VOID;
    if (!gjs_object_require_property(context, JSVAL_TO_OBJECT(importer), "importer",
                                        "gi", &girepository) ||
        !JSVAL_IS_OBJECT(girepository)) {
        gjs_log_exception(context, NULL);
        gjs_throw(context, "No gi property in importer");
        goto fail;
    }

    repo_obj = JSVAL_TO_OBJECT(girepository);

    if (!gjs_object_require_property(context, repo_obj, "GI repository object", ns, &ns_obj)) {
        goto fail;
    }

    if (!JSVAL_IS_OBJECT(ns_obj)) {
        gjs_throw(context, "Namespace '%s' is not an object?", ns);
        goto fail;
    }

    JS_EndRequest(context);
    return JSVAL_TO_OBJECT(ns_obj);

 fail:
    JS_EndRequest(context);
    return NULL;
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
    }

    return "???";
}

char*
gjs_camel_from_hyphen(const char *hyphen_name)
{
    GString *s;
    const char *p;
    gboolean next_upper;

    s = g_string_sized_new(strlen(hyphen_name) + 1);

    next_upper = FALSE;
    for (p = hyphen_name; *p; ++p) {
        if (*p == '-' || *p == '_') {
            next_upper = TRUE;
        } else {
            if (next_upper) {
                g_string_append_c(s, g_ascii_toupper(*p));
                next_upper = FALSE;
            } else {
                g_string_append_c(s, *p);
            }
        }
    }

    return g_string_free(s, FALSE);
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

    return g_string_free(s, FALSE);
}
