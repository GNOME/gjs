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
#include <util/error.h>

#include "jsapi-util.h"
#include "compat.h"
#include "context-private.h"
#include "jsapi-private.h"
#include <gi/boxed.h>

#include <string.h>
#include <math.h>

GQuark
gjs_util_error_quark (void)
{
    return g_quark_from_static_string ("gjs-util-error-quark");
}

static JSClass global_class = {
    "GjsGlobal", JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(GJS_GLOBAL_SLOT_LAST),
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, NULL,
    NULL /* checkAccess */, NULL /* call */, NULL /* hasInstance */, NULL /* construct */, NULL,
    { NULL }
};

/**
 * gjs_init_context_standard:
 * @context: a #JSContext
 * @global_out: (out): The created global object.

 * This function creates a global object given the context,
 * and initializes it with the default API.
 *
 * Returns: true on success, false otherwise
 */
bool
gjs_init_context_standard (JSContext  *context,
                           JSObject  **global_out)
{
    JSObject *global;
    JS::CompartmentOptions options;
    guint32 options_flags;

    /* JSOPTION_DONT_REPORT_UNCAUGHT: Don't send exceptions to our
     * error report handler; instead leave them set.  This allows us
     * to get at the exception object.
     *
     * JSOPTION_STRICT: Report warnings to error reporter function.
     */
    options_flags = JSOPTION_DONT_REPORT_UNCAUGHT;

    if (!g_getenv("GJS_DISABLE_JIT")) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling JIT");
        options_flags |= JSOPTION_TYPE_INFERENCE | JSOPTION_ION | JSOPTION_BASELINE | JSOPTION_ASMJS;
    }

    if (!g_getenv("GJS_DISABLE_EXTRA_WARNINGS")) {
        gjs_debug(GJS_DEBUG_CONTEXT, "Enabling extra warnings");
        options_flags |= JSOPTION_EXTRA_WARNINGS;
    }

    JS_SetOptions(context, JS_GetOptions(context) | options_flags);
    JS_SetErrorReporter(context, gjs_error_reporter);

    options.setVersion(JSVERSION_LATEST);
    global = JS_NewGlobalObject(context, &global_class, NULL, options);
    if (global == NULL)
        return false;

    /* Set the context's global */
    JSAutoCompartment ac(context, global);

    if (!JS_InitStandardClasses(context, global))
        return false;

    if (!JS_InitReflect(context, global))
        return false;

    if (!JS_DefineDebuggerObject(context, global))
        return false;

    *global_out = global;

    return true;
}

void
gjs_set_global_slot (JSContext     *context,
                     GjsGlobalSlot  slot,
                     JS::Value      value)
{
    JSObject *global;
    global = gjs_get_global_object(context);
    JS_SetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot, value);
}

JS::Value
gjs_get_global_slot (JSContext     *context,
                     GjsGlobalSlot  slot)
{
    JSObject *global;
    global = gjs_get_global_object(context);
    return JS_GetReservedSlot(global, JSCLASS_GLOBAL_SLOT_COUNT + slot);
}

/* Returns whether the object had the property; if the object did
 * not have the property, always sets an exception. Treats
 * "the property's value is undefined" the same as "no such property,".
 * Guarantees that *value_p is set to something, if only JS::UndefinedValue(),
 * even if an exception is set and false is returned.
 *
 * Requires request.
 */
bool
gjs_object_require_property(JSContext       *context,
                            JSObject        *obj,
                            const char      *obj_description,
                            jsid             property_name,
                            JS::Value       *value_p)
{
    JS::Value value;
    char *name;

    value = JS::UndefinedValue();
    if (value_p)
        *value_p = value;

    if (G_UNLIKELY (!JS_GetPropertyById(context, obj, property_name, &value)))
        return false;

    if (G_LIKELY (!value.isUndefined())) {
        if (value_p)
            *value_p = value;
        return true;
    }

    /* remember gjs_throw() is a no-op if JS_GetProperty()
     * already set an exception
     */

    gjs_get_string_id(context, property_name, &name);

    if (obj_description)
        gjs_throw(context,
                  "No property '%s' in %s (or its value was undefined)",
                  name, obj_description);
    else
        gjs_throw(context,
                  "No property '%s' in object %p (or its value was undefined)",
                  name, obj);

    g_free(name);
    return false;
}

void
gjs_throw_constructor_error(JSContext *context)
{
    gjs_throw(context,
              "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
}

void
gjs_throw_abstract_constructor_error(JSContext *context,
                                     JS::Value *vp)
{
    JS::Value callee;
    JS::Value prototype;
    JSClass *proto_class;
    const char *name = "anonymous";

    callee = JS_CALLEE(context, vp);

    if (callee.isObject()) {
        if (gjs_object_get_property_const(context, &callee.toObject(),
                                          GJS_STRING_PROTOTYPE, &prototype)) {
            proto_class = JS_GetClass(&prototype.toObject());
            name = proto_class->name;
        }
    }

    gjs_throw(context, "You cannot construct new instances of '%s'", name);
}

JSObject *
gjs_build_string_array(JSContext   *context,
                       gssize       array_length,
                       char       **array_values)
{
    int i;

    if (array_length == -1)
        array_length = g_strv_length(array_values);

    JS::AutoValueVector elems(context);
    elems.reserve(array_length);

    for (i = 0; i < array_length; ++i) {
        JS::RootedValue element(context,
            JS::StringValue(JS_NewStringCopyZ(context, array_values[i])));
        elems.append(element);
    }

    return JS_NewArrayObject(context, elems.length(), &elems[0]);
}

JSObject*
gjs_define_string_array(JSContext   *context,
                        JSObject    *in_object,
                        const char  *array_name,
                        gssize       array_length,
                        const char **array_values,
                        unsigned     attrs)
{
    JSObject *array;

    JSAutoRequest ar(context);

    array = gjs_build_string_array(context, array_length, (char **) array_values);

    if (array != NULL) {
        if (!JS_DefineProperty(context, in_object,
                               array_name, JS::ObjectValue(*array),
                               NULL, NULL, attrs))
            array = NULL;
    }

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

    if (!gjs_string_to_utf8(context, JS::StringValue(string), &chars)) {
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

    return g_string_free(buf, false);
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
                       JS::Value       value)
{
    JSString *str;
    char *bytes;
    char *debugstr;

    /* Special case debug strings for strings */
    if (value.isString()) {
        return gjs_string_readable(context, value.toString());
    }

    JS_BeginRequest(context);

    str = JS_ValueToString(context, value);

    if (str == NULL) {
        if (value.isObject()) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            JSClass *klass;

            klass = JS_GetClass(&value.toObject());
            if (klass != NULL) {
                str = JS_NewStringCopyZ(context, klass->name);
                JS_ClearPendingException(context);
                if (str == NULL) {
                    JS_EndRequest(context);
                    return g_strdup("[out of memory copying class name]");
                }
            } else {
                gjs_log_exception(context);
                JS_EndRequest(context);
                return g_strdup("[unknown object]");
            }
        } else {
            JS_EndRequest(context);
            return g_strdup("[unknown non-object]");
        }
    }

    g_assert(str != NULL);

    size_t len = JS_GetStringEncodingLength(context, str);
    if (len != (size_t)(-1)) {
        bytes = (char*) g_malloc((len + 1) * sizeof(char));
        JS_EncodeStringToBuffer(context, str, bytes, len);
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

    props_iter = JS_NewPropertyIterator(context, obj);
    if (props_iter == NULL) {
        gjs_log_exception(context);
        goto done;
    }

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, props_iter, &prop_id))
        goto done;

    while (!JSID_IS_VOID(prop_id)) {
        JS::Value propval;
        char *debugstr;
        char *name = NULL;

        if (!JS_GetPropertyById(context, obj, prop_id, &propval))
            goto next;

        if (!gjs_get_string_id(context, prop_id, &name))
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

    gjs_debug(GJS_DEBUG_SCOPE,
              "  Context: %p %s",
              context,
              "");

    global = gjs_get_global_object(context);
    debugstr = gjs_value_debug_string(context, JS::ObjectOrNullValue(global));
    gjs_debug(GJS_DEBUG_SCOPE,
              "  Global: %p %s",
              global, debugstr);
    g_free(debugstr);

    parent = JS_GetGlobalForScopeChain(context);
    chain = g_string_new(NULL);
    while (parent != NULL) {
        char *debug;
        debug = gjs_value_debug_string(context, JS::ObjectOrNullValue(parent));

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
    g_string_free(chain, true);

    JS_EndRequest(context);
}

static char *
utf8_exception_from_non_gerror_value(JSContext *cx,
                                     JS::Value  exc)
{
    char *utf8_exception = NULL;
    JSString *exc_str = JS_ValueToString(cx, exc);
    if (exc_str != NULL)
        gjs_string_to_utf8(cx, JS::StringValue(exc_str), &utf8_exception);
    return utf8_exception;
}

bool
gjs_log_exception_full(JSContext *context,
                       JS::Value  exc,
                       JSString  *message)
{
    JS::Value stack;
    char *utf8_exception, *utf8_message;
    bool is_syntax;

    JS_BeginRequest(context);

    is_syntax = false;

    if (!exc.isObject()) {
        utf8_exception = utf8_exception_from_non_gerror_value(context, exc);
    } else {
        JS::RootedObject exc_obj(context, &exc.toObject());
        if (gjs_typecheck_boxed(context, exc_obj, NULL, G_TYPE_ERROR, false)) {
            GError *gerror = (GError *) gjs_c_struct_from_boxed(context, exc_obj);
            utf8_exception = g_strdup_printf("GLib.Error %s: %s",
                                             g_quark_to_string(gerror->domain),
                                             gerror->message);
        } else {
            JS::Value js_name;
            char *utf8_name;

            if (gjs_object_get_property_const(context, exc_obj,
                                              GJS_STRING_NAME, &js_name) &&
                js_name.isString() &&
                gjs_string_to_utf8(context, js_name, &utf8_name)) {
                is_syntax = strcmp("SyntaxError", utf8_name) == 0;
            }

            utf8_exception = utf8_exception_from_non_gerror_value(context, exc);
        }
    }

    if (message != NULL)
        gjs_string_to_utf8(context, JS::StringValue(message), &utf8_message);
    else
        utf8_message = NULL;

    /* We log syntax errors differently, because the stack for those includes
       only the referencing module, but we want to print out the filename and
       line number from the exception.
    */

    if (is_syntax) {
        JS::Value js_lineNumber, js_fileName;
        unsigned lineNumber;
        char *utf8_fileName;

        gjs_object_get_property_const(context, &exc.toObject(),
                                      GJS_STRING_LINE_NUMBER, &js_lineNumber);
        gjs_object_get_property_const(context, &exc.toObject(),
                                      GJS_STRING_FILENAME, &js_fileName);

        if (js_fileName.isString())
            gjs_string_to_utf8(context, js_fileName, &utf8_fileName);
        else
            utf8_fileName = g_strdup("unknown");

        lineNumber = js_lineNumber.toInt32();

        if (utf8_message) {
            g_critical("JS ERROR: %s: %s @ %s:%u", utf8_message, utf8_exception,
                       utf8_fileName, lineNumber);
        } else {
            g_critical("JS ERROR: %s @ %s:%u", utf8_exception,
                       utf8_fileName, lineNumber);
        }

        g_free(utf8_fileName);
    } else {
        char *utf8_stack;

        if (exc.isObject() &&
            gjs_object_get_property_const(context, &exc.toObject(),
                                          GJS_STRING_STACK, &stack) &&
            stack.isString())
            gjs_string_to_utf8(context, stack, &utf8_stack);
        else
            utf8_stack = NULL;

        if (utf8_message) {
            if (utf8_stack)
                g_warning("JS ERROR: %s: %s\n%s", utf8_message, utf8_exception, utf8_stack);
            else
                g_warning("JS ERROR: %s: %s", utf8_message, utf8_exception);
        } else {
            if (utf8_stack)
                g_warning("JS ERROR: %s\n%s", utf8_exception, utf8_stack);
            else
                g_warning("JS ERROR: %s", utf8_exception);
        }

        g_free(utf8_stack);
    }

    g_free(utf8_exception);
    g_free(utf8_message);

    JS_EndRequest(context);

    return true;
}

static bool
log_and_maybe_keep_exception(JSContext  *context,
                             bool        keep)
{
    bool retval = false;

    JS_BeginRequest(context);

    JS::RootedValue exc(context, JS::UndefinedValue());
    if (!JS_GetPendingException(context, exc.address()))
        goto out;

    JS_ClearPendingException(context);

    gjs_log_exception_full(context, exc, NULL);

    /* We clear above and then set it back so any exceptions
     * from the logging process don't overwrite the original
     */
    if (keep)
        JS_SetPendingException(context, exc);

    retval = true;

 out:
    JS_EndRequest(context);

    return retval;
}

bool
gjs_log_exception(JSContext  *context)
{
    return log_and_maybe_keep_exception(context, false);
}

bool
gjs_log_and_keep_exception(JSContext *context)
{
    return log_and_maybe_keep_exception(context, true);
}

static void
try_to_chain_stack_trace(JSContext *src_context,
                         JSContext *dst_context,
                         JS::Value  src_exc)
{
    /* append current stack of dst_context to stack trace for src_exc.
     * we bail if anything goes wrong, just using the src_exc unmodified
     * in that case. */
    JS::Value chained, src_stack, dst_stack, new_stack;
    JSString *new_stack_str;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dst_context);

    if (!src_exc.isObject())
        goto out; // src_exc doesn't have a stack trace

    /* create a new exception in dst_context to get a stack trace */
    gjs_throw_literal(dst_context, "Chained exception");
    if (!(JS_GetPendingException(dst_context, &chained) && chained.isObject()))
        goto out; // gjs_throw_literal didn't work?!
    JS_ClearPendingException(dst_context);

    /* get stack trace for src_exc and chained */
    if (!(JS_GetProperty(dst_context, &chained.toObject(),
                         "stack", &dst_stack) &&
          dst_stack.isString()))
        goto out; // couldn't get chained stack
    if (!(JS_GetProperty(src_context, &src_exc.toObject(),
                         "stack", &src_stack) &&
          src_stack.isString()))
        goto out; // couldn't get source stack

    /* add chained exception's stack trace to src_exc */
    new_stack_str = JS_ConcatStrings(dst_context, src_stack.toString(),
        dst_stack.toString());
    if (new_stack_str==NULL)
        goto out; // couldn't concatenate src and dst stacks?!
    new_stack = JS::StringValue(new_stack_str);
    JS_SetProperty(dst_context, &src_exc.toObject(), "stack", &new_stack);

 out:
    JS_EndRequest(dst_context);
    JS_EndRequest(src_context);
}

bool
gjs_move_exception(JSContext      *src_context,
                   JSContext      *dest_context)
{
    bool success;

    JS_BeginRequest(src_context);
    JS_BeginRequest(dest_context);

    /* NOTE: src and dest could be the same. */
    JS::Value exc;
    if (JS_GetPendingException(src_context, &exc)) {
        if (src_context != dest_context) {
            /* try to add the current stack of dest_context to the
             * stack trace of exc */
            try_to_chain_stack_trace(src_context, dest_context, exc);
            /* move the exception to dest_context */
            JS_SetPendingException(dest_context, exc);
            JS_ClearPendingException(src_context);
        }
        success = true;
    } else {
        success = false;
    }

    JS_EndRequest(dest_context);
    JS_EndRequest(src_context);

    return success;
}

bool
gjs_call_function_value(JSContext      *context,
                        JSObject       *obj,
                        JS::Value       fval,
                        unsigned        argc,
                        JS::Value      *argv,
                        JS::Value      *rval)
{
    bool result;

    JS_BeginRequest(context);

    result = JS_CallFunctionValue(context, obj, fval,
                                  argc, argv, rval);

    if (result)
        gjs_schedule_gc_if_needed(context);

    JS_EndRequest(context);
    return result;
}

static bool
log_prop(JSContext  *context,
         JSObject   *obj,
         JS::Value   id,
         JS::Value  *value_p,
         const char *what)
{
    if (id.isString()) {
        char *name;

        gjs_string_to_utf8(context, id, &name);
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %s: %s",
                  name, what);
        g_free(name);
    } else if (id.isInt32()) {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop %d: %s",
                  id.toInt32(), what);
    } else {
        gjs_debug(GJS_DEBUG_PROPS,
                  "prop not-sure-what: %s",
                  what);
    }

    return true;
}

bool
gjs_get_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          JS::Value  id,
                          JS::Value *value_p)
{
    return log_prop(context, obj, id, value_p, "get");
}

bool
gjs_set_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          JS::Value  id,
                          JS::Value *value_p)
{
    return log_prop(context, obj, id, value_p, "set");
}

bool
gjs_add_prop_verbose_stub(JSContext *context,
                          JSObject  *obj,
                          JS::Value  id,
                          JS::Value *value_p)
{
    return log_prop(context, obj, id, value_p, "add");
}

bool
gjs_delete_prop_verbose_stub(JSContext *context,
                             JSObject  *obj,
                             JS::Value  id,
                             JS::Value *value_p)
{
    return log_prop(context, obj, id, value_p, "delete");
}

/* get a debug string for type tag in JS::Value */
const char*
gjs_get_type_name(JS::Value value)
{
    if (value.isNull()) {
        return "null";
    } else if (value.isUndefined()) {
        return "undefined";
    } else if (value.isInt32()) {
        return "integer";
    } else if (value.isDouble()) {
        return "double";
    } else if (value.isBoolean()) {
        return "boolean";
    } else if (value.isString()) {
        return "string";
    } else if (value.isObject()) {
        return "object";
    } else {
        return "<unknown>";
    }
}

/**
 * gjs_value_to_int64:
 * @context: the Javascript context object
 * @val: Javascript value to convert
 * @result: location to store the return value
 *
 * Converts a Javascript value into the nearest 64 bit signed value.
 *
 * Deprecated: Use JS::ToInt64() instead.
 */
bool
gjs_value_to_int64(JSContext      *context,
                   const JS::Value val,
                   gint64         *result)
{
    return JS::ToInt64(context, val, (int64_t *) result);
}

static bool
gjs_parse_args_valist (JSContext  *context,
                       const char *function_name,
                       const char *format,
                       unsigned    argc,
                       JS::Value  *argv,
                       va_list     args)
{
    guint i;
    const char *fmt_iter;
    guint n_unwind = 0;
#define MAX_UNWIND_STRINGS 16
    gpointer unwind_strings[MAX_UNWIND_STRINGS];
    bool ignore_trailing_args = false;
    guint n_required = 0;
    guint n_total = 0;
    guint consumed_args;

    JS_BeginRequest(context);

    if (*format == '!') {
        ignore_trailing_args = true;
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
        JS::Value js_value;
        const char *arg_error_message = NULL;

        if (*fmt_iter == '|')
            continue;

        if (consumed_args == argc) {
            break;
        }

        argname = va_arg (args, char *);
        arg_location = va_arg (args, gpointer);

        g_return_val_if_fail (argname != NULL, false);
        g_return_val_if_fail (arg_location != NULL, false);

        js_value = argv[consumed_args];

        if (*fmt_iter == '?') {
            fmt_iter++;

            if (js_value.isNull()) {
                gpointer *arg = (gpointer*) arg_location;
                *arg = NULL;
                goto got_value;
            }
        }

        switch (*fmt_iter) {
        case 'b': {
            if (!js_value.isBoolean()) {
                arg_error_message = "Not a boolean";
            } else {
                bool *arg = (bool *) arg_location;
                *arg = js_value.toBoolean();
            }
        }
            break;
        case 'o': {
            if (!js_value.isObject()) {
                arg_error_message = "Not an object";
            } else {
                JSObject **arg = (JSObject**) arg_location;
                *arg = &js_value.toObject();
            }
        }
            break;
        case 's': {
            char **arg = (char**) arg_location;

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
            char **arg = (char**) arg_location;

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
            if (!JS::ToInt32(context, js_value, (int32_t *) arg_location)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to integer";
            }
        }
            break;
        case 'u': {
            gdouble num;
            if (!js_value.isNumber() || !JS::ToNumber(context, js_value, &num)) {
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
            if (!JS::ToInt64(context, js_value, (int64_t *) arg_location)) {
                /* Our error message is going to be more useful */
                JS_ClearPendingException(context);
                arg_error_message = "Couldn't convert to 64-bit integer";
            }
        }
            break;
        case 'f': {
            double num;
            if (!JS::ToNumber(context, js_value, &num)) {
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

    JS_EndRequest(context);
    return true;

 error_unwind:
    /* We still own the strings in the error case, free any we converted */
    for (i = 0; i < n_unwind; i++) {
        g_free (unwind_strings[i]);
    }
    JS_EndRequest(context);
    return false;
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
 * t: A 64-bit number, converted into a C "gint64"
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
bool
gjs_parse_args (JSContext  *context,
                const char *function_name,
                const char *format,
                unsigned    argc,
                JS::Value  *argv,
                ...)
{
    va_list args;
    bool ret;
    va_start (args, argv);
    ret = gjs_parse_args_valist (context, function_name, format, argc, argv, args);
    va_end (args);
    return ret;
}

bool
gjs_parse_call_args (JSContext    *context,
                     const char   *function_name,
                     const char   *format,
                     JS::CallArgs &call_args,
                     ...)
{
    va_list args;
    bool ret;
    va_start (args, call_args);
    ret = gjs_parse_args_valist (context, function_name, format, call_args.length(), call_args.array(), args);
    va_end (args);
    return ret;
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
static gint64 last_gc_time;
#endif

void
gjs_gc_if_needed (JSContext *context)
{
#ifdef __linux__
    {
        /* We initiate a GC if VM or RSS has grown by this much */
        gulong vmsize;
        gulong rss_size;
        gint64 now;

        /* We rate limit GCs to at most one per 5 frames.
           One frame is 16666 microseconds (1000000/60)*/
        now = g_get_monotonic_time();
        if (now - last_gc_time < 5 * 16666)
            return;

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
            last_gc_time = now;
        } else if (rss_size < (0.75 * linux_rss_trigger)) {
            /* If we've shrunk by 75%, lower the trigger */
            linux_rss_trigger = (rss_size * 1.25);
        }
    }
#endif
}

/**
 * gjs_maybe_gc:
 *
 * Low level version of gjs_context_maybe_gc().
 */
void
gjs_maybe_gc (JSContext *context)
{
    JS_MaybeGC(context);
    gjs_gc_if_needed(context);
}

void
gjs_schedule_gc_if_needed (JSContext *context)
{
    GjsContext *gjs_context;

    /* We call JS_MaybeGC immediately, but defer a check for a full
     * GC cycle to an idle handler.
     */
    JS_MaybeGC(context);

    gjs_context = (GjsContext *) JS_GetContextPrivate(context);
    if (gjs_context)
        _gjs_context_schedule_gc_if_needed(gjs_context);
}

/**
 * gjs_strip_unix_shebang:
 *
 * @script: (in): A pointer to a JS script
 * @script_len: (inout): A pointer to the script length. The
 * pointer will be modified if a shebang is stripped.
 * @new_start_line_number: (out) (allow-none): A pointer to
 * write the start-line number to account for the offset
 * as a result of stripping the shebang.
 *
 * Returns a pointer to the beginning of a script with unix
 * shebangs removed. The outparams are useful to know the
 * new length of the script and on what line of the
 * original script we're executing from, so that any relevant
 * offsets can be applied to the results of an execution pass.
 */
const char *
gjs_strip_unix_shebang(const char  *script,
                       gssize      *script_len,
                       int         *start_line_number_out)
{
    g_assert(script_len);

    /* handle scripts with UNIX shebangs */
    if (strncmp(script, "#!", 2) == 0) {
        /* If we found a newline, advance the script by one line */
        const char *s = (const char *) strstr (script, "\n");
        if (s != NULL) {
            if (*script_len > 0)
                *script_len -= (s + 1 - script);
            script = s + 1;

            if (start_line_number_out)
                *start_line_number_out = 2;

            return script;
        } else {
            /* Just a shebang */
            if (start_line_number_out)
                *start_line_number_out = -1;

            *script_len = 0;

            return NULL;
        }
    }

    /* No shebang, return the original script */
    if (start_line_number_out)
        *start_line_number_out = 1;

    return script;
}

bool
gjs_eval_with_scope(JSContext    *context,
                    JSObject     *object,
                    const char   *script,
                    gssize        script_len,
                    const char   *filename,
                    JS::Value    *retval_p)
{
    int start_line_number = 1;
    JS::Value retval = JS::UndefinedValue();
    JSAutoRequest ar(context);

    if (script_len < 0)
        script_len = strlen(script);

    script = gjs_strip_unix_shebang(script,
                                    &script_len,
                                    &start_line_number);

    /* log and clear exception if it's set (should not be, normally...) */
    if (JS_IsExceptionPending(context)) {
        g_warning("gjs_eval_in_scope called with a pending exception");
        return false;
    }

    if (!object)
        object = JS_NewObject(context, NULL, NULL, NULL);

    JS::CompileOptions options(context);
    options.setUTF8(true)
           .setFileAndLine(filename, start_line_number)
           .setSourcePolicy(JS::CompileOptions::LAZY_SOURCE);

    js::RootedObject rootedObj(context, object);

    if (!JS::Evaluate(context, rootedObj, options, script, script_len, &retval))
        return false;

    gjs_schedule_gc_if_needed(context);

    if (JS_IsExceptionPending(context)) {
        g_warning("EvaluateScript returned true but exception was pending; "
                  "did somebody call gjs_throw() without returning false?");
        return false;
    }

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Script evaluation succeeded");

    if (retval_p)
        *retval_p = retval;

    return true;
}
