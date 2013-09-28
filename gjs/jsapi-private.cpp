/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2010  litl, LLC
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
#include "jsapi-private.h"
#include "compat.h"

#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#include <jsfriendapi.h>
#pragma GCC diagnostic pop

void
gjs_error_reporter(JSContext     *context,
                   const char    *message,
                   JSErrorReport *report)
{
    const char *warning;
    GLogLevelFlags level;

    if (gjs_environment_variable_is_set("GJS_ABORT_ON_OOM") &&
        report->flags == JSREPORT_ERROR &&
        report->errorNumber == JSMSG_OUT_OF_MEMORY) {
        g_error("GJS ran out of memory at %s: %i.",
                report->filename,
                report->lineno);
    }

    if ((report->flags & JSREPORT_WARNING) != 0) {
        warning = "WARNING";
        level = G_LOG_LEVEL_MESSAGE;

        /* suppress bogus warnings. See mozilla/js/src/js.msg */
        switch (report->errorNumber) {
            /* 162, JSMSG_UNDEFINED_PROP: warns every time a lazy property
             * is resolved, since the property starts out
             * undefined. When this is a real bug it should usually
             * fail somewhere else anyhow.
             */
        case 162:
            return;
        }
    } else {
        warning = "REPORTED";
        level = G_LOG_LEVEL_WARNING;
    }

    g_log(G_LOG_DOMAIN, level, "JS %s: [%s %d]: %s", warning, report->filename, report->lineno, message);
}

JSObject *
gjs_get_global_object(JSContext *cx){
    return js::GetDefaultGlobalForContext(cx);
}
