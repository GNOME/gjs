#pragma once

#include <config.h>

#include <js/RootingAPI.h>
#include <js/StructuredClone.h>
#include <js/TypeDecls.h>

namespace Gjs {
namespace WorkerGlobal {
bool post_message(JSContext* cx, unsigned argc, JS::Value* vp);
};
};  // namespace Gjs

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_worker_stuff(JSContext* cx, JS::MutableHandleObject module);