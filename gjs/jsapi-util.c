/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
 * Copyright (c) 2009 Red Hat, Inc.
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

#include <string.h>
#include <math.h>

GQuark
gjs_util_error_quark (void)
{
    return g_quark_from_static_string ("gjs-util-error-quark");
}


/**
 * gjs_get_import_global:
 * @context: a #JSContext
 *
 * Gets the "import global" for the context's runtime. The import
 * global object is the global object for the context. It is used
 * as the root object for the scope of modules loaded by GJS in this
 * runtime, and should also be used as the globals 'obj' argument passed
 * to JS_InitClass() and the parent argument passed to JS_ConstructObject()
 * when creating a native classes that are shared between all contexts using
 * the runtime. (The standard JS classes are not shared, but we share
 * classes such as GObject proxy classes since objects of these classes can
 * easily migrate between contexts and having different classes depending
 * on the context where they were first accessed would be confusing.)
 *
 * Return value: the "import global" for the context's
 *  runtime. Will never return %NULL while GJS has an active context
 *  for the runtime.
 */
JSObject*
gjs_get_import_global(JSContext *context)
{
    return JS_GetGlobalObject(context);
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
    return (JSContext *) JS_GetRuntimePrivate (runtime);
}

static JSClass global_class = {
    "GjsGlobal", JSCLASS_GLOBAL_FLAGS,
    JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

/**
 * gjs_init_context_standard:
 * @context: a #JSContext
 *
 * This function creates a default global object for @context,
 * and calls JS_InitStandardClasses using it.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
gjs_init_context_standard (JSContext       *context)
{
    JSObject *global;
    global = JS_NewGlobalObject(context, &global_class, NULL);
    if (global == NULL)
        return FALSE;
    if (!JS_InitStandardClasses(context, global))
        return FALSE;
    return TRUE;
}


/* Checks whether an object has a property; unlike JS_GetProperty(),
 * never sets an exception. Treats a property with a value of JSVAL_VOID
 * the same as an absent property and returns false in both cases.
 */
gboolean
gjs_object_has_property(JSContext  *context,
                        JSObject   *obj,
                        const char *property_name)
{
    return gjs_object_get_property(context, obj, property_name, NULL);
}

/* Checks whether an object has a property; unlike JS_GetProperty(),
 * never sets an exception. Treats a property with a value of JSVAL_VOID
 * the same as an absent property and returns false in both cases.
 * Always initializes *value_p, if only to JSVAL_VOID, even if it
 * returns FALSE.
 */
gboolean
gjs_object_get_property(JSContext  *context,
                        JSObject   *obj,
                        const char *property_name,
                        jsval      *value_p)
{
    jsval value;
    JSExceptionState *state;

    JS_BeginRequest(context);

    value = JSVAL_VOID;
    state = JS_SaveExceptionState(context);
    JS_GetProperty(context, obj, property_name, &value);
    JS_RestoreExceptionState(context, state);

    if (value_p)
        *value_p = value;

    JS_EndRequest(context);

    return !JSVAL_IS_VOID(value);
}

/* Returns whether the object had the property; if the object did
 * not have the property, always sets an exception. Treats
 * "the property's value is JSVAL_VOID" the same as "no such property,"
 * while JS_GetProperty() treats only "no such property" as an error.
 * Guarantees that *value_p is set to something, if only JSVAL_VOID,
 * even if an exception is set and false is returned.
 */
gboolean
gjs_object_require_property(JSContext       *context,
                            JSObject        *obj,
                            const char      *obj_description,
                            const char      *property_name,
                            jsval           *value_p)
{
    jsval value;

    JS_BeginRequest(context);

    value = JSVAL_VOID;
    JS_GetProperty(context, obj, property_name, &value);

    if (value_p)
        *value_p = value;

    if (!JSVAL_IS_VOID(value)) {
        JS_ClearPendingException(context); /* in case JS_GetProperty() was on crack */
        JS_EndRequest(context);
        return TRUE;
    } else {
        /* remember gjs_throw() is a no-op if JS_GetProperty()
         * already set an exception
         */
        if (obj_description)
            gjs_throw(context,
                      "No property '%s' in %s (or its value was undefined)",
                      property_name, obj_description);
        else
            gjs_throw(context,
                      "No property '%s' in object %p (or its value was undefined)",
                      property_name, obj);

        JS_EndRequest(context);
        return FALSE;
    }
}

void
gjs_throw_constructor_error(JSContext *context)
{
    gjs_throw(context,
              "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
}

void
gjs_throw_abstract_constructor_error(JSContext *context,
                                     jsval     *vp)
{
    jsval callee;
    jsval prototype;
    JSClass *proto_class;
    const char *name = "anonymous";

    callee = JS_CALLEE(context, vp);

    if (JSVAL_IS_OBJECT(callee)) {
        if (gjs_object_get_property(context, JSVAL_TO_OBJECT(callee),
                                    "prototype", &prototype)) {
            proto_class = JS_GetClass(JSVAL_TO_OBJECT(prototype));
            name = proto_class->name;
        }
    }

    gjs_throw(context, "You cannot construct new instances of '%s'", name);
}

JSObject*
gjs_define_string_array(JSContext   *context,
                        JSObject    *in_object,
                        const char  *array_name,
                        gssize       array_length,
                        const char **array_values,
                        unsigned     attrs)
{
    GArray *elems;
    JSObject *array;
    int i;

    JS_BeginRequest(context);

    if (!JS_EnterLocalRootScope(context))
        return JS_FALSE;

    if (array_length == -1)
        array_length = g_strv_length((char**)array_values);

    elems = g_array_sized_new(FALSE, FALSE, sizeof(jsval), array_length);

    for (i = 0; i < array_length; ++i) {
        jsval element;
        element = STRING_TO_JSVAL(JS_NewStringCopyZ(context, array_values[i]));
        g_array_append_val(elems, element);
    }

    array = JS_NewArrayObject(context, elems->len, (jsval*) elems->data);
    g_array_free(elems, TRUE);

    if (array != NULL) {
        if (!JS_DefineProperty(context, in_object,
                               array_name, OBJECT_TO_JSVAL(array),
                               NULL, NULL, attrs))
            array = NULL;
    }

    JS_LeaveLocalRootScope(context);

    JS_EndRequest(context);
    return array;
}

/**
 * gjs_string_readable:
 *
 * Return a string that can be read back by gjs-console; for
 * JS strings that contain valid Unicode, we return a UTF-8 formatted
 * string.  Otherwise, we return one where non-ASCII-printable bytes
 * are \x escaped.
 *
 */
static char *
gjs_string_readable (JSContext   *context,
                     JSString    *string)
{
    GString *buf = g_string_new("");
    char *chars;

    JS_BeginRequest(context);

    g_string_append_c(buf, '"');

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(string), &chars)) {
        size_t i, len;
        const jschar *uchars;

        uchars = JS_GetStringCharsAndLength(context, string, &len);

        for (i = 0; i < len; i++) {
            jschar c = uchars[i];
            if (c >> 8 == 0 && g_ascii_isprint(c & 0xFF))
                g_string_append_c(buf, c & 0xFF);
            else
                g_string_append_printf(buf, "\\u%04X", c);
        }
    } else {
        g_string_append(buf, chars);
        g_free(chars);
    }

    g_string_append_c(buf, '"');

    JS_EndRequest(context);

    return g_string_free(buf, FALSE);
}

/**
 * gjs_value_debug_string:
 * @context:
 * @value: Any JavaScript value
 *
 * Returns: A UTF-8 encoded string describing @value
 */
char*
gjs_value_debug_string(JSContext      *context,
                       jsval           value)
{
    JSString *str;
    char *bytes;
    char *debugstr;

    /* Special case debug strings for strings */
    if (JSVAL_IS_STRING(value)) {
        return gjs_string_readable(context, JSVAL_TO_STRING(value));
    }

    JS_BeginRequest(context);

    str = JS_ValueToString(context, value);

    if (str == NULL) {
        if (JSVAL_IS_OBJECT(value)) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            JSClass *klass;

            klass = JS_GetClass(JSVAL_TO_OBJECT(value));
            if (klass != NULL) {
                str = JS_NewStringCopyZ(context, klass->name);
                JS_ClearPendingException(context);
                if (str == NULL) {
                    return g_strdup("[out of memory copying class name]");
                }
            } else {
                gjs_log_exception(context, NULL);
                return g_strdup("[unknown object]");
            }
        } else {
            return g_strdup("[unknown non-object]");
        }
    }

    g_assert(str != NULL);

    size_t len = JS_GetStringEncodingLength(context, str);
    if (len != (size_t)(-1)) {
        bytes = g_malloc((len + 1) * sizeof(char));
        JS_EncodeStringToBuffer(str, bytes, len);
        bytes[len] = '\0';
    } else {
        bytes = g_strdup("[invalid string]");
    }
    JS_EndRequest(context);

    debugstr = _gjs_g_utf8_make_valid(bytes);
    g_free(bytes);

    return debugstr;
}

void
gjs_log_object_props(JSContext      *context,
                     JSObject       *obj,
                     GjsDebugTopic   topic,
                     const char     *prefix)
{
    JSObject *props_iter;
    jsid prop_id;

    JS_BeginRequest(context);

    /* We potentially create new strings, plus the property iterator,
     * that could get collected as we go through this process. So
     * create a local root scope.
     */
    (void)JS_EnterLocalRootScope(context);

    props_iter = JS_NewPropertyIterator(context, obj);
    if (props_iter == NULL) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to create property iterator for object props");
        goto done;
    }

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, props_iter, &prop_id))
        goto done;

    while (!JSID_IS_VOID(prop_id)) {
        jsval propval;
        char *name;
        char *debugstr;

        if (!gjs_get_string_id(context, prop_id, &name))
            goto next;

        if (!gjs_object_get_property(context, obj, name, &propval))
            goto next;

        debugstr = gjs_value_debug_string(context, propval);
        gjs_debug(topic,
                  "%s%s = '%s'",
                  prefix, name,
                  debugstr);
        g_free(debugstr);

    next:
        g_free(name);
        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, props_iter, &prop_id))
            break;
    }

 done:
    JS_LeaveLocalRootScope(context);
    JS_EndRequest(context);
}

void
gjs_explain_scope(JSContext  *context,
                  const char *title)
{
    JSObject *global;
    JSObject *parent;
    GString *chain;
    char *debugstr;

    gjs_debug(GJS_DEBUG_SCOPE,
              "=== %s ===",
              title);

    JS_BeginRequest(context);

    (void)JS_EnterLocalRootScope(context);

    gjs_debug(GJS_DEBUG_SCOPE,
              "  Context: %p %s",
              context,
              "");

    global = JS_GetGlobalObject(context);
    debugstr = gjs_value_debug_string(context, OBJECT_TO_JSVAL(global));
    gjs_debug(GJS_DEBUG_SCOPE,
              "  Global: %p %s",
              global, debugstr);
    g_free(debugstr);

    parent = JS_GetGlobalForScopeChain(context);
    chain = g_string_new(NULL);
    while (parent != NULL) {
        char *debug;
        debug = gjs_value_debug_string(context, OBJECT_TO_JSVAL(parent));

        if (chain->len > 0)
            g_string_append(chain, ", ");

        g_string_append_printf(chain, "%p %s",
                               parent, debug);
        g_free(debug);
        parent = JS_GetParent(parent);
    }
    gjs_debug(GJS_DEBUG_SCOPE,
              "  Chain: %s",
              chain->str);
    g_string_free(chain, TRUE);

    JS_LeaveLocalRootScope(context);

    JS_EndRequest(context);
}

static void
log_one_exception_property(JSContext  *context,
                           JSObject   *object,
                           const char *name)
{
    jsval v;
    char *debugstr;

    gjs_object_get_property(context, object, name, &v);

    debugstr = gjs_value_debug_string(context, v);
    gjs_debug(GJS_DEBUG_ERROR, "  %s = '%s'", name, debugstr);
    g_free(debugstr);
}

void
gjs_log_exception_props(JSContext *context,
                        jsval      exc)
{
    JS_BeginRequest(context);

    /* This is useful when the exception was never sent to an error reporter
     * due to JSOPTION_DONT_REPORT_UNCAUGHT, or if the exception was not
     * a normal Error object so jsapi didn't know how to report it sensibly.
     */
    if (JSVAL_IS_NULL(exc)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception was null");
    } else if (JSVAL_IS_OBJECT(exc)) {
        JSObject *exc_obj;

        exc_obj = JSVAL_TO_OBJECT(exc);

        log_one_exception_property(context, exc_obj, "message");
        log_one_exception_property(context, exc_obj, "fileName");
        log_one_exception_property(context, exc_obj, "lineNumber");
        log_one_exception_property(context, exc_obj, "stack");
    } else if (JSVAL_IS_STRING(exc)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception was a String");
    } else {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Exception had some strange type");
    }
    JS_EndRequest(context);
}

static JSBool
log_and_maybe_keep_exception(JSContext  *context,
                             char      **message_p,
                             gboolean    keep)
{
    jsval exc = JSVAL_VOID;
    JSString *s;
    char *message;
    JSBool retval = JS_FALSE;

    JS_BeginRequest(context);

    if (message_p)
        *message_p = NULL;

    JS_AddValueRoot(context, &exc);
    if (!JS_GetPendingException(context, &exc))
        goto out;

    JS_ClearPendingException(context);

    s = JS_ValueToString(context, exc);

    if (s == NULL) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to convert exception to string");
        goto out; /* Exception should be thrown already */
    }

    if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(s), &message)) {
        gjs_debug(GJS_DEBUG_ERROR,
                  "Failed to convert exception string to UTF-8");
        goto out; /* Error already set */
    }

    gjs_debug(GJS_DEBUG_ERROR,
              "Exception was: %s",
              message);

    if (message_p) {
        *message_p = message;
    } else {
        g_free(message);
    }

    gjs_log_exception_props(context, exc);

    /* We clear above and then set it back so any exceptions
     * from the logging process don't overwrite the original
     */
    if (keep)
        JS_SetPendingException(context, exc);

    retval = JS_TRUE;

 out:
    JS_RemoveValueRoot(context, &exc);

    JS_EndRequest(context);

    return retval;
}

JSBool
gjs_log_exception(JSContext  *context,
                  char      **message_p)
{
    return log_and_maybe_keep_exception(context, message_p, FALSE);
}

JSBool
gjs_log_and_keep_exception(JSContext *context,
                           char     **message_p)
{
    return log_and_maybe_keep_exception(context, message_p, TRUE);
}

static void
try_to_chain_stack_trace(JSContext *src_context, JSContext *dst_context,
                         jsval src_exc) {
    /* append current stack of dst_context to stack trace for src_exc.
     * we bail if anything goes wrong, just using the src_exc unmodified
     * in that case. */
    jsval chained, src_stack, dst_stack, new_stack;
    JSString *new_stack_str;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dst_context);

    if (!JSVAL_IS_OBJECT(src_exc))
        goto out; // src_exc doesn't have a stack trace

    /* create a new exception in dst_context to get a stack trace */
    gjs_throw_literal(dst_context, "Chained exception");
    if (!(JS_GetPendingException(dst_context, &chained) &&
          JSVAL_IS_OBJECT(chained)))
        goto out; // gjs_throw_literal didn't work?!
    JS_ClearPendingException(dst_context);

    /* get stack trace for src_exc and chained */
    if (!(gjs_object_get_property(dst_context, JSVAL_TO_OBJECT(chained),
                                  "stack", &dst_stack) &&
          JSVAL_IS_STRING(dst_stack)))
        goto out; // couldn't get chained stack
    if (!(gjs_object_get_property(src_context, JSVAL_TO_OBJECT(src_exc),
                                  "stack", &src_stack) &&
          JSVAL_IS_STRING(src_stack)))
        goto out; // couldn't get source stack

    /* add chained exception's stack trace to src_exc */
    new_stack_str = JS_ConcatStrings
        (dst_context, JSVAL_TO_STRING(src_stack), JSVAL_TO_STRING(dst_stack));
    if (new_stack_str==NULL)
        goto out; // couldn't concatenate src and dst stacks?!
    new_stack = STRING_TO_JSVAL(new_stack_str);
    JS_SetProperty(dst_context, JSVAL_TO_OBJECT(src_exc), "stack", &new_stack);

 out:
    JS_EndRequest(dst_context);
    JS_EndRequest(src_context);
}

JSBool
gjs_move_exception(JSContext      *src_context,
                   JSContext      *dest_context)
{
    JSBool success;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dest_context);

    /* NOTE: src and dest could be the same. */
    jsval exc;
    if (JS_GetPendingException(src_context, &exc)) {
        if (src_context != dest_context) {
            /* try to add the current stack of dest_context to the
             * stack trace of exc */
            try_to_chain_stack_trace(src_context, dest_context, exc);
            /* move the exception to dest_context */
            JS_SetPendingException(dest_context, exc);
            JS_ClearPendingException(src_context);
        }
        success = JS_TRUE;
    } else {
        success = JS_FALSE;
    }

    JS_EndRequest(dest_context);
    JS_EndRequest(src_context);

    return success;
}

JSBool
gjs_call_function_value(JSContext      *context,
                        JSObject       *obj,
                        jsval           fval,
                        unsigned        argc,
                        jsval          *argv,
                        jsval          *rval)
{
    JSBool result;

    JS_BeginRequest(context);

    result = JS_CallFunctionValue(context, obj, fval,
                                  argc, argv, rval);

    JS_EndRequest(context);
    return result;
}

static JSBool
log_prop(JSContext  *context,
         JSObject   *obj,
         jsval       id,
         jsval      *value_p,
         const char *what)
{
    if (JSVAL_IS_STRING(id)) {
        char *name;

        gjs_string_to_utf8(context, id, &name);
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %s: %s",
                  name, what);
        g_free(name);
    } else if (JSVAL_IS_INT(id)) {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %d: %s",
                  JSVAL_TO_INT(id), what);
    } else {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop not-sure-what: %s",
                  what);
    }

    return JS_TRUE;
}

JSBool
gjs_get_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "get");
}

JSBool
gjs_set_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "set");
}

JSBool
gjs_add_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          jsval      id,
                          jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "add");
}

JSBool
gjs_delete_prop_verbose_stub(JSContext *context,
                             JSObject  *obj,
                             jsval      id,
                             jsval     *value_p)
{
    return log_prop(context, obj, id, value_p, "delete");
}

/* get a debug string for type tag in jsval */
const char*
gjs_get_type_name(jsval value)
{
    if (JSVAL_IS_NULL(value)) {
        return "null";
    } else if (JSVAL_IS_VOID(value)) {
        return "undefined";
    } else if (JSVAL_IS_INT(value)) {
        return "integer";
    } else if (JSVAL_IS_DOUBLE(value)) {
        return "double";
    } else if (JSVAL_IS_BOOLEAN(value)) {
        return "boolean";
    } else if (JSVAL_IS_STRING(value)) {
        return "string";
    } else if (JSVAL_IS_OBJECT(value)) {
        return "object";
    } else {
        return "<unknown>";
    }
}

jsval
gjs_date_from_time_t (JSContext *context, time_t time)
{
    JSObject *date;
    JSClass *date_class;
    JSObject *date_constructor;
    jsval date_prototype;
    jsval args[1];
    jsval result;

    JS_BeginRequest(context);

    if (!JS_EnterLocalRootScope(context))
        return JSVAL_VOID;

    if (!JS_GetClassObject(context, JS_GetGlobalObject(context), JSProto_Date,
                           &date_constructor))
        gjs_fatal("Failed to lookup Date prototype");

    if (!JS_GetProperty(context, date_constructor, "prototype", &date_prototype))
        gjs_fatal("Failed to get prototype from Date constructor");

    if (!JS_NewNumberValue(context, ((double) time) * 1000, &(args[0])))
        gjs_fatal("Failed to convert time_t to number");

    date = JS_New(context, JSVAL_TO_OBJECT (date_prototype), 1, args);

    result = OBJECT_TO_JSVAL(date);
    JS_LeaveLocalRootScope(context);
    JS_EndRequest(context);

    return result;
}

/**
 * gjs_value_to_int64:
 * @context: the Javascript context object
 * @val: Javascript value to convert
 * @gint64: location to store the return value
 *
 * Converts a Javascript value into the nearest 64 bit signed value.
 *
 * This function behaves indentically for rounding to JSValToInt32(), which
 * means that it rounds (0.5 toward positive infinity) rather than doing
 * a C-style truncation to 0. If we change to using JSValToEcmaInt32() then
 * this should be changed to match.
 *
 * Return value: If the javascript value converted to a number (see
 *   JS_ValueToNumber()) is NaN, or outside the range of 64-bit signed
 *   numbers, fails and sets an exception. Otherwise returns the value
 *   rounded to the nearest 64-bit integer. Like JS_ValueToInt32(),
 *   undefined throws, but null => 0, false => 0, true => 1.
 */
JSBool
gjs_value_to_int64  (JSContext  *context,
                     const jsval val,
                     gint64     *result)
{
    if (JSVAL_IS_INT (val)) {
        *result = JSVAL_TO_INT (val);
        return JS_TRUE;
    } else {
        double value_double;
        if (!JS_ValueToNumber(context, val, &value_double))
            return JS_FALSE;

        if (isnan(value_double) ||
            value_double < G_MININT64 ||
            value_double > G_MAXINT64) {

            gjs_throw(context,
                      "Value is not a valid 64-bit integer");
            return JS_FALSE;
        }

        *result = (gint64)(value_double + 0.5);
        return JS_TRUE;
    }
}

/**
 * gjs_parse_args:
 * @context:
 * @function_name: The name of the function being called
 * @format: Printf-like format specifier containing the expected arguments
 * @argc: Number of JavaScript arguments
 * @argv: JavaScript argument array
 * @Varargs: for each character in @format, a pair of a char * which is the name
 * of the argument, and a pointer to a location to store the value. The type of
 * value stored depends on the format character, as described below.
 *
 * This function is inspired by Python's PyArg_ParseTuple for those
 * familiar with it.  It takes a format specifier which gives the
 * types of the expected arguments, and a list of argument names and
 * value location pairs.  The currently accepted format specifiers are:
 *
 * b: A boolean
 * s: A string, converted into UTF-8
 * F: A string, converted into "filename encoding" (i.e. active locale)
 * i: A number, will be converted to a C "gint32"
 * u: A number, converted into a C "guint32"
 * t: A 64-bit number, converted into a C "gint64" by way of gjs_value_to_int64()
 * o: A JavaScript object, as a "JSObject *"
 *
 * If the first character in the format string is a '!', then JS is allowed
 * to pass extra arguments that are ignored, to the function.
 *
 * The '|' character introduces optional arguments.  All format specifiers
 * after a '|' when not specified, do not cause any changes in the C
 * value location.
 *
 * A prefix character '?' means that the next value may be null, in
 * which case the C value %NULL is returned.
 */
JSBool
gjs_parse_args (JSContext  *context,
                const char *function_name,
                const char *format,
                unsigned   argc,
                jsval     *argv,
                ...)
{
    guint i;
    const char *fmt_iter;
    va_list args;
    guint n_unwind = 0;
#define MAX_UNWIND_STRINGS 16
    gpointer unwind_strings[MAX_UNWIND_STRINGS];
    gboolean ignore_trailing_args = FALSE;
    guint n_required = 0;
    guint n_total = 0;
    guint consumed_args;

    JS_BeginRequest(context);

    va_start (args, argv);

    if (*format == '!') {
        ignore_trailing_args = TRUE;
        format++;
    }

    for (fmt_iter = format; *fmt_iter; fmt_iter++) {
        switch (*fmt_iter) {
        case '|':
            n_required = n_total;
            continue;
        case '?':
            continue;
        default:
            break;
        }

        n_total++;
    }

    if (n_required == 0)
        n_required = n_total;

    if (argc < n_required || (argc > n_total && !ignore_trailing_args)) {
        if (n_required == n_total) {
            gjs_throw(context, "Error invoking %s: Expected %d arguments, got %d", function_name,
                      n_required, argc);
        } else {
            gjs_throw(context, "Error invoking %s: Expected minimum %d arguments (and %d optional), got %d", function_name,
                      n_required, n_total - n_required, argc);
        }
        goto error_unwind;
    }

    /* We have 3 iteration variables here.
     * @i: The current integer position in fmt_args
     * @fmt_iter: A pointer to the character in fmt_args
     * @consumed_args: How many arguments we've taken from argv
     *
     * consumed_args can currently be different from 'i' because of the '|' character.
     */
    for (i = 0, consumed_args = 0, fmt_iter = format; *fmt_iter; fmt_iter++, i++) {
        const char *argname;
        gpointer arg_location;
        jsval js_value;
        const char *arg_error_message = NULL;

        if (*fmt_iter == '|')
            continue;

        if (consumed_args == argc) {
            break;
        }

        argname = va_arg (args, char *);
        arg_location = va_arg (args, gpointer);

        g_return_val_if_fail (argname != NULL, JS_FALSE);
        g_return_val_if_fail (arg_location != NULL, JS_FALSE);

        js_value = argv[consumed_args];

        if (*fmt_iter == '?') {
            fmt_iter++;

            if (JSVAL_IS_NULL (js_value)) {
                gpointer *arg = arg_location;
                *arg = NULL;
                goto got_value;
            }
        }

        switch (*fmt_iter) {
        case 'b': {
            if (!JSVAL_IS_BOOLEAN(js_value)) {
                arg_error_message = "Not a boolean";
            } else {
                gboolean *arg = arg_location;
                *arg = JSVAL_TO_BOOLEAN(js_value);
            }
        }
            break;
        case 'o': {
            if (!JSVAL_IS_OBJECT(js_value)) {
                arg_error_message = "Not an object";
            } else {
                JSObject **arg = arg_location;
                *arg = JSVAL_TO_OBJECT(js_value);
            }
        }
            break;
        case 's': {
            char **arg = arg_location;

            if (gjs_string_to_utf8 (context, js_value, arg)) {
                unwind_strings[n_unwind++] = *arg;
                g_assert(n_unwind < MAX_UNWIND_STRINGS);
            } else {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to string";
            }
        }
            break;
        case 'F': {
            char **arg = arg_location;

            if (gjs_string_to_filename (context, js_value, arg)) {
                unwind_strings[n_unwind++] = *arg;
                g_assert(n_unwind < MAX_UNWIND_STRINGS);
            } else {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to filename";
            }
        }
            break;
        case 'i': {
            if (!JS_ValueToInt32(context, js_value, (gint32*) arg_location)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to integer";
            }
        }
            break;
        case 'u': {
            gdouble num;
            if (!JS_ValueToNumber(context, js_value, &num)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to unsigned integer";
            } else if (num > G_MAXUINT32 || num < 0) {
                arg_error_message = "Value is out of range";
            } else {
                *((guint32*) arg_location) = num;
            }
        }
            break;
        case 't': {
            if (!gjs_value_to_int64(context, js_value, (gint64*) arg_location)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to 64-bit integer";
            }
        }
            break;
        case 'f': {
            double num;
            if (!JS_ValueToNumber(context, js_value, &num)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to double";
            } else {
                *((double*) arg_location) = num;
            }
        }
            break;
        default:
            g_assert_not_reached ();
        }

    got_value:
        if (arg_error_message != NULL) {
            gjs_throw(context, "Error invoking %s, at argument %d (%s): %s", function_name,
                      consumed_args+1, argname, arg_error_message);
            goto error_unwind;
        }

        consumed_args++;
    }

    va_end (args);

    JS_EndRequest(context);
    return JS_TRUE;

 error_unwind:
    va_end (args);
    /* We still own the strings in the error case, free any we converted */
    for (i = 0; i < n_unwind; i++) {
        g_free (unwind_strings[i]);
    }
    JS_EndRequest(context);
    return JS_FALSE;
}

#ifdef __linux__
static void
_linux_get_self_process_size (gulong *vm_size,
                              gulong *rss_size)
{
    char *contents;
    char *iter;
    gsize len;
    int i;

    *vm_size = *rss_size = 0;

    if (!g_file_get_contents ("/proc/self/stat", &contents, &len, NULL))
        return;

    iter = contents;
    /* See "man proc" for where this 22 comes from */
    for (i = 0; i < 22; i++) {
        iter = strchr (iter, ' ');
        if (!iter)
            goto out;
        iter++;
    }
    sscanf (iter, " %lu", vm_size);
    iter = strchr (iter, ' ');
    if (iter)
        sscanf (iter, " %lu", rss_size);

 out:
    g_free (contents);
}

static gulong linux_rss_trigger;
#endif

/**
 * gjs_maybe_gc:
 *
 * Low level version of gjs_context_maybe_gc().
 */
void
gjs_maybe_gc (JSContext *context)
{
    JS_MaybeGC(context);

#ifdef __linux__
    {
        /* We initiate a GC if VM or RSS has grown by this much */
        gulong vmsize;
        gulong rss_size;

        _linux_get_self_process_size (&vmsize, &rss_size);

        /* linux_rss_trigger is initialized to 0, so currently
         * we always do a full GC early.
         *
         * Here we see if the RSS has grown by 25% since
         * our last look; if so, initiate a full GC.  In
         * theory using RSS is bad if we get swapped out,
         * since we may be overzealous in GC, but on the
         * other hand, if swapping is going on, better
         * to GC.
         */
        if (rss_size > linux_rss_trigger) {
            linux_rss_trigger = (gulong) MIN(G_MAXULONG, rss_size * 1.25);
            JS_GC(JS_GetRuntime(context));
        } else if (rss_size < (0.75 * linux_rss_trigger)) {
            /* If we've shrunk by 75%, lower the trigger */
            linux_rss_trigger = (rss_size * 1.25);
        }
    }
#endif
}
