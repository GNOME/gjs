/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2016 Endless Mobile, Inc.
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
 *
 * Authored by: Philip Chimento <philip@endlessm.com>
 */

#include "jsapi-constructor-proxy.h"
#include "jsapi-util.h"
#include "jsapi-wrapper.h"
#include "mem.h"
#include "util/log.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmismatched-tags"
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif /* __clang__ */
#include "jsproxy.h"
#pragma GCC diagnostic pop

/* This code exposes a __private_GjsConstructorProxy function to JS, which is
 * approximately equivalent to
 *
 * function __private_GjsConstructorProxy(constructor, prototype) {
 *     let my_prototype = prototype;
 *     return new Proxy(constructor, {
 *         getPrototypeOf: function (target) { return my_prototype; },
 *     });
 * }
 *
 * but with a C++-only flag that routes all property accesses through the
 * getPrototypeOf() trap, which may or may not be turned on in JS proxies,
 * I'm not sure.
 *
 * COMPAT: SpiderMonkey doesn't support the getPrototypeOf() trap in JS
 * proxies yet. That has yet to be released, in the upcoming SpiderMonkey 52.
 * When that is available, then this whole file can be discontinued.
 *
 * That is the reason for the existence of this C++ file, but the reason why it
 * is needed at all is because of Lang.Class and GObject.Class. We must give
 * class objects (e.g. "const MyClass = new Lang.Class({...})") a custom
 * prototype, so that "MyClass instanceof Lang.Class" will be true, and MyClass
 * will have methods from Class.
 *
 * Usually you would give an object a custom prototype using Object.create(),
 * but that's not possible for function or constructor objects, and MyClass of
 * course must be a constructor. Previously we solved this with
 * Object.setPrototypeOf(), but that has performance effects on any code that
 * uses objects whose prototypes have been altered [1], and SpiderMonkey started
 * printing conspicuous warnings about it.
 *
 * [1] https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Object/setPrototypeOf
 */

static const char constructor_proxy_create_name[] = "__private_GjsConstructorProxy";
/* This char's address is an arbitrary identifier for use in debugging */
static const char constructor_proxy_family = 'p';

enum {
    SLOT_PROTO,
};

/* This class is the C++ equivalent of a proxy handler object. In JS, that is
 * the second argument passed to the "new Proxy(target, handler)" constructor.
 */
class GjsConstructorHandler : public js::DirectProxyHandler {
    static inline JSObject *
    proto(JS::HandleObject proxy)
    {
        return &js::GetProxyExtra(proxy, SLOT_PROTO).toObject();
    }

public:
    GjsConstructorHandler() : js::DirectProxyHandler(&constructor_proxy_family)
    {
        setHasPrototype(true);
    }

    bool
    getPrototypeOf(JSContext              *cx,
                   JS::HandleObject        proxy,
                   JS::MutableHandleObject proto_p)
    override
    {
        proto_p.set(proto(proxy));
        return true;
    }

    /* This is called when the associated proxy object is finalized, not the
     * handler itself */
    void
    finalize(JSFreeOp *fop,
             JSObject *proxy)
    override
    {
        GJS_DEC_COUNTER(constructor_proxy);
        gjs_debug_lifecycle(GJS_DEBUG_PROXY,
                            "constructor proxy %p destroyed", proxy);
    }

    static GjsConstructorHandler&
    singleton(void)
    {
        static GjsConstructorHandler the_singleton;
        return the_singleton;
    }
};

/* Visible to JS as __private_GjsConstructorProxy(constructor, prototype) */
static bool
create_gjs_constructor_proxy(JSContext *cx,
                             unsigned   argc,
                             JS::Value *vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    if (args.length() < 2) {
        gjs_throw(cx, "Expected 2 arguments to %s, got %d",
                  constructor_proxy_create_name, args.length());
        return false;
    }

    if (!args[0].isObject() || !JS_ObjectIsFunction(cx, &args[0].toObject())) {
        /* COMPAT: Use JS::IsConstructor() in mozjs38 */
        gjs_throw(cx, "First argument must be a constructor");
        return false;
    }
    if (!args[1].isObject()) {
        gjs_throw(cx, "Second argument must be a prototype object");
        return false;
    }

    js::ProxyOptions options;
    /* "true" makes the proxy callable, otherwise the "call" and "construct"
     * traps are ignored */
    options.selectDefaultClass(true);

    JS::RootedObject proxy(cx,
        js::NewProxyObject(cx, &GjsConstructorHandler::singleton(), args[0],
                           &args[1].toObject(), nullptr, options));
    /* We stick this extra object into one of the proxy object's "extra slots",
     * even though it is private data of the proxy handler. This is because
     * proxy handlers cannot have trace callbacks. The proxy object does have a
     * built-in trace callback which traces the "extra slots", so this object
     * will be kept alive. This also means the handler has no private state at
     * all, so it can be a singleton. */
    js::SetProxyExtra(proxy, SLOT_PROTO, args[1]);

    args.rval().setObject(*proxy);

    GJS_INC_COUNTER(constructor_proxy);
    gjs_debug_lifecycle(GJS_DEBUG_PROXY,
                        "created constructor proxy %p", proxy.get());
    return true;
}

bool
gjs_define_constructor_proxy_factory(JSContext *cx)
{
    bool found;
    JS::RootedObject global(cx, gjs_get_import_global(cx));

    if (!JS_HasProperty(cx, global, constructor_proxy_create_name, &found))
        return false;
    if (found)
        return true;
    if (!JS_DefineFunction(cx, global, constructor_proxy_create_name,
        create_gjs_constructor_proxy, 2, JSPROP_READONLY | JSPROP_PERMANENT))
        return false;

    gjs_debug(GJS_DEBUG_PROXY, "Initialized constructor proxy factory");
    return true;
}
