#include <config.h>  // for GJS_VERSION

#include <errno.h>
#include <stdio.h>   // for FILE, fclose, stdout
#include <string.h>  // for strerror
#include <time.h>    // for tzset

#include "gjs/jsapi-wrapper.h"

#include "gjs/context-private.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "modules/process.h"

static bool gjs_argv(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs argv = JS::CallArgsFromVp(argc, vp);
    GjsContextPrivate* gjs_cx = GjsContextPrivate::from_cx(cx);
    std::vector<std::string> args = gjs_cx->get_args();
    std::vector<const char*> c_args;

    std::transform(args.begin(), args.end(), std::back_inserter(c_args),
                   [](const std::string& str) { return str.c_str(); });

    argv.rval().setObjectOrNull(
        gjs_build_string_array(cx, c_args.size(), (char**)c_args.data()));
    return true;
}

bool gjs_define_process_stuff(JSContext* context,
                              JS::MutableHandleObject module) {
    module.set(JS_NewPlainObject(context));

    JS::RootedValue value(context);
    return JS_DefineProperty(context, module, "argv", gjs_argv, nullptr,
                             GJS_MODULE_PROP_FLAGS | JSPROP_READONLY);
}