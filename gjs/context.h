/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2008 litl, LLC
 */

#ifndef GJS_CONTEXT_H_
#define GJS_CONTEXT_H_

#if !defined(INSIDE_GJS_H) && !defined(GJS_COMPILATION)
#    error "Only <gjs/gjs.h> can be included directly."
#endif

#include <stdbool.h>    /* IWYU pragma: keep */
#include <stdint.h>
#include <sys/types.h> /* for ssize_t */

#ifndef _WIN32
#    include <signal.h> /* for siginfo_t */
#endif

#include <glib-object.h>
#include <glib.h>

#include <gjs/macros.h>
#include <gjs/profiler.h>

G_BEGIN_DECLS

#define GJS_TYPE_CONTEXT              (gjs_context_get_type ())

GJS_EXPORT GJS_USE G_DECLARE_FINAL_TYPE(GjsContext, gjs_context, GJS, CONTEXT,
                                        GObject);

/* These class macros are not defined by G_DECLARE_FINAL_TYPE, but are kept for
 * backwards compatibility */
#define GJS_CONTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GJS_TYPE_CONTEXT, GjsContextClass))
#define GJS_IS_CONTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GJS_TYPE_CONTEXT))
#define GJS_CONTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GJS_TYPE_CONTEXT, GjsContextClass))

typedef void (*GjsContextInRealmFunc)(GjsContext*, void*);

GJS_EXPORT GJS_USE GjsContext* gjs_context_new(void);
GJS_EXPORT GJS_USE GjsContext* gjs_context_new_with_search_path(
    char** search_path);
GJS_EXPORT GJS_USE bool gjs_context_eval_file(GjsContext* js_context,
                                              const char* filename,
                                              int* exit_status_p,
                                              GError** error);
GJS_EXPORT GJS_USE bool gjs_context_eval_module_file(GjsContext* js_context,
                                                     const char* filename,
                                                     uint8_t* exit_status_p,
                                                     GError** error);
GJS_EXPORT GJS_USE bool gjs_context_eval(GjsContext* js_context,
                                         const char* script, gssize script_len,
                                         const char* filename,
                                         int* exit_status_p, GError** error);
GJS_EXPORT GJS_USE bool gjs_context_register_module(GjsContext* context,
                                                    const char* identifier,
                                                    const char* uri,
                                                    GError** error);
GJS_EXPORT GJS_USE bool gjs_context_eval_module(GjsContext* context,
                                                const char* identifier,
                                                uint8_t* exit_code,
                                                GError** error);
GJS_EXPORT GJS_USE bool gjs_context_define_string_array(
    GjsContext* js_context, const char* array_name, gssize array_length,
    const char** array_values, GError** error);

GJS_EXPORT void gjs_context_set_argv(GjsContext* js_context,
                                     ssize_t array_length,
                                     const char** array_values);

GJS_EXPORT GJS_USE GList* gjs_context_get_all(void);

GJS_EXPORT GJS_USE GjsContext* gjs_context_get_current(void);
GJS_EXPORT
void            gjs_context_make_current         (GjsContext *js_context);

GJS_EXPORT
void*           gjs_context_get_native_context   (GjsContext *js_context);

GJS_EXPORT void gjs_context_run_in_realm(GjsContext* gjs,
                                         GjsContextInRealmFunc func,
                                         void* user_data);

GJS_EXPORT
void            gjs_context_print_stack_stderr    (GjsContext *js_context);

GJS_EXPORT
void            gjs_context_maybe_gc              (GjsContext  *context);

GJS_EXPORT
void            gjs_context_gc                    (GjsContext  *context);

GJS_EXPORT GJS_USE GjsProfiler* gjs_context_get_profiler(GjsContext* self);

GJS_EXPORT GJS_USE bool gjs_profiler_chain_signal(GjsContext* context,
                                                  siginfo_t* info);

GJS_EXPORT
void            gjs_dumpstack                     (void);

GJS_EXPORT GJS_USE const char* gjs_get_js_version(void);

GJS_EXPORT
void gjs_context_setup_debugger_console(GjsContext* gjs);

GJS_EXPORT GJS_USE const char* gjs_context_get_repl_history_path(GjsContext*);
G_END_DECLS

#endif /* GJS_CONTEXT_H_ */
