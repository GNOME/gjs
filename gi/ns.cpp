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

#include "ns.h"
#include "repo.h"
#include "param.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include "gjs/mem.h"

#include <util/log.h>
#include <girepository.h>

#include <string.h>

typedef struct {
    char *gi_namespace;
} Ns;

extern struct JSClass gjs_ns_class;

GJS_DEFINE_PRIV_FROM_JS(Ns, gjs_ns_class)

/* The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved. */
GJS_JSAPI_RETURN_CONVENTION
static bool
ns_resolve(JSContext       *context,
           JS::HandleObject obj,
           JS::HandleId     id,
           bool            *resolved)
{
    Ns *priv;
    bool defined;

    if (!JSID_IS_STRING(id)) {
        *resolved = false;
        return true; /* not resolved, but no error */
    }

    /* let Object.prototype resolve these */
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (id == atoms.to_string() || id == atoms.value_of()) {
        *resolved = false;
        return true;
    }

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_GNAMESPACE,
                     "Resolve prop '%s' hook, obj %s, priv %p",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str(), priv);

    if (priv == NULL) {
        *resolved = false;  /* we are the prototype, or have the wrong class */
        return true;
    }

    JS::UniqueChars name;
    if (!gjs_get_string_id(context, id, &name))
        return false;
    if (!name) {
        *resolved = false;
        return true;  /* not resolved, but no error */
    }

    GjsAutoBaseInfo info =
        g_irepository_find_by_name(nullptr, priv->gi_namespace, name.get());
    if (!info) {
        *resolved = false; /* No property defined, but no error either */
        return true;
    }

    gjs_debug(GJS_DEBUG_GNAMESPACE,
              "Found info type %s for '%s' in namespace '%s'",
              gjs_info_type_name(info.type()), info.name(), info.ns());

    JSAutoRequest ar(context);

    if (!gjs_define_info(context, obj, info, &defined)) {
        gjs_debug(GJS_DEBUG_GNAMESPACE, "Failed to define info '%s'",
                  info.name());
        return false;
    }

    /* we defined the property in this object? */
    *resolved = defined;
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
get_name (JSContext *context,
          unsigned   argc,
          JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, args, obj, Ns, priv);

    if (priv == NULL)
        return false;

    return gjs_string_from_utf8(context, priv->gi_namespace, args.rval());
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(ns)

static void
ns_finalize(JSFreeOp *fop,
            JSObject *obj)
{
    Ns *priv;

    priv = (Ns *)JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance */

    if (priv->gi_namespace)
        g_free(priv->gi_namespace);

    GJS_DEC_COUNTER(ns);
    g_slice_free(Ns, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const struct JSClassOps gjs_ns_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    ns_resolve,
    nullptr,  // mayResolve
    ns_finalize};

struct JSClass gjs_ns_class = {
    "GIRepositoryNamespace",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &gjs_ns_class_ops
};

static JSPropertySpec gjs_ns_proto_props[] = {
    JS_PSG("__name__", get_name, GJS_MODULE_PROP_FLAGS),
    JS_PS_END
};

static JSFunctionSpec *gjs_ns_proto_funcs = nullptr;
static JSFunctionSpec *gjs_ns_static_funcs = nullptr;

GJS_DEFINE_PROTO_FUNCS(ns)

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
ns_new(JSContext    *context,
       const char   *ns_name)
{
    Ns *priv;

    JS::RootedObject proto(context);
    if (!gjs_ns_define_proto(context, nullptr, &proto))
        return nullptr;

    JS::RootedObject ns(context,
        JS_NewObjectWithGivenProto(context, &gjs_ns_class, proto));
    if (!ns)
        return nullptr;

    priv = g_slice_new0(Ns);

    GJS_INC_COUNTER(ns);

    g_assert(priv_from_js(context, ns) == NULL);
    JS_SetPrivate(ns, priv);

    gjs_debug_lifecycle(GJS_DEBUG_GNAMESPACE, "ns constructor, obj %p priv %p",
                        ns.get(), priv);

    priv = priv_from_js(context, ns);
    priv->gi_namespace = g_strdup(ns_name);
    return ns;
}

JSObject*
gjs_create_ns(JSContext    *context,
              const char   *ns_name)
{
    return ns_new(context, ns_name);
}
