/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#include <math.h>
#include <string.h>

#include <codecvt>
#include <locale>

#include "jsapi-wrapper.h"
#include <js/GCAPI.h>
#include <js/Printf.h>

#include "gjs/context-private.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "util/error.h"
#include "util/glib.h"
#include "util/log.h"
#include "util/misc.h"

GQuark
gjs_util_error_quark (void)
{
    return g_quark_from_static_string ("gjs-util-error-quark");
}

bool
gjs_object_get_property(JSContext             *cx,
                        JS::HandleObject       obj,
                        GjsConstString         property_name,
                        JS::MutableHandleValue value_p)
{
    return JS_GetPropertyById(cx, obj,
                              gjs_context_get_const_string(cx, property_name),
                              value_p);
}

bool
gjs_object_set_property(JSContext       *cx,
                        JS::HandleObject obj,
                        GjsConstString   property_name,
                        JS::HandleValue  value)
{
    return JS_SetPropertyById(cx, obj,
                              gjs_context_get_const_string(cx, property_name),
                              value);
}

bool
gjs_object_has_property(JSContext       *cx,
                        JS::HandleObject obj,
                        GjsConstString   property_name,
                        bool            *found)
{
    return JS_HasPropertyById(cx, obj,
                              gjs_context_get_const_string(cx, property_name),
                              found);
}

bool
gjs_object_define_property(JSContext       *cx,
                           JS::HandleObject obj,
                           GjsConstString   property_name,
                           JS::HandleValue  value,
                           unsigned         flags)
{
    return JS_DefinePropertyById(cx, obj,
                                 gjs_context_get_const_string(cx, property_name),
                                 value, flags);
}

bool
gjs_object_define_property(JSContext       *cx,
                           JS::HandleObject obj,
                           GjsConstString   property_name,
                           JS::HandleObject value,
                           unsigned         flags)
{
    return JS_DefinePropertyById(cx, obj,
                                 gjs_context_get_const_string(cx, property_name),
                                 value, flags);
}

bool
gjs_object_define_property(JSContext       *cx,
                           JS::HandleObject obj,
                           GjsConstString   property_name,
                           JS::HandleString value,
                           unsigned         flags)
{
    return JS_DefinePropertyById(cx, obj,
                                 gjs_context_get_const_string(cx, property_name),
                                 value, flags);
}

bool
gjs_object_define_property(JSContext       *cx,
                           JS::HandleObject obj,
                           GjsConstString   property_name,
                           uint32_t         value,
                           unsigned         flags)
{
    return JS_DefinePropertyById(cx, obj,
                                 gjs_context_get_const_string(cx, property_name),
                                 value, flags);
}

static void
throw_property_lookup_error(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            const char      *reason)
{
    /* remember gjs_throw() is a no-op if JS_GetProperty()
     * already set an exception
     */
    if (description)
        gjs_throw(cx, "No property '%s' in %s (or %s)",
                  gjs_debug_id(property_name).c_str(), description, reason);
    else
        gjs_throw(cx, "No property '%s' in object %p (or %s)",
                  gjs_debug_id(property_name).c_str(), obj.get(), reason);
}

/* Returns whether the object had the property; if the object did
 * not have the property, always sets an exception. Treats
 * "the property's value is undefined" the same as "no such property,".
 * Guarantees that *value_p is set to something, if only JS::UndefinedValue(),
 * even if an exception is set and false is returned.
 *
 * SpiderMonkey will emit a warning if the property is not present, so don't
 * use this if you expect the property not to be present some of the time.
 *
 * Requires request.
 */
bool
gjs_object_require_property(JSContext             *context,
                            JS::HandleObject       obj,
                            const char            *obj_description,
                            JS::HandleId           property_name,
                            JS::MutableHandleValue value)
{
    value.setUndefined();

    if (G_UNLIKELY(!JS_GetPropertyById(context, obj, property_name, value)))
        return false;

    if (G_LIKELY(!value.isUndefined()))
        return true;

    throw_property_lookup_error(context, obj, obj_description, property_name,
                                "its value was undefined");
    return false;
}

bool
gjs_object_require_property(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            bool            *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isBoolean()) {
        *value = prop_value.toBoolean();
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a boolean");
    return false;
}

bool
gjs_object_require_property(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            int32_t         *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isInt32()) {
        *value = prop_value.toInt32();
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a 32-bit integer");
    return false;
}

/* Converts JS string value to UTF-8 string. value must be freed with JS_free. */
bool gjs_object_require_property(JSContext* cx, JS::HandleObject obj,
                                 const char* description,
                                 JS::HandleId property_name,
                                 JS::UniqueChars* value) {
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        gjs_string_to_utf8(cx, prop_value, value))
        return true;

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a valid string");
    return false;
}

bool
gjs_object_require_property(JSContext              *cx,
                            JS::HandleObject        obj,
                            const char             *description,
                            JS::HandleId            property_name,
                            JS::MutableHandleObject value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isObject()) {
        value.set(&prop_value.toObject());
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not an object");
    return false;
}

bool
gjs_object_require_converted_property(JSContext       *cx,
                                      JS::HandleObject obj,
                                      const char      *description,
                                      JS::HandleId     property_name,
                                      uint32_t        *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        JS::ToUint32(cx, prop_value, value)) {
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it couldn't be converted to uint32");
    return false;
}

void
gjs_throw_constructor_error(JSContext *context)
{
    gjs_throw(context,
              "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
}

void
gjs_throw_abstract_constructor_error(JSContext    *context,
                                     JS::CallArgs& args)
{
    const JSClass *proto_class;
    const char *name = "anonymous";

    JS::RootedObject callee(context, &args.callee());
    JS::RootedValue prototype(context);
    if (gjs_object_get_property(context, callee, GJS_STRING_PROTOTYPE,
                                &prototype)) {
        proto_class = JS_GetClass(&prototype.toObject());
        name = proto_class->name;
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
    if (!elems.reserve(array_length))
        g_error("Unable to reserve memory for vector");

    for (i = 0; i < array_length; ++i) {
        JS::ConstUTF8CharsZ chars(array_values[i], strlen(array_values[i]));
        JS::RootedValue element(context,
            JS::StringValue(JS_NewStringCopyUTF8Z(context, chars)));
        if (!elems.append(element))
            g_error("Unable to append to vector");
    }

    return JS_NewArrayObject(context, elems);
}

JSObject*
gjs_define_string_array(JSContext       *context,
                        JS::HandleObject in_object,
                        const char      *array_name,
                        ssize_t          array_length,
                        const char     **array_values,
                        unsigned         attrs)
{
    JSAutoRequest ar(context);

    JS::RootedObject array(context,
        gjs_build_string_array(context, array_length, (char **) array_values));

    if (!array)
        return nullptr;

    if (!JS_DefineProperty(context, in_object, array_name, array, attrs))
        return nullptr;

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
GJS_USE
static char *
gjs_string_readable(JSContext       *context,
                    JS::HandleString string)
{
    GString *buf = g_string_new("");

    JS_BeginRequest(context);

    g_string_append_c(buf, '"');

    JS::UniqueChars chars(JS_EncodeStringToUTF8(context, string));
    if (!chars) {
        /* I'm not sure this code will actually ever be reached except in the
         * case of OOM, since JS_EncodeStringToUTF8() seems to happily output
         * non-valid UTF-8 bytes. However, let's leave this in, since
         * SpiderMonkey may decide to do validation in the future. */

        /* Find out size of buffer to allocate, not counting 0-terminator */
        size_t len = JS_PutEscapedString(context, NULL, 0, string, '"');
        char *escaped = g_new(char, len + 1);

        JS_PutEscapedString(context, escaped, len, string, '"');
        g_string_append(buf, escaped);
        g_free(escaped);
    } else {
        g_string_append(buf, chars.get());
    }

    g_string_append_c(buf, '"');

    JS_EndRequest(context);

    return g_string_free(buf, false);
}

GJS_USE
static char *
_gjs_g_utf8_make_valid (const char *name)
{
    GString *string;
    const char *remainder, *invalid;
    int remaining_bytes, valid_bytes;

    g_return_val_if_fail (name != NULL, NULL);

    string = nullptr;
    remainder = name;
    remaining_bytes = strlen (name);

    while (remaining_bytes != 0) {
        if (g_utf8_validate (remainder, remaining_bytes, &invalid))
            break;
        valid_bytes = invalid - remainder;

        if (!string)
            string = g_string_sized_new (remaining_bytes);

        g_string_append_len (string, remainder, valid_bytes);
        /* append U+FFFD REPLACEMENT CHARACTER */
        g_string_append (string, "\357\277\275");

        remaining_bytes -= valid_bytes + 1;
        remainder = invalid + 1;
    }

    if (!string)
        return g_strdup (name);

    g_string_append (string, remainder);

    g_assert (g_utf8_validate (string->str, -1, NULL));

    return g_string_free (string, false);
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
                       JS::HandleValue value)
{
    char *bytes;
    char *debugstr;

    /* Special case debug strings for strings */
    if (value.isString()) {
        JS::RootedString str(context, value.toString());
        return gjs_string_readable(context, str);
    }

    JS_BeginRequest(context);

    JS::RootedString str(context, JS::ToString(context, value));

    if (!str) {
        JS_ClearPendingException(context);
        str = JS_ValueToSource(context, value);
    }

    if (!str) {
        if (value.isObject()) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            const JSClass *klass = JS_GetClass(&value.toObject());
            if (klass != NULL) {
                str = JS_NewStringCopyZ(context, klass->name);
                JS_ClearPendingException(context);
                if (!str) {
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

    g_assert(str);

    bytes = JS_EncodeStringToUTF8(context, str);
    JS_EndRequest(context);

    debugstr = _gjs_g_utf8_make_valid(bytes);
    JS_free(context, bytes);

    return debugstr;
}

bool
gjs_log_exception_full(JSContext       *context,
                       JS::HandleValue  exc,
                       JS::HandleString message)
{
    bool is_syntax;

    JS_BeginRequest(context);
    JS::RootedObject exc_obj(context);
    JS::RootedString exc_str(context, JS::ToString(context, exc));
    JS::UniqueChars utf8_exception;
    if (exc_str)
        utf8_exception.reset(JS_EncodeStringToUTF8(context, exc_str));
    if (!utf8_exception)
        JS_ClearPendingException(context);

    is_syntax = false;
    if (exc.isObject()) {
        exc_obj = &exc.toObject();
        const JSClass* syntax_error =
            js::Jsvalify(js::ProtoKeyToClass(JSProto_SyntaxError));
        is_syntax = JS_InstanceOf(context, exc_obj, syntax_error, nullptr);
    }

    JS::UniqueChars utf8_message;
    if (message)
        utf8_message.reset(JS_EncodeStringToUTF8(context, message));

    /* We log syntax errors differently, because the stack for those includes
       only the referencing module, but we want to print out the filename and
       line number from the exception.
    */

    if (is_syntax) {
        JS::RootedValue js_lineNumber(context), js_fileName(context);
        unsigned lineNumber;

        gjs_object_get_property(context, exc_obj, GJS_STRING_LINE_NUMBER,
                                &js_lineNumber);
        gjs_object_get_property(context, exc_obj, GJS_STRING_FILENAME,
                                &js_fileName);

        JS::UniqueChars utf8_filename;
        if (js_fileName.isString()) {
            JS::RootedString str(context, js_fileName.toString());
            utf8_filename.reset(JS_EncodeStringToUTF8(context, str));
        }
        if (!utf8_filename)
            utf8_filename.reset(JS_strdup(context, "unknown"));

        lineNumber = js_lineNumber.toInt32();

        if (message) {
            g_critical("JS ERROR: %s: %s @ %s:%u", utf8_message.get(),
                       utf8_exception.get(), utf8_filename.get(), lineNumber);
        } else {
            g_critical("JS ERROR: %s @ %s:%u", utf8_exception.get(),
                       utf8_filename.get(), lineNumber);
        }

    } else {
        JS::UniqueChars utf8_stack;
        JS::RootedValue stack(context);

        if (exc.isObject() &&
            gjs_object_get_property(context, exc_obj, GJS_STRING_STACK,
                                    &stack) &&
            stack.isString()) {
            JS::RootedString str(context, stack.toString());
            utf8_stack.reset(JS_EncodeStringToUTF8(context, str));
        }

        if (message) {
            if (utf8_stack)
                g_warning("JS ERROR: %s: %s\n%s", utf8_message.get(),
                          utf8_exception.get(), utf8_stack.get());
            else
                g_warning("JS ERROR: %s: %s", utf8_message.get(),
                          utf8_exception.get());
        } else {
            if (utf8_stack)
                g_warning("JS ERROR: %s\n%s", utf8_exception.get(),
                          utf8_stack.get());
            else
                g_warning("JS ERROR: %s", utf8_exception.get());
        }
    }

    JS_EndRequest(context);

    return true;
}

bool
gjs_log_exception(JSContext  *context)
{
    bool retval = false;

    JS_BeginRequest(context);

    JS::RootedValue exc(context);
    if (!JS_GetPendingException(context, &exc))
        goto out;

    JS_ClearPendingException(context);

    gjs_log_exception_full(context, exc, nullptr);

    retval = true;

 out:
    JS_EndRequest(context);

    return retval;
}

bool
gjs_call_function_value(JSContext                  *context,
                        JS::HandleObject            obj,
                        JS::HandleValue             fval,
                        const JS::HandleValueArray& args,
                        JS::MutableHandleValue      rval)
{
    bool result;

    JS_BeginRequest(context);

    result = JS_CallFunctionValue(context, obj, fval, args, rval);

    if (result)
        gjs_schedule_gc_if_needed(context);

    JS_EndRequest(context);
    return result;
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
static int64_t last_gc_check_time;
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
        if (now - last_gc_check_time < 5 * 16666)
            return;

        last_gc_check_time = now;

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
            JS::GCForReason(context, GC_SHRINK, JS::gcreason::Reason::API);
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
                       size_t      *script_len,
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
gjs_eval_with_scope(JSContext             *context,
                    JS::HandleObject       object,
                    const char            *script,
                    ssize_t                script_len,
                    const char            *filename,
                    JS::MutableHandleValue retval)
{
    int start_line_number = 1;
    JSAutoRequest ar(context);
    size_t real_len = script_len;

    if (script_len < 0)
        real_len = strlen(script);

    script = gjs_strip_unix_shebang(script,
                                    &real_len,
                                    &start_line_number);

    /* log and clear exception if it's set (should not be, normally...) */
    if (JS_IsExceptionPending(context)) {
        g_warning("gjs_eval_in_scope called with a pending exception");
        return false;
    }

    JS::RootedObject eval_obj(context, object);
    if (!eval_obj)
        eval_obj = JS_NewPlainObject(context);

    JS::CompileOptions options(context);
    options.setFileAndLine(filename, start_line_number);

    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string utf16_string = convert.from_bytes(script);
    JS::SourceBufferHolder buf(utf16_string.c_str(), utf16_string.size(),
                               JS::SourceBufferHolder::NoOwnership);

    JS::AutoObjectVector scope_chain(context);
    if (!scope_chain.append(eval_obj))
        g_error("Unable to append to vector");

    if (!JS::Evaluate(context, scope_chain, options, buf, retval))
        return false;

    gjs_schedule_gc_if_needed(context);

    if (JS_IsExceptionPending(context)) {
        g_warning("EvaluateScript returned true but exception was pending; "
                  "did somebody call gjs_throw() without returning false?");
        return false;
    }

    gjs_debug(GJS_DEBUG_CONTEXT,
              "Script evaluation succeeded");

    return true;
}
