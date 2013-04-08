/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2013 Giovanni Campagna <scampa.giovanni@gmail.com>
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

#include <util/log.h>
#include <util/glib.h>
#include <util/misc.h>

#include "jsapi-util.h"
#include "compat.h"
#include "jsapi-private.h"
#include "runtime.h"

#include <string.h>
#include <math.h>

typedef struct {
    JSContext *context;
    jsid const_strings[GJS_STRING_LAST];
} GjsRuntimeData;

/* Keep this consistent with GjsConstString */
static const char *const_strings[] = {
    "constructor", "prototype", "length",
    "imports", "__parentModule__", "__init__", "searchPath",
    "__gjsKeepAlive", "__gjsPrivateNS",
    "gi", "versions", "overrides",
    "_init", "_new_internal", "new",
    "message", "code", "stack", "fileName", "lineNumber"
};

G_STATIC_ASSERT(G_N_ELEMENTS(const_strings) == GJS_STRING_LAST);

static inline GjsRuntimeData *
get_data(JSRuntime *runtime)
{
    return (GjsRuntimeData*) JS_GetRuntimePrivate(runtime);
}

/**
 * gjs_runtime_get_context:
 * @runtime: a #JSRuntime
 *
 * Gets the context associated with this runtime.
 *
 * Return value: the context, or %NULL if GJS hasn't been initialized
 * for the runtime or is being shut down.
 */
JSContext *
gjs_runtime_get_context(JSRuntime *runtime)
{
    return get_data(runtime)->context;
}

jsid
gjs_runtime_get_const_string(JSRuntime      *runtime,
                             GjsConstString  name)
{
    /* Do not add prerequisite checks here, this is a very hot call! */

    return get_data(runtime)->const_strings[name];
}

void
gjs_runtime_init_for_context(JSRuntime *runtime,
                             JSContext *context)
{
    GjsRuntimeData *data;
    int i;

    data = g_new(GjsRuntimeData, 1);

    data->context = context;
    for (i = 0; i < GJS_STRING_LAST; i++)
        data->const_strings[i] = gjs_intern_string_to_id(context, const_strings[i]);

    JS_SetRuntimePrivate(runtime, data);
}

void
gjs_runtime_deinit(JSRuntime *runtime)
{
    g_free(get_data(runtime));
}
