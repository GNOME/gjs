/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#include <glib-object.h>

#include <gjs/gjs.h>
#include <gjs/compat.h>
#include <gjs/gjs-module.h>

#include <gjs/debug-connection.h>
#include <gjs/debug-interrupt-register.h>
#include <gjs/executable-linesutil.h>
#include <gjs/interrupt-register.h>

static void gjs_debug_interrupt_register_interface_init(GjsInterruptRegisterInterface *);

struct _GjsDebugInterruptRegisterPrivate
{
    /* Hook lock count */
    guint      debug_mode_lock_count;
    guint      single_step_mode_lock_count;
    guint      interrupt_function_lock_count;
    guint      call_and_execute_hook_lock_count;
    guint      new_script_hook_lock_count;

    /* These are data structures which contain callback points
     * whenever our internal JS debugger hooks get called */
    GHashTable *breakpoints;
    GHashTable *pending_breakpoints;
    GArray     *single_step_hooks;
    GArray     *call_and_execute_hooks;
    GArray     *new_script_hooks;

    /* These are data structures which we can use to
     * look up the keys for the above structures on
     * destruction. */
    GHashTable *breakpoints_connections;
    GHashTable *single_step_connections;
    GHashTable *call_and_execute_connections;
    GHashTable *new_script_connections;

    /* This is a hashtable of names to known scripts */
    GHashTable *scripts_loaded;

    /* References */
    GjsContext *context;
};

G_DEFINE_TYPE_WITH_CODE(GjsDebugInterruptRegister,
                        gjs_debug_interrupt_register,
                        G_TYPE_OBJECT,
                        G_ADD_PRIVATE(GjsDebugInterruptRegister)
                        G_IMPLEMENT_INTERFACE(GJS_TYPE_INTERRUPT_REGISTER_INTERFACE,
                                              gjs_debug_interrupt_register_interface_init))

typedef struct _GjsDebugUserCallback
{
    GCallback callback;
    gpointer  user_data;
} GjsDebugUserCallback;

static void gjs_debug_interrupt_register_unlock_new_script_callback(GjsDebugInterruptRegister *reg);

static void
gjs_debug_user_callback_asssign(GjsDebugUserCallback *user_callback,
                                GCallback            callback,
                                gpointer             user_data)
{
    user_callback->callback = callback;
    user_callback->user_data = user_data;
}

static GjsDebugUserCallback *
gjs_debug_user_callback_new(GCallback callback,
                            gpointer  user_data)
{
    GjsDebugUserCallback *user_callback = g_new0(GjsDebugUserCallback, 1);
    gjs_debug_user_callback_asssign(user_callback, callback, user_data);
    return user_callback;
}

static void
gjs_debug_user_callback_free(GjsDebugUserCallback *user_callback)
{
    g_free(user_callback);
}

static gint
gjs_debug_user_callback_compare(gconstpointer first,
                                gconstpointer second)
{
    return first > second;
}

typedef struct _GjsDebugScriptLookupInfo
{
    gchar *name;
    guint lineno;
} GjsDebugScriptLookupInfo;

GjsDebugScriptLookupInfo *
gjs_debug_script_lookup_info_populate(GjsDebugScriptLookupInfo *info,
                                      const gchar              *name,
                                      guint                    lineno)
{
    info->name = g_strdup(name);
    info->lineno = lineno;
    return info;
}

GjsDebugScriptLookupInfo *
gjs_debug_script_lookup_info_new(const gchar *name,
                                 guint       lineno)
{
    GjsDebugScriptLookupInfo *info = g_new0(GjsDebugScriptLookupInfo, 1);
    return gjs_debug_script_lookup_info_populate(info, name, lineno);
}

guint
gjs_debug_script_lookup_info_hash (gconstpointer key)
{
    GjsDebugScriptLookupInfo *info = (GjsDebugScriptLookupInfo *) key;
    return g_int_hash(&info->lineno) ^ g_str_hash(info->name);
}

gboolean
gjs_debug_script_lookup_info_equal(gconstpointer first,
                                   gconstpointer second)
{
    GjsDebugScriptLookupInfo *first_info = (GjsDebugScriptLookupInfo *) first;
    GjsDebugScriptLookupInfo *second_info = (GjsDebugScriptLookupInfo *) second;

    return first_info->lineno == second_info->lineno &&
           g_strcmp0(first_info->name, second_info->name) == 0;
}

void
gjs_debug_script_lookup_info_destroy (gpointer info)
{
    GjsDebugScriptLookupInfo *lookup_info = (GjsDebugScriptLookupInfo *) info;
    g_free(lookup_info->name);
    g_free(lookup_info);
}

typedef struct _InterruptCallbackDispatchData
{
    GjsDebugInterruptRegister *reg;
    GjsInterruptInfo          *info;
} InterruptCallbackDispatchData;

static void
dispatch_interrupt_callbacks (gpointer element,
                              gpointer user_data)
{
    GjsDebugUserCallback          *user_callback = (GjsDebugUserCallback *) element;
    InterruptCallbackDispatchData *dispatch_data = (InterruptCallbackDispatchData *) user_data;
    GjsContext                    *context = dispatch_data->reg->priv->context;
    GjsInterruptCallback          callback = (GjsInterruptCallback) user_callback->callback;

    callback(GJS_INTERRUPT_REGISTER_INTERFACE (dispatch_data->reg),
             context,
             dispatch_data->info,
             user_callback->user_data);
}

typedef struct _InfoCallbackDispatchData
{
    GjsDebugInterruptRegister *reg;
    GjsDebugScriptInfo        *info;
} InfoCallbackDispatchData;

static void
dispatch_info_callbacks (gpointer element,
                         gpointer user_data)

{
    GjsDebugUserCallback     *user_callback = (GjsDebugUserCallback *) element;
    InfoCallbackDispatchData *dispatch_data = (InfoCallbackDispatchData *) user_data;
    GjsContext               *context = dispatch_data->reg->priv->context;
    GjsInfoCallback          callback = (GjsInfoCallback) user_callback->callback;

    callback(GJS_INTERRUPT_REGISTER_INTERFACE (dispatch_data->reg),
             context,
             dispatch_data->info,
             user_callback->user_data);
}

typedef struct _FrameCallbackDispatchData
{
  GjsDebugInterruptRegister *reg;
  GjsFrameInfo              *info;
} FrameCallbackDispatchData;

static void
dispatch_frame_callbacks (gpointer element,
                          gpointer user_data)

{
    GjsDebugUserCallback      *user_callback = (GjsDebugUserCallback *) element;
    FrameCallbackDispatchData *dispatch_data = (FrameCallbackDispatchData *) user_data;
    GjsContext                *context = dispatch_data->reg->priv->context;
    GjsFrameCallback          callback = (GjsFrameCallback) user_callback->callback;

    callback(GJS_INTERRUPT_REGISTER_INTERFACE(dispatch_data->reg),
             context,
             dispatch_data->info,
             user_callback->user_data);
}

static void
g_array_foreach(GArray   *array,
                GFunc    func,
                gpointer user_data)
{
    const gsize element_size = g_array_get_element_size(array);
    guint       i = 0;
    gchar       *current_array_pointer = (gchar *) array->data;

    for (; i < array->len; ++i, current_array_pointer += element_size)
        (*func)(current_array_pointer, user_data);
}

static gchar *
get_fully_qualified_path(const gchar *filename)
{
    gchar *fully_qualified_path = NULL;
    /* Sometimes we might get just a basename if the script is in the current
     * working directly. If that's the case, then we need to add the fully
     * qualified pathname */
    if (strstr(filename, "/") == NULL)
    {
        gchar *current_dir = g_get_current_dir();
        fully_qualified_path = g_strconcat(current_dir,
                                           "/",
                                           filename,
                                           NULL);
        g_free(current_dir);
    }
    else
        fully_qualified_path = g_strdup(filename);

    return fully_qualified_path;
}

static void
gjs_debug_interrupt_register_populate_interrupt_info_from_js_function(GjsInterruptInfo *info,
                                                                      JSContext        *js_context,
                                                                      JSScript         *script,
                                                                      JSFunction       *js_function)
{
    JSString *js_function_name = NULL;

    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    if (js_function)
        js_function_name = JS_GetFunctionId(js_function);

    info->filename = get_fully_qualified_path(JS_GetScriptFilename(js_context, script));
    info->line = JS_GetScriptBaseLineNumber(js_context, script);

    gchar *function_name = NULL;

    if (!js_function_name ||
      !gjs_string_to_utf8(js_context,
                          STRING_TO_JSVAL(js_function_name),
                          &function_name))
    function_name = g_strdup("(unknown)");

    info->functionName = function_name;
}

static void
gjs_debug_interrupt_register_populate_interrupt_info(GjsInterruptInfo *info,
                                                     JSContext        *js_context,
                                                     JSScript         *script,
                                                     jsbytecode       *pc)
{
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

  JSFunction *js_function  = JS_GetScriptFunction(js_context, script);
  gjs_debug_interrupt_register_populate_interrupt_info_from_js_function(info,
                                                                        js_context,
                                                                        script,
                                                                        js_function);
  info->line = JS_PCToLineNumber(js_context, script, pc);
}

static void
gjs_debug_interrupt_register_clear_interrupt_info_dynamic(GjsInterruptInfo *info)
{
    if (info->functionName)
        g_free((gchar *) info->functionName);

    if (info->filename)
        g_free((gchar *) info->filename);
}

static void
gjs_debug_interrupt_register_populate_script_info(GjsDebugScriptInfo *info,
                                                  JSContext          *js_context,
                                                  JSScript           *script,
                                                  const gchar        *filename,
                                                  guint              begin_line)
{
    gchar  *contents = NULL;
    gsize  length = 0;
    GError *error = NULL;

    /* If we couldn't read the script contents, we can still continue here
    * although the actual executable lines might not be as accurate
    * as we would like */
    if (!g_file_get_contents(filename,
                             &contents,
                             &length,
                             &error))
    {
        g_printerr("Error occurred in reading file %s: %s\n",
                   filename,
                   error->message);
        g_clear_error(&error);
    }

    info->executable_lines =
        gjs_context_get_executable_lines_for_native_script((GjsContext *) JS_GetContextPrivate(js_context),
                                                           script,
                                                           contents,
                                                           begin_line,
                                                           &info->n_executable_lines);

    if (contents)
        g_free(contents);

    info->filename = filename;
}

static void
gjs_debug_interrupt_register_clear_script_info_dynamic(GjsDebugScriptInfo *info)
{
    if (info->executable_lines)
        g_free(info->executable_lines);
}

typedef struct _GjsBreakpoint
{
    JSScript *script;
    jsbytecode *pc;
} GjsBreakpoint;

static void
gjs_breakpoint_destroy(gpointer data)
{
    GjsBreakpoint *breakpoint = (GjsBreakpoint *) data;

    g_free(breakpoint);
}

static GjsBreakpoint *
gjs_breakpoint_new(JSScript   *script,
                   jsbytecode *pc)
{
    GjsBreakpoint *breakpoint = g_new0(GjsBreakpoint, 1);
    breakpoint->script = script;
    breakpoint->pc = pc;
    return breakpoint;
}

typedef struct _GjsPendingBreakpoint
{
    gchar *filename;
    guint lineno;
} GjsPendingBreakpoint;

static void
gjs_pending_breakpoint_destroy(gpointer data)
{
    GjsPendingBreakpoint *pending = (GjsPendingBreakpoint *) data;
    g_free(pending->filename);
    g_free(pending);
}

static GjsPendingBreakpoint *
gjs_pending_breakpoint_new(const gchar *filename,
                           guint       lineno)
{
    GjsPendingBreakpoint *pending = g_new0(GjsPendingBreakpoint, 1);
    pending->filename = g_strdup(filename);
    pending->lineno = lineno;
    return pending;
}

typedef struct _BreakpointActivationData
{
    GjsDebugInterruptRegister *debug_register;
    JSContext                 *js_context;
    JSScript                  *js_script;
    const gchar               *filename;
    guint                     begin_lineno;
    GHashTable                *breakpoints;
    GList                     *breakpoints_changed;
} BreakpointActivationData;

static void
remove_breakpoint_from_hashtable_by_user_callback(gpointer list_item,
                                                  gpointer hashtable_pointer)
{
    GHashTable           *breakpoints = (GHashTable *) hashtable_pointer;
    GjsPendingBreakpoint *pending_breakpoint = (GjsPendingBreakpoint *) g_hash_table_lookup(breakpoints, list_item);

    gjs_pending_breakpoint_destroy(pending_breakpoint);
    g_hash_table_remove(breakpoints, list_item);
}

static void
remove_activated_breakpoints_from_pending(BreakpointActivationData *data,
                                          GHashTable               *pending)
{
    if (data->breakpoints_changed)
    {
        g_list_foreach(data->breakpoints_changed,
                       remove_breakpoint_from_hashtable_by_user_callback,
                       pending);
        g_list_free(data->breakpoints_changed);
    }
}

static guint
get_script_end_lineno(JSContext *js_context,
                      JSScript  *js_script)
{
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    jsbytecode *pc = JS_EndPC(js_context, js_script);
    return JS_PCToLineNumber(js_context,
                             js_script,
                             pc);
}

typedef struct _GjsDebugInterruptRegisterTrapPrivateData
{
    GjsDebugInterruptRegister *reg;
    GjsDebugUserCallback *user_callback;
} GjsDebugInterruptRegisterTrapPrivateData;

GjsDebugInterruptRegisterTrapPrivateData *
gjs_debug_interrupt_register_trap_private_data_new(GjsDebugInterruptRegister *reg,
                                                   GjsDebugUserCallback      *user_callback)
{
    GjsDebugInterruptRegisterTrapPrivateData *data =
        g_new0(GjsDebugInterruptRegisterTrapPrivateData, 1);

    data->reg = reg;
    data->user_callback = user_callback;

    return data;
}

static void
gjs_debug_interrupt_register_trap_private_data_destroy(GjsDebugInterruptRegisterTrapPrivateData *data)
{
    g_free (data);
}

/* Callbacks */
static JSTrapStatus
gjs_debug_interrupt_register_trap_handler(JSContext  *context,
                                          JSScript   *script,
                                          jsbytecode *pc,
                                          jsval      *rval,
                                          jsval      closure)
{
    GjsDebugInterruptRegisterTrapPrivateData *data =
        (GjsDebugInterruptRegisterTrapPrivateData *) JSVAL_TO_PRIVATE(closure);

    /* And there goes the law of demeter */
    GjsDebugInterruptRegister *reg = data->reg;
    GjsInterruptInfo interrupt_info;
    GjsInterruptCallback callback = (GjsInterruptCallback) data->user_callback->callback;
    gjs_debug_interrupt_register_populate_interrupt_info(&interrupt_info, context, script, pc);

    (*callback)(GJS_INTERRUPT_REGISTER_INTERFACE(reg),
                reg->priv->context,
                &interrupt_info,
                data->user_callback->user_data);

    gjs_debug_interrupt_register_clear_interrupt_info_dynamic(&interrupt_info);

    return JSTRAP_CONTINUE;
}

static GjsBreakpoint *
gjs_debug_create_native_breakpoint_for_script(GjsDebugInterruptRegister *reg,
                                              JSContext                 *js_context,
                                              JSScript                  *script,
                                              guint                     line,
                                              GjsDebugUserCallback      *user_callback)
{
    GjsDebugInterruptRegisterTrapPrivateData *data =
        gjs_debug_interrupt_register_trap_private_data_new(reg, user_callback);

    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    /* This always succeeds, although it might only return the very-end
     * or very-beginning program counter if the line is out of range */
    jsbytecode *pc =
        JS_LineNumberToPC(js_context, script, line);

    /* Set the breakpoint on the JS side now that we're tracking it */
    JS_SetTrap(js_context,
               script,
               pc,
               gjs_debug_interrupt_register_trap_handler,
               PRIVATE_TO_JSVAL(data));

    return gjs_breakpoint_new(script, pc);
}

static void
activate_breakpoint_if_within_script(gpointer key,
                                     gpointer value,
                                     gpointer user_data)
{
    GjsPendingBreakpoint     *pending = (GjsPendingBreakpoint *) value;
    BreakpointActivationData *activation_data = (BreakpointActivationData *) user_data;

    /* Interrogate the script for its last program counter and thus its
     * last line. If the desired breakpoint line falls within this script's
     * line range then activate it. */
    if (strcmp(activation_data->filename, pending->filename) == 0)
    {
        guint end_lineno = get_script_end_lineno(activation_data->js_context,
                                                 activation_data->js_script);

        if (activation_data->begin_lineno <= pending->lineno &&
            end_lineno >= pending->lineno)
        {
            GjsBreakpoint *breakpoint =
                gjs_debug_create_native_breakpoint_for_script(activation_data->debug_register,
                                                              activation_data->js_context,
                                                              activation_data->js_script,
                                                              pending->lineno,
                                                              (GjsDebugUserCallback *) key);
            g_hash_table_insert(activation_data->breakpoints,
                                key,
                                breakpoint);

            /* We append "key" here as that is what we need to remove-by later */
            activation_data->breakpoints_changed =
                g_list_append(activation_data->breakpoints_changed,
                              key);

            /* Decrement new script callback, we might not need to know about
             * new scripts anymore as the breakpoint is no longer pending */
            gjs_debug_interrupt_register_unlock_new_script_callback(activation_data->debug_register);
        }
    }
}

static void
gjs_debug_interrupt_register_new_script_callback(JSContext    *context,
                                                 const gchar  *filename,
                                                 unsigned int lineno,
                                                 JSScript     *script,
                                                 JSFunction   *function,
                                                 gpointer     caller_data)
{
    /* We don't care about NULL-filename scripts, they are probably just initialization
    * scripts */
    if (!filename)
        return;

    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(caller_data);
    GjsDebugScriptLookupInfo *info =
        gjs_debug_script_lookup_info_new(filename, lineno);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(reg->priv->context);
    gchar *fully_qualified_path = get_fully_qualified_path(filename);

    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    g_hash_table_insert(reg->priv->scripts_loaded,
                        info,
                        script);

    /* Special case - if single-step mode is enabled then we should enable it
     * here */
    if (reg->priv->single_step_mode_lock_count)
        JS_SetSingleStepMode(js_context, script, TRUE);

    /* Special case - search pending breakpoints for the current script filename
     * and convert them to real breakpoints if need be */
    BreakpointActivationData activation_data =
    {
        reg,
        js_context,
        script,
        fully_qualified_path,
        lineno,
        reg->priv->breakpoints,
        NULL
    };

    g_hash_table_foreach(reg->priv->pending_breakpoints,
                         activate_breakpoint_if_within_script,
                         &activation_data);
    remove_activated_breakpoints_from_pending(&activation_data,
                                              reg->priv->pending_breakpoints);

    GjsDebugScriptInfo debug_script_info;
    gjs_debug_interrupt_register_populate_script_info(&debug_script_info,
                                                      context,
                                                      script,
                                                      fully_qualified_path,
                                                      lineno);


    InfoCallbackDispatchData data =
    {
        reg,
        &debug_script_info
    };

    /* Finally, call the callback function */
    g_array_foreach(reg->priv->new_script_hooks,
                    dispatch_info_callbacks,
                    &data);

    gjs_debug_interrupt_register_clear_script_info_dynamic(&debug_script_info);
    g_free(fully_qualified_path);
}

static void
gjs_debug_interrupt_register_script_destroyed_callback(JSFreeOp     *fo,
                                                       JSScript     *script,
                                                       gpointer     caller_data)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(caller_data);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(reg->priv->context);

    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    GjsDebugScriptLookupInfo info =
    {
        (gchar *) JS_GetScriptFilename(js_context, script),
        JS_GetScriptBaseLineNumber(js_context, script)
    };

    g_hash_table_remove(reg->priv->scripts_loaded, &info);
}

static JSTrapStatus
gjs_debug_interrupt_register_interrupt_callback(JSContext  *context,
                                                JSScript   *script,
                                                jsbytecode *pc,
                                                jsval      *rval,
                                                gpointer   closure)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(closure);

    GjsInterruptInfo interrupt_info;
    gjs_debug_interrupt_register_populate_interrupt_info(&interrupt_info,
                                                         context,
                                                         script,
                                                         pc);

    InterruptCallbackDispatchData data =
    {
        reg,
        &interrupt_info,
    };

    g_array_foreach(reg->priv->single_step_hooks,
                    dispatch_interrupt_callbacks,
                    &data);

    gjs_debug_interrupt_register_clear_interrupt_info_dynamic(&interrupt_info);

    return JSTRAP_CONTINUE;
}

static void *
gjs_debug_interrupt_register_function_call_or_execution_callback(JSContext          *context,
                                                                 JSAbstractFramePtr frame,
                                                                 bool               is_constructing,
                                                                 JSBool             before,
                                                                 JSBool             *ok,
                                                                 gpointer           closure)
{
    JSFunction                *function = frame.maybeFun();
    JSScript                  *script = frame.script();
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(closure);

    GjsFrameInfo frame_info;
    gjs_debug_interrupt_register_populate_interrupt_info_from_js_function(&frame_info.interrupt,
                                                                          context,
                                                                          script,
                                                                          function);

    frame_info.frame_state = before ? GJS_INTERRUPT_FRAME_BEFORE : GJS_INTERRUPT_FRAME_AFTER;

    FrameCallbackDispatchData data =
    {
        reg,
        &frame_info
    };

    g_array_foreach(reg->priv->call_and_execute_hooks,
                    dispatch_frame_callbacks,
                    &data);

    gjs_debug_interrupt_register_clear_interrupt_info_dynamic(&frame_info.interrupt);

    return closure;
}

typedef struct _ChangeDebugModeData
{
    guint    flags;
    gboolean enabled;
} ChangeDebugModeData;

typedef void (*LockAction)(JSContext *context,
                           gpointer  user_data);

static void
lock_and_perform_if_unlocked(GjsContext *context,
                             guint      *lock_count,
                             LockAction action,
                             gpointer   user_data)
{
    if ((*lock_count)++ == 0)
    {
        JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);
        (*action)(js_context, user_data);
    }
}

static void
unlock_and_perform_if_locked(GjsContext *context,
                             guint      *lock_count,
                             LockAction action,
                             gpointer   user_data)
{
    if (--(*lock_count) == 0)
    {
        JSContext *js_context = (JSContext *) gjs_context_get_native_context(context);
        (*action)(js_context, user_data);
    }
}

static void
change_debug_mode(JSContext *context,
                  gpointer  user_data)
{
  ChangeDebugModeData *data = (ChangeDebugModeData *) user_data;

  JSAutoCompartment ac(context,
                       JS_GetGlobalObject(context));

  JS_BeginRequest(context);
  JS_SetOptions(context, data->flags);
  JS_SetDebugMode(context, data->enabled);
  JS_EndRequest(context);
}

static void
gjs_debug_interrupt_register_lock_debug_mode(GjsDebugInterruptRegister *reg)
{
    ChangeDebugModeData data =
    {
        JSOPTION_BASELINE | JSOPTION_TYPE_INFERENCE,
        TRUE
    };

    lock_and_perform_if_unlocked(reg->priv->context,
                                 &reg->priv->debug_mode_lock_count,
                                 change_debug_mode,
                                 &data);
}

static void
gjs_debug_interrupt_register_unlock_debug_mode(GjsDebugInterruptRegister *reg)
{
    ChangeDebugModeData data =
    {
        0,
        FALSE
    };

    unlock_and_perform_if_locked(reg->priv->context,
                                 &reg->priv->debug_mode_lock_count,
                                 change_debug_mode,
                                 &data);
}

typedef struct _ChangeInterruptFunctionData
{
  JSInterruptHook callback;
  gpointer        user_data;
} ChangeInterruptFunctionData;

static void
set_interrupt_function_hook(JSContext *context,
                            gpointer  user_data)
{
    ChangeInterruptFunctionData *data = (ChangeInterruptFunctionData *) user_data;

    JSAutoCompartment ac(context,
                         JS_GetGlobalObject(context));

    JS_SetInterrupt(JS_GetRuntime(context),
                    data->callback,
                    data->user_data);
}

static void
gjs_debug_interrupt_register_lock_interrupt_function(GjsDebugInterruptRegister *reg)
{
    ChangeInterruptFunctionData data =
    {
        gjs_debug_interrupt_register_interrupt_callback,
        reg
    };

    lock_and_perform_if_unlocked(reg->priv->context,
                                &reg->priv->interrupt_function_lock_count,
                                set_interrupt_function_hook,
                                &data);
}

static void
gjs_debug_interrupt_register_unlock_interrupt_function(GjsDebugInterruptRegister *reg)
{
    ChangeInterruptFunctionData data =
    {
        NULL,
        NULL
    };

    unlock_and_perform_if_locked(reg->priv->context,
                                 &reg->priv->interrupt_function_lock_count,
                                 set_interrupt_function_hook,
                                 &data);
}

typedef struct _NewScriptHookData
{
    JSNewScriptHook      new_callback;
    JSDestroyScriptHook  destroy_callback;
    gpointer             user_data;
} NewScriptHookData;

static void
set_new_script_hook(JSContext *context,
                    gpointer  user_data)
{
    NewScriptHookData *data = (NewScriptHookData *) user_data;

    JSAutoCompartment ac(context,
                        JS_GetGlobalObject(context));

    JS_SetNewScriptHook(JS_GetRuntime(context), data->new_callback, data->user_data);
    JS_SetDestroyScriptHook(JS_GetRuntime(context), data->destroy_callback, data->user_data);
}

static void
gjs_debug_interrupt_register_lock_new_script_callback (GjsDebugInterruptRegister *reg)
{
    NewScriptHookData data =
    {
        gjs_debug_interrupt_register_new_script_callback,
        gjs_debug_interrupt_register_script_destroyed_callback,
        reg
    };

    lock_and_perform_if_unlocked(reg->priv->context,
                                 &reg->priv->new_script_hook_lock_count,
                                 set_new_script_hook,
                                 &data);
}

static void
gjs_debug_interrupt_register_unlock_new_script_callback (GjsDebugInterruptRegister *reg)
{
    NewScriptHookData data =
    {
        NULL,
        NULL,
        NULL
    };

  unlock_and_perform_if_locked(reg->priv->context,
                               &reg->priv->new_script_hook_lock_count,
                               set_new_script_hook,
                               &data);
}

typedef struct _SingleStepModeData
{
    JSContext  *context;
    GHashTable *scripts;
    gboolean   enabled;
} SingleStepModeData;

static void
set_single_step_mode_on_registered_script(gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
    SingleStepModeData *data = (SingleStepModeData *) user_data;

    JSAutoCompartment ac(data->context,
                         JS_GetGlobalObject(data->context));

    JS_SetSingleStepMode(data->context,
                         (JSScript *) value,
                         data->enabled);
}

typedef struct _SingleStepModeForeachData
{
    GHashTable *scripts;
    gboolean   enabled;
} SingleStepModeForeachData;

static void
set_single_step_mode(JSContext *context,
                     gpointer  user_data)
{
    SingleStepModeForeachData *foreach_data = (SingleStepModeForeachData *) user_data;

    SingleStepModeData data =
    {
        context,
        foreach_data->scripts,
        foreach_data->enabled
    };

    g_hash_table_foreach(foreach_data->scripts,
                         set_single_step_mode_on_registered_script,
                         &data);
}

static void
gjs_debug_interrupt_register_lock_single_step_mode(GjsDebugInterruptRegister *reg)
{
    SingleStepModeForeachData data =
    {
        reg->priv->scripts_loaded,
        TRUE
    };

    lock_and_perform_if_unlocked(reg->priv->context,
                                 &reg->priv->single_step_mode_lock_count,
                                 set_single_step_mode,
                                 &data);
}

static void
gjs_debug_interrupt_register_unlock_single_step_mode(GjsDebugInterruptRegister *reg)
{
    SingleStepModeForeachData data =
    {
        reg->priv->scripts_loaded,
        FALSE
    };

    unlock_and_perform_if_locked(reg->priv->context,
                                 &reg->priv->single_step_mode_lock_count,
                                 set_single_step_mode,
                                 &data);
}

typedef struct _FunctionCallsAndExecutionHooksData
{
    JSInterpreterHook hook;
    gpointer          user_data;
} FunctionCallsAndExecutionHooksData;

static void
set_function_calls_and_execution_hooks(JSContext *context,
                                       gpointer  user_data)
{
    JSRuntime                          *js_runtime = JS_GetRuntime(context);
    FunctionCallsAndExecutionHooksData *data = (FunctionCallsAndExecutionHooksData *) user_data;

    JSAutoCompartment ac(context,
                         JS_GetGlobalObject(context));

    JS_SetExecuteHook(js_runtime, data->hook, data->user_data);
    JS_SetCallHook(js_runtime, data->hook, data->user_data);
}

static void
gjs_debug_interrupt_register_lock_function_calls_and_execution(GjsDebugInterruptRegister *reg)
{
    FunctionCallsAndExecutionHooksData data =
    {
        gjs_debug_interrupt_register_function_call_or_execution_callback,
        reg
    };

    lock_and_perform_if_unlocked(reg->priv->context,
                                 &reg->priv->call_and_execute_hook_lock_count,
                                 set_function_calls_and_execution_hooks,
                                 &data);
}

static void
gjs_debug_interrupt_register_unlock_function_calls_and_execution(GjsDebugInterruptRegister *reg)
{
    FunctionCallsAndExecutionHooksData data =
    {
        NULL,
        NULL
    };

    unlock_and_perform_if_locked(reg->priv->context,
                                 &reg->priv->call_and_execute_hook_lock_count,
                                 set_function_calls_and_execution_hooks,
                                 &data);
}

static void
gjs_debug_interrupt_register_remove_breakpoint(GjsDebugConnection *connection,
                                               gpointer           user_data)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(user_data);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(reg->priv->context);
    GjsDebugUserCallback *callback =
        (GjsDebugUserCallback *) g_hash_table_lookup(reg->priv->breakpoints_connections,
                                                     connection);
    GjsBreakpoint *breakpoint =
        (GjsBreakpoint *) g_hash_table_lookup(reg->priv->breakpoints, callback);

    gboolean item_was_removed = FALSE;

    /* Remove breakpoint if there was one */
    if (breakpoint)
    {
        g_hash_table_remove(reg->priv->breakpoints, callback);

        JSAutoCompartment ac(js_context,
                           JS_GetGlobalObject(js_context));

        jsval previous_closure;

        JS_ClearTrap(js_context,
                     breakpoint->script,
                     breakpoint->pc,
                     NULL,
                     &previous_closure);

        GjsDebugInterruptRegisterTrapPrivateData *private_data =
            (GjsDebugInterruptRegisterTrapPrivateData *) JSVAL_TO_PRIVATE(previous_closure);
        gjs_debug_interrupt_register_trap_private_data_destroy(private_data);

        gjs_breakpoint_destroy(breakpoint);
        item_was_removed = TRUE;
    }

    GjsPendingBreakpoint *pending_breakpoint =
        (GjsPendingBreakpoint *) g_hash_table_lookup(reg->priv->pending_breakpoints, callback);

    if (pending_breakpoint)
    {
        g_hash_table_remove(reg->priv->pending_breakpoints, callback);
        gjs_pending_breakpoint_destroy(pending_breakpoint);

        /* When removing a pending breakpoint, we must also unlock the new
         * script hook as we might not care about new scripts anymore if pending
         * breakpoints are empty */
        gjs_debug_interrupt_register_unlock_new_script_callback(reg);

        item_was_removed = TRUE;
    }

    g_assert(item_was_removed);

    g_hash_table_remove(reg->priv->breakpoints_connections, connection);
    gjs_debug_user_callback_free(callback);

    gjs_debug_interrupt_register_unlock_debug_mode(reg);
}

typedef struct _GjsScriptHashTableSearchData
{
    JSContext *js_context;
    JSScript *return_result;
    const gchar *filename;
    guint line;
} GjsScriptHashTableSearchData;

void
search_for_script_with_closest_baseline_floor_callback(gpointer key,
                                                       gpointer value,
                                                       gpointer user_data)
{
    GjsScriptHashTableSearchData *data = (GjsScriptHashTableSearchData *) user_data;

    if (!data->return_result)
    {
        GjsDebugScriptLookupInfo     *info = (GjsDebugScriptLookupInfo *) key;
        if (g_strcmp0(info->name, data->filename) == 0)
        {
            JSScript *script = (JSScript *) value;
            guint    script_end_line = get_script_end_lineno(data->js_context,
                                                             script);

            if (info->lineno <= data->line &&
                script_end_line >= data->line)
                data->return_result = (JSScript *) value;
        }
    }
}

static JSScript *
lookup_script_for_filename_with_closest_baseline_floor(GjsDebugInterruptRegister *reg,
                                                       const gchar               *filename,
                                                       guint                     line)
{
    GjsScriptHashTableSearchData data =
    {
        (JSContext *) gjs_context_get_native_context(reg->priv->context),
        NULL,
        filename,
        line
    };

    g_hash_table_foreach(reg->priv->scripts_loaded,
                         search_for_script_with_closest_baseline_floor_callback,
                         &data);

    return data.return_result;
}

static GjsBreakpoint *
lookup_line_and_create_native_breakpoint(JSContext                 *js_context,
                                         GjsDebugInterruptRegister *debug_register,
                                         const gchar               *filename,
                                         guint                     line,
                                         GjsDebugUserCallback      *user_callback)
{
    JSScript *script =
        lookup_script_for_filename_with_closest_baseline_floor(debug_register,
                                                               filename,
                                                               line);

    if (!script)
        return NULL;

    return gjs_debug_create_native_breakpoint_for_script(debug_register,
                                                         js_context,
                                                         script,
                                                         line,
                                                         user_callback);
}

static GjsDebugConnection *
gjs_debug_interrupt_register_add_breakpoint(GjsInterruptRegister *reg,
                                            const gchar          *filename,
                                            guint                line,
                                            GjsInterruptCallback callback,
                                            gpointer             user_data)
{
    GjsDebugInterruptRegister *debug_register = GJS_DEBUG_INTERRUPT_REGISTER(reg);

    JSContext *js_context =
        (JSContext *) gjs_context_get_native_context(debug_register->priv->context);

    /* We always have a user callback even if we couldn't successfully create a native
     * breakpoint as we can always fall back to creating a pending one */
    GjsDebugUserCallback *user_callback = gjs_debug_user_callback_new(G_CALLBACK (callback),
                                                                      user_data);
    GjsDebugConnection *connection =
        gjs_debug_connection_new(gjs_debug_interrupt_register_remove_breakpoint,
                                 debug_register);

    /* Try to create a native breakpoint. If it succeeds, add it to the breakpoints
     * table, otherwise create a pending breakpoint */
    GjsBreakpoint *breakpoint = lookup_line_and_create_native_breakpoint(js_context,
                                                                         debug_register,
                                                                         filename,
                                                                         line,
                                                                         user_callback);

    if (breakpoint)
    {
        g_hash_table_insert(debug_register->priv->breakpoints,
                            user_callback,
                            breakpoint);
    }
    else
    {
        GjsPendingBreakpoint *pending = gjs_pending_breakpoint_new(filename, line);
        g_hash_table_insert(debug_register->priv->pending_breakpoints,
                            user_callback,
                            pending);

        /* We'll need to know about new scripts being loaded too */
        gjs_debug_interrupt_register_lock_new_script_callback(debug_register);
    }

    g_hash_table_insert(debug_register->priv->breakpoints_connections,
                        connection,
                        user_callback);

    /* We need debug mode for now */
    gjs_debug_interrupt_register_lock_debug_mode(debug_register);

    return connection;
}

static gint
g_array_lookup_index_by_data(GArray       *array,
                             gpointer     data,
                             GCompareFunc compare)
{
    guint i = 0;
    gsize element_size = g_array_get_element_size(array);
    gchar *underlying_array_pointer = (gchar *) array->data;

    for (; i < array->len; ++i, underlying_array_pointer += element_size)
        if ((*compare)(data, underlying_array_pointer) == 0)
            return (guint) i;

    return -1;
}

static GjsDebugConnection *
insert_hook_callback(GArray                            *hooks_array,
                     GHashTable                        *hooks_connections_table,
                     GCallback                         callback,
                     gpointer                          user_data,
                     GjsDebugConnectionDisposeCallback dispose_callback,
                     GjsDebugInterruptRegister         *debug_register)
{
    guint last_size = hooks_array->len;
    g_array_set_size(hooks_array,
                     last_size + 1);

    GjsDebugUserCallback *user_callback =
        &(g_array_index(hooks_array,
                        GjsDebugUserCallback,
                        last_size));

    gjs_debug_user_callback_asssign(user_callback,
                                    callback,
                                    user_data);
    GjsDebugConnection *connection =
        gjs_debug_connection_new(dispose_callback,
                                 debug_register);

    g_hash_table_insert(hooks_connections_table,
                        connection,
                        user_callback);

  return connection;
}

static void
remove_hook_callback (GjsDebugConnection *connection,
                      GHashTable         *hooks_connection_table,
                      GArray             *hooks_array)
{
    GjsDebugUserCallback *user_callback =
        (GjsDebugUserCallback *) g_hash_table_lookup(hooks_connection_table,
                                                     connection);
    gint array_index = g_array_lookup_index_by_data(hooks_array,
                                                    user_callback,
                                                    gjs_debug_user_callback_compare);

    g_hash_table_remove(hooks_connection_table,
                        connection);

    if (array_index > -1)
        g_array_remove_index(hooks_array, array_index);
    else
        g_error("Unable to find user callback %p in array index!", user_callback);
}

static void
gjs_debug_interrupt_register_remove_singlestep (GjsDebugConnection *connection,
                                                gpointer           user_data)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(user_data);
    remove_hook_callback(connection,
                         reg->priv->single_step_connections,
                         reg->priv->single_step_hooks);

    gjs_debug_interrupt_register_unlock_interrupt_function(reg);
    gjs_debug_interrupt_register_unlock_single_step_mode(reg);
    gjs_debug_interrupt_register_unlock_debug_mode(reg);
}

static GjsDebugConnection *
gjs_debug_interrupt_register_add_singlestep(GjsInterruptRegister *reg,
                                            GjsInterruptCallback callback,
                                            gpointer             user_data)
{
    GjsDebugInterruptRegister *debug_register = GJS_DEBUG_INTERRUPT_REGISTER(reg);
    gjs_debug_interrupt_register_lock_debug_mode(debug_register);
    gjs_debug_interrupt_register_lock_interrupt_function(debug_register);
    gjs_debug_interrupt_register_lock_single_step_mode(debug_register);
    return insert_hook_callback(debug_register->priv->single_step_hooks,
                                debug_register->priv->single_step_connections,
                                G_CALLBACK(callback),
                                user_data,
                                gjs_debug_interrupt_register_remove_singlestep,
                                debug_register);
}

static void
gjs_debug_interrupt_register_remove_connection_to_script_load(GjsDebugConnection *connection,
                                                              gpointer           user_data)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(user_data);
    remove_hook_callback(connection,
                         reg->priv->new_script_connections,
                         reg->priv->new_script_hooks);
    gjs_debug_interrupt_register_unlock_new_script_callback(reg);
    gjs_debug_interrupt_register_unlock_debug_mode(reg);
}

static GjsDebugConnection *
gjs_debug_interrupt_register_connect_to_script_load(GjsInterruptRegister *reg,
                                                    GjsInfoCallback      callback,
                                                    gpointer             user_data)
{
  GjsDebugInterruptRegister *debug_register = GJS_DEBUG_INTERRUPT_REGISTER(reg);
  gjs_debug_interrupt_register_lock_debug_mode(debug_register);
  gjs_debug_interrupt_register_lock_new_script_callback(debug_register);
  return insert_hook_callback(debug_register->priv->new_script_hooks,
                              debug_register->priv->new_script_connections,
                              G_CALLBACK(callback),
                              user_data,
                              gjs_debug_interrupt_register_remove_connection_to_script_load,
                              debug_register);
}

static void
gjs_debug_interrupt_register_remove_connection_to_function_calls_and_execution(GjsDebugConnection *connection,
                                                                               gpointer           user_data)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(user_data);
    remove_hook_callback(connection,
                         reg->priv->call_and_execute_connections,
                         reg->priv->call_and_execute_hooks);
    gjs_debug_interrupt_register_unlock_function_calls_and_execution(reg);
    gjs_debug_interrupt_register_unlock_debug_mode(reg);
}

static GjsDebugConnection *
gjs_debug_interrupt_register_connect_to_function_calls_and_execution(GjsInterruptRegister *reg,
                                                                     GjsFrameCallback     callback,
                                                                     gpointer             user_data)
{
    GjsDebugInterruptRegister *debug_register = GJS_DEBUG_INTERRUPT_REGISTER(reg);
    gjs_debug_interrupt_register_lock_debug_mode(debug_register);
    gjs_debug_interrupt_register_lock_function_calls_and_execution(debug_register);
    return insert_hook_callback(debug_register->priv->call_and_execute_hooks,
                                debug_register->priv->call_and_execute_connections,
                                G_CALLBACK(callback),
                                user_data,
                                gjs_debug_interrupt_register_remove_connection_to_function_calls_and_execution,
                                debug_register);
}

static void
gjs_debug_interrupt_register_interface_init (GjsInterruptRegisterInterface *interface)
{
    interface->add_breakpoint = gjs_debug_interrupt_register_add_breakpoint;
    interface->start_singlestep = gjs_debug_interrupt_register_add_singlestep;
    interface->connect_to_script_load = gjs_debug_interrupt_register_connect_to_script_load;
    interface->connect_to_function_calls_and_execution = gjs_debug_interrupt_register_connect_to_function_calls_and_execution;
}

static void
gjs_debug_interrupt_register_init (GjsDebugInterruptRegister *reg)
{
    reg->priv = (GjsDebugInterruptRegisterPrivate *) gjs_debug_interrupt_register_get_instance_private(reg);
    reg->priv->scripts_loaded = g_hash_table_new_full(gjs_debug_script_lookup_info_hash,
                                                      gjs_debug_script_lookup_info_equal,
                                                      gjs_debug_script_lookup_info_destroy,
                                                      NULL);

    reg->priv->breakpoints_connections = g_hash_table_new(g_direct_hash, g_direct_equal);
    reg->priv->new_script_connections = g_hash_table_new(g_direct_hash, g_direct_equal);
    reg->priv->call_and_execute_connections = g_hash_table_new(g_direct_hash, g_direct_equal);
    reg->priv->single_step_connections = g_hash_table_new(g_direct_hash, g_direct_equal);

    reg->priv->breakpoints = g_hash_table_new(g_direct_hash, g_direct_equal);
    reg->priv->pending_breakpoints = g_hash_table_new(g_direct_hash, g_direct_equal);
    reg->priv->single_step_hooks = g_array_new(TRUE, TRUE, sizeof(GjsDebugUserCallback));
    reg->priv->call_and_execute_hooks = g_array_new(TRUE, TRUE, sizeof(GjsDebugUserCallback));
    reg->priv->new_script_hooks = g_array_new(TRUE, TRUE, sizeof(GjsDebugUserCallback));
}

static void
gjs_debug_interrupt_register_dispose (GObject *object)
{
}

static void
unref_all_hashtables (GHashTable **hashtable_array)
{
    GHashTable **hashtable_iterator = hashtable_array;

    do
    {
        g_assert (g_hash_table_size(*hashtable_iterator) == 0);
        g_hash_table_unref(*hashtable_iterator);
    }
    while (*(++hashtable_iterator));
}

static void
destroy_all_arrays (GArray **array_array)
{
    GArray **array_iterator = array_array;

    do
    {
        g_assert((*array_iterator)->len == 0);
        g_array_free(*array_iterator, TRUE);
    }
    while (*(++array_iterator));
}

static void
gjs_debug_interrupt_register_finalize (GObject *object)
{
    GjsDebugInterruptRegister *reg = GJS_DEBUG_INTERRUPT_REGISTER(object);

    /* Unref scripts_loaded here as there's no guaruntee it will be empty
     * since the garbage-collect phase might happen after we're unreffed */
    g_hash_table_unref(reg->priv->scripts_loaded);

    GHashTable *hashtables_to_unref[] =
    {
        reg->priv->breakpoints_connections,
        reg->priv->new_script_connections,
        reg->priv->single_step_connections,
        reg->priv->call_and_execute_connections,
        reg->priv->breakpoints,
        reg->priv->pending_breakpoints,
        NULL
    };

    GArray *arrays_to_destroy[] =
    {
        reg->priv->new_script_hooks,
        reg->priv->call_and_execute_hooks,
        reg->priv->single_step_hooks,
        NULL
    };

    unref_all_hashtables(hashtables_to_unref);
    destroy_all_arrays(arrays_to_destroy);

    /* If we've still got locks on the context debug hooks then that's
     * an error and we should assert here */
    g_assert(reg->priv->call_and_execute_hook_lock_count == 0);
    g_assert(reg->priv->debug_mode_lock_count == 0);
    g_assert(reg->priv->interrupt_function_lock_count == 0);
    g_assert(reg->priv->new_script_hook_lock_count == 0);
    g_assert(reg->priv->single_step_mode_lock_count == 0);
}

static void
gjs_debug_interrupt_register_class_init(GjsDebugInterruptRegisterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = gjs_debug_interrupt_register_dispose;
    object_class->finalize = gjs_debug_interrupt_register_finalize;
}

GjsDebugInterruptRegister *
gjs_debug_interrupt_register_new(GjsContext *context)
{
    GjsDebugInterruptRegister *reg =
        GJS_DEBUG_INTERRUPT_REGISTER(g_object_new(GJS_TYPE_DEBUG_INTERRUPT_REGISTER, NULL));
    reg->priv->context = context;
    return reg;
}
