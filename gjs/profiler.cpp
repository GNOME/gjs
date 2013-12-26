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

#include "debug-connection.h"
#include "interrupt-register.h"
#include "profiler.h"
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
    GHashTable *by_file;    /* GjsProfileFunctionKey -> GjsProfileFunction */
    GjsInterruptRegister *interrupts;
    GjsDebugConnection *interpreter_connection;

    GjsProfileData *last_function_entered; /* weak ref to by_file */
    int64_t         last_function_exit_time;
};

struct _GjsProfileData {
    /* runtime state tracking */
    GjsProfileData *caller;
    int64_t enter_time;
    int64_t runtime_so_far;
    unsigned recurse_depth;

    /* final statistics */
    unsigned call_count;

    int64_t total_time;
    int64_t self_time;
};

typedef struct {
    char       *filename;
    unsigned    lineno;
    char       *function_name;
} GjsProfileFunctionKey;

struct _GjsProfileFunction {
    GjsProfileFunctionKey key;

    GjsProfileData profile;
};

static guint
gjs_profile_function_key_hash(gconstpointer keyp)
{
    const GjsProfileFunctionKey *key = (const GjsProfileFunctionKey*) keyp;

    return g_str_hash(key->filename) ^
        key->lineno ^
        g_str_hash(key->function_name);
}

static gboolean
gjs_profile_function_key_equal(gconstpointer ap,
                               gconstpointer bp)
{
    const GjsProfileFunctionKey *a = (const GjsProfileFunctionKey*) ap;
    const GjsProfileFunctionKey *b = (const GjsProfileFunctionKey*) bp;

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
    self->key.function_name = g_strdup (key->function_name);

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

static GjsProfileFunction *
gjs_profiler_lookup_function(GjsProfiler       *self,
                             GjsFrameInfo      *info,
                             gboolean          create_if_missing)
{
    GjsProfileFunctionKey key =
    {
      (char *) info->interrupt.filename,
      info->interrupt.line,
      (char *) info->interrupt.functionName
    };
    GjsProfileFunction *function;

    function = (GjsProfileFunction*) g_hash_table_lookup(self->by_file, &key);
    if (function)
        return function;

    if (!create_if_missing)
        return NULL;

    function = gjs_profile_function_new(&key);

    /* The function now owns the strings inside of key, so use its
     * copy as the key and not the one we just initialized above
     * (which has no ownership) */
    g_hash_table_insert(self->by_file, &function->key, function);

    /* Don't free key.function_name if we get here since we passed its
     * ownership to the new function.
     */
    return function;
}

static void
gjs_profiler_log_call (GjsInterruptRegister *reg,
                       GjsContext           *context,
                       GjsFrameInfo         *info,
                       gpointer             user_data)
{
    GjsProfiler *self = (GjsProfiler *) user_data;
    GjsProfileFunction *function;
    GjsProfileData *p;
    int64_t now;

    function = gjs_profiler_lookup_function(self,
                                            info,
                                            info->frame_state == GJS_INTERRUPT_FRAME_BEFORE);
    if (!function)
        return;

    p = &function->profile;
    now = JS_Now();

    if (info->frame_state == GJS_INTERRUPT_FRAME_BEFORE) {
        if (p->recurse_depth == 0) {
            g_assert(p->enter_time == 0);

            /* we just exited the caller so account for the time spent */
            if (p->caller) {
                int64_t delta;

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
        int64_t delta;

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

        self->interpreter_connection =
            gjs_interrupt_register_connect_to_function_calls_and_execution (self->interrupts,
                                                                            gjs_profiler_log_call,
                                                                            self);
    } else if (self == global_profiler) {
        g_object_unref(self->interpreter_connection);
        global_profiler = NULL;
    }
}

static void
by_file_reset_one(gpointer key,
                  gpointer value,
                  gpointer user_data)
{
    GjsProfileFunction *function = (GjsProfileFunction*) value;
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
    GjsProfileFunction *function = (GjsProfileFunction*) value;
    FILE *fp = (FILE*) user_data;
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
gjs_profiler_new(GjsInterruptRegister *interrupts)
{
    GjsProfiler *self;
    const char  *profiler_output;

    /* FIXME: can handle only one runtime at the moment */
    g_return_val_if_fail(global_profiler == NULL, NULL);

    self = g_slice_new0(GjsProfiler);
    self->by_file =
        g_hash_table_new_full(gjs_profile_function_key_hash,
                              gjs_profile_function_key_equal,
                              NULL,
                              (GDestroyNotify)gjs_profile_function_free);
    self->interrupts = (GjsInterruptRegister *) g_object_ref(interrupts);

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

    if (self->interpreter_connection)
        g_object_unref(self->interpreter_connection);
    g_object_unref(self->interrupts);

    g_slice_free(GjsProfiler, self);
}
