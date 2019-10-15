#ifndef MODULES_PROCESS_H_
#define MODULES_PROCESS_H_

#include "gjs/jsapi-wrapper.h"

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_process_stuff(JSContext* context,
                              JS::MutableHandleObject module);

#endif  // MODULES_PROCESS_H_
