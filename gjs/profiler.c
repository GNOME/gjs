/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2009  litl, LLC
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

#include "profiler.h"
#include <jsdbgapi.h>
#include "compat.h"
#include "jsapi-util.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

static GjsProfiler *global_profiler = NULL;
static char        *global_profiler_output = NULL;
static guint        global_profiler_output_counter = 0;
static guint        global_profile_idle = 0;


typedef struct _GjsProfileData     GjsProfileData;
typedef struct _GjsProfileFunction GjsProfileFunction;

struct _GjsProfiler {
    JSRuntime *runtime;

    GHashTable *by_file;    /* GjsProfileFunctionKey -> GjsProfileFunction */

    GjsProfileData *last_function_entered; /* weak ref to by_file */
    int64           last_function_exit_time;
};

struct _GjsProfileData {
    /* runtime state tracking */
    GjsProfileData *caller;
    int64 enter_time;
    int64 runtime_so_far;
    uintN recurse_depth;

    /* final statistics */
    uintN call_count;

    int64 total_time;
    int64 self_time;
};

typedef struct {
    char       *filename;
    uintN       lineno;
    char       *function_name;
} GjsProfileFunctionKey;

struct _GjsProfileFunction {
    GjsProfileFunctionKey key;

    GjsProfileData profile;
};

static guint
gjs_profile_function_key_hash(gconstpointer keyp)
{
    const GjsProfileFunctionKey *key = keyp;

    return g_str_hash(key->filename) ^
        key->lineno ^
        g_str_hash(key->function_name);
}

static gboolean
gjs_profile_function_key_equal(gconstpointer ap,
                               gconstpointer bp)
{
    const GjsProfileFunctionKey *a = ap;
    const GjsProfileFunctionKey *b = bp;

    g_assert(a != NULL);
    g_assert(b != NULL);

    return g_str_equal(a->filename, b->filename) &&
        a->lineno == b->lineno &&
        g_str_equal(a->function_name, b->function_name);
}

static GjsProfileFunction *
gjs_profile_function_new(GjsProfileFunctionKey *key)
{
    GjsProfileFunction *self;

    self = g_slice_new0(GjsProfileFunction);
    self->key.filename = g_strdup(key->filename);
    self->key.lineno = key->lineno;
    // Pass ownership of function_name from key to the new function
    self->key.function_name = key->function_name;

    g_assert(self->key.filename != NULL);
    g_assert(self->key.function_name != NULL);

    return self;
}

static void
gjs_profile_function_free(GjsProfileFunction *self)
{
    g_free(self->key.filename);
    g_free(self->key.function_name);
    g_slice_free(GjsProfileFunction, self);
}

static void
gjs_profile_function_key_from_js(JSContext             *cx,
                                 JSStackFrame          *fp,
                                 GjsProfileFunctionKey *key)
{
    JSScript *script;
    JSFunction *function;
    JSString *function_name;

    /* We're not using the JSScript or JSFunction as the key since the script
     * could be unloaded and addresses reused.
     */

    script = JS_GetFrameScript(cx, fp);
    if (script != NULL) {
        key->filename = (char*)JS_GetScriptFilename(cx, script);
        key->lineno = JS_GetScriptBaseLineNumber(cx, script);
    } else {
        key->filename = "(native)";
        key->lineno = 0;
    }

    function = JS_GetFrameFunction(cx, fp);
    /* If function == NULL we're probably calling a GIRepositoryFunction object
     * (or other object with a 'call' method) and would be good to somehow
     * figure out the name of the called function.
     */
    function_name = JS_GetFunctionId(function);
    if (function_name)
        key->function_name = gjs_string_get_ascii(cx, STRING_TO_JSVAL(function_name));
    else
        key->function_name = g_strdup("(unknown)");

    g_assert(key->filename != NULL);
    g_assert(key->function_name != NULL);
}

static GjsProfileFunction *
gjs_profiler_lookup_function(GjsProfiler  *self,
                             JSContext    *cx,
                             JSStackFrame *fp,
                             gboolean      create_if_missing)
{
    GjsProfileFunctionKey key;
    GjsProfileFunction *function;

    gjs_profile_function_key_from_js(cx, fp, &key);

    function = g_hash_table_lookup(self->by_file, &key);
    if (function)
        goto error;

    if (!create_if_missing)
        goto error;

    function = gjs_profile_function_new(&key);

    g_hash_table_insert(self->by_file, &function->key, function);

    /* Don't free key.function_name if we get here since we passed its
     * ownership to the new function.
     */
    return function;

 error:
    g_free(key.function_name);
    return NULL;
}

static void
gjs_profiler_log_call(GjsProfiler  *self,
                      JSContext    *cx,
                      JSStackFrame *fp,
                      JSBool        before,
                      JSBool       *ok)
{
    GjsProfileFunction *function;
    GjsProfileData *p;
    int64 now;

    function = gjs_profiler_lookup_function(self, cx, fp, before);
    if (!function)
        return;

    p = &function->profile;
    now = JS_Now();

    if (before) {
        if (p->recurse_depth == 0) {
            g_assert(p->enter_time == 0);

            /* we just exited the caller so account for the time spent */
            if (p->caller) {
                int64 delta;

                if (self->last_function_exit_time != 0) {
                    delta = now - self->last_function_exit_time;
                } else {
                    delta = now - p->caller->enter_time;
                }

                p->caller->runtime_so_far += delta;
            }

            self->last_function_exit_time = 0;
            p->runtime_so_far = 0;
            p->enter_time = now;

            p->caller = self->last_function_entered;
            self->last_function_entered = p;
        } else {
            g_assert(p->enter_time != 0);
        }

        p->recurse_depth += 1;
    } else {
        int64 delta;

        g_assert(p->recurse_depth > 0);

        p->recurse_depth -= 1;
        if (p->recurse_depth == 0) {
            g_assert(p->enter_time != 0);

            delta = now - p->enter_time;
            p->total_time += delta;

            /* two returns without function call in between */
            if (self->last_function_exit_time != 0) {
                delta = now - self->last_function_exit_time;
                p->runtime_so_far += delta;

                delta = p->runtime_so_far;
            }

            p->self_time += delta;

            self->last_function_entered = p->caller;
            p->caller = NULL;

            self->last_function_exit_time = now;
            p->enter_time = 0;
        }

        p->call_count += 1;
    }
}

static void
gjs_profiler_new_script_hook(JSContext  *cx,
                             const char *filename,
                             uintN       lineno,
                             JSScript   *script,
                             JSFunction *fun,
                             void       *callerdata)
{
#if 0
    GjsProfiler *self = callerdata;
#endif
}

static void
gjs_profiler_destroy_script_hook(JSContext *cx,
                                 JSScript  *script,
                                 void      *callerdata)
{
#if 0
    GjsProfiler *self = callerdata;
#endif
}

static void *
gjs_profiler_execute_hook(JSContext    *cx,
                          JSStackFrame *fp,
                          JSBool        before,
                          JSBool       *ok,
                          void         *callerdata)
{
    GjsProfiler *self = callerdata;

    gjs_profiler_log_call(self, cx, fp, before, ok);

    return callerdata;
}

static void *
gjs_profiler_call_hook(JSContext    *cx,
                       JSStackFrame *fp,
                       JSBool        before,
                       JSBool       *ok,
                       void         *callerdata)
{
    GjsProfiler *self = callerdata;

    gjs_profiler_log_call(self, cx, fp, before, ok);

    return callerdata;
}

static gboolean
dump_profile_idle(gpointer user_data)
{
    global_profile_idle = 0;

    gjs_profiler_dump(global_profiler);

    return FALSE;
}

static void
dump_profile_signal_handler(int signum)
{
    if (global_profile_idle == 0)
        global_profile_idle = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                                              dump_profile_idle,
                                              NULL, NULL);
}

static void
gjs_profiler_profile(GjsProfiler *self, gboolean enabled)
{
    JSRuntime *rt;

    rt = self->runtime;

    if (enabled) {
        static gboolean signal_handler_initialized = FALSE;

        if (!signal_handler_initialized) {
            struct sigaction sa;

            signal_handler_initialized = TRUE;

            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = dump_profile_signal_handler;
            sigaction(SIGUSR1, &sa, NULL);
        }

        global_profiler = self;
        g_assert(global_profiler_output != NULL);

        /* script lifetime */
        JS_SetNewScriptHook(rt, gjs_profiler_new_script_hook, self);
        JS_SetDestroyScriptHook(rt, gjs_profiler_destroy_script_hook, self);

        /* "toplevel" execution */
        JS_SetExecuteHook(rt, gjs_profiler_execute_hook, self);
        /* function call */
        JS_SetCallHook(rt, gjs_profiler_call_hook, self);
    } else if (self == global_profiler) {
        JS_SetNewScriptHook(rt, NULL, NULL);
        JS_SetDestroyScriptHook(rt, NULL, NULL);
        JS_SetExecuteHook(rt, NULL, NULL);
        JS_SetCallHook(rt, NULL, NULL);

        global_profiler = NULL;
    }
}

static void
by_file_reset_one(gpointer key,
                  gpointer value,
                  gpointer user_data)
{
    GjsProfileFunction *function = value;
    GjsProfileData *p;

    p = &function->profile;

    p->call_count = 0;
    p->self_time  = 0;
    p->total_time = 0;
}

void
gjs_profiler_reset(GjsProfiler *self)
{
    g_hash_table_foreach(self->by_file,
                         by_file_reset_one,
                         NULL);
}

static void
by_file_dump_one(gpointer key,
                 gpointer value,
                 gpointer user_data)
{
    GjsProfileFunction *function = value;
    FILE *fp = user_data;
    GjsProfileData *p;

    p = &function->profile;

    if (p->call_count == 0)
        return;

    /* file:line function calls self total */
    fprintf(fp, "%s:%u\t%s\t%u\t%.2f\t%.2f\n",
            function->key.filename, function->key.lineno,
            function->key.function_name,
            p->call_count,
            p->self_time / 1000.,
            p->total_time / 1000.);

    /* reset counters so that next dump is delta from previous */
    by_file_reset_one(key, value, user_data);
}

void
gjs_profiler_dump(GjsProfiler *self)
{
    char *filename;
    FILE *fp;

    filename = g_strdup_printf("%s.%u.%u",
                               global_profiler_output,
                               (guint)getpid(),
                               global_profiler_output_counter);
    global_profiler_output_counter += 1;

    fp = fopen(filename, "w");
    g_free(filename);

    if (!fp)
        return;

    /* file:line function calls self total */
    fprintf(fp, "file:line\tfunction\tcalls\tself\ttotal\n");

    g_hash_table_foreach(self->by_file,
                         by_file_dump_one,
                         fp);

    fclose(fp);
}

GjsProfiler *
gjs_profiler_new(JSRuntime *runtime)
{
    GjsProfiler *self;
    const char  *profiler_output;

    /* FIXME: can handle only one runtime at the moment */
    g_return_val_if_fail(global_profiler == NULL, NULL);

    self = g_slice_new0(GjsProfiler);
    self->runtime = runtime;
    self->by_file =
        g_hash_table_new_full(gjs_profile_function_key_hash,
                              gjs_profile_function_key_equal,
                              NULL,
                              (GDestroyNotify)gjs_profile_function_free);

    profiler_output = g_getenv("GJS_DEBUG_PROFILER_OUTPUT");
    if (profiler_output != NULL) {
        if (global_profiler_output == NULL) {
            global_profiler_output = g_strdup(profiler_output);
        }

        gjs_profiler_profile(self, TRUE);
        g_assert(global_profiler == self);
    }

    return self;
}

void
gjs_profiler_free(GjsProfiler *self)
{
    gjs_profiler_profile(self, FALSE);
    g_assert(global_profiler == NULL);

    g_hash_table_destroy(self->by_file);
    g_slice_free(GjsProfiler, self);
}
