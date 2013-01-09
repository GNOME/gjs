/* This file contains code derived from xpcdebug.cpp in Mozilla.  The license
 * for that file follows:
 */
/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <config.h>
#include <glib.h>
#include <string.h>
#include "context.h"
#include "compat.h"
#include "jsapi-util.h"

/* Mimick the behaviour exposed by standard Error objects
   (http://mxr.mozilla.org/mozilla-central/source/js/src/jsexn.cpp#554)
*/
static char*
jsvalue_to_string(JSContext* cx, jsval val, gboolean* is_string)
{
    char* value = NULL;
    JSString* value_str = NULL;

    if (JSVAL_IS_PRIMITIVE(val)) {
      value_str = JS_ValueToSource(cx, val);
    } else {
      JSObject *obj = JSVAL_TO_OBJECT(val);

      if (JS_ObjectIsFunction(cx, obj)) {
	JSFunction *fn = JS_ValueToFunction(cx, val);
	value_str = JS_GetFunctionId(fn);

	if (!value_str)
	  value = g_strdup("[unknown function]");
      } else {
	value = g_strdup_printf("[object %s]", JS_GetClass(obj)->name);
      }
    }

    if (!value && value_str)
      value = gjs_value_debug_string(cx, val);

    if (is_string)
        *is_string = JSVAL_IS_STRING(val);

    return value;
}

static void
format_frame(JSContext* cx, JSStackFrame* fp,
             GString *buf, int num)
{
    JSPropertyDescArray call_props = { 0, NULL };
    JSObject* call_obj = NULL;
    char* funname_str = NULL;
    const char* filename = NULL;
    guint32 lineno = 0;
    guint32 named_arg_count = 0;
    JSFunction* fun = NULL;
    JSScript* script;
    guchar* pc;
    guint32 i;
    gboolean is_string;
    jsval val;

    (void)JS_EnterLocalRootScope(cx);

    /* get the info for this stack frame */

    script = JS_GetFrameScript(cx, fp);
    pc = JS_GetFramePC(cx, fp);

    if (!script) {
        g_string_append_printf(buf, "%d [native frame]\n", num);
        goto out;
    }


    if (script && pc) {
        filename = JS_GetScriptFilename(cx, script);
        lineno =  (guint32) JS_PCToLineNumber(cx, script, pc);
        fun = JS_GetFrameFunction(cx, fp);
        if (fun) {
	    JSString* funname = JS_GetFunctionId(fun);
            if (funname)
              gjs_string_to_utf8(cx, STRING_TO_JSVAL(funname), &funname_str);
	}

        call_obj = JS_GetFrameCallObject(cx, fp);
        if (call_obj) {
            if (!JS_GetPropertyDescArray(cx, call_obj, &call_props))
                call_props.array = NULL;
        }

    }

    /* print the frame number and function name */

    if (funname_str) {
        g_string_append_printf(buf, "%d %s(", num, funname_str);
        g_free(funname_str);
    }
    else if (fun)
        g_string_append_printf(buf, "%d anonymous(", num);
    else
        g_string_append_printf(buf, "%d <TOP LEVEL>", num);

    for (i = 0; i < call_props.length; i++) {
        char *name = NULL;
        char *value = NULL;
        JSPropertyDesc* desc = &call_props.array[i];
            name = jsvalue_to_string(cx, desc->id, &is_string);
            if(!is_string) {
                g_free(name);
                name = NULL;
            }
            value = jsvalue_to_string(cx, desc->value, &is_string);

            g_string_append_printf(buf, "%s%s%s%s%s%s",
                                   named_arg_count ? ", " : "",
                                   name ? name :"",
                                   name ? " = " : "",
                                   is_string ? "\"" : "",
                                   value ? value : "?unknown?",
                                   is_string ? "\"" : "");
            named_arg_count++;
        g_free(name);
        g_free(value);
    }

    /* print any unnamed trailing args (found in 'arguments' object) */

    if (call_obj != NULL &&
        JS_GetProperty(cx, call_obj, "arguments", &val) &&
        JSVAL_IS_OBJECT(val)) {
        guint32 k;
        guint32 arg_count;
        JSObject* args_obj = JSVAL_TO_OBJECT(val);
        if (JS_GetArrayLength(cx, args_obj, &arg_count) &&
            arg_count > named_arg_count) {
            for (k = named_arg_count; k < arg_count; k++) {
                if (JS_GetElement(cx, args_obj, k, &val)) {
                    char *value = jsvalue_to_string(cx, val, &is_string);
                    g_string_append_printf(buf, "%s%s%s%s",
                                           k ? ", " : "",
                                           is_string ? "\"" : "",
                                           value ? value : "?unknown?",
                                           is_string ? "\"" : "");
                    g_free(value);
                }
            }
        }
    }

    /* print filename and line number */

    g_string_append_printf(buf, "%s@%s:%d\n",
                           fun ? ")" : "",
                           filename ? filename : "",
                           lineno);

  out:
    if (call_props.array)
      JS_PutPropertyDescArray(cx, &call_props);

    JS_LeaveLocalRootScope(cx);
}

void
gjs_context_print_stack_to_buffer(GjsContext* context, void *initial, GString *buf)
{
    JSContext *js_context = (JSContext*)gjs_context_get_native_context(context);
    JSStackFrame *fp = initial;
    int num = 0;

    if (fp == NULL)
        JS_FrameIterator(js_context, &fp);

    while (fp) {
        format_frame(js_context, fp, buf, num);
        num++;

	JS_FrameIterator(js_context, &fp);
    }
}

void
gjs_context_print_stack_stderr(GjsContext *context)
{
  GString *str = g_string_new("");

  g_string_append_printf(str, "== Stack trace for context %p ==\n", context);
  gjs_context_print_stack_to_buffer(context, NULL, str);

  g_printerr("%s\n", str->str);
  g_string_free(str, TRUE);
}

void
gjs_dumpstack(void)
{
  GList *contexts = gjs_context_get_all();
  GList *iter;

  for (iter = contexts; iter; iter = iter->next) {
    GjsContext *context = (GjsContext*)iter->data;
    gjs_context_print_stack_stderr(context);
    g_object_unref(context);
  }
  g_list_free(contexts);
}
