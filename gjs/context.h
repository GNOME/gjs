/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef __GJS_CONTEXT_H__
#define __GJS_CONTEXT_H__

#if !defined (__GJS_GJS_H__) && !defined (GJS_COMPILATION)
#error "Only <gjs/gjs.h> can be included directly."
#endif

#include <stdbool.h>
#include <glib-object.h>

#include <gjs/macros.h>
#include <gjs/profiler.h>

G_BEGIN_DECLS

typedef struct _GjsContext      GjsContext;
typedef struct _GjsContextClass GjsContextClass;

#define GJS_TYPE_CONTEXT              (gjs_context_get_type ())
#define GJS_CONTEXT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GJS_TYPE_CONTEXT, GjsContext))
#define GJS_CONTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GJS_TYPE_CONTEXT, GjsContextClass))
#define GJS_IS_CONTEXT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GJS_TYPE_CONTEXT))
#define GJS_IS_CONTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GJS_TYPE_CONTEXT))
#define GJS_CONTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GJS_TYPE_CONTEXT, GjsContextClass))

GJS_EXPORT
GType           gjs_context_get_type             (void) G_GNUC_CONST;

GJS_EXPORT
GjsContext*     gjs_context_new                  (void);
GJS_EXPORT
GjsContext*     gjs_context_new_with_search_path (char         **search_path);
GJS_EXPORT
bool            gjs_context_eval_file            (GjsContext  *js_context,
                                                  const char    *filename,
                                                  int           *exit_status_p,
                                                  GError       **error);
GJS_EXPORT
bool            gjs_context_eval                 (GjsContext  *js_context,
                                                  const char    *script,
                                                  gssize         script_len,
                                                  const char    *filename,
                                                  int           *exit_status_p,
                                                  GError       **error);
GJS_EXPORT
bool            gjs_context_define_string_array  (GjsContext  *js_context,
                                                  const char    *array_name,
                                                  gssize         array_length,
                                                  const char   **array_values,
                                                  GError       **error);

GJS_EXPORT
GList*          gjs_context_get_all              (void);

GJS_EXPORT
GjsContext     *gjs_context_get_current          (void);
GJS_EXPORT
void            gjs_context_make_current         (GjsContext *js_context);

GJS_EXPORT
void*           gjs_context_get_native_context   (GjsContext *js_context);

GJS_EXPORT
void            gjs_context_print_stack_stderr    (GjsContext *js_context);

GJS_EXPORT
void            gjs_context_get_stack_trace       (GjsContext *context,
                                                   char       *buffer,
                                                   gsize       buffer_size);

GJS_EXPORT
void            gjs_context_maybe_gc              (GjsContext  *context);

GJS_EXPORT
void            gjs_context_gc                    (GjsContext  *context);

GJS_EXPORT
GjsProfiler *gjs_context_get_profiler(GjsContext *self);

GJS_EXPORT
bool gjs_profiler_chain_signal(GjsContext *context,
                               siginfo_t  *info);

GJS_EXPORT
void            gjs_dumpstack                     (void);

G_END_DECLS

#endif  /* __GJS_CONTEXT_H__ */
