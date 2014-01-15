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
} GjsRuntimeData;

static inline GjsRuntimeData *
get_data(JSRuntime *runtime)
{
    return (GjsRuntimeData*) JS_GetRuntimePrivate(runtime);
}

void
gjs_runtime_init_for_context(JSRuntime *runtime,
                             JSContext *context)
{
    GjsRuntimeData *data;

    data = g_new(GjsRuntimeData, 1);
    data->context = context;
    JS_SetRuntimePrivate(runtime, data);
}

void
gjs_runtime_deinit(JSRuntime *runtime)
{
    g_free(get_data(runtime));
}
