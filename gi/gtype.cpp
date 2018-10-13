/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2012  Red Hat, Inc.
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

#include <unordered_map>

#include "gtype.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-wrapper.h"
#include <util/log.h>
#include <girepository.h>

static bool weak_pointer_callback = false;
static std::unordered_map<GType, std::unique_ptr<JS::Heap<JSObject*>>>
    weak_pointer_list;

GJS_USE static JSObject* gjs_gtype_get_proto(JSContext* cx) G_GNUC_UNUSED;
GJS_JSAPI_RETURN_CONVENTION
static bool gjs_gtype_define_proto(JSContext *, JS::HandleObject,
                                   JS::MutableHandleObject);

GJS_DEFINE_PROTO_ABSTRACT("GIRepositoryGType", gtype,
                          JSCLASS_FOREGROUND_FINALIZE);

/* priv_from_js adds a "*", so this returns "void *" */
GJS_DEFINE_PRIV_FROM_JS(void, gjs_gtype_class);

static void
update_gtype_weak_pointers(JSContext     *cx,
                           JSCompartment *compartment,
                           void          *data)
{
    for (auto iter = weak_pointer_list.begin(); iter != weak_pointer_list.end(); ) {
        JS::Heap<JSObject *> *heap_wrapper = iter->second.get();
        JS_UpdateWeakPointerAfterGC(heap_wrapper);

        /* No read barriers are needed if the only thing we are doing with the
         * pointer is comparing it to nullptr. */
        if (heap_wrapper->unbarrieredGet() == nullptr)
            iter = weak_pointer_list.erase(iter);
        else
            iter++;
    }
}

static void
ensure_weak_pointer_callback(JSContext *cx)
{
    if (!weak_pointer_callback) {
        JS_AddWeakPointerCompartmentCallback(cx, update_gtype_weak_pointers,
                                             nullptr);
        weak_pointer_callback = true;
    }
}

static void
gjs_gtype_finalize(JSFreeOp *fop,
                   JSObject *obj)
{
    GType gtype = GPOINTER_TO_SIZE(JS_GetPrivate(obj));

    /* proto doesn't have a private set */
    if (G_UNLIKELY(gtype == 0))
        return;

    weak_pointer_list.erase(gtype);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
to_string_func(JSContext *cx,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(cx, argc, vp, rec, obj, void, priv);
    GType gtype = GPOINTER_TO_SIZE(priv);

    if (gtype == 0) {
        JS::RootedString str(cx, JS_NewStringCopyZ(cx, "[object GType prototype]"));
        if (!str)
            return false;
        rec.rval().setString(str);
        return true;
    }

    GjsAutoChar strval = g_strdup_printf("[object GType for '%s']",
                                         g_type_name(gtype));
    return gjs_string_from_utf8(cx, strval, rec.rval());
}

GJS_JSAPI_RETURN_CONVENTION
static bool
get_name_func (JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    GJS_GET_PRIV(context, argc, vp, rec, obj, void, priv);
    GType gtype;

    gtype = GPOINTER_TO_SIZE(priv);

    if (gtype == 0) {
        rec.rval().setNull();
        return true;
    }
    return gjs_string_from_utf8(context, g_type_name(gtype), rec.rval());
}

/* Properties */
JSPropertySpec gjs_gtype_proto_props[] = {
    JS_PSG("name", get_name_func, JSPROP_PERMANENT),
    JS_PS_END,
};

/* Functions */
JSFunctionSpec gjs_gtype_proto_funcs[] = {
    JS_FN("toString", to_string_func, 0, 0),
    JS_FS_END};

JSFunctionSpec gjs_gtype_static_funcs[] = { JS_FS_END };

JSObject *
gjs_gtype_create_gtype_wrapper (JSContext *context,
                                GType      gtype)
{
    g_assert(((void) "Attempted to create wrapper object for invalid GType",
              gtype != 0));

    JSAutoRequest ar(context);

    auto heap_wrapper_it = weak_pointer_list.find(gtype);
    if (heap_wrapper_it != std::end(weak_pointer_list))
        return *heap_wrapper_it->second;

    JS::RootedObject proto(context);
    if (!gjs_gtype_define_proto(context, nullptr, &proto))
        return nullptr;

    auto heap_wrapper = std::make_unique<JS::Heap<JSObject *>>();
    if (!(*heap_wrapper =
          JS_NewObjectWithGivenProto(context, &gjs_gtype_class, proto)))
        return nullptr;

    JS_SetPrivate(*heap_wrapper, GSIZE_TO_POINTER(gtype));
    ensure_weak_pointer_callback(context);

    /* Saving a reference to the wrapper pointer, as heap_wrapper will be
     * nullified by std::move */
    JSObject *gtype_wrapper = *heap_wrapper;
    weak_pointer_list.insert({gtype, std::move(heap_wrapper)});

    return gtype_wrapper;
}

GJS_JSAPI_RETURN_CONVENTION
static bool _gjs_gtype_get_actual_gtype(JSContext* context,
                                        JS::HandleObject object,
                                        GType* gtype_out, int recurse) {
    if (JS_InstanceOf(context, object, &gjs_gtype_class, nullptr)) {
        *gtype_out = GPOINTER_TO_SIZE(priv_from_js(context, object));
        return true;
    }

    JS::RootedValue gtype_val(context);

    /* OK, we don't have a GType wrapper object -- grab the "$gtype"
     * property on that and hope it's a GType wrapper object */
    if (!JS_GetProperty(context, object, "$gtype", &gtype_val))
        return false;
    if (!gtype_val.isObject()) {
        /* OK, so we're not a class. But maybe we're an instance. Check
           for "constructor" and recurse on that. */
        if (!JS_GetProperty(context, object, "constructor", &gtype_val))
            return false;
    }

    if (recurse > 0 && gtype_val.isObject()) {
        JS::RootedObject gtype_obj(context, &gtype_val.toObject());
        return _gjs_gtype_get_actual_gtype(context, gtype_obj, gtype_out,
                                           recurse - 1);
    }

    *gtype_out = G_TYPE_INVALID;
    return true;
}

bool gjs_gtype_get_actual_gtype(JSContext* context, JS::HandleObject object,
                                GType* gtype_out) {
    g_assert(gtype_out && "Missing return location");
    JSAutoRequest ar(context);

    /* 2 means: recurse at most three times (including this
       call).
       The levels are calculated considering that, in the
       worst case we need to go from instance to class, from
       class to GType object and from GType object to
       GType value.
     */

    return _gjs_gtype_get_actual_gtype(context, object, gtype_out, 2);
}

bool
gjs_typecheck_gtype (JSContext             *context,
                     JS::HandleObject       obj,
                     bool                   throw_error)
{
    return do_base_typecheck(context, obj, throw_error);
}

const char *
gjs_get_names_from_gtype_and_gi_info(GType        gtype,
                                     GIBaseInfo  *info,
                                     const char **constructor_name)
{
    const char *ns;
    /* ns is only used to set the JSClass->name field (exposed by
     * Object.prototype.toString).
     * We can safely set "unknown" if there is no info, as in that case
     * the name is globally unique (it's a GType name). */
    if (info) {
        ns = g_base_info_get_namespace((GIBaseInfo*) info);
        if (constructor_name)
            *constructor_name = g_base_info_get_name((GIBaseInfo*) info);
    } else {
        ns = "unknown";
        if (constructor_name)
            *constructor_name = g_type_name(gtype);
    }
    return ns;
}
